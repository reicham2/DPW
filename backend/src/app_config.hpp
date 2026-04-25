#pragma once

#include <optional>
#include <string>
#include <vector>
#include "db.hpp"
#include "json.hpp"

namespace app_config
{
    enum class ValueType
    {
        Text,
        Url,
        Integer,
        Boolean,
        Slug
    };

    struct SettingDef
    {
        const char *key;
        const char *env_name;
        const char *default_value;
        bool is_secret;
        bool expose_value;
        ValueType value_type;
        int min_int;
        int max_int;
    };

    // Known runtime settings manageable through admin UI.
    const std::vector<SettingDef> &definitions();

    // Resolve setting from DB only.
    std::optional<std::string> get(Database &db, const std::string &key);
    std::string get_or(Database &db, const std::string &key, const std::string &fallback);
    int get_int_or(Database &db, const std::string &key, int fallback, int min_value, int max_value);

    // Import ENV overrides into DB once at startup.
    int import_env_overrides_to_db(Database &db, std::vector<std::string> &warnings);
    bool ensure_generated_vapid(Database &db, std::string &error, bool &generated);

    // Admin API helpers (never returns secret plaintext).
    nlohmann::json list_for_admin(Database &db);
    bool set_from_admin(Database &db,
                        const std::string &key,
                        const std::optional<std::string> &value,
                        std::string &error);

    // Setting keys
    inline constexpr const char *kMidataApiKey = "midata.api_key";
    inline constexpr const char *kMidataApiUrlTemplate = "midata.api_url_template";
    inline constexpr const char *kMidataApiTimeoutMs = "midata.api_timeout_ms";
    inline constexpr const char *kAzureTenantId = "azure.tenant_id";
    inline constexpr const char *kAzureClientId = "azure.client_id";
    inline constexpr const char *kAzureClientSecret = "azure.client_secret";
    inline constexpr const char *kWpUrl = "wp.url";
    inline constexpr const char *kWpUser = "wp.user";
    inline constexpr const char *kWpAppPassword = "wp.app_password";
    inline constexpr const char *kVapidPublicKey = "push.vapid_public_key";
    inline constexpr const char *kVapidPrivateKey = "push.vapid_private_key";
    inline constexpr const char *kVapidSubject = "push.vapid_subject";
    inline constexpr const char *kPublicBaseUrl = "frontend.public_base_url";
    inline constexpr const char *kGitHubToken = "github.token";
    inline constexpr const char *kGitHubRepo = "github.repo";
    inline constexpr const char *kAutosaveIntervalMs = "frontend.autosave_interval_ms";
    inline constexpr const char *kAutosaveDebounce = "frontend.autosave_debounce";
    inline constexpr const char *kMidataWeatherRefreshIntervalMs = "midata.weather_refresh_interval_ms";
}
