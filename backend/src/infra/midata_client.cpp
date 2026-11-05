#include "infra/midata_client.hpp"
#include "infra/app_config.hpp"
#include "db/database.hpp"
#include "json.hpp"
#include <algorithm>
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
        error = std::string("request-failed: ") + curl_easy_strerror(rc);
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

    return count_midata_children(payload, group_id, error);
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

std::optional<int> count_midata_children(const nlohmann::json &payload,
                                         const std::string &group_id,
                                         std::string &error)
{
    error.clear();
    const auto *people = find_people_array(payload);
    if (!people)
    {
        error = "people-array-missing";
        return std::nullopt;
    }

    auto id_string = [](const nlohmann::json &id) -> std::string
    {
        if (id.is_string())
            return id.get<std::string>();
        if (id.is_number_integer())
            return id.dump();
        return "";
    };
    std::unordered_map<std::string, nlohmann::json> roles;
    if (payload.is_object() && payload.contains("linked") &&
        payload["linked"].is_object() && payload["linked"].contains("roles") &&
        payload["linked"]["roles"].is_array())
    {
        for (const auto &role : payload["linked"]["roles"])
            if (role.is_object() && role.contains("id"))
                roles[id_string(role["id"])] = role;
    }
    if (payload.is_object() && payload.contains("included") && payload["included"].is_array())
    {
        for (const auto &role : payload["included"])
            if (role.is_object() && role.value("type", "") == "roles" && role.contains("id"))
                roles[id_string(role["id"])] = role;
    }

    int count = 0;
    for (const auto &person : *people)
    {
        if (!person.is_object())
        {
            error = "person-invalid";
            return std::nullopt;
        }
        const nlohmann::json *person_roles = nullptr;
        if (person.contains("links") && person["links"].is_object() &&
            person["links"].contains("roles") && person["links"]["roles"].is_array())
            person_roles = &person["links"]["roles"];
        else if (person.contains("relationships") && person["relationships"].is_object() &&
                 person["relationships"].contains("roles") &&
                 person["relationships"]["roles"].is_object() &&
                 person["relationships"]["roles"].contains("data") &&
                 person["relationships"]["roles"]["data"].is_array())
            person_roles = &person["relationships"]["roles"]["data"];
        else if (person.contains("roles") && person["roles"].is_array())
            person_roles = &person["roles"];

        if (!person_roles)
        {
            error = "person-roles-missing";
            return std::nullopt;
        }
        bool participant = person_roles->empty();
        for (const auto &reference : *person_roles)
        {
            const auto id = reference.is_object() && reference.contains("id")
                                ? id_string(reference["id"]) : id_string(reference);
            auto it = roles.find(id);
            const auto *role = it != roles.end() ? &it->second :
                               reference.is_object() ? &reference : nullptr;
            if (!role)
            {
                error = "role-details-missing";
                return std::nullopt;
            }
            const bool json_api_role = role->contains("attributes") && (*role)["attributes"].is_object();
            const auto &attrs = json_api_role ? (*role)["attributes"] : *role;
            if (!attrs.contains("role_class") && !attrs.contains("role_type") &&
                !(json_api_role && attrs.contains("type")))
            {
                error = "role-details-missing";
                return std::nullopt;
            }
            if (attrs.contains("group_id") && id_string(attrs["group_id"]) != group_id)
                continue;
            if (role->contains("links") && (*role)["links"].is_object() &&
                (*role)["links"].contains("group") &&
                id_string((*role)["links"]["group"]) != group_id)
                continue;
            if (role->contains("relationships") && (*role)["relationships"].is_object() &&
                (*role)["relationships"].contains("group"))
            {
                const auto &group = (*role)["relationships"]["group"];
                if (group.is_object() && group.contains("data") && group["data"].is_object() &&
                    group["data"].contains("id") && id_string(group["data"]["id"]) != group_id)
                    continue;
            }

            std::string role_class = attrs.value("role_class", json_api_role ? attrs.value("type", "") : "");
            auto separator = role_class.rfind("::");
            std::string name = separator == std::string::npos ? role_class : role_class.substr(separator + 2);
            static const std::string child_roles[] = {
                "Biber", "Wolf", "Leitwolf", "Pfadi", "Leitpfadi", "Pio", "Rover", "Teilnehmer"};
            const auto label = attrs.value("role_type", "");
            if (std::find(std::begin(child_roles), std::end(child_roles), name) != std::end(child_roles) ||
                (role_class.empty() && (label == "Teilnehmer" || label == "Teilnehmer/-in")))
                participant = true;
        }
        if (participant)
            ++count;
    }
    return count;
}

std::optional<int> fetch_midata_children_count_cached(Database &db,
                                                      const std::string &group_id,
                                                      std::string &error)
{
    error.clear();
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
