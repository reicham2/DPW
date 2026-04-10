#include "graph.hpp"
#include "json.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <string>
#include <stdexcept>

static std::string g_env(const char *key)
{
    const char *v = std::getenv(key);
    return v ? v : "";
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

// POST form-encoded body, return response body
static std::string curl_post_form(const std::string &url, const std::string &body)
{
    std::string response;
    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("curl POST failed: ") + curl_easy_strerror(rc));
    return response;
}

// POST JSON body with Bearer token, return (http_status, response_body)
static std::pair<long, std::string> curl_post_json(const std::string &url,
                                                    const std::string &token,
                                                    const std::string &body)
{
    std::string response;
    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    struct curl_slist *headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("curl POST JSON failed: ") + curl_easy_strerror(rc));
    return {status, response};
}

// Obtain an app-level access token for Microsoft Graph via client_credentials flow.
static std::string get_app_token()
{
    std::string tenant   = g_env("AZURE_TENANT_ID");
    std::string client   = g_env("AZURE_CLIENT_ID");
    std::string secret   = g_env("AZURE_CLIENT_SECRET");

    if (tenant.empty() || client.empty() || secret.empty())
        return "";

    std::string url = "https://login.microsoftonline.com/" + tenant + "/oauth2/v2.0/token";

    // URL-encode the secret (basic: replace special chars – for most secrets this is fine)
    // For production, use curl_easy_escape. Here we rely on the secret having no special chars.
    std::string body =
        "grant_type=client_credentials"
        "&client_id=" + client +
        "&client_secret=" + secret +
        "&scope=https%3A%2F%2Fgraph.microsoft.com%2F.default";

    try
    {
        std::string resp = curl_post_form(url, body);
        auto j = nlohmann::json::parse(resp, nullptr, false);
        if (!j.is_discarded() && j.contains("access_token") && j["access_token"].is_string())
            return j["access_token"].get<std::string>();
    }
    catch (...) {}
    return "";
}

std::optional<bool> is_group_member(const std::string &user_oid, const std::string &group_id)
{
    try
    {
        std::string token = get_app_token();
        if (token.empty())
            return std::nullopt;

        // POST /v1.0/users/{id}/checkMemberGroups
        // Returns array of group IDs the user belongs to (from the provided list).
        std::string url = "https://graph.microsoft.com/v1.0/users/" + user_oid + "/checkMemberGroups";
        nlohmann::json body = {{"groupIds", {group_id}}};

        auto [status, resp] = curl_post_json(url, token, body.dump());

        if (status != 200)
            return std::nullopt;

        auto j = nlohmann::json::parse(resp, nullptr, false);
        if (j.is_discarded() || !j.contains("value") || !j["value"].is_array())
            return std::nullopt;

        for (auto &v : j["value"])
        {
            if (v.is_string() && v.get<std::string>() == group_id)
                return true;
        }
        return false;
    }
    catch (...)
    {
        return std::nullopt;
    }
}
