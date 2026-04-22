#include "handlers.hpp"
#include "models.hpp"
#include "json.hpp"
#include "graph.hpp"
#include "utils.hpp"
#include <string>
#include <memory>
#include <algorithm>
#include <ctime>
#include <map>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <regex>
#include <sstream>
#include <limits>
#include <vector>
#include <utility>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <curl/curl.h>

#if !defined(DPW_ENABLE_DEBUG_AUTH)
#define DPW_ENABLE_DEBUG_AUTH 0
#endif

static nlohmann::json notification_to_json(const NotificationRecord &n);

namespace
{

    static size_t midata_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *out = static_cast<std::string *>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::string to_lower_ascii(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim_ascii(std::string s)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                        { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c)
                             { return !std::isspace(c); })
                    .base(),
                s.end());
        return s;
    }

    std::string public_base_url()
    {
        static const char *keys[] = {"DPW_PUBLIC_URL", "PUBLIC_BASE_URL", "APP_BASE_URL"};
        for (const char *k : keys)
        {
            const char *v = std::getenv(k);
            if (v && *v)
            {
                std::string out = trim_ascii(v);
                while (!out.empty() && out.back() == '/')
                    out.pop_back();
                if (!out.empty())
                    return out;
            }
        }
        // Last-resort local fallback when no public URL env is configured.
        return "http://localhost:8000";
    }

    std::string activity_absolute_link(const std::string &activity_id)
    {
        return public_base_url() + "/activities/" + activity_id;
    }

    std::string env_or(const char *key, const std::string &fallback = "")
    {
        const char *v = std::getenv(key);
        if (!v)
            return fallback;
        std::string out = trim_ascii(v);
        return out.empty() ? fallback : out;
    }

    std::string base64url_encode(const std::string &raw)
    {
        if (raw.empty())
            return "";
        std::string out;
        out.resize(4 * ((raw.size() + 2) / 3));
        int len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                                  reinterpret_cast<const unsigned char *>(raw.data()),
                                  static_cast<int>(raw.size()));
        if (len < 0)
            return "";
        out.resize(static_cast<size_t>(len));
        for (char &c : out)
        {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
        }
        while (!out.empty() && out.back() == '=')
            out.pop_back();
        return out;
    }

    std::string base64url_decode(const std::string &in)
    {
        if (in.empty())
            return "";
        std::string b64 = in;
        for (char &c : b64)
        {
            if (c == '-')
                c = '+';
            else if (c == '_')
                c = '/';
        }
        while (b64.size() % 4 != 0)
            b64.push_back('=');

        std::string out;
        out.resize((b64.size() / 4) * 3);
        int len = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                                  reinterpret_cast<const unsigned char *>(b64.data()),
                                  static_cast<int>(b64.size()));
        if (len < 0)
            return "";
        out.resize(static_cast<size_t>(len));
        return out;
    }

    std::string endpoint_origin(const std::string &endpoint)
    {
        auto scheme_pos = endpoint.find("://");
        if (scheme_pos == std::string::npos)
            return "";
        auto start = scheme_pos + 3;
        auto slash = endpoint.find('/', start);
        if (slash == std::string::npos)
            return endpoint;
        return endpoint.substr(0, slash);
    }

    std::optional<std::string> build_vapid_jwt_for_audience(const std::string &audience)
    {
        const std::string vapid_pub = env_or("DPW_VAPID_PUBLIC_KEY");
        const std::string vapid_priv_b64u = env_or("DPW_VAPID_PRIVATE_KEY");
        const std::string vapid_sub = env_or("DPW_VAPID_SUBJECT", "mailto:admin@localhost");
        if (vapid_pub.empty() || vapid_priv_b64u.empty() || audience.empty())
            return std::nullopt;

        const std::string header = R"({"typ":"JWT","alg":"ES256"})";
        const std::time_t now = std::time(nullptr);
        const std::time_t exp = now + (12 * 60 * 60);
        nlohmann::json payload = {
            {"aud", audience},
            {"exp", exp},
            {"sub", vapid_sub},
        };

        const std::string enc_header = base64url_encode(header);
        const std::string enc_payload = base64url_encode(payload.dump());
        if (enc_header.empty() || enc_payload.empty())
            return std::nullopt;

        const std::string signing_input = enc_header + "." + enc_payload;

        std::string priv_raw = base64url_decode(vapid_priv_b64u);
        if (priv_raw.size() < 32)
            return std::nullopt;
        if (priv_raw.size() > 32)
            priv_raw.resize(32);

        EVP_PKEY_CTX *pkey_ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!pkey_ctx)
            return std::nullopt;

        EVP_PKEY *pkey = nullptr;
        if (EVP_PKEY_fromdata_init(pkey_ctx) <= 0)
        {
            EVP_PKEY_CTX_free(pkey_ctx);
            return std::nullopt;
        }

        OSSL_PARAM params[3];
        params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                     const_cast<char *>("prime256v1"),
                                                     0);
        params[1] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                                      const_cast<char *>(priv_raw.data()),
                                                      priv_raw.size());
        params[2] = OSSL_PARAM_construct_end();

        if (EVP_PKEY_fromdata(pkey_ctx, &pkey, EVP_PKEY_KEYPAIR, params) <= 0 || !pkey)
        {
            EVP_PKEY_CTX_free(pkey_ctx);
            return std::nullopt;
        }
        EVP_PKEY_CTX_free(pkey_ctx);

        EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
        if (!md_ctx)
        {
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }

        if (EVP_DigestSignInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) <= 0 ||
            EVP_DigestSignUpdate(md_ctx, signing_input.data(), signing_input.size()) <= 0)
        {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }

        size_t der_len = 0;
        if (EVP_DigestSignFinal(md_ctx, nullptr, &der_len) <= 0 || der_len == 0)
        {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }

        std::string der_sig(der_len, '\0');
        if (EVP_DigestSignFinal(md_ctx,
                                reinterpret_cast<unsigned char *>(&der_sig[0]),
                                &der_len) <= 0)
        {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);

        const unsigned char *der_ptr = reinterpret_cast<const unsigned char *>(der_sig.data());
        ECDSA_SIG *sig = d2i_ECDSA_SIG(nullptr, &der_ptr, static_cast<long>(der_len));
        if (!sig)
            return std::nullopt;

        const BIGNUM *r = nullptr;
        const BIGNUM *s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        if (!r || !s)
        {
            ECDSA_SIG_free(sig);
            return std::nullopt;
        }

        std::string sig_raw(64, '\0');
        if (BN_bn2binpad(r, reinterpret_cast<unsigned char *>(&sig_raw[0]), 32) != 32 ||
            BN_bn2binpad(s, reinterpret_cast<unsigned char *>(&sig_raw[32]), 32) != 32)
        {
            ECDSA_SIG_free(sig);
            return std::nullopt;
        }
        ECDSA_SIG_free(sig);

        const std::string enc_sig = base64url_encode(sig_raw);
        if (enc_sig.empty())
            return std::nullopt;
        return signing_input + "." + enc_sig;
    }

    long send_web_push_empty(const std::string &endpoint, const std::string &jwt, const std::string &vapid_pub)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
            return 0;

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "TTL: 60");
        headers = curl_slist_append(headers, "Urgency: normal");
        headers = curl_slist_append(headers, "Content-Length: 0");
        std::string auth = "Authorization: vapid t=" + jwt + ", k=" + vapid_pub;
        headers = curl_slist_append(headers, auth.c_str());

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, midata_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        if (rc == CURLE_OK)
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK)
            return 0;
        return status;
    }

    void deliver_web_push_for_user(Database &db, const UserRecord &user)
    {
        const std::string vapid_pub = env_or("DPW_VAPID_PUBLIC_KEY");
        if (vapid_pub.empty())
            return;

        auto subscriptions = db.list_push_subscriptions_for_user(user.id);
        for (const auto &sub : subscriptions)
        {
            std::string aud = endpoint_origin(sub.endpoint);
            if (aud.empty())
                continue;

            auto jwt = build_vapid_jwt_for_audience(aud);
            if (!jwt)
                continue;

            long status = send_web_push_empty(sub.endpoint, *jwt, vapid_pub);
            // Endpoint no longer valid.
            if (status == 404 || status == 410)
                db.delete_push_subscription_by_endpoint(sub.endpoint);
        }
    }

    std::string format_date_ddmmyyyy(const std::string &iso)
    {
        if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-')
            return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
        return iso;
    }

    std::string join_display(const std::vector<std::string> &items)
    {
        std::string out;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].empty())
                continue;
            if (!out.empty())
                out += ", ";
            out += items[i];
        }
        if (out.empty())
            return "-";
        return out;
    }

    std::vector<std::string> unique_names_from_material(const std::vector<MaterialItem> &material)
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;
        for (const auto &m : material)
        {
            for (const auto &name : m.responsible)
            {
                std::string t = trim_ascii(name);
                if (t.empty())
                    continue;
                std::string key = to_lower_ascii(t);
                if (seen.insert(key).second)
                    out.push_back(t);
            }
        }
        return out;
    }

    std::string normalize_assignee_key(const std::string &name)
    {
        return to_lower_ascii(trim_ascii(name));
    }

    std::string normalize_material_key(const std::string &name)
    {
        return to_lower_ascii(trim_ascii(name));
    }

    bool user_matches_assignee(const UserRecord &user, const std::string &assignee)
    {
        const std::string assignee_key = normalize_assignee_key(assignee);
        if (assignee_key.empty())
            return false;

        if (assignee_key == normalize_assignee_key(user.display_name))
            return true;
        if (assignee_key == normalize_assignee_key(user.email))
            return true;

        const auto at = user.email.find('@');
        if (at != std::string::npos)
        {
            const std::string mail_local = user.email.substr(0, at);
            if (assignee_key == normalize_assignee_key(mail_local))
                return true;
        }

        return false;
    }

    bool user_in_assignee_list(const std::vector<std::string> &assignees, const UserRecord &user)
    {
        for (const auto &assignee : assignees)
        {
            if (user_matches_assignee(user, assignee))
                return true;
        }
        return false;
    }

    std::vector<std::string> newly_assigned_users_from_material_delta(const std::vector<MaterialItem> &old_material,
                                                                      const std::vector<MaterialItem> &new_material)
    {
        std::unordered_set<std::string> old_pairs;
        for (const auto &m : old_material)
        {
            const std::string material_key = normalize_material_key(m.name);
            if (material_key.empty())
                continue;
            for (const auto &name : m.responsible)
            {
                const std::string assignee_key = normalize_assignee_key(name);
                if (assignee_key.empty())
                    continue;
                old_pairs.insert(material_key + "\n" + assignee_key);
            }
        }

        std::vector<std::string> out;
        std::unordered_set<std::string> seen_assignees;
        for (const auto &m : new_material)
        {
            const std::string material_key = normalize_material_key(m.name);
            if (material_key.empty())
                continue;
            for (const auto &name : m.responsible)
            {
                const std::string trimmed_name = trim_ascii(name);
                const std::string assignee_key = normalize_assignee_key(trimmed_name);
                if (assignee_key.empty())
                    continue;

                const std::string pair_key = material_key + "\n" + assignee_key;
                if (old_pairs.find(pair_key) != old_pairs.end())
                    continue;

                if (seen_assignees.insert(assignee_key).second)
                    out.push_back(trimmed_name);
            }
        }
        return out;
    }

    bool contains_name_ci(const std::vector<std::string> &names, const std::string &needle)
    {
        std::string key = to_lower_ascii(trim_ascii(needle));
        if (key.empty())
            return false;
        for (const auto &n : names)
        {
            if (to_lower_ascii(trim_ascii(n)) == key)
                return true;
        }
        return false;
    }

    std::string sanitize_meteo_text(std::string s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 32 && c <= 126)
            {
                out.push_back(static_cast<char>(c));
                continue;
            }

            switch (c)
            {
            case 0xE4:
                out += "ae";
                break; // ä
            case 0xF6:
                out += "oe";
                break; // ö
            case 0xFC:
                out += "ue";
                break; // ü
            case 0xC4:
                out += "Ae";
                break; // Ä
            case 0xD6:
                out += "Oe";
                break; // Ö
            case 0xDC:
                out += "Ue";
                break; // Ü
            case 0xDF:
                out += "ss";
                break; // ß
            default:
                // skip non-ASCII control/extended bytes to keep JSON UTF-8 safe
                break;
            }
        }
        return trim_ascii(out);
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

    std::optional<int> fetch_midata_children_count(const std::string &group_id,
                                                   std::string &error)
    {
        const char *api_key_env = std::getenv("MIDATA_API_KEY");
        if (!api_key_env || std::string(api_key_env).empty())
        {
            error = "not-configured";
            return std::nullopt;
        }

        std::string url_tmpl = std::getenv("MIDATA_API_URL_TEMPLATE")
                                   ? std::getenv("MIDATA_API_URL_TEMPLATE")
                                   : "https://db.scout.ch/de/groups/{group_id}/people.json";

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
        std::string auth_header = header_name + ": " + std::string(api_key_env);
        headers = curl_slist_append(headers, auth_header.c_str());
        headers = curl_slist_append(headers, "Accept: application/json");

        long timeout_ms = 8000;
        if (const char *timeout_env = std::getenv("MIDATA_API_TIMEOUT_MS"))
        {
            try
            {
                timeout_ms = std::stol(timeout_env);
            }
            catch (...)
            {
            }
        }

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

    struct MeteoPointMeta
    {
        int point_id = 0;
        int point_type_id = 0;
        std::string postal_code;
        std::string point_name;
    };

    struct WeatherResult
    {
        std::string mode; // forecast | seasonal-average
        double temperature_c = 0.0;
        std::optional<double> temperature_min_c;
        std::optional<double> temperature_max_c;
        std::vector<std::pair<time_t, double>> hourly_temps;
        std::optional<int> rain_probability_percent;
        std::vector<std::pair<time_t, int>> hourly_rain_probability;
        std::string weather_symbol;
        std::string season;
        std::string point_name;
        std::string postal_code;
        std::string source;
        std::string note;
    };

    struct MidataCountCacheEntry
    {
        std::optional<int> count;
        bool configured = false;
        std::string error;
        std::chrono::steady_clock::time_point expires_at;
    };

    std::mutex meteo_cache_mutex;
    std::vector<MeteoPointMeta> meteo_point_cache;
    std::chrono::steady_clock::time_point meteo_point_cache_expires;
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

    std::optional<int> fetch_midata_children_count_cached(const std::string &group_id,
                                                          std::string &error)
    {
        bool configured = false;
        auto cached = get_cached_midata_children_count(group_id, error, configured);
        if (cached || configured || !error.empty())
            return cached;

        auto count = fetch_midata_children_count(group_id, error);
        configured = count.has_value() || error != "not-configured";
        store_cached_midata_children_count(group_id, count, configured, error);
        return count;
    }

    std::optional<std::string> http_get_text(const std::string &url, long timeout_ms, std::string &error)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            error = "curl-init-failed";
            return std::nullopt;
        }
        std::string body;
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json,text/csv,*/*");

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
        return body;
    }

    time_t parse_compact_utc(const std::string &s)
    {
        if (s.size() != 12)
            return -1;
        std::tm tm{};
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(s.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(6, 2));
        tm.tm_hour = std::stoi(s.substr(8, 2));
        tm.tm_min = std::stoi(s.substr(10, 2));
        tm.tm_sec = 0;
        return timegm(&tm);
    }

    int days_in_month(int year, int month)
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12)
            return 31;
        if (month != 2)
            return days[month - 1];
        bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap_year ? 29 : 28;
    }

    int weekday_utc(int year, int month, int day)
    {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = 12;
        time_t ts = timegm(&tm);
        std::tm *utc = gmtime(&ts);
        return utc ? utc->tm_wday : 0;
    }

    int last_sunday_of_month(int year, int month)
    {
        int last_day = days_in_month(year, month);
        int weekday = weekday_utc(year, month, last_day);
        return last_day - weekday;
    }

    bool is_switzerland_dst_local(int year, int month, int day, int hour)
    {
        if (month < 3 || month > 10)
            return false;
        if (month > 3 && month < 10)
            return true;

        if (month == 3)
        {
            int change_day = last_sunday_of_month(year, 3);
            if (day > change_day)
                return true;
            if (day < change_day)
                return false;
            return hour >= 2;
        }

        int change_day = last_sunday_of_month(year, 10);
        if (day < change_day)
            return true;
        if (day > change_day)
            return false;
        return hour < 3;
    }

    time_t parse_activity_local_to_utc(const std::string &date, const std::string &time)
    {
        if (date.size() < 10 || time.size() < 5)
            return -1;

        int year = std::stoi(date.substr(0, 4));
        int month = std::stoi(date.substr(5, 2));
        int day = std::stoi(date.substr(8, 2));
        int hour = std::stoi(time.substr(0, 2));
        int minute = std::stoi(time.substr(3, 2));

        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;

        time_t local_as_utc = timegm(&tm);
        int offset_seconds = is_switzerland_dst_local(year, month, day, hour) ? 2 * 3600 : 1 * 3600;
        return local_as_utc - offset_seconds;
    }

    time_t parse_activity_start_utc(const std::string &date, const std::string &start_time)
    {
        return parse_activity_local_to_utc(date, start_time);
    }

    time_t parse_activity_end_utc(const std::string &date, const std::string &end_time, time_t start_ts)
    {
        time_t end_ts = parse_activity_local_to_utc(date, end_time);
        if (start_ts > 0 && end_ts > 0 && end_ts <= start_ts)
            end_ts += 24 * 3600;
        return end_ts;
    }

    std::string season_from_month(int month)
    {
        if (month == 12 || month <= 2)
            return "Winter";
        if (month <= 5)
            return "Fruehling";
        if (month <= 8)
            return "Sommer";
        return "Herbst";
    }

    double seasonal_avg_temp_for_month(int month)
    {
        if (month == 12 || month <= 2)
            return 2.0;
        if (month <= 5)
            return 11.0;
        if (month <= 8)
            return 21.0;
        return 12.0;
    }

    WeatherResult seasonal_average_weather(time_t activity_ts, const std::string &note)
    {
        std::tm *ptm = gmtime(&activity_ts);
        int month = ptm ? (ptm->tm_mon + 1) : 1;
        WeatherResult out;
        out.mode = "seasonal-average";
        out.temperature_c = seasonal_avg_temp_for_month(month);
        out.temperature_min_c = out.temperature_c - 3.0;
        out.temperature_max_c = out.temperature_c + 3.0;
        out.season = season_from_month(month);
        out.weather_symbol = (month == 12 || month <= 2) ? "cloud" : "partly-cloudy";
        out.source = "MeteoSwiss (saisonaler Durchschnitt CH)";
        out.note = note;
        return out;
    }

    std::string pick_weather_symbol(double temp_c, const std::optional<int> &rain_probability_percent)
    {
        if (rain_probability_percent && *rain_probability_percent >= 65)
            return "rain";
        if (temp_c <= 0.0 && rain_probability_percent && *rain_probability_percent >= 45)
            return "snow";
        if (rain_probability_percent && *rain_probability_percent >= 35)
            return "partly-cloudy";
        return "sun";
    }

    std::optional<std::vector<MeteoPointMeta>> load_meteoswiss_points(std::string &error)
    {
        {
            std::lock_guard<std::mutex> lock(meteo_cache_mutex);
            if (!meteo_point_cache.empty() && std::chrono::steady_clock::now() < meteo_point_cache_expires)
                return meteo_point_cache;
        }

        auto collection_body = http_get_text("https://data.geo.admin.ch/api/stac/v1/collections/ch.meteoschweiz.ogd-local-forecasting", 12000, error);
        if (!collection_body)
            return std::nullopt;
        auto collection = nlohmann::json::parse(*collection_body, nullptr, false);
        if (collection.is_discarded() || !collection.is_object() || !collection.contains("assets"))
        {
            error = "meteo-collection-invalid";
            return std::nullopt;
        }
        std::string point_meta_url;
        if (collection["assets"].contains("ogd-local-forecasting_meta_point.csv") &&
            collection["assets"]["ogd-local-forecasting_meta_point.csv"].is_object() &&
            collection["assets"]["ogd-local-forecasting_meta_point.csv"].contains("href"))
        {
            point_meta_url = collection["assets"]["ogd-local-forecasting_meta_point.csv"]["href"].get<std::string>();
        }
        if (point_meta_url.empty())
        {
            error = "meteo-point-meta-url-missing";
            return std::nullopt;
        }

        auto csv_body = http_get_text(point_meta_url, 12000, error);
        if (!csv_body)
            return std::nullopt;

        std::vector<MeteoPointMeta> points;
        std::istringstream iss(*csv_body);
        std::string line;
        bool first = true;
        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            std::vector<std::string> cols;
            std::stringstream ls(line);
            std::string col;
            while (std::getline(ls, col, ';'))
                cols.push_back(col);
            if (cols.size() < 5)
                continue;
            MeteoPointMeta p;
            try
            {
                p.point_id = std::stoi(cols[0]);
                p.point_type_id = std::stoi(cols[1]);
            }
            catch (...)
            {
                continue;
            }
            p.postal_code = sanitize_meteo_text(cols[3]);
            p.point_name = sanitize_meteo_text(cols[4]);
            points.push_back(std::move(p));
        }

        if (points.empty())
        {
            error = "meteo-point-meta-empty";
            return std::nullopt;
        }

        {
            std::lock_guard<std::mutex> lock(meteo_cache_mutex);
            meteo_point_cache = points;
            meteo_point_cache_expires = std::chrono::steady_clock::now() + std::chrono::hours(6);
        }
        return points;
    }

    std::optional<MeteoPointMeta> find_point_for_location(const std::string &location,
                                                          const std::vector<MeteoPointMeta> &points)
    {
        std::smatch m;
        std::regex zip_re("(^|[^0-9])([0-9]{4})([^0-9]|$)");
        if (std::regex_search(location, m, zip_re) && m.size() >= 3)
        {
            std::string zip = m[2].str();
            for (const auto &p : points)
            {
                if (p.postal_code == zip && p.point_type_id != 1)
                    return p;
            }
            for (const auto &p : points)
            {
                if (p.postal_code == zip)
                    return p;
            }

            // Fallback for postal-code-only inputs: choose nearest available postal code.
            int zip_num = -1;
            try
            {
                zip_num = std::stoi(zip);
            }
            catch (...)
            {
                zip_num = -1;
            }
            if (zip_num >= 0)
            {
                const MeteoPointMeta *best = nullptr;
                int best_diff = std::numeric_limits<int>::max();
                for (const auto &p : points)
                {
                    if (p.postal_code.size() != 4)
                        continue;
                    int p_zip_num = -1;
                    try
                    {
                        p_zip_num = std::stoi(p.postal_code);
                    }
                    catch (...)
                    {
                        continue;
                    }
                    int diff = std::abs(p_zip_num - zip_num);
                    if (!best || diff < best_diff ||
                        (diff == best_diff && best->point_type_id == 1 && p.point_type_id != 1))
                    {
                        best = &p;
                        best_diff = diff;
                    }
                }
                if (best)
                    return *best;
            }
        }

        std::string loc_lower = to_lower_ascii(trim_ascii(location));
        if (loc_lower.empty())
            return std::nullopt;

        for (const auto &p : points)
        {
            if (!p.point_name.empty())
            {
                std::string point_lower = to_lower_ascii(p.point_name);
                if (loc_lower.find(point_lower) != std::string::npos || point_lower.find(loc_lower) != std::string::npos)
                    return p;
            }
        }

        // Token fallback for city names inside longer location strings.
        std::string token;
        std::vector<std::string> tokens;
        for (char c : loc_lower)
        {
            if (std::isalnum(static_cast<unsigned char>(c)))
            {
                token.push_back(c);
            }
            else if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
        }
        if (!token.empty())
            tokens.push_back(token);

        for (const auto &t : tokens)
        {
            if (t.size() < 3)
                continue;
            bool digits_only = std::all_of(t.begin(), t.end(), [](unsigned char c)
                                           { return std::isdigit(c); });
            if (digits_only)
                continue;

            for (const auto &p : points)
            {
                if (p.point_name.empty())
                    continue;
                std::string point_lower = to_lower_ascii(p.point_name);
                if (point_lower.find(t) != std::string::npos)
                    return p;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> get_latest_asset_url(const std::string &asset_suffix, std::string &error)
    {
        auto body = http_get_text("https://data.geo.admin.ch/api/stac/v1/collections/ch.meteoschweiz.ogd-local-forecasting/items?limit=1", 12000, error);
        if (!body)
            return std::nullopt;
        auto payload = nlohmann::json::parse(*body, nullptr, false);
        if (payload.is_discarded() || !payload.contains("features") || !payload["features"].is_array() || payload["features"].empty())
        {
            error = "meteo-items-invalid";
            return std::nullopt;
        }
        const auto &feature = payload["features"][0];
        if (!feature.contains("assets") || !feature["assets"].is_object())
        {
            error = "meteo-assets-missing";
            return std::nullopt;
        }
        for (auto it = feature["assets"].begin(); it != feature["assets"].end(); ++it)
        {
            if (!it.value().is_object() || !it.value().contains("href") || !it.value()["href"].is_string())
                continue;
            std::string href = it.value()["href"].get<std::string>();
            if (href.find(asset_suffix) != std::string::npos)
                return href;
        }
        error = "meteo-asset-missing-" + asset_suffix;
        return std::nullopt;
    }

    std::optional<std::string> get_latest_tre200h0_url(std::string &error)
    {
        return get_latest_asset_url("tre200h0.csv", error);
    }

    std::optional<std::string> get_latest_rre150h0_url(std::string &error)
    {
        return get_latest_asset_url("rre150h0.csv", error);
    }

    std::optional<double> lookup_forecast_temp_for_point(const std::string &csv_url,
                                                         int point_id,
                                                         time_t target_ts,
                                                         std::string &error)
    {
        auto csv_body = http_get_text(csv_url, 15000, error);
        if (!csv_body)
            return std::nullopt;

        std::istringstream iss(*csv_body);
        std::string line;
        bool first = true;
        long long best_diff = std::numeric_limits<long long>::max();
        std::optional<double> best_temp = std::nullopt;
        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            std::vector<std::string> cols;
            std::stringstream ls(line);
            std::string col;
            while (std::getline(ls, col, ';'))
                cols.push_back(col);
            if (cols.size() < 4)
                continue;
            int row_point_id = 0;
            try
            {
                row_point_id = std::stoi(cols[0]);
            }
            catch (...)
            {
                continue;
            }
            if (row_point_id != point_id)
                continue;
            time_t row_ts = parse_compact_utc(cols[2]);
            if (row_ts <= 0)
                continue;
            double temp = 0.0;
            try
            {
                temp = std::stod(cols[3]);
            }
            catch (...)
            {
                continue;
            }
            long long diff = std::llabs(static_cast<long long>(row_ts) - static_cast<long long>(target_ts));
            if (diff < best_diff)
            {
                best_diff = diff;
                best_temp = temp;
            }
        }
        if (!best_temp)
        {
            error = "meteo-temp-point-missing";
            return std::nullopt;
        }
        return best_temp;
    }

    std::optional<int> lookup_rain_probability_for_point(const std::string &csv_url,
                                                         int point_id,
                                                         time_t target_ts,
                                                         std::string &error)
    {
        auto precip_mm = lookup_forecast_temp_for_point(csv_url, point_id, target_ts, error);
        if (!precip_mm)
            return std::nullopt;

        auto precip_mm_to_probability = [](double precip_mm)
        {
            // rre150h0 is precipitation amount; map it to a simple probability for UI display.
            if (precip_mm <= 0.0)
                return 5;

            int probability = static_cast<int>(15.0 + (precip_mm * 25.0));
            if (probability < 5)
                probability = 5;
            if (probability > 95)
                probability = 95;
            return probability;
        };

        return precip_mm_to_probability(*precip_mm);
    }

    std::optional<std::vector<std::pair<time_t, double>>> lookup_forecast_series_for_point(const std::string &csv_url,
                                                                                           int point_id,
                                                                                           time_t start_ts,
                                                                                           time_t end_ts,
                                                                                           std::string &error)
    {
        auto csv_body = http_get_text(csv_url, 15000, error);
        if (!csv_body)
            return std::nullopt;

        std::istringstream iss(*csv_body);
        std::string line;
        bool first = true;
        std::vector<std::pair<time_t, double>> points;

        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (first)
            {
                first = false;
                continue;
            }

            std::vector<std::string> cols;
            std::stringstream ls(line);
            std::string col;
            while (std::getline(ls, col, ';'))
                cols.push_back(col);
            if (cols.size() < 4)
                continue;

            int row_point_id = 0;
            try
            {
                row_point_id = std::stoi(cols[0]);
            }
            catch (...)
            {
                continue;
            }
            if (row_point_id != point_id)
                continue;

            time_t row_ts = parse_compact_utc(cols[2]);
            if (row_ts <= 0 || row_ts < start_ts || row_ts > end_ts)
                continue;

            double temp = 0.0;
            try
            {
                temp = std::stod(cols[3]);
            }
            catch (...)
            {
                continue;
            }

            points.push_back({row_ts, temp});
        }

        if (points.empty())
        {
            error = "meteo-temp-range-missing";
            return std::nullopt;
        }

        std::sort(points.begin(), points.end(), [](const auto &a, const auto &b)
                  { return a.first < b.first; });
        return points;
    }

    std::optional<WeatherResult> fetch_expected_weather_for_activity(const Activity &activity,
                                                                     const std::string &location_input,
                                                                     std::string &error)
    {
        time_t activity_ts = parse_activity_start_utc(activity.date, activity.start_time);
        if (activity_ts <= 0)
        {
            error = "activity-datetime-invalid";
            return std::nullopt;
        }

        time_t activity_end_ts = parse_activity_end_utc(activity.date, activity.end_time, activity_ts);
        if (activity_end_ts <= 0)
        {
            activity_end_ts = activity_ts;
        }

        time_t now = time(nullptr);
        constexpr time_t kForecastHorizonSeconds = 9 * 24 * 3600;
        if (activity_ts > now + kForecastHorizonSeconds)
        {
            return seasonal_average_weather(activity_ts, "Termin liegt ausserhalb des MeteoSwiss-Vorhersagehorizonts.");
        }

        if (location_input.empty())
        {
            error = "location-missing";
            return std::nullopt;
        }

        auto points = load_meteoswiss_points(error);
        if (!points)
            return std::nullopt;
        auto point = find_point_for_location(location_input, *points);
        if (!point)
        {
            return seasonal_average_weather(activity_ts, "Ort konnte nicht auf MeteoSwiss-Punkt gemappt werden.");
        }

        auto tre_url = get_latest_tre200h0_url(error);
        if (!tre_url)
            return std::nullopt;
        auto temp = lookup_forecast_temp_for_point(*tre_url, point->point_id, activity_ts, error);
        if (!temp)
        {
            return seasonal_average_weather(activity_ts, "Keine punktgenaue Vorhersage verfuegbar; saisonaler Durchschnitt verwendet.");
        }

        std::string range_error;
        auto temp_series = lookup_forecast_series_for_point(*tre_url, point->point_id, activity_ts, activity_end_ts, range_error);

        WeatherResult out;
        out.mode = "forecast";
        out.temperature_c = *temp;
        if (temp_series)
        {
            out.hourly_temps = *temp_series;
            double min_temp = out.hourly_temps.front().second;
            double max_temp = out.hourly_temps.front().second;
            for (const auto &p : out.hourly_temps)
            {
                min_temp = std::min(min_temp, p.second);
                max_temp = std::max(max_temp, p.second);
            }
            out.temperature_min_c = min_temp;
            out.temperature_max_c = max_temp;
            out.temperature_c = (min_temp + max_temp) / 2.0;
        }
        else
        {
            out.temperature_min_c = *temp;
            out.temperature_max_c = *temp;
        }
        std::string rain_error;
        auto rain_url = get_latest_rre150h0_url(rain_error);
        if (rain_url)
        {
            out.rain_probability_percent = lookup_rain_probability_for_point(*rain_url, point->point_id, activity_ts, rain_error);
            auto rain_series = lookup_forecast_series_for_point(*rain_url, point->point_id, activity_ts, activity_end_ts, rain_error);
            if (rain_series)
            {
                for (const auto &p : *rain_series)
                {
                    double precip_mm = p.second;
                    int probability = 5;
                    if (precip_mm > 0.0)
                    {
                        probability = static_cast<int>(15.0 + (precip_mm * 25.0));
                        if (probability > 95)
                            probability = 95;
                    }
                    out.hourly_rain_probability.push_back({p.first, probability});
                }
            }
        }
        out.weather_symbol = pick_weather_symbol(out.temperature_c, out.rain_probability_percent);
        out.point_name = point->point_name;
        out.postal_code = point->postal_code;
        out.source = "MeteoSwiss Local Forecast";
        out.note = "Vorhersage basiert auf tre200h0 (stundlicher Mittelwert, naechster Zeitpunkt).";
        return out;
    }

    struct PublicSubmitBucket
    {
        std::deque<std::chrono::steady_clock::time_point> attempts;
    };

    std::mutex public_submit_rate_limit_mutex;
    std::unordered_map<std::string, PublicSubmitBucket> public_submit_rate_limit;

    constexpr size_t kMaxPublicFormPayloadBytes = 128 * 1024;
    constexpr size_t kPublicSubmitBurstLimit = 3;
    constexpr size_t kPublicSubmitSustainedLimit = 12;
    constexpr auto kPublicSubmitBurstWindow = std::chrono::seconds(30);
    constexpr auto kPublicSubmitSustainedWindow = std::chrono::minutes(10);
    constexpr size_t kMaxPublicAnswerLength = 4000;

    std::string normalize_public_submit_client_key(const std::string &ip_address, const std::string &user_agent)
    {
        if (!ip_address.empty())
            return ip_address;
        if (user_agent.empty())
            return "anonymous";
        return user_agent.substr(0, std::min<size_t>(user_agent.size(), 160));
    }

    bool is_public_submit_rate_limited(const std::string &public_slug, const std::string &client_key)
    {
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - kPublicSubmitSustainedWindow;
        std::lock_guard<std::mutex> lock(public_submit_rate_limit_mutex);
        auto &bucket = public_submit_rate_limit[public_slug + "|" + client_key];

        while (!bucket.attempts.empty() && bucket.attempts.front() < cutoff)
            bucket.attempts.pop_front();

        size_t burst_count = 0;
        auto burst_cutoff = now - kPublicSubmitBurstWindow;
        for (auto it = bucket.attempts.rbegin(); it != bucket.attempts.rend(); ++it)
        {
            if (*it < burst_cutoff)
                break;
            ++burst_count;
        }

        if (burst_count >= kPublicSubmitBurstLimit || bucket.attempts.size() >= kPublicSubmitSustainedLimit)
            return true;

        bucket.attempts.push_back(now);
        return false;
    }

    std::string utc_today_ymd()
    {
        time_t now = time(nullptr);
        std::tm *tm = gmtime(&now);
        char buf[11] = {0};
        if (!tm)
            return "";
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        return std::string(buf);
    }

    bool should_use_frozen_values(const Activity &activity)
    {
        const std::string today = utc_today_ymd();
        if (today.empty())
            return false;
        return !activity.date.empty() && activity.date < today;
    }

} // namespace

void set_cors(HttpRes *res)
{
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
}

// ── Debug token support ─────────────────────────────────────────────────────
static TokenClaims validate_token(const std::string &token)
{
#if DPW_ENABLE_DEBUG_AUTH
    if (token.rfind("debug:", 0) == 0)
    {
        std::string user_id = token.substr(6);
        if (user_id.empty())
            throw std::runtime_error("debug token: user_id missing");
        TokenClaims c;
        c.oid = "debug:" + user_id;
        c.email = "debug@local";
        c.display_name = "Debug";
        c.tid = "debug";
        return c;
    }
#endif
    return validate_microsoft_token(token);
}

bool require_auth(HttpRes *res, HttpReq *req, TokenClaims &out_claims)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return false;
    }
    try
    {
        out_claims = validate_token(token);
        return true;
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return false;
    }
}

std::string auth_token_from_header(HttpRes *res, const std::string &auth_header)
{
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
    }
    return token;
}

static std::optional<UserRecord> resolve_user(Database &db, const TokenClaims &claims)
{
    if (claims.oid.rfind("debug:", 0) == 0)
        return db.get_user_by_id(claims.oid.substr(6));
    return db.get_user_by_oid(claims.oid);
}

void send_json(HttpRes *res, int status, const std::string &body)
{
    res->writeStatus(status_text(status));
    set_cors(res);
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}

static void send_internal_error(HttpRes *res, const char *context, const std::exception &e)
{
    fprintf(stderr, "[error] %s: %s\n", context, e.what());
    send_json(res, 500, R"({"error":"Interner Serverfehler"})");
}

// ---- GET /departments -------------------------------------------------------

void handle_get_departments(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto depts = db.list_departments();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &d : depts)
            arr.push_back({{"name", d.name},
                           {"color", d.color},
                           {"midata_group_id", d.midata_group_id ? nlohmann::json(*d.midata_group_id) : nlohmann::json(nullptr)}});
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities --------------------------------------------------------

void handle_get_activities(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        auto activities = db.list_activities();

        std::optional<RolePermission> perm;
        std::vector<RoleDeptAccess> dept_access;
        if (current_user)
        {
            perm = db.get_role_permission(current_user->role);
            dept_access = db.list_role_dept_access(current_user->role);
        }

        nlohmann::json arr = nlohmann::json::array();
        for (auto &a : activities)
        {
            if (current_user && perm)
            {
                if (!can_read_activity(*perm, *current_user, dept_access, a))
                    continue;
            }
            arr.push_back(to_json(a));
        }
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id ----------------------------------------------------

void handle_get_activity(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (current_user)
        {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_activity_midata_children_count(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (current_user)
        {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        if (should_use_frozen_values(*activity))
        {
            auto frozen = db.get_activity_midata_children_value(id);
            if (frozen)
            {
                send_json(res, 200, nlohmann::json{{"configured", true}, {"children_count", *frozen}, {"mode", "frozen"}}.dump());
                return;
            }
            send_json(res, 200, nlohmann::json{{"configured", false}, {"children_count", nullptr}, {"mode", "frozen"}}.dump());
            return;
        }

        if (!activity->department)
        {
            send_json(res, 200, nlohmann::json{{"configured", false}, {"children_count", nullptr}}.dump());
            return;
        }

        auto departments = db.list_departments();
        std::optional<std::string> group_id = std::nullopt;
        for (const auto &dept : departments)
        {
            if (dept.name == *activity->department)
            {
                group_id = dept.midata_group_id;
                break;
            }
        }

        if (!group_id || group_id->empty())
        {
            send_json(res, 200, nlohmann::json{{"configured", false}, {"children_count", nullptr}}.dump());
            return;
        }

        std::string midata_error;
        auto count = fetch_midata_children_count_cached(*group_id, midata_error);
        if (!count)
        {
            bool configured = midata_error != "not-configured";
            send_json(res, 200, nlohmann::json{{"configured", configured}, {"children_count", nullptr}, {"error", midata_error}}.dump());
            return;
        }

        db.set_activity_midata_children_value(id, *count);

        send_json(res, 200, nlohmann::json{{"configured", true}, {"children_count", *count}, {"mode", "live"}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_midata_children_counts(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);
        if (!perm)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto departments = db.list_departments();
        nlohmann::json by_department = nlohmann::json::object();
        for (const auto &dept : departments)
        {
            if (!can_read_dept(*perm, *current_user, dept_access, dept.name))
                continue;

            if (!dept.midata_group_id || dept.midata_group_id->empty())
            {
                by_department[dept.name] = nlohmann::json{{"configured", false}, {"children_count", nullptr}};
                continue;
            }

            std::string midata_error;
            auto count = fetch_midata_children_count_cached(*dept.midata_group_id, midata_error);
            if (!count)
            {
                bool configured = midata_error != "not-configured";
                by_department[dept.name] = nlohmann::json{{"configured", configured}, {"children_count", nullptr}, {"error", midata_error}};
                continue;
            }

            by_department[dept.name] = nlohmann::json{{"configured", true}, {"children_count", *count}};
        }

        send_json(res, 200, nlohmann::json{{"departments", by_department}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_activity_weather_location(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (current_user)
        {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        auto location = db.get_activity_weather_location(id);
        send_json(res, 200, nlohmann::json{{"location", location ? nlohmann::json(*location) : nlohmann::json(nullptr)}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_put_activity_weather_location(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        auto perm = current_user ? db.get_role_permission(current_user->role) : std::optional<RolePermission>{};
        if (!current_user || !perm || !can_edit_activity(*perm, *current_user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto buf = std::make_shared<std::string>();
        res->onAborted([] {});
        res->onData([res, buf, &db, id](std::string_view chunk, bool last) mutable
                    {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            send_json(res, 400, R"({"error":"Ungueltiges JSON"})");
            return;
        }

        std::string location = trim_ascii(j.value("location", ""));
        if (location.empty()) {
            send_json(res, 400, R"({"error":"location-required"})");
            return;
        }

        if (!db.set_activity_weather_location(id, location)) {
            send_json(res, 500, R"({"error":"Konnte nicht speichern"})");
            return;
        }

        send_json(res, 200, nlohmann::json{{"location", location}}.dump()); });
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_activity_expected_weather(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (current_user)
        {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        if (should_use_frozen_values(*activity))
        {
            auto frozen = db.get_activity_weather_snapshot(id);
            if (frozen)
            {
                auto payload = *frozen;
                payload["mode"] = "frozen";
                send_json(res, 200, payload.dump());
                return;
            }

            send_json(res, 200, nlohmann::json{{"available", false}, {"error", "frozen-weather-missing"}, {"mode", "frozen"}}.dump());
            return;
        }

        std::string location_input = trim_ascii(url_decode(std::string{req->getQuery("location")}));
        if (location_input.empty())
        {
            auto saved_location = db.get_activity_weather_location(id);
            if (saved_location)
                location_input = trim_ascii(*saved_location);
        }
        if (location_input.empty())
        {
            send_json(res, 200, nlohmann::json{{"available", false}, {"error", "location-missing"}}.dump());
            return;
        }

        std::string weather_error;
        auto weather = fetch_expected_weather_for_activity(*activity, location_input, weather_error);
        if (!weather)
        {
            send_json(res, 200, nlohmann::json{{"available", false}, {"error", weather_error}}.dump());
            return;
        }

        nlohmann::json payload = {
            {"available", true},
            {"mode", weather->mode},
            {"temperature_c", weather->temperature_c},
            {"temperature_min_c", weather->temperature_min_c ? nlohmann::json(*weather->temperature_min_c) : nlohmann::json(nullptr)},
            {"temperature_max_c", weather->temperature_max_c ? nlohmann::json(*weather->temperature_max_c) : nlohmann::json(nullptr)},
            {"hourly_temps", nlohmann::json::array()},
            {"hourly_rain_probability", nlohmann::json::array()},
            {"rain_probability_percent", weather->rain_probability_percent ? nlohmann::json(*weather->rain_probability_percent) : nlohmann::json(nullptr)},
            {"weather_symbol", weather->weather_symbol.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->weather_symbol)},
            {"season", weather->season.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->season)},
            {"point_name", weather->point_name.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->point_name)},
            {"postal_code", weather->postal_code.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->postal_code)},
            {"source", weather->source},
            {"note", weather->note.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->note)}};
        for (const auto &p : weather->hourly_temps)
            payload["hourly_temps"].push_back(nlohmann::json{{"ts_unix", p.first}, {"temperature_c", p.second}});
        for (const auto &p : weather->hourly_rain_probability)
            payload["hourly_rain_probability"].push_back(nlohmann::json{{"ts_unix", p.first}, {"probability_percent", p.second}});
        db.set_activity_weather_snapshot(id, payload);
        send_json(res, 200, payload.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /locations ---------------------------------------------------------

void handle_get_locations(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto locs = db.get_predefined_locations();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : locs)
            arr.push_back(l);
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// Helper: require locations_manage_scope == 'all'
static std::optional<UserRecord> require_locations_admin(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return std::nullopt;
    auto user = resolve_user(db, claims);
    if (!user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    auto perm = db.get_role_permission(user->role);
    if (!perm || perm->locations_manage_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
}

static nlohmann::json location_to_json(const LocationRecord &loc)
{
    return {{"id", loc.id}, {"name", loc.name}, {"created_at", loc.created_at}, {"updated_at", loc.updated_at}};
}

// ---- GET /admin/locations ---------------------------------------------------

void handle_get_locations_admin(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    try
    {
        auto locs = db.list_predefined_locations();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : locs)
            arr.push_back(location_to_json(l));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /admin/locations --------------------------------------------------

void handle_post_location(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last) mutable
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        try {
            auto loc = db.create_predefined_location(name);
            if (!loc) { send_json(res, 409, R"({"error":"Ort existiert bereits"})"); return; }
            send_json(res, 201, location_to_json(*loc).dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- PATCH /admin/locations/:id ---------------------------------------------

void handle_patch_location(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    std::string id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, id](std::string_view chunk, bool last) mutable
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        try {
            auto loc = db.update_predefined_location(id, name);
            if (!loc) { send_json(res, 404, R"({"error":"Nicht gefunden"})"); return; }
            send_json(res, 200, location_to_json(*loc).dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- DELETE /admin/locations/:id --------------------------------------------

void handle_delete_location(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        bool ok = db.delete_predefined_location(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 204, "");
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/attachments ----------------------------------------

void handle_get_attachments(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (current_user)
        {
            auto activity = db.get_activity_by_id(id);
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!activity || !perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        auto atts = db.list_attachments(id);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &a : atts)
            arr.push_back(attachment_to_json(a));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /activities/:id/attachments ---------------------------------------

void handle_post_attachment(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    std::string activity_id{req->getParameter(0)};

    auto activity = db.get_activity_by_id(activity_id);
    if (!activity)
    {
        send_json(res, 404, R"({"error":"Nicht gefunden"})");
        return;
    }

    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_edit_activity(*perm, *current_user, *activity, claims.email))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    res->onAborted([] {});
    std::string body;
    res->onData([res, &db, activity_id, body = std::move(body)](std::string_view chunk, bool last) mutable
                {
        body.append(chunk);
        if (!last) return;

        auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }
        std::string filename = j.value("filename", "");
        std::string content_type = j.value("content_type", "application/octet-stream");
        std::string data_base64 = j.value("data", "");
        if (filename.empty() || data_base64.empty()) {
            send_json(res, 400, R"({"error":"filename and data required"})");
            return;
        }
        try {
            auto att = db.add_attachment(activity_id, filename, content_type, data_base64);
            if (!att) {
                send_json(res, 500, R"({"error":"failed to add attachment"})");
                return;
            }
            send_json(res, 201, attachment_to_json(*att).dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- GET /attachments/:id/download ------------------------------------------

void handle_get_attachment_download(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto ad = db.get_attachment_data(id);
        if (!ad || ad->data.empty())
        {
            send_json(res, 404, R"({"error":"Keine SiKo-Datei vorhanden"})");
            return;
        }

        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto activity = db.get_activity_by_id(ad->activity_id);
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);
        if (!activity || !perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        set_cors(res);
        res->writeStatus("200 OK");
        res->writeHeader("Content-Type", ad->content_type);
        // Sanitize filename: strip quotes, backslashes, and control chars to prevent header injection
        std::string safe_name;
        for (char c : ad->filename)
        {
            if (c == '"' || c == '\\' || c == '\r' || c == '\n' || static_cast<unsigned char>(c) < 0x20)
                continue;
            safe_name += c;
        }
        if (safe_name.empty())
            safe_name = "attachment";
        std::string disp = "inline; filename=\"" + safe_name + "\"";
        res->writeHeader("Content-Disposition", disp);
        res->end(std::string_view(
            reinterpret_cast<const char *>(ad->data.data()), ad->data.size()));
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- DELETE /attachments/:id ------------------------------------------------

void handle_delete_attachment(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto attachment = db.get_attachment_data(id);
        if (!attachment)
        {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }

        auto activity = db.get_activity_by_id(attachment->activity_id);
        auto perm = db.get_role_permission(current_user->role);
        if (!activity || !perm || !can_edit_activity(*perm, *current_user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        if (!db.delete_attachment(id))
        {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /activities -------------------------------------------------------

void handle_post_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    // Read auth header synchronously (req is only valid here, not inside onData)
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    // All authenticated users can create (Pio restricted to own dept — enforced in onData)
    auto current_user = resolve_user(db, claims);

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        std::string notification_graph_token = j.value("notification_access_token", "");
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"Titel, Datum, Startzeit und Endzeit sind erforderlich"})");
            return;
        }

        // Check create permission for the target department.
        if (current_user && input.department) {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_create_dept(*perm, *current_user, dept_access, *input.department)) {
                send_json(res, 403, R"({"error":"Keine Schreibberechtigung für diese Stufe"})");
                return;
            }
        }

        try {
            auto activity = db.create_activity(input);
            if (!activity) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }

            if (current_user) {
                auto all_assigned = unique_names_from_material(activity->material);
                auto users = db.list_users();
                for (const auto &u : users) {
                    if (u.id == current_user->id || !u.notify_material_assigned)
                        continue;
                    if (!user_in_assignee_list(all_assigned, u))
                        continue;

                    std::string time_range = activity->start_time + "-" + activity->end_time;
                    std::string date_short = format_date_ddmmyyyy(activity->date);
                    std::string full_link = activity_absolute_link(activity->id);
                    std::string recipients_text = join_display(all_assigned);
                    nlohmann::json payload = {
                        {"activity_id", activity->id},
                        {"activity_title", activity->title},
                        {"activity_date", activity->date},
                        {"activity_date_display", date_short},
                        {"activity_time", time_range},
                        {"location", activity->location},
                        {"assigned_users", all_assigned},
                        {"recipients", all_assigned},
                        {"notification_recipient_name", u.display_name},
                        {"notification_recipient_email", u.email},
                        {"triggered_by_name", current_user->display_name},
                        {"triggered_by_email", current_user->email},
                        {"activity_url", full_link}
                    };
                    auto note = db.create_notification(
                        u.id,
                        "material_assigned",
                        "Material dir zugewiesen",
                        "Neue Materialverantwortung in \"" + activity->title + "\" am " + date_short + ". Empfänger: " + recipients_text,
                        full_link,
                        payload
                    );
                    if (note) {
                        if (u.notify_channel_websocket) {
                            nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                            wm.send_to_user_ids({u.id}, ws_msg.dump());
                            deliver_web_push_for_user(db, u);
                        }
                        if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                            std::string mail_subject = "[DPWeb] Material dir zugewiesen: " + activity->title;
                            std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dir Material zugewiesen.</p>"
                                "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                "<strong>Ort:</strong> " + activity->location + "<br/>"
                                "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                            db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                        }
                    }
                }
            }

            nlohmann::json msg = {{"event", "created"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            send_json(res, 201, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- PATCH /activities/:id --------------------------------------------------

void handle_patch_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string id{req->getParameter(0)};
    std::optional<std::string> current_department;
    std::optional<Activity> previous_activity;

    // Permission check: user needs activity edit permission for this activity.
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    {
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        bool allowed = perm && can_edit_activity(*perm, *current_user, *activity, claims.email);
        current_department = activity->department;
        previous_activity = *activity;
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, id, &db, &wm, current_user, current_department, previous_activity](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        std::string notification_graph_token = j.value("notification_access_token", "");
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"Titel, Datum, Startzeit und Endzeit sind erforderlich"})");
            return;
        }

        if (current_user && input.department != current_department) {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!input.department || !perm || !can_create_dept(*perm, *current_user, dept_access, *input.department)) {
                send_json(res, 403, R"({"error":"Keine Schreibberechtigung für diese Stufe"})");
                return;
            }
        }

        try {
            auto activity = db.update_activity(id, input);
            if (!activity) {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }

            if (current_user) {
                auto new_assigned = unique_names_from_material(activity->material);
                auto newly_assigned = newly_assigned_users_from_material_delta(
                    previous_activity ? previous_activity->material : std::vector<MaterialItem>{},
                    activity->material
                );

                if (!newly_assigned.empty()) {
                    auto users = db.list_users();
                    for (const auto &u : users) {
                        if (u.id == current_user->id || !u.notify_material_assigned)
                            continue;
                        if (!user_in_assignee_list(newly_assigned, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = format_date_ddmmyyyy(activity->date);
                        std::string full_link = activity_absolute_link(activity->id);
                        std::string recipients_text = join_display(new_assigned);
                        nlohmann::json payload = {
                            {"activity_id", activity->id},
                            {"activity_title", activity->title},
                            {"activity_date", activity->date},
                            {"activity_date_display", date_short},
                            {"activity_time", time_range},
                            {"location", activity->location},
                            {"assigned_users", new_assigned},
                            {"newly_assigned_users", newly_assigned},
                            {"recipients", new_assigned},
                            {"notification_recipient_name", u.display_name},
                            {"notification_recipient_email", u.email},
                            {"triggered_by_name", current_user->display_name},
                            {"triggered_by_email", current_user->email},
                            {"activity_url", full_link}
                        };

                        auto note = db.create_notification(
                            u.id,
                            "material_assigned",
                            "Material dir zugewiesen",
                            "Neue Materialverantwortung in \"" + activity->title + "\" am " + date_short + ". Empfänger: " + recipients_text,
                            full_link,
                            payload
                        );
                        if (note) {
                            if (u.notify_channel_websocket) {
                                nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                wm.send_to_user_ids({u.id}, ws_msg.dump());
                                deliver_web_push_for_user(db, u);
                            }
                            if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                                std::string mail_subject = "[DPWeb] Material dir zugewiesen: " + activity->title;
                                std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dir Material zugewiesen.</p>"
                                    "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                    "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                    "<strong>Ort:</strong> " + activity->location + "<br/>"
                                    "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                    "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                                db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                            }
                        }
                    }
                }
            }

            nlohmann::json msg = {{"event", "updated"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            send_json(res, 200, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- DELETE /activities/:id -------------------------------------------------

void handle_delete_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};

    // Permission checks are resolved dynamically from the activity edit scope.
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    {
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        bool allowed = perm && can_edit_activity(*perm, *current_user, *activity, claims.email);
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    try
    {
        bool ok = db.delete_activity(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        nlohmann::json msg = {{"event", "deleted"}, {"id", id}};
        wm.broadcast(msg.dump());
        set_cors(res);
        res->writeStatus("204 No Content");
        res->end();
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- User helpers -----------------------------------------------------------

static nlohmann::json user_to_json(const UserRecord &u)
{
    nlohmann::json j;
    j["id"] = u.id;
    j["microsoft_oid"] = u.microsoft_oid;
    j["email"] = u.email;
    j["display_name"] = u.display_name;
    j["department"] = u.department ? nlohmann::json(*u.department) : nlohmann::json(nullptr);
    j["role"] = u.role;
    j["time_display_mode"] = u.time_display_mode;
    j["notify_material_assigned"] = u.notify_material_assigned;
    j["notify_mail_own_activity"] = u.notify_mail_own_activity;
    j["notify_mail_department"] = u.notify_mail_department;
    j["notify_channel_websocket"] = u.notify_channel_websocket;
    j["notify_channel_email"] = u.notify_channel_email;
    j["created_at"] = u.created_at;
    j["updated_at"] = u.updated_at;
    return j;
}

static nlohmann::json notification_to_json(const NotificationRecord &n)
{
    nlohmann::json j = {
        {"id", n.id},
        {"user_id", n.user_id},
        {"category", n.category},
        {"title", n.title},
        {"message", n.message},
        {"payload", n.payload},
        {"is_read", n.is_read},
        {"created_at", n.created_at},
    };
    j["link"] = n.link ? nlohmann::json(*n.link) : nlohmann::json(nullptr);
    return j;
}

// ---- POST /auth/me ----------------------------------------------------------
// Called by the frontend immediately after Microsoft login.
// Validates the token and upserts the user record.

void handle_post_auth_me(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        if (claims.oid.rfind("debug:", 0) == 0)
        {
            auto existing = resolve_user(db, claims);
            if (!existing)
            {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            send_json(res, 200, user_to_json(*existing).dump());
            return;
        }

        // Determine initial role / department for new users.
        std::string initial_role = "Mitglied";
        std::string initial_dept = "Allgemein";
        bool force_role = false;

        // Lowercase email once for all comparisons.
        std::string email_lower = claims.email;
        std::transform(email_lower.begin(), email_lower.end(), email_lower.begin(), ::tolower);

        // ── Debug-Mode ──────────────────────────────────────────────────────
        // If DEBUG_ADMIN_EMAIL matches, force admin role (compile-time gated).
#if DPW_ENABLE_DEBUG_AUTH
        {
            const char *dbg_mail = std::getenv("DEBUG_ADMIN_EMAIL");
            if (dbg_mail && !std::string(dbg_mail).empty())
            {
                std::string dbg_mail_lower(dbg_mail);
                std::transform(dbg_mail_lower.begin(), dbg_mail_lower.end(),
                               dbg_mail_lower.begin(), ::tolower);
                if (email_lower == dbg_mail_lower)
                {
                    initial_role = "admin";
                    initial_dept = "Allgemein";
                    force_role = true;
                }
            }
        }
#endif

        // ── Normale Rollen-Zuweisung (nur wenn kein Debug-Override) ─────────
        if (!force_role)
        {
            // Email contains "admin" → always admin role (applied even for existing users).
            if (email_lower.find("admin") != std::string::npos)
            {
                initial_role = "admin";
                initial_dept = "Allgemein";
                force_role = true;
            }
        }

        auto user = db.upsert_user(claims.oid, claims.email, claims.display_name,
                                   initial_role, initial_dept, force_role);
        if (!user)
        {
            send_json(res, 500, R"({"error":"Datenbankfehler"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /me ----------------------------------------------------------------

void handle_get_me(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto user = resolve_user(db, claims);
        if (!user)
        {
            send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /users -------------------------------------------------------------

void handle_get_users(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto users = db.list_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &u : users)
            arr.push_back(user_to_json(u));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- Debug-only endpoints ---------------------------------------------------

static bool is_debug_mode()
{
#if DPW_ENABLE_DEBUG_AUTH
    return true;
#else
    return false;
#endif
}

// GET /debug/users — list debug-login candidates without auth (debug mode only)
void handle_debug_get_users(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    if (!is_debug_mode())
    {
        send_json(res, 404, R"({"error":"Nicht gefunden"})");
        return;
    }
    try
    {
        auto users = db.list_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &u : users)
            arr.push_back({
                {"id", u.id},
                {"display_name", u.display_name},
                {"department", u.department ? nlohmann::json(*u.department) : nlohmann::json(nullptr)},
                {"role", u.role},
            });
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- PATCH /admin/users/:id -------------------------------------------------

void handle_patch_admin_user(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    // Check user management permissions
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto current_perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!current_perm || (current_perm->user_dept_scope == "none" && current_perm->user_role_scope == "none")))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string target_id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, target_id, current_user, current_perm, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        // Check scope-based restrictions
        std::string role = j.value("role", "Mitglied");
        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string())
            department = j["department"].get<std::string>();

        if (current_perm) {
            auto target = db.get_user_by_id(target_id);
            if (!target) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }

            // own scope: only own user record
            if (current_perm->user_dept_scope == "own" || current_perm->user_role_scope == "own") {
                if (target_id != current_user->id) {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }

            // own_dept scope: only users in same department
            if (current_perm->user_dept_scope == "own_dept" || current_perm->user_role_scope == "own_dept") {
                if (!current_user->department || !target->department ||
                    *target->department != *current_user->department) {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }

            // Cannot change role if role scope is not enough
            if (current_perm->user_role_scope != "all" &&
                current_perm->user_role_scope != "own_dept" &&
                current_perm->user_role_scope != "own") {
                role = target->role;
            }
            else if (current_user->role != "admin" && role != target->role) {
                auto roles = db.list_roles();
                int current_role_index = -1;
                int new_role_index = -1;
                for (size_t i = 0; i < roles.size(); ++i) {
                    if (roles[i].name == current_user->role) current_role_index = static_cast<int>(i);
                    if (roles[i].name == role) new_role_index = static_cast<int>(i);
                }
                if (current_role_index < 0 || new_role_index < 0 || new_role_index <= current_role_index) {
                    send_json(res, 403, R"({"error":"Diese Rolle darf nicht vergeben werden"})");
                    return;
                }
            }

            // Cannot change department if dept scope is not enough
            if (current_perm->user_dept_scope != "all" &&
                current_perm->user_dept_scope != "own_dept" &&
                current_perm->user_dept_scope != "own") {
                department = target->department;
            }
        }

        try {
            auto user = db.update_user_admin(target_id, display_name, department, role);
            if (!user) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- DELETE /admin/users/:id ------------------------------------------------

void handle_delete_admin_user(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto current_perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!current_perm || (current_perm->user_dept_scope == "none" && current_perm->user_role_scope == "none")))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string target_id{req->getParameter(0)};

    // Cannot delete yourself
    if (target_id == current_user->id)
    {
        send_json(res, 400, R"({"error":"Du kannst dich nicht selbst löschen"})");
        return;
    }

    auto target = db.get_user_by_id(target_id);
    if (!target)
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    // own scope: only own user record (self-delete still blocked above)
    if (current_perm->user_dept_scope == "own" || current_perm->user_role_scope == "own")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    // own_dept scope: only users in same department
    if (current_perm->user_dept_scope == "own_dept" || current_perm->user_role_scope == "own_dept")
    {
        if (!current_user->department || !target->department ||
            *target->department != *current_user->department)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    if (db.delete_user(target_id))
    {
        send_json(res, 200, R"({"ok":true})");
    }
    else
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
    }
}

// ---- PATCH /me --------------------------------------------------------------

void handle_patch_me(HttpRes *res, HttpReq *req, Database &db)
{
    // Read auth synchronously before onData
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }

    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, claims, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        // Fetch current user once; own profile updates require an existing user record.
        auto current_user = resolve_user(db, claims);
        if (!current_user) {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        // Keep the current department unless an explicit department key is provided.
        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string()) {
            std::string new_dept = j["department"].get<std::string>();
            department = new_dept;

            // Only enforce permission when the department actually changes.
            if (department != current_user->department) {
                auto perm = db.get_role_permission(current_user->role);
                if (!perm || (perm->user_dept_scope != "all" && perm->user_dept_scope != "own")) {
                    send_json(res, 403, R"({"error":"Stufe kann nicht selbst geändert werden"})");
                    return;
                }
            }
        } else if (j.contains("department") && j["department"].is_null()) {
            // NULL means explicit department removal.
            department = std::nullopt;

            // Only enforce permission when the department actually changes.
            if (department != current_user->department) {
                auto perm = db.get_role_permission(current_user->role);
                if (!perm || (perm->user_dept_scope != "all" && perm->user_dept_scope != "own")) {
                    send_json(res, 403, R"({"error":"Stufe kann nicht selbst geändert werden"})");
                    return;
                }
            }
        } else {
            // department key missing → keep existing department
            department = current_user->department;
        }

        std::optional<std::string> time_display_mode;
        if (j.contains("time_display_mode") && j["time_display_mode"].is_string()) {
            std::string mode = j["time_display_mode"].get<std::string>();
            if (mode != "minutes" && mode != "clock") {
                send_json(res, 400, R"({"error":"Ung\u00fcltiger time_display_mode"})");
                return;
            }
            time_display_mode = mode;
        }

        std::optional<bool> notify_material_assigned;
        if (j.contains("notify_material_assigned") && j["notify_material_assigned"].is_boolean()) {
            notify_material_assigned = j["notify_material_assigned"].get<bool>();
        }

        std::optional<bool> notify_mail_own_activity;
        if (j.contains("notify_mail_own_activity") && j["notify_mail_own_activity"].is_boolean()) {
            notify_mail_own_activity = j["notify_mail_own_activity"].get<bool>();
        }

        std::optional<bool> notify_mail_department;
        if (j.contains("notify_mail_department") && j["notify_mail_department"].is_boolean()) {
            notify_mail_department = j["notify_mail_department"].get<bool>();
        }

        std::optional<bool> notify_channel_websocket;
        if (j.contains("notify_channel_websocket") && j["notify_channel_websocket"].is_boolean()) {
            notify_channel_websocket = j["notify_channel_websocket"].get<bool>();
        }

        std::optional<bool> notify_channel_email;
        if (j.contains("notify_channel_email") && j["notify_channel_email"].is_boolean()) {
            notify_channel_email = j["notify_channel_email"].get<bool>();
        }

        try {
            auto user = db.update_user(
                current_user->microsoft_oid,
                display_name,
                department,
                time_display_mode,
                notify_material_assigned,
                notify_mail_own_activity,
                notify_mail_department,
                notify_channel_websocket,
                notify_channel_email
            );
            if (!user) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- GET /notifications ----------------------------------------------------

void handle_get_notifications(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        int limit = 0;
        std::string limit_raw = std::string(req->getQuery("limit"));
        if (!limit_raw.empty())
        {
            try
            {
                limit = std::max(1, std::min(200, std::stoi(limit_raw)));
            }
            catch (...)
            {
                limit = 0;
            }
        }

        auto notes = db.list_notifications_for_user(current_user->id, limit);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &n : notes)
            arr.push_back(notification_to_json(n));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- PATCH /notifications/:id/read ----------------------------------------

void handle_patch_notification_read(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        std::string notification_id{req->getParameter(0)};
        bool ok = db.mark_notification_read(current_user->id, notification_id);
        if (!ok)
        {
            send_json(res, 500, R"({"error":"Konnte Benachrichtigung nicht aktualisieren"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /notifications/read-all -----------------------------------------

void handle_post_notifications_read_all(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        bool ok = db.mark_all_notifications_read(current_user->id);
        if (!ok)
        {
            send_json(res, 500, R"({"error":"Konnte Benachrichtigungen nicht aktualisieren"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /push/vapid-public-key -------------------------------------------

void handle_get_push_vapid_public_key(HttpRes *res, HttpReq * /*req*/)
{
    std::string pub = env_or("DPW_VAPID_PUBLIC_KEY");
    if (pub.empty())
    {
        send_json(res, 503, R"({"error":"Web-Push nicht konfiguriert"})");
        return;
    }
    send_json(res, 200, nlohmann::json{{"publicKey", pub}}.dump());
}

// ---- POST /push/subscriptions ---------------------------------------------

void handle_post_push_subscription(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }

        std::string endpoint = j.value("endpoint", "");
        std::string p256dh;
        std::string auth;
        if (j.contains("keys") && j["keys"].is_object()) {
            p256dh = j["keys"].value("p256dh", "");
            auth = j["keys"].value("auth", "");
        }

        if (endpoint.empty() || p256dh.empty() || auth.empty()) {
            send_json(res, 400, R"({"error":"endpoint, keys.p256dh und keys.auth sind erforderlich"})");
            return;
        }

        auto sub = db.upsert_push_subscription(current_user->id, endpoint, p256dh, auth);
        if (!sub) {
            send_json(res, 500, R"({"error":"Konnte Push-Subscription nicht speichern"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})"); });
}

// ---- DELETE /push/subscriptions -------------------------------------------

void handle_delete_push_subscription(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }

        std::string endpoint = j.value("endpoint", "");
        if (endpoint.empty()) {
            send_json(res, 400, R"({"error":"endpoint ist erforderlich"})");
            return;
        }

        bool ok = db.delete_push_subscription(current_user->id, endpoint);
        if (!ok) {
            send_json(res, 500, R"({"error":"Konnte Push-Subscription nicht löschen"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})"); });
}

// ---- POST /push/payload ----------------------------------------------------

void handle_post_push_payload(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }

        std::string endpoint = j.value("endpoint", "");
        std::string auth = j.value("auth", "");
        if (endpoint.empty() || auth.empty()) {
            send_json(res, 400, R"({"error":"endpoint und auth sind erforderlich"})");
            return;
        }

        auto n = db.get_latest_unread_notification_for_push(endpoint, auth);
        if (!n) {
            set_cors(res);
            res->writeStatus("204 No Content");
            res->end();
            return;
        }

        std::string link = n->link ? *n->link : "/profile";
        std::string date_short;
        if (n->payload.is_object() && n->payload.contains("activity_date_display") && n->payload["activity_date_display"].is_string()) {
            date_short = trim_ascii(n->payload["activity_date_display"].get<std::string>());
        }
        if (date_short.empty()) {
            date_short = format_date_ddmmyyyy(n->created_at);
        }
        nlohmann::json payload = {
            {"id", n->id},
            {"title", n->title},
            {"body", date_short.empty() ? std::string("Neue Benachrichtigung") : std::string("Datum: ") + date_short},
            {"url", link},
            {"notification", notification_to_json(*n)}
        };
        send_json(res, 200, payload.dump()); });
}

// ---- Mail template helpers --------------------------------------------------

static nlohmann::json template_to_json(const MailTemplate &t)
{
    return {
        {"id", t.id},
        {"department", t.department},
        {"subject", t.subject},
        {"body", t.body},
        {"recipients", t.recipients},
        {"cc", t.cc},
        {"created_at", t.created_at},
        {"updated_at", t.updated_at}};
}

// ---- GET /mail-templates ----------------------------------------------------

void handle_get_mail_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->mail_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto templates = db.list_mail_templates();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
        {
            // own_dept: only templates for user's own department
            if (!is_admin(*current_user) && perm->mail_templates_scope == "own_dept")
            {
                if (!current_user->department || t.department != *current_user->department)
                    continue;
            }
            arr.push_back(template_to_json(t));
        }
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /mail-templates/:department ----------------------------------------

void handle_get_mail_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string department = url_decode(std::string{req->getParameter(0)});
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->mail_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (!is_admin(*current_user) && perm->mail_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        auto tpl = db.get_mail_template_by_department(department);
        if (!tpl)
        {
            send_json(res, 404, R"({"error":"template not found"})");
            return;
        }
        send_json(res, 200, template_to_json(*tpl).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- PUT /mail-templates/:department ----------------------------------------

void handle_put_mail_template(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string department = url_decode(std::string{req->getParameter(0)});

    // Permission check: mail_templates_scope controls access
    auto current_user = resolve_user(db, claims);
    if (current_user)
    {
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->mail_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (!is_admin(*current_user) && perm->mail_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }
        // 'all' → no restriction
    }
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, department, &db, &wm](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string subject = j.value("subject", "");
        std::string body    = j.value("body", "");

        std::vector<std::string> recipients;
        if (j.contains("recipients") && j["recipients"].is_array()) {
            for (auto& e : j["recipients"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    recipients.push_back(e.get<std::string>());
            }
        }
        std::vector<std::string> cc;
        if (j.contains("cc") && j["cc"].is_array()) {
            for (auto& e : j["cc"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    cc.push_back(e.get<std::string>());
            }
        }

        try {
            auto tpl = db.upsert_mail_template(department, subject, body, recipients, cc);
            if (!tpl) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }
            nlohmann::json msg = {{"event", "template_updated"}, {"template", template_to_json(*tpl)}};
            wm.broadcast(msg.dump());
            send_json(res, 200, template_to_json(*tpl).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- POST /send-mail --------------------------------------------------------

void handle_post_send_mail(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);

    // Permission check: mail_send_scope
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm_check = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm_check || perm_check->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, token, &db, &wm, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string subject     = j.value("subject", "");
        std::string body_html   = j.value("body", "");
        std::string from_email  = j.value("from", "");
        std::string graph_token = j.value("access_token", "");
        std::string activity_id = j.value("activity_id", "");

        if (subject.empty() || body_html.empty() || graph_token.empty()) {
            send_json(res, 400, R"({"error":"subject, body and access_token are required"})");
            return;
        }

        std::vector<std::string> to_emails;
        if (j.contains("to") && j["to"].is_array()) {
            for (auto& e : j["to"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    to_emails.push_back(e.get<std::string>());
            }
        }
        if (to_emails.empty()) {
            send_json(res, 400, R"({"error":"at least one recipient required"})");
            return;
        }

        std::vector<std::string> cc_emails;
        if (j.contains("cc") && j["cc"].is_array()) {
            for (auto& e : j["cc"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    cc_emails.push_back(e.get<std::string>());
            }
        }

        try {
            bool ok = db.send_mail(graph_token, from_email, to_emails, cc_emails, subject, body_html);
            if (ok) {
                // Log sent mail if activity_id was provided
                if (!activity_id.empty()) {
                    std::string sender_id = current_user ? current_user->id : "";
                    std::string sender_email = current_user ? current_user->email : from_email;
                    db.log_sent_mail(activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html);
                    db.delete_mail_draft(activity_id);

                    auto activity = db.get_activity_by_id(activity_id);
                    if (activity && current_user) {
                        auto users = db.list_users();
                        for (const auto &u : users) {
                            if (u.id == current_user->id)
                                continue;

                            std::string link = activity_absolute_link(activity->id);
                            std::string date_short = format_date_ddmmyyyy(activity->date);
                            std::string recipients_text = join_display(to_emails);
                            std::string cc_text = join_display(cc_emails);
                            nlohmann::json payload = {
                                {"activity_id", activity->id},
                                {"activity_title", activity->title},
                                {"activity_date", activity->date},
                                {"activity_date_display", date_short},
                                {"activity_department", activity->department ? nlohmann::json(*activity->department) : nlohmann::json(nullptr)},
                                {"mail_subject", subject},
                                {"mail_body_html", body_html},
                                {"to", to_emails},
                                {"cc", cc_emails},
                                {"recipients", to_emails},
                                {"notification_recipient_name", u.display_name},
                                {"notification_recipient_email", u.email},
                                {"triggered_by_name", current_user->display_name},
                                {"triggered_by_email", current_user->email},
                                {"activity_url", link}
                            };

                            bool is_own_activity = contains_name_ci(activity->responsible, u.display_name);
                            bool is_same_department = activity->department && u.department && *activity->department == *u.department;

                            if (is_own_activity && u.notify_mail_own_activity) {
                                auto note = db.create_notification(
                                    u.id,
                                    "mail_own_activity",
                                    "Mail für deine Aktivität versendet",
                                    "Mail zu \"" + activity->title + "\" wurde am " + date_short + " versendet. Empfänger: " + recipients_text + (cc_emails.empty() ? "" : ("; CC: " + cc_text)),
                                    link,
                                    payload
                                );
                                if (note) {
                                    if (u.notify_channel_websocket) {
                                        nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                        wm.send_to_user_ids({u.id}, ws_msg.dump());
                                        deliver_web_push_for_user(db, u);
                                    }
                                    if (u.notify_channel_email && !u.email.empty()) {
                                        std::string subj = "[DPWeb] Mail für deine Aktivität: " + activity->title;
                                        std::string body = "<p><strong>" + current_user->display_name + "</strong> hat eine Mail für deine Aktivität versendet.</p>"
                                            "<p><strong>Aktivität:</strong> <a href=\"" + link + "\">" + activity->title + "</a><br/>"
                                            "<strong>Datum:</strong> " + date_short + "<br/>"
                                            "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                            "<p><strong>Empfänger:</strong> " + recipients_text + (cc_emails.empty() ? "" : ("<br/><strong>CC:</strong> " + cc_text)) + "</p>"
                                            "<p><strong>Betreff:</strong> " + subject + "</p>"
                                            "<hr/>" + body_html +
                                            "<p>Die Aktivität ist direkt im Titel verlinkt.</p>";
                                        db.send_mail(graph_token, current_user->email, {u.email}, {}, subj, body);
                                    }
                                }
                            }

                            else if (is_same_department && u.notify_mail_department) {
                                auto note = db.create_notification(
                                    u.id,
                                    "mail_department",
                                    "Mail in deiner Stufe versendet",
                                    "Mail zu \"" + activity->title + "\" in deiner Stufe wurde am " + date_short + " versendet. Empfänger: " + recipients_text + (cc_emails.empty() ? "" : ("; CC: " + cc_text)),
                                    link,
                                    payload
                                );
                                if (note) {
                                    if (u.notify_channel_websocket) {
                                        nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                        wm.send_to_user_ids({u.id}, ws_msg.dump());
                                        deliver_web_push_for_user(db, u);
                                    }
                                    if (u.notify_channel_email && !u.email.empty()) {
                                        std::string subj = "[DPWeb] Mail in deiner Stufe: " + activity->title;
                                        std::string body = "<p><strong>" + current_user->display_name + "</strong> hat eine Mail in deiner Stufe versendet.</p>"
                                            "<p><strong>Aktivität:</strong> <a href=\"" + link + "\">" + activity->title + "</a><br/>"
                                            "<strong>Datum:</strong> " + date_short + "<br/>"
                                            "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                            "<p><strong>Empfänger:</strong> " + recipients_text + (cc_emails.empty() ? "" : ("<br/><strong>CC:</strong> " + cc_text)) + "</p>"
                                            "<p><strong>Betreff:</strong> " + subject + "</p>"
                                            "<hr/>" + body_html +
                                            "<p>Die Aktivität ist direkt im Titel verlinkt.</p>";
                                        db.send_mail(graph_token, current_user->email, {u.email}, {}, subj, body);
                                    }
                                }
                            }
                        }
                    }
                }
                send_json(res, 200, R"({"status":"sent"})");
            } else {
                send_json(res, 502, R"({"error":"Failed to send mail via Microsoft Graph"})");
            }
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ─── GET /activities/:id/sent-mails ──────────────────────────────────────────

void handle_get_sent_mails(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    // Permission check: user must be able to read the activity
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto activity = db.get_activity_by_id(activity_id);
    if (!activity)
    {
        send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    auto dept_access = db.list_role_dept_access(current_user->role);
    if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto mails = db.list_sent_mails(activity_id);

    nlohmann::json arr = nlohmann::json::array();
    for (auto &m : mails)
    {
        nlohmann::json to_arr = nlohmann::json::array();
        for (auto &e : m.to_emails)
            to_arr.push_back(e);
        nlohmann::json cc_arr = nlohmann::json::array();
        for (auto &e : m.cc_emails)
            cc_arr.push_back(e);

        arr.push_back({
            {"id", m.id},
            {"activity_id", m.activity_id},
            {"sender_id", m.sender_id},
            {"sender_email", m.sender_email},
            {"to_emails", to_arr},
            {"cc_emails", cc_arr},
            {"subject", m.subject},
            {"body_html", m.body_html},
            {"sent_at", m.sent_at},
        });
    }
    send_json(res, 200, arr.dump());
}

// ─── GET /activities/:id/mail-draft ──────────────────────────────────────────

void handle_get_mail_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto activity = db.get_activity_by_id(activity_id);
    if (!activity)
    {
        send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto draft = db.get_mail_draft(activity_id);
    if (!draft)
    {
        send_json(res, 404, R"({"error":"Kein Entwurf vorhanden"})");
        return;
    }

    nlohmann::json recip_arr = nlohmann::json::array();
    for (auto &r : draft->recipients)
        recip_arr.push_back(r);
    nlohmann::json cc_arr = nlohmann::json::array();
    for (auto &c : draft->cc)
        cc_arr.push_back(c);

    nlohmann::json j = {
        {"id", draft->id},
        {"activity_id", draft->activity_id},
        {"recipients", recip_arr},
        {"cc", cc_arr},
        {"subject", draft->subject},
        {"body_html", draft->body_html},
        {"updated_by", draft->updated_by},
        {"updated_at", draft->updated_at},
    };
    send_json(res, 200, j.dump());
}

// ─── PUT /activities/:id/mail-draft ──────────────────────────────────────────

void handle_put_mail_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string body_buf;
    res->onData([res, &db, activity_id, user_id = current_user->id,
                 body_buf = std::move(body_buf)](std::string_view data, bool last) mutable
                {
        body_buf.append(data);
        if (!last) return;

        nlohmann::json j;
        try { j = nlohmann::json::parse(body_buf); }
        catch (...)
        {
            send_json(res, 400, R"({"error":"Ungültiges JSON"})");
            return;
        }

        std::vector<std::string> recipients;
        if (j.contains("recipients") && j["recipients"].is_array())
        {
            for (auto &r : j["recipients"])
                if (r.is_string() && !r.get<std::string>().empty())
                    recipients.push_back(r.get<std::string>());
        }
        std::vector<std::string> cc;
        if (j.contains("cc") && j["cc"].is_array())
        {
            for (auto &c : j["cc"])
                if (c.is_string() && !c.get<std::string>().empty())
                    cc.push_back(c.get<std::string>());
        }

        std::string subject = j.value("subject", "");
        std::string body_html = j.value("body_html", "");

        auto draft = db.upsert_mail_draft(activity_id, recipients, cc, subject, body_html, user_id);
        if (!draft)
        {
            send_json(res, 500, R"({"error":"Entwurf konnte nicht gespeichert werden"})");
            return;
        }

        nlohmann::json recip_arr = nlohmann::json::array();
        for (auto &r : draft->recipients)
            recip_arr.push_back(r);
        nlohmann::json cc_arr = nlohmann::json::array();
        for (auto &c : draft->cc)
            cc_arr.push_back(c);

        nlohmann::json out = {
            {"id", draft->id},
            {"activity_id", draft->activity_id},
            {"recipients", recip_arr},
            {"cc", cc_arr},
            {"subject", draft->subject},
            {"body_html", draft->body_html},
            {"updated_by", draft->updated_by},
            {"updated_at", draft->updated_at},
        };
        send_json(res, 200, out.dump()); });

    res->onAborted([]() {});
}

// ─── DELETE /activities/:id/mail-draft ───────────────────────────────────────

void handle_delete_mail_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    db.delete_mail_draft(activity_id);
    send_json(res, 200, R"({"ok":true})");
}

// ─── GET /activities/:id/form-draft ─────────────────────────────────────────

void handle_get_form_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto activity = db.get_activity_by_id(activity_id);
    if (!activity)
    {
        send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
        return;
    }

    auto draft = db.get_form_draft(activity_id);
    if (!draft)
    {
        send_json(res, 404, R"({"error":"Kein Entwurf vorhanden"})");
        return;
    }

    nlohmann::json questions;
    try
    {
        questions = nlohmann::json::parse(draft->questions_json);
    }
    catch (...)
    {
        questions = nlohmann::json::array();
    }

    nlohmann::json j = {
        {"id", draft->id},
        {"activity_id", draft->activity_id},
        {"form_type", draft->form_type},
        {"title", draft->title},
        {"questions", questions},
        {"updated_by", draft->updated_by},
        {"updated_at", draft->updated_at},
    };
    send_json(res, 200, j.dump());
}

// ─── PUT /activities/:id/form-draft ─────────────────────────────────────────

void handle_put_form_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string body_buf;
    res->onData([res, &db, activity_id, user_id = current_user->id,
                 body_buf = std::move(body_buf)](std::string_view data, bool last) mutable
                {
        body_buf.append(data);
        if (!last) return;

        nlohmann::json j;
        try { j = nlohmann::json::parse(body_buf); }
        catch (...)
        {
            send_json(res, 400, R"({"error":"Ungültiges JSON"})");
            return;
        }

        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        std::string questions_json = "[]";
        if (j.contains("questions") && j["questions"].is_array())
            questions_json = j["questions"].dump();

        auto draft = db.upsert_form_draft(activity_id, form_type, title, questions_json, user_id);
        if (!draft)
        {
            send_json(res, 500, R"({"error":"Entwurf konnte nicht gespeichert werden"})");
            return;
        }

        nlohmann::json questions;
        try
        {
            questions = nlohmann::json::parse(draft->questions_json);
        }
        catch (...)
        {
            questions = nlohmann::json::array();
        }

        nlohmann::json out = {
            {"id", draft->id},
            {"activity_id", draft->activity_id},
            {"form_type", draft->form_type},
            {"title", draft->title},
            {"questions", questions},
            {"updated_by", draft->updated_by},
            {"updated_at", draft->updated_at},
        };
        send_json(res, 200, out.dump()); });

    res->onAborted([]() {});
}

// ─── DELETE /activities/:id/form-draft ───────────────────────────────────────

void handle_delete_form_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    db.delete_form_draft(activity_id);
    send_json(res, 200, R"({"ok":true})");
}

// ─── GitHub API helpers ───────────────────────────────────────────────────────

static size_t github_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::pair<long, std::string> github_post_issue(
    const std::string &token, const std::string &body_json)
{
    const char *repo_env = std::getenv("GITHUB_REPO");
    std::string repo = (repo_env && repo_env[0]) ? repo_env : "reicham2/DPW";
    const std::string url = "https://api.github.com/repos/" + repo + "/issues";
    std::string response;

    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    struct curl_slist *headers = nullptr;
    std::string auth_hdr = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth_hdr.c_str());
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: DPW-BugReport/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, github_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("GitHub API call failed: ") + curl_easy_strerror(rc));

    return {status, response};
}

// ─── POST /bug-report ─────────────────────────────────────────────────────────

void handle_post_bug_report(HttpRes *res, HttpReq *req, Database &db)
{
    const char *github_token_env = std::getenv("GITHUB_TOKEN");
    if (!github_token_env || std::string(github_token_env).empty())
    {
        send_json(res, 503, R"({"error":"Bug report service not configured"})");
        return;
    }
    std::string github_token = github_token_env;

    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, github_token, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded())
        {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string description = str_field(j, "description");
        std::string url         = str_field(j, "url");
        std::string user_agent  = str_field(j, "userAgent");
        auto debug_info         = j.contains("debugInfo") ? j["debugInfo"] : nlohmann::json{};

        if (description.empty())
        {
            send_json(res, 400, R"({"error":"description is required"})");
            return;
        }

        // Title: first 80 chars of description
        std::string title = "Bug Report: " + description.substr(0, 80);
        if (description.size() > 80) title += "...";

        // Reporter info
        std::string reporter = current_user
            ? current_user->display_name + " (" + current_user->email + ")"
            : "Unbekannt";

        // Timestamp
        std::time_t now = std::time(nullptr);
        char ts_buf[32];
        std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M UTC", std::gmtime(&now));

        std::string body =
            "## Beschreibung\n\n" + description +
            "\n\n---\n"
            "**Gemeldet von:** " + reporter + "  \n"
            "**URL:** " + url + "  \n"
            "**Browser:** " + user_agent + "  \n"
            "**Zeitpunkt:** " + ts_buf;

        // ─── Debug info section ──────────────────────────────────────
        if (!debug_info.is_null() && debug_info.is_object())
        {
            body += "\n\n---\n<details>\n<summary>Debug-Informationen</summary>\n\n";

            // Session
            body += "### Session\n";
            body += "- **Report Timestamp:** " + debug_info.value("timestamp", "") + "\n";
            body += "- **Session Start:** " + debug_info.value("sessionStart", "") + "\n";
            int dur = debug_info.value("sessionDuration", 0);
            body += "- **Session Duration:** " + std::to_string(dur / 60) + "m " + std::to_string(dur % 60) + "s\n\n";

            // Route
            if (debug_info.contains("route") && debug_info["route"].is_object())
            {
                auto &r = debug_info["route"];
                body += "### Route\n";
                body += "- **Path:** `" + r.value("path", "") + "`\n";
                body += "- **Full Path:** `" + r.value("fullPath", "") + "`\n";
                if (r.contains("params") && !r["params"].empty())
                    body += "- **Params:** `" + r["params"].dump() + "`\n";
                if (r.contains("query") && !r["query"].empty())
                    body += "- **Query:** `" + r["query"].dump() + "`\n";
                body += "\n";
            }

            // User context
            if (debug_info.contains("user") && debug_info["user"].is_object())
            {
                auto &u = debug_info["user"];
                body += "### User\n";
                body += "- **ID:** " + u.value("id", "?") + "\n";
                body += "- **Email:** " + u.value("email", "?") + "\n";
                body += "- **Role:** " + u.value("role", "?") + "\n";
                body += "- **Department:** " + u.value("department", "null") + "\n\n";
            }

            // Viewport
            if (debug_info.contains("viewport") && debug_info["viewport"].is_object())
            {
                auto &v = debug_info["viewport"];
                body += "### Viewport & Device\n";
                body += "- **Window:** " + std::to_string(v.value("width", 0)) + " × " + std::to_string(v.value("height", 0)) + "\n";
                body += "- **Screen:** " + std::to_string(v.value("screenWidth", 0)) + " × " + std::to_string(v.value("screenHeight", 0)) + "\n";
                body += "- **DPR:** " + std::to_string(v.value("devicePixelRatio", 1.0)) + "\n";
                body += "- **Orientation:** " + v.value("orientation", "?") + "\n";
            }
            if (debug_info.contains("inputCapabilities") && debug_info["inputCapabilities"].is_object())
            {
                auto &ic = debug_info["inputCapabilities"];
                body += "- **Touch Points:** " + std::to_string(ic.value("touchPoints", 0)) + "\n";
                body += "- **Pointer:** " + ic.value("pointerType", "?") + "\n";
            }
            body += "\n";

            // DOM
            if (debug_info.contains("dom") && debug_info["dom"].is_object())
            {
                auto &d = debug_info["dom"];
                body += "### DOM\n";
                body += "- **Element Count:** " + std::to_string(d.value("elementCount", 0)) + "\n";
                body += "- **Body Scroll Height:** " + std::to_string(d.value("bodyScrollHeight", 0)) + " px\n";
                if (d.contains("activeElement") && d["activeElement"].is_string())
                    body += "- **Active Element:** `" + d.value("activeElement", "") + "`\n";
                body += "\n";
            }

            // WebSocket
            if (debug_info.contains("webSocket") && debug_info["webSocket"].is_object())
            {
                auto &w = debug_info["webSocket"];
                body += "### WebSocket\n";
                body += "- **Connected:** " + std::string(w.value("connected", false) ? "ja" : "nein") + "\n";
                body += "- **Ready State:** " + std::to_string(w.value("readyState", -1)) + "\n";
                body += "- **Connect Count:** " + std::to_string(w.value("connectCount", 0)) + "\n";
                body += "- **Disconnect Count:** " + std::to_string(w.value("disconnectCount", 0)) + "\n";
                body += "- **Messages Received:** " + std::to_string(w.value("messageCount", 0)) + "\n";
                if (w.contains("lastMessageAt") && w["lastMessageAt"].is_string())
                    body += "- **Last Message:** " + w.value("lastMessageAt", "") + "\n";
                if (w.contains("lastError") && w["lastError"].is_string())
                    body += "- **Last Error:** " + w.value("lastError", "") + "\n";
                body += "- **Registered:** " + std::string(w.value("registered", false) ? "ja" : "nein") + "\n\n";
            }

            // Performance
            if (debug_info.contains("performance") && debug_info["performance"].is_object())
            {
                auto &p = debug_info["performance"];
                body += "### Performance\n";
                body += "- **DOM Content Loaded:** " + std::to_string(p.value("domContentLoaded", 0)) + " ms\n";
                body += "- **Load Complete:** " + std::to_string(p.value("loadComplete", 0)) + " ms\n";
                body += "- **Redirect Count:** " + std::to_string(p.value("redirectCount", 0)) + "\n\n";
            }

            // Memory
            if (debug_info.contains("memory") && debug_info["memory"].is_object())
            {
                auto &m = debug_info["memory"];
                body += "### Memory\n";
                long used = m.value("usedJSHeapSize", 0L);
                long total = m.value("totalJSHeapSize", 0L);
                long limit = m.value("jsHeapSizeLimit", 0L);
                body += "- **Used:** " + std::to_string(used / 1048576) + " MB\n";
                body += "- **Total:** " + std::to_string(total / 1048576) + " MB\n";
                body += "- **Limit:** " + std::to_string(limit / 1048576) + " MB\n\n";
            }

            // Connection
            if (debug_info.contains("connection") && debug_info["connection"].is_object())
            {
                auto &c = debug_info["connection"];
                body += "### Connection\n";
                body += "- **Type:** " + c.value("effectiveType", "?") + "\n";
                body += "- **Downlink:** " + std::to_string(c.value("downlink", 0.0)) + " Mbps\n";
                body += "- **RTT:** " + std::to_string(c.value("rtt", 0)) + " ms\n";
                body += "- **Save Data:** " + std::string(c.value("saveData", false) ? "ja" : "nein") + "\n\n";
            }

            // Misc
            body += "### Environment\n";
            body += "- **Language:** " + debug_info.value("language", "?") + "\n";
            if (debug_info.contains("languages") && debug_info["languages"].is_array())
                body += "- **Languages:** " + debug_info["languages"].dump() + "\n";
            body += "- **Platform:** " + debug_info.value("platform", "?") + "\n";
            body += "- **CPU Cores:** " + std::to_string(debug_info.value("hardwareConcurrency", 0)) + "\n";
            body += "- **Online:** " + std::string(debug_info.value("online", true) ? "ja" : "nein") + "\n";
            body += "- **Cookies:** " + std::string(debug_info.value("cookiesEnabled", true) ? "ja" : "nein") + "\n";
            if (debug_info.contains("referrer") && debug_info["referrer"].is_string())
                body += "- **Referrer:** " + debug_info.value("referrer", "") + "\n";
            body += "- **Document Title:** " + debug_info.value("documentTitle", "") + "\n";
            if (debug_info.contains("localStorage") && debug_info["localStorage"].is_object())
            {
                body += "- **localStorage Count:** " + std::to_string(debug_info["localStorage"].value("count", 0)) + "\n";
                if (debug_info["localStorage"].contains("keys") && debug_info["localStorage"]["keys"].is_array())
                    body += "- **localStorage Keys:** " + debug_info["localStorage"]["keys"].dump() + "\n";
            }
            body += "\n";

            // Breadcrumbs (user interaction trace)
            if (debug_info.contains("breadcrumbs") && debug_info["breadcrumbs"].is_array() && !debug_info["breadcrumbs"].empty())
            {
                body += "### User Interaction Trace\n```\n";
                for (auto &entry : debug_info["breadcrumbs"])
                {
                    body += "[" + entry.value("timestamp", "") + "] "
                          + entry.value("type", "?") + ": "
                          + entry.value("message", "") + "\n";
                }
                body += "```\n\n";
            }

            // All API requests
            if (debug_info.contains("recentApiRequests") && debug_info["recentApiRequests"].is_array() && !debug_info["recentApiRequests"].empty())
            {
                body += "### Recent API Requests\n```\n";
                for (auto &entry : debug_info["recentApiRequests"])
                {
                    body += "[" + entry.value("timestamp", "") + "] "
                          + entry.value("method", "?") + " " + entry.value("url", "?")
                          + " → " + std::to_string(entry.value("status", 0))
                          + " (" + std::to_string(entry.value("duration", 0)) + " ms)\n";
                }
                body += "```\n\n";
            }

            // Console errors/warnings
            if (debug_info.contains("recentConsole") && debug_info["recentConsole"].is_array() && !debug_info["recentConsole"].empty())
            {
                body += "### Console Errors/Warnings\n```\n";
                for (auto &entry : debug_info["recentConsole"])
                {
                    body += "[" + entry.value("level", "?") + " " + entry.value("timestamp", "") + "] "
                          + entry.value("message", "") + "\n";
                    if (entry.contains("stack") && entry["stack"].is_string() && !entry["stack"].empty())
                        body += "  Stack: " + entry.value("stack", "") + "\n";
                }
                body += "```\n\n";
            }

            // JS errors
            if (debug_info.contains("recentJsErrors") && debug_info["recentJsErrors"].is_array() && !debug_info["recentJsErrors"].empty())
            {
                body += "### Uncaught JS Errors\n```\n";
                for (auto &entry : debug_info["recentJsErrors"])
                {
                    if (entry.is_string())
                        body += entry.get<std::string>() + "\n";
                }
                body += "```\n\n";
            }

            // Resource errors
            if (debug_info.contains("recentResourceErrors") && debug_info["recentResourceErrors"].is_array() && !debug_info["recentResourceErrors"].empty())
            {
                body += "### Failed Resources\n```\n";
                for (auto &entry : debug_info["recentResourceErrors"])
                {
                    if (entry.is_string())
                        body += entry.get<std::string>() + "\n";
                }
                body += "```\n\n";
            }

            body += "</details>";
        }

        nlohmann::json issue_payload = {
            {"title", title},
            {"body", body},
            {"labels", nlohmann::json::array({"User-Bug"})}
        };

        try
        {
            auto [create_status, create_resp] = github_post_issue(github_token, issue_payload.dump());

            if (create_status != 201)
            {
                send_json(res, 502, nlohmann::json{
                    {"error", "GitHub API Fehler: " + std::to_string(create_status)}
                }.dump());
                return;
            }

            auto resp_j = nlohmann::json::parse(create_resp, nullptr, false);
            std::string issue_url = resp_j.value("html_url", "");
            send_json(res, 200, nlohmann::json{{"issue_url", issue_url}}.dump());
        }
        catch (std::exception &e)
        {
            send_internal_error(res, "handler", e);
        } });
}

// ── Permission management (admin only) ──────────────────────────────────────

static nlohmann::json role_perm_to_json(const RolePermission &rp)
{
    return {
        {"role", rp.role},
        {"can_read_own_dept", rp.can_read_own_dept},
        {"can_write_own_dept", rp.can_write_own_dept},
        {"can_read_all_depts", rp.can_read_all_depts},
        {"can_write_all_depts", rp.can_write_all_depts},
        {"activity_read_scope", rp.activity_read_scope},
        {"activity_create_scope", rp.activity_create_scope},
        {"activity_edit_scope", rp.activity_edit_scope},
        {"mail_send_scope", rp.mail_send_scope},
        {"mail_templates_scope", rp.mail_templates_scope},
        {"form_scope", rp.form_scope},
        {"form_templates_scope", rp.form_templates_scope},
        {"user_dept_scope", rp.user_dept_scope},
        {"user_role_scope", rp.user_role_scope},
        {"locations_manage_scope", rp.locations_manage_scope}};
}

// ── Current user permissions ─────────────────────────────────────────────────

void handle_get_my_permissions(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    auto user = resolve_user(db, claims);
    if (!user)
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }
    try
    {
        if (is_admin(*user))
        {
            nlohmann::json j = {
                {"role", user->role},
                {"can_read_own_dept", true},
                {"can_write_own_dept", true},
                {"can_read_all_depts", true},
                {"can_write_all_depts", true},
                {"activity_read_scope", "all"},
                {"activity_create_scope", "all"},
                {"activity_edit_scope", "all"},
                {"mail_send_scope", "all"},
                {"mail_templates_scope", "all"},
                {"form_scope", "all"},
                {"form_templates_scope", "all"},
                {"user_dept_scope", "all"},
                {"user_role_scope", "all"},
                {"dept_access", nlohmann::json::array()}};
            send_json(res, 200, j.dump());
            return;
        }
        auto perm = db.get_role_permission(user->role);
        if (!perm)
        {
            send_json(res, 200, "{}");
            return;
        }
        auto j = role_perm_to_json(*perm);
        // Include cross-department access entries
        auto dept_access = db.list_role_dept_access(user->role);
        nlohmann::json da_arr = nlohmann::json::array();
        for (auto &da : dept_access)
            da_arr.push_back({{"department", da.department}, {"can_read", da.can_read}, {"can_write", da.can_write}});
        j["dept_access"] = da_arr;
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// Require system-admin permissions (user_role_scope == 'all') — returns user or sends 403
static std::optional<UserRecord> require_admin(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return std::nullopt;
    auto user = resolve_user(db, claims);
    if (!user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    if (is_admin(*user))
        return user;
    auto perm = db.get_role_permission(user->role);
    if (!perm || perm->user_role_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
}

void handle_get_admin_midata_status(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;

    bool api_key_configured = false;
    if (const char *api_key = std::getenv("MIDATA_API_KEY"))
        api_key_configured = std::string(api_key).size() > 0;

    std::string api_url_template = std::getenv("MIDATA_API_URL_TEMPLATE")
                                       ? std::getenv("MIDATA_API_URL_TEMPLATE")
                                       : "https://db.scout.ch/de/groups/{group_id}/people.json";
    std::string api_key_header = "X-Token";

    send_json(res, 200, nlohmann::json{{"api_key_configured", api_key_configured}, {"api_key_header", api_key_header}, {"api_url_template", api_url_template}}.dump());
}

// POST /admin/departments
void handle_post_department(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        std::string color = j.value("color", "#6b7280");
        std::optional<std::string> midata_group_id = std::nullopt;
        if (j.contains("midata_group_id") && j["midata_group_id"].is_string()) {
            std::string value = j["midata_group_id"].get<std::string>();
            if (!value.empty()) midata_group_id = value;
        }
        try {
            auto d = db.create_department(name, color, midata_group_id);
            if (!d) { send_json(res, 409, R"({"error":"Abteilung existiert bereits"})"); return; }
            send_json(res, 201, nlohmann::json{{"name", d->name}, {"color", d->color}, {"midata_group_id", d->midata_group_id ? nlohmann::json(*d->midata_group_id) : nlohmann::json(nullptr)}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// PATCH /admin/departments/:name
void handle_patch_department(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string new_name = j.value("name", name);
        std::string color = j.value("color", "#6b7280");
        std::optional<std::string> midata_group_id;
        bool has_midata_group_field = j.contains("midata_group_id");
        if (has_midata_group_field) {
            if (j["midata_group_id"].is_string()) {
                std::string value = j["midata_group_id"].get<std::string>();
                if (!value.empty()) midata_group_id = value;
            }
        }
        try {
            if (!has_midata_group_field) {
                auto depts = db.list_departments();
                for (const auto &dept : depts) {
                    if (dept.name == name) {
                        midata_group_id = dept.midata_group_id;
                        break;
                    }
                }
            }
            auto d = db.update_department(name, new_name, color, midata_group_id);
            if (!d) { send_json(res, 404, R"({"error":"Abteilung nicht gefunden"})"); return; }
            send_json(res, 200, nlohmann::json{{"name", d->name}, {"color", d->color}, {"midata_group_id", d->midata_group_id ? nlohmann::json(*d->midata_group_id) : nlohmann::json(nullptr)}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// DELETE /admin/departments/:name
void handle_delete_department(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "Allgemein")
    {
        send_json(res, 403, R"({"error":"Die Standard-Stufe kann nicht gelöscht werden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, name](std::string_view chunk, bool last) mutable
                {
        buf->append(chunk);
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) j = nlohmann::json::object();

        std::string transfer_activities_to = j.value("transfer_activities_to", "");
        bool delete_activities = j.value("delete_activities", false);
        std::string transfer_users_to = j.value("transfer_users_to", "");
        bool delete_users = j.value("delete_users", false);

        try
        {
            bool ok = db.delete_department_with_transfers(
                name, transfer_activities_to, delete_activities,
                transfer_users_to, delete_users);
            if (!ok)
            {
                send_json(res, 404, R"({"error":"Stufe nicht gefunden"})");
                return;
            }
            nlohmann::json msg = {{"event", "department_deleted"}, {"name", name}};
            wm.broadcast(msg.dump());
            send_json(res, 200, R"({"ok":true})");
        }
        catch (std::exception &e)
        {
            send_internal_error(res, "handler", e);
        } });
}

// GET /admin/roles
//
// Any authenticated user can list roles — role name + color are required
// throughout the UI to render consistent role badges (avatar dropdown, etc.).
// CRUD on roles remains gated via the per-handler permission checks below.
void handle_get_roles(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto roles = db.list_roles();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &r : roles)
            arr.push_back({{"name", r.name}, {"color", r.color}, {"sort_order", r.sort_order}});
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// POST /admin/roles
void handle_post_role(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        std::string color = j.value("color", "#6b7280");
        try {
            auto r = db.create_role(name, color);
            if (!r) { send_json(res, 409, R"({"error":"Rolle existiert bereits"})"); return; }
            send_json(res, 201, nlohmann::json{{"name", r->name}, {"color", r->color}, {"sort_order", r->sort_order}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// PATCH /admin/roles/:name
void handle_patch_role(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "admin")
    {
        send_json(res, 403, R"({"error":"Die Admin-Rolle kann nicht bearbeitet werden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string new_name = j.value("name", name);
        std::string color = j.value("color", "#6b7280");
        try {
            auto r = db.update_role(name, new_name, color);
            if (!r) { send_json(res, 404, R"({"error":"Rolle nicht gefunden"})"); return; }
            send_json(res, 200, nlohmann::json{{"name", r->name}, {"color", r->color}, {"sort_order", r->sort_order}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// POST /admin/roles/:name/move
void handle_post_role_move(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "admin")
    {
        send_json(res, 403, R"({"error":"Die Admin-Rolle bleibt immer zuoberst"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string direction = j.value("direction", "");
        if (direction != "up" && direction != "down") {
            send_json(res, 400, R"({"error":"direction muss up oder down sein"})");
            return;
        }
        try {
            bool ok = db.move_role(name, direction == "up");
            if (!ok) { send_json(res, 404, R"({"error":"Rolle nicht gefunden"})"); return; }
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// POST /admin/roles/reorder
void handle_post_roles_reorder(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object() || !j.contains("order") || !j["order"].is_array()) {
            send_json(res, 400, R"({"error":"Erwarte {order: [...]}"})");
            return;
        }
        std::vector<std::string> names;
        for (const auto &item : j["order"]) {
            if (!item.is_string()) {
                send_json(res, 400, R"({"error":"order muss ein Array von Strings sein"})");
                return;
            }
            names.push_back(item.get<std::string>());
        }
        try {
            db.reorder_roles(names);
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// DELETE /admin/roles/:name
void handle_delete_role(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "admin")
    {
        send_json(res, 403, R"({"error":"Die Admin-Rolle kann nicht gelöscht werden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        std::string transfer_users_to;
        bool del_users = false;

        if (!buf->empty())
        {
            auto j = nlohmann::json::parse(*buf, nullptr, false);
            if (j.is_object())
            {
                if (j.contains("transfer_users_to") && j["transfer_users_to"].is_string())
                    transfer_users_to = j["transfer_users_to"].get<std::string>();
                if (j.contains("delete_users") && j["delete_users"].is_boolean())
                    del_users = j["delete_users"].get<bool>();
            }
        }

        try {
            bool ok = db.delete_role(name, transfer_users_to, del_users);
            if (!ok) { send_json(res, 404, R"({"error":"Rolle nicht gefunden"})"); return; }
            nlohmann::json msg = {{"event", "role_deleted"}, {"name", name}};
            wm.broadcast(msg.dump());
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// GET /admin/role-permissions
void handle_get_role_permissions(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    try
    {
        auto perms = db.list_role_permissions();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &p : perms)
            arr.push_back(role_perm_to_json(p));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// PUT /admin/role-permissions
void handle_put_role_permission(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto current_perm = db.get_role_permission(current_user->role);
    if (!current_perm || current_perm->user_role_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }
        std::string role = j.value("role", "");
        if (role.empty()) {
            send_json(res, 400, R"({"error":"role erforderlich"})");
            return;
        }
        if (role == "admin") {
            send_json(res, 403, R"({"error":"Die Berechtigungen der Admin-Rolle können nicht geändert werden"})");
            return;
        }
        std::string activity_read_scope = j.value("activity_read_scope", "none");
        std::string activity_create_scope = j.value("activity_create_scope", "none");
        std::string activity_edit_scope = j.value("activity_edit_scope", "none");
        std::string mail_send_scope = j.value("mail_send_scope", "none");
        std::string mail_templates_scope = j.value("mail_templates_scope", "none");
        std::string form_scope = j.value("form_scope", "none");
        std::string form_templates_scope = j.value("form_templates_scope", "none");
        std::string user_dept_scope = j.value("user_dept_scope", "none");
        std::string user_role_scope = j.value("user_role_scope", "none");
        std::string locations_manage_scope = j.value("locations_manage_scope", "none");

        bool can_read_own_dept = activity_read_scope == "same_dept" || activity_read_scope == "all";
        bool can_read_all_depts = activity_read_scope == "all";
        bool can_write_own_dept = activity_create_scope == "own_dept" || activity_create_scope == "all";
        bool can_write_all_depts = activity_create_scope == "all";

        try {
            bool ok = db.update_role_permission(role, can_read_own_dept, can_write_own_dept,
                                                can_read_all_depts, can_write_all_depts,
                                                activity_read_scope, activity_create_scope, activity_edit_scope,
                                                mail_send_scope, mail_templates_scope,
                                                form_scope, form_templates_scope,
                                                user_dept_scope, user_role_scope, locations_manage_scope);
            if (!ok) {
                send_json(res, 404, R"({"error":"Rolle nicht gefunden"})");
                return;
            }
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// GET /admin/role-dept-access?role=XYZ
void handle_get_role_dept_access(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    std::string role{req->getQuery("role")};
    if (role.empty())
    {
        send_json(res, 400, R"({"error":"role Query-Parameter erforderlich"})");
        return;
    }
    try
    {
        auto access = db.list_role_dept_access(role);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &a : access)
            arr.push_back({{"role", a.role}, {"department", a.department}, {"can_read", a.can_read}, {"can_write", a.can_write}});
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// PUT /admin/role-dept-access
void handle_put_role_dept_access(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string role = j.value("role", "");
        std::string department = j.value("department", "");
        if (role.empty() || department.empty()) {
            send_json(res, 400, R"({"error":"role und department erforderlich"})");
            return;
        }
        bool can_read = j.value("can_read", false);
        bool can_write = j.value("can_write", false);
        try {
            db.set_role_dept_access(role, department, can_read, can_write);
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// ── Forms ─────────────────────────────────────────────────────────────────────
//
// Permission model: reuses activity-based dept access.
//  - Read form / responses / stats: can_read_dept on activity's department
//  - Create / update / delete form / responses: can_write_dept on activity's department
//  - Templates: can_write_all_depts (admin) or own department write access

// Helper: check form_scope-based read access for an activity
static bool can_form_access(Database &db, const RolePermission &perm, const UserRecord &user,
                            const TokenClaims &claims, const std::string &activity_id, bool /*write*/)
{
    if (is_admin(user))
        return true;
    if (perm.form_scope == "none")
        return false;
    auto act = db.get_activity_by_id(activity_id);
    if (!act)
        return false;
    if (perm.form_scope == "all")
        return true;
    if (perm.form_scope == "same_dept" && act->department && user.department && *act->department == *user.department)
        return true;
    if (perm.form_scope == "own" && is_activity_responsible(*act, user, claims.email))
        return true;
    return false;
}

// Helper: resolve activity department from activity_id
static std::optional<std::string> get_activity_dept(Database &db, const std::string &activity_id)
{
    auto act = db.get_activity_by_id(activity_id);
    if (!act)
        return std::nullopt;
    return act->department;
}

// Helper: parse questions array from JSON
static std::vector<FormQuestion> parse_questions(const nlohmann::json &arr)
{
    std::vector<FormQuestion> questions;
    if (!arr.is_array())
        return questions;
    int pos = 0;
    for (auto &q : arr)
    {
        FormQuestion fq;
        fq.question_text = q.value("question_text", "");
        fq.question_type = q.value("question_type", "text_input");
        fq.position = q.contains("position") ? q["position"].get<int>() : pos;
        fq.is_required = q.value("is_required", true);
        fq.metadata = q.contains("metadata") ? q["metadata"] : nlohmann::json::object();
        questions.push_back(fq);
        ++pos;
    }
    return questions;
}

// ---- GET /activities/:id/form -----------------------------------------------

void handle_get_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }
        send_json(res, 200, signup_form_to_json(*form).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /activities/:id/form ----------------------------------------------

void handle_post_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string activity_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    // Check: exactly one form per activity
    auto existing = db.get_form_for_activity(activity_id);
    if (existing)
    {
        send_json(res, 409, R"({"error":"Formular existiert bereits f\u00fcr diese Aktivit\u00e4t"})");
        return;
    }

    std::string user_id = current_user->id;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, user_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }
        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        if (title.empty()) { send_json(res, 400, R"({"error":"Titel erforderlich"})"); return; }
        auto questions = parse_questions(j.value("questions", nlohmann::json::array()));
        try {
            auto form = db.create_form(activity_id, form_type, title, user_id, questions);
            if (!form) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, signup_form_to_json(*form).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- PUT /activities/:id/form -----------------------------------------------

void handle_put_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string activity_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }
        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        if (title.empty()) { send_json(res, 400, R"({"error":"Titel erforderlich"})"); return; }
        auto questions = parse_questions(j.value("questions", nlohmann::json::array()));
        try {
            auto form = db.update_form(activity_id, form_type, title, questions);
            if (!form) { send_json(res, 404, R"({"error":"Formular nicht gefunden"})"); return; }
            send_json(res, 200, signup_form_to_json(*form).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- DELETE /activities/:id/form --------------------------------------------

void handle_delete_activity_form(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_form(activity_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Formular nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/form/responses -------------------------------------

void handle_get_form_responses(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }

        auto responses = db.list_responses(form->id);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &r : responses)
            arr.push_back(form_response_to_json(r));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/form/responses/:rid --------------------------------

void handle_get_form_response(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    std::string response_id{req->getParameter(1)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto response = db.get_response(response_id);
        if (!response)
        {
            send_json(res, 404, R"({"error":"Antwort nicht gefunden"})");
            return;
        }
        send_json(res, 200, form_response_to_json(*response, true).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- DELETE /activities/:id/form/responses/:rid -----------------------------

void handle_delete_form_response(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    std::string response_id{req->getParameter(1)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_response(response_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Antwort nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/form/stats -----------------------------------------

void handle_get_form_stats(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);

        auto act_dept = get_activity_dept(db, activity_id);
        bool allowed = false;
        if (perm)
        {
            if (act_dept)
                allowed = can_read_dept(*perm, *current_user, dept_access, *act_dept);
            else
                allowed = perm->can_read_all_depts || perm->can_read_own_dept;
        }
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }

        auto stats = db.get_form_stats(form->id);
        stats["form_type"] = form->form_type;
        send_json(res, 200, stats.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /forms/:slug (public, no auth) --------------------------------

void handle_get_public_form(HttpRes *res, HttpReq *req, Database &db)
{
    std::string public_slug{req->getParameter(0)};
    try
    {
        auto form = db.get_form_for_public_slug(public_slug);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Formular nicht gefunden"})");
            return;
        }

        // Resolve template variables in question texts using activity data
        auto act = db.get_activity_by_id(form->activity_id);
        if (act)
        {
            form->title = replace_template_vars(form->title, *act);
            for (auto &q : form->questions)
            {
                q.question_text = replace_template_vars(q.question_text, *act);
                // Also resolve subtitle in section metadata
                if (q.metadata.contains("subtitle") && q.metadata["subtitle"].is_string())
                {
                    q.metadata["subtitle"] = replace_template_vars(q.metadata["subtitle"].get<std::string>(), *act);
                }
                // Resolve choice labels
                if (q.metadata.contains("choices") && q.metadata["choices"].is_array())
                {
                    for (auto &c : q.metadata["choices"])
                    {
                        if (c.contains("label") && c["label"].is_string())
                        {
                            c["label"] = replace_template_vars(c["label"].get<std::string>(), *act);
                        }
                    }
                }
            }
        }

        send_json(res, 200, signup_form_to_json(*form).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /forms/:slug/submit (public, no auth) -----------------------

void handle_post_form_submit(HttpRes *res, HttpReq *req, Database &db)
{
    std::string public_slug{req->getParameter(0)};

    std::string user_agent{req->getHeader("user-agent")};
    std::string ip_address{req->getHeader("x-real-ip")};

    auto buf = std::make_shared<std::string>();
    auto handled = std::make_shared<bool>(false);
    res->onAborted([] {});
    res->onData([res, buf, handled, public_slug, user_agent, ip_address, &db](std::string_view chunk, bool last)
                {
        if (*handled) return;
        if (buf->size() + chunk.size() > kMaxPublicFormPayloadBytes)
        {
            *handled = true;
            send_json(res, 413, R"({"error":"Anfrage zu gross"})");
            return;
        }

        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        *handled = true;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        try {
            auto form = db.get_form_for_public_slug(public_slug);
            if (!form) { send_json(res, 404, R"({"error":"Formular nicht gefunden"})"); return; }

            auto client_key = normalize_public_submit_client_key(ip_address, user_agent);
            if (is_public_submit_rate_limited(public_slug, client_key))
            {
                send_json(res, 429, R"({"error":"Zu viele Einsendungen in kurzer Zeit"})");
                return;
            }

            // Parse answers: array of { question_id, answer_value }
            std::vector<std::pair<std::string, std::string>> answers;
            if (j.contains("answers") && j["answers"].is_array())
            {
                for (auto &a : j["answers"])
                {
                    std::string qid = a.value("question_id", "");
                    std::string val = a.value("answer_value", "");
                    if (val.size() > kMaxPublicAnswerLength)
                    {
                        send_json(res, 400, R"({"error":"Antwort ist zu lang"})");
                        return;
                    }
                    if (!qid.empty())
                        answers.emplace_back(qid, val);
                }
            }

            // Validate required questions
            for (const auto &q : form->questions)
            {
                if (q.question_type == "section" || !q.is_required) continue;
                bool answered = false;
                for (auto &[qid, val] : answers)
                    if (qid == q.id && !val.empty()) { answered = true; break; }
                if (!answered)
                {
                    send_json(res, 400, nlohmann::json{{"error", "Pflichtfeld fehlt: " + q.question_text}}.dump());
                    return;
                }
            }

            auto resp = db.submit_response(form->id, form->form_type, user_agent, ip_address, answers);
            if (!resp) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, form_response_to_json(*resp, true).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- GET /form-templates?department=... -------------------------------------

void handle_get_form_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string department = url_decode(std::string{req->getQuery("department")});
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto templates = db.list_form_templates(department);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
            arr.push_back(form_template_to_json(t));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /form-templates ---------------------------------------------------

void handle_post_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->form_templates_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string user_id = current_user->id;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, user_id, current_user, perm, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        std::string name = j.value("name", "");
        std::string department = j.value("department", "");
        std::string form_type = j.value("form_type", "registration");
        nlohmann::json config = j.contains("template_config") ? j["template_config"] : nlohmann::json::array();
        bool is_default = j.value("is_default", false);

        if (name.empty() || department.empty()) {
            send_json(res, 400, R"({"error":"name und department erforderlich"})");
            return;
        }
        // Scope check: own_dept only allows own department
        if (!is_admin(*current_user) && perm->form_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department) {
                send_json(res, 403, R"({"error":"Keine Berechtigung f\u00fcr dieses Department"})");
                return;
            }
        }
        try {
            auto tpl = db.create_form_template(name, department, form_type, config, user_id, is_default);
            if (!tpl) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, form_template_to_json(*tpl).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- PUT /form-templates/:id ------------------------------------------------

void handle_put_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string tpl_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->form_templates_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, tpl_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        std::string name = j.value("name", "");
        std::string form_type = j.value("form_type", "registration");
        nlohmann::json config = j.contains("template_config") ? j["template_config"] : nlohmann::json::array();
        bool is_default = j.value("is_default", false);

        try {
            auto tpl = db.update_form_template(tpl_id, name, form_type, config, is_default);
            if (!tpl) { send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})"); return; }
            send_json(res, 200, form_template_to_json(*tpl).dump());
        } catch (std::exception &e) { send_internal_error(res, "handler", e); } });
}

// ---- DELETE /form-templates/:id ---------------------------------------------

void handle_delete_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string tpl_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->form_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        bool ok = db.delete_form_template(tpl_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- Activity share links ---------------------------------------------------

void handle_post_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);
        if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto link = db.create_share_link(activity_id, current_user->id);
        if (!link)
        {
            send_json(res, 500, R"({"error":"Share-Link konnte nicht erstellt werden"})");
            return;
        }
        nlohmann::json j = {
            {"id", link->id},
            {"activity_id", link->activity_id},
            {"share_token", link->share_token},
            {"created_at", link->created_at}};
        send_json(res, 201, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto link = db.get_share_link(activity_id);
        if (!link)
        {
            send_json(res, 200, R"({"share_token":null})");
            return;
        }
        nlohmann::json j = {
            {"id", link->id},
            {"activity_id", link->activity_id},
            {"share_token", link->share_token},
            {"created_at", link->created_at}};
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_delete_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        bool ok = db.delete_share_link(activity_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Kein Share-Link vorhanden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_shared_activity(HttpRes *res, HttpReq *req, Database &db)
{
    std::string token{req->getParameter(0)};
    try
    {
        auto activity = db.get_activity_by_share_token(token);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}
