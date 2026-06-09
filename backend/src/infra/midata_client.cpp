#include "infra/midata_client.hpp"
#include "infra/app_config.hpp"
#include "db/database.hpp"
#include "json.hpp"
#include <string>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <curl/curl.h>

namespace
{

static size_t midata_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

const nlohmann::json *find_people_array(const nlohmann::json &payload)
{
    if (payload.is_array())
        return &payload;
    if (!payload.is_object())
        return nullptr;

    static const char *keys[] = {"people", "members", "participants", "entries", "data"};
    for (const char *k : keys)
    {
        if (payload.contains(k) && payload[k].is_array())
            return &payload[k];
    }
    return nullptr;
}

std::optional<int> fetch_midata_children_count(Database &db,
                                               const std::string &group_id,
                                               std::string &error)
{
    std::string api_key = app_config::get_or(db, app_config::kMidataApiKey, "");
    if (api_key.empty())
    {
        error = "not-configured";
        return std::nullopt;
    }

    std::string url_tmpl = app_config::get_or(db,
                                              app_config::kMidataApiUrlTemplate,
                                              "https://db.scout.ch/de/groups/{group_id}/people.json");

    CURL *escape_curl = curl_easy_init();
    if (!escape_curl)
    {
        error = "curl-init-failed";
        return std::nullopt;
    }
    char *escaped_group = curl_easy_escape(escape_curl, group_id.c_str(), static_cast<int>(group_id.size()));
    if (!escaped_group)
    {
        curl_easy_cleanup(escape_curl);
        error = "url-escape-failed";
        return std::nullopt;
    }

    std::string url = url_tmpl;
    auto pos = url.find("{group_id}");
    if (pos != std::string::npos)
        url.replace(pos, std::string("{group_id}").size(), escaped_group);
    curl_free(escaped_group);
    curl_easy_cleanup(escape_curl);

    if (url.find("include=") == std::string::npos)
        url += (url.find('?') == std::string::npos) ? "?include=roles" : "&include=roles";

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        error = "curl-init-failed";
        return std::nullopt;
    }

    std::string body;
    struct curl_slist *headers = nullptr;
    std::string header_name = "X-Token";
    std::string auth_header = header_name + ": " + api_key;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    long timeout_ms = app_config::get_int_or(db, app_config::kMidataApiTimeoutMs, 8000, 1000, 60000);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, midata_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
    {
        error = "request-failed";
        return std::nullopt;
    }
    if (status < 200 || status >= 300)
    {
        error = "http-status-" + std::to_string(status);
        return std::nullopt;
    }

    auto payload = nlohmann::json::parse(body, nullptr, false);
    if (payload.is_discarded())
    {
        error = "invalid-json";
        return std::nullopt;
    }

    const nlohmann::json *people = find_people_array(payload);
    if (!people)
    {
        error = "people-array-missing";
        return std::nullopt;
    }

    int count = 0;
    for (const auto &entry : *people)
    {
        if (!entry.is_object())
            continue;
        bool has_roles = entry.contains("links") &&
                         entry["links"].is_object() &&
                         entry["links"].contains("roles") &&
                         entry["links"]["roles"].is_array() &&
                         !entry["links"]["roles"].empty();
        if (!has_roles)
            ++count;
    }
    return count;
}

struct MidataCountCacheEntry
{
    std::optional<int> count;
    bool configured = false;
    std::string error;
    std::chrono::steady_clock::time_point expires_at;
};

std::mutex midata_cache_mutex;
std::unordered_map<std::string, MidataCountCacheEntry> midata_count_cache;
constexpr auto kMidataCountCacheTtl = std::chrono::minutes(5);

std::optional<int> get_cached_midata_children_count(const std::string &group_id,
                                                    std::string &error,
                                                    bool &configured)
{
    std::lock_guard<std::mutex> lock(midata_cache_mutex);
    auto it = midata_count_cache.find(group_id);
    if (it == midata_count_cache.end())
        return std::nullopt;
    if (std::chrono::steady_clock::now() >= it->second.expires_at)
    {
        midata_count_cache.erase(it);
        return std::nullopt;
    }

    error = it->second.error;
    configured = it->second.configured;
    return it->second.count;
}

void store_cached_midata_children_count(const std::string &group_id,
                                        const std::optional<int> &count,
                                        bool configured,
                                        const std::string &error)
{
    std::lock_guard<std::mutex> lock(midata_cache_mutex);
    midata_count_cache[group_id] = MidataCountCacheEntry{
        .count = count,
        .configured = configured,
        .error = error,
        .expires_at = std::chrono::steady_clock::now() + kMidataCountCacheTtl,
    };
}

} // namespace

std::optional<int> fetch_midata_children_count_cached(Database &db,
                                                      const std::string &group_id,
                                                      std::string &error)
{
    bool configured = false;
    auto cached = get_cached_midata_children_count(group_id, error, configured);
    if (cached || configured || !error.empty())
        return cached;

    auto count = fetch_midata_children_count(db, group_id, error);
    configured = count.has_value() || error != "not-configured";
    store_cached_midata_children_count(group_id, count, configured, error);
    return count;
}

void clear_cached_midata_children_counts()
{
    std::lock_guard<std::mutex> lock(midata_cache_mutex);
    midata_count_cache.clear();
}
