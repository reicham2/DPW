#include "wp_client.hpp"
#include "app_config.hpp"
#include "db.hpp"
#include "json.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

// ── helpers ─────────────────────────────────────────────────────────────────

static size_t wp_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// Base64-encode for HTTP Basic auth (minimal, ASCII-only).
static std::string base64_encode(const std::string &in)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

struct WpConfig
{
    std::string url;
    std::string auth; // "Basic <base64>"
};

static std::optional<WpConfig> get_wp_config(Database &db)
{
    std::string url = app_config::get_or(db, app_config::kWpUrl, "");
    std::string user = app_config::get_or(db, app_config::kWpUser, "");
    std::string pass = app_config::get_or(db, app_config::kWpAppPassword, "");
    if (url.empty() || user.empty() || pass.empty())
        return std::nullopt;

    // Remove trailing slash
    while (!url.empty() && url.back() == '/')
        url.pop_back();

    WpConfig cfg;
    cfg.url = url;
    cfg.auth = "Basic " + base64_encode(user + ":" + pass);
    return cfg;
}

// Send JSON to WordPress REST API. method = "POST" or "PUT" (mapped to POST + _method for WP).
static std::pair<long, std::string> wp_request(const std::string &method,
                                               const std::string &url,
                                               const std::string &auth,
                                               const std::string &body)
{
    std::string response;
    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    struct curl_slist *headers = nullptr;
    std::string auth_header = "Authorization: " + auth;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wp_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    // pfadihue.ch uses a certificate chain that Alpine's CA bundle doesn't trust
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (method == "POST")
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
    else if (method == "PUT")
    {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }
    else if (method == "DELETE")
    {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("WordPress API request failed: ") + curl_easy_strerror(rc));
    return {status, response};
}

// ── event type taxonomy ─────────────────────────────────────────────────────

// Find an EventON event_type term by name, or create it if it doesn't exist.
// Returns the term ID, or 0 on failure.
static int find_or_create_event_type(const WpConfig &cfg, const std::string &name)
{
    if (name.empty())
        return 0;

    // Search for existing term
    CURL *esc_curl = curl_easy_init();
    char *esc = curl_easy_escape(esc_curl, name.c_str(), (int)name.size());
    std::string search_url = cfg.url + "/wp-json/wp/v2/event_type?search=" + esc;
    curl_free(esc);
    curl_easy_cleanup(esc_curl);

    try
    {
        auto [status, resp] = wp_request("GET", search_url, cfg.auth, "");
        if (status == 200)
        {
            auto arr = nlohmann::json::parse(resp, nullptr, false);
            if (arr.is_array())
            {
                for (auto &term : arr)
                {
                    if (term.contains("name") && term["name"].is_string() &&
                        term["name"].get<std::string>() == name)
                        return term["id"].get<int>();
                }
            }
        }
    }
    catch (...)
    {
    }

    // Not found — create it
    try
    {
        nlohmann::json body = {{"name", name}};
        auto [status, resp] = wp_request("POST", cfg.url + "/wp-json/wp/v2/event_type", cfg.auth, body.dump());
        if (status >= 200 && status < 300)
        {
            auto j = nlohmann::json::parse(resp, nullptr, false);
            if (!j.is_discarded() && j.contains("id"))
                return j["id"].get<int>();
        }
        else
        {
            fprintf(stderr, "[wp_client] create event_type '%s' failed: HTTP %ld – %s\n", name.c_str(), status, resp.c_str());
        }
    }
    catch (std::exception &e)
    {
        fprintf(stderr, "[wp_client] create event_type error: %s\n", e.what());
    }
    return 0;
}

// ── public API ──────────────────────────────────────────────────────────────

bool wp_configured(Database &db)
{
    return get_wp_config(db).has_value();
}

std::optional<WpEventResult> wp_create_event(const std::string &title,
                                             const std::string &body_html,
                                             long start_unix,
                                             long end_unix,
                                             const std::string &location,
                                             const std::string &department,
                                             Database &db)
{
    auto cfg = get_wp_config(db);
    if (!cfg)
        return std::nullopt;

    std::string url = cfg->url + "/wp-json/wp/v2/ajde_events";

    nlohmann::json body = {
        {"title", title},
        {"content", body_html},
        {"status", "publish"},
        {"meta", {
                     {"evcal_srow", start_unix},
                     {"evcal_erow", end_unix},
                     {"evcal_allday", "no"},
                     {"_evcal_exlink_option", "1"},
                 }},
    };
    if (!location.empty())
        body["meta"]["evcal_location_name"] = location;

    int type_id = find_or_create_event_type(*cfg, department);
    if (type_id > 0)
        body["event_type"] = nlohmann::json::array({type_id});

    try
    {
        auto [status, resp] = wp_request("POST", url, cfg->auth, body.dump());
        if (status < 200 || status >= 300)
        {
            fprintf(stderr, "[wp_client] create failed: HTTP %ld – %s\n", status, resp.c_str());
            return std::nullopt;
        }
        auto j = nlohmann::json::parse(resp, nullptr, false);
        if (j.is_discarded() || !j.contains("id"))
            return std::nullopt;
        return WpEventResult{std::to_string(j["id"].get<int>())};
    }
    catch (std::exception &e)
    {
        fprintf(stderr, "[wp_client] create error: %s\n", e.what());
        return std::nullopt;
    }
}

std::optional<WpEventResult> wp_update_event(const std::string &wp_id,
                                             const std::string &title,
                                             const std::string &body_html,
                                             long start_unix,
                                             long end_unix,
                                             const std::string &location,
                                             const std::string &department,
                                             Database &db)
{
    auto cfg = get_wp_config(db);
    if (!cfg)
        return std::nullopt;

    std::string url = cfg->url + "/wp-json/wp/v2/ajde_events/" + wp_id;

    nlohmann::json body = {
        {"title", title},
        {"content", body_html},
        {"status", "publish"},
        {"meta", {
                     {"evcal_srow", start_unix},
                     {"evcal_erow", end_unix},
                     {"evcal_allday", "no"},
                 }},
    };
    if (!location.empty())
        body["meta"]["evcal_location_name"] = location;

    int type_id = find_or_create_event_type(*cfg, department);
    if (type_id > 0)
        body["event_type"] = nlohmann::json::array({type_id});

    try
    {
        // WordPress REST API uses POST to update an existing resource by ID
        auto [status, resp] = wp_request("POST", url, cfg->auth, body.dump());
        if (status < 200 || status >= 300)
        {
            fprintf(stderr, "[wp_client] update %s failed: HTTP %ld – %s\n", wp_id.c_str(), status, resp.c_str());
            return std::nullopt;
        }
        auto j = nlohmann::json::parse(resp, nullptr, false);
        if (j.is_discarded() || !j.contains("id"))
            return std::nullopt;
        return WpEventResult{std::to_string(j["id"].get<int>())};
    }
    catch (std::exception &e)
    {
        fprintf(stderr, "[wp_client] update error: %s\n", e.what());
        return std::nullopt;
    }
}

bool wp_delete_event(const std::string &wp_id, Database &db)
{
    auto cfg = get_wp_config(db);
    if (!cfg)
        return false;

    // WordPress requires ?force=true to actually delete (skip trash)
    std::string url = cfg->url + "/wp-json/wp/v2/ajde_events/" + wp_id + "?force=true";

    try
    {
        auto [status, resp] = wp_request("DELETE", url, cfg->auth, "");
        if (status < 200 || status >= 300)
        {
            fprintf(stderr, "[wp_client] delete %s failed: HTTP %ld – %s\n", wp_id.c_str(), status, resp.c_str());
            return false;
        }
        return true;
    }
    catch (std::exception &e)
    {
        fprintf(stderr, "[wp_client] delete error: %s\n", e.what());
        return false;
    }
}
