#include "app_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <regex>

namespace
{
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

    const app_config::SettingDef *find_def(const std::string &key)
    {
        const auto &defs = app_config::definitions();
        for (const auto &def : defs)
        {
            if (key == def.key)
                return &def;
        }
        return nullptr;
    }

    std::optional<std::string> getenv_trimmed(const char *name)
    {
        if (!name || !*name)
            return std::nullopt;
        const char *v = std::getenv(name);
        if (!v)
            return std::nullopt;
        std::string out = trim_ascii(v);
        if (out.empty())
            return std::nullopt;
        return out;
    }

    std::string encryption_key()
    {
        auto v = getenv_trimmed("DPW_CONFIG_ENCRYPTION_KEY");
        return v ? *v : "";
    }

    std::string default_for_def(const app_config::SettingDef &def)
    {
        return trim_ascii(def.default_value ? def.default_value : "");
    }

    bool looks_like_http_url(const std::string &value)
    {
        static const std::regex re(R"(^https?://[^\s]+$)", std::regex::icase);
        return std::regex_match(value, re);
    }

    bool looks_like_slug(const std::string &value)
    {
        static const std::regex re(R"(^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$)");
        return std::regex_match(value, re);
    }

    bool looks_like_email(const std::string &value)
    {
        static const std::regex re(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
        return std::regex_match(value, re);
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

    std::optional<EVP_PKEY *> generate_vapid_keypair_raw()
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!ctx)
            return std::nullopt;

        EVP_PKEY *pkey = nullptr;
        OSSL_PARAM params[] = {
            OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                             const_cast<char *>("prime256v1"),
                                             0),
            OSSL_PARAM_construct_end(),
        };

        const bool ok = EVP_PKEY_keygen_init(ctx) > 0 &&
                        EVP_PKEY_CTX_set_params(ctx, params) > 0 &&
                        EVP_PKEY_generate(ctx, &pkey) > 0 &&
                        pkey != nullptr;
        EVP_PKEY_CTX_free(ctx);
        if (!ok)
            return std::nullopt;

        return pkey;
    }

    std::optional<std::string> pkey_octet_param(EVP_PKEY *pkey, const char *name)
    {
        if (!pkey || !name)
            return std::nullopt;

        if (strcmp(name, "pub") == 0 || strcmp(name, "pub-key") == 0)
        {
            size_t len = 0;
            if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &len) <= 0 || len == 0)
                return std::nullopt;

            std::string out(len, '\0');
            if (EVP_PKEY_get_octet_string_param(pkey,
                                                OSSL_PKEY_PARAM_PUB_KEY,
                                                reinterpret_cast<unsigned char *>(&out[0]),
                                                out.size(),
                                                &len) <= 0)
                return std::nullopt;
            out.resize(len);
            return out;
        }

        if (strcmp(name, "priv") == 0 || strcmp(name, "priv-key") == 0)
        {
            BIGNUM *priv_bn = nullptr;
            if (EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_PRIV_KEY, &priv_bn) <= 0 || !priv_bn)
                return std::nullopt;

            std::string out(32, '\0');
            if (BN_bn2binpad(priv_bn,
                             reinterpret_cast<unsigned char *>(&out[0]),
                             static_cast<int>(out.size())) != static_cast<int>(out.size()))
            {
                BN_free(priv_bn);
                return std::nullopt;
            }
            BN_free(priv_bn);
            return out;
        }

        return std::nullopt;
    }

    std::optional<std::pair<std::string, std::string>> generate_vapid_keypair_encoded()
    {
        auto pkey = generate_vapid_keypair_raw();
        if (!pkey)
            return std::nullopt;

        auto public_bytes = pkey_octet_param(*pkey, "pub");
        auto private_bytes = pkey_octet_param(*pkey, "priv");
        EVP_PKEY_free(*pkey);

        if (!public_bytes || !private_bytes)
            return std::nullopt;

        const std::string public_b64u = base64url_encode(*public_bytes);
        const std::string private_b64u = base64url_encode(*private_bytes);
        if (public_b64u.empty() || private_b64u.empty())
            return std::nullopt;

        return std::make_pair(public_b64u, private_b64u);
    }

    bool normalize_and_validate_value(const app_config::SettingDef &def,
                                      std::string &value,
                                      std::string &error)
    {
        switch (def.value_type)
        {
        case app_config::ValueType::Text:
            return true;
        case app_config::ValueType::Url:
            if (!looks_like_http_url(value))
            {
                error = "invalid-url";
                return false;
            }
            while (!value.empty() && value.back() == '/')
                value.pop_back();
            return true;
        case app_config::ValueType::Integer:
            try
            {
                const int parsed = std::stoi(value);
                if (parsed < def.min_int || parsed > def.max_int)
                {
                    error = "out-of-range";
                    return false;
                }
                value = std::to_string(parsed);
                return true;
            }
            catch (...)
            {
                error = "invalid-integer";
                return false;
            }
        case app_config::ValueType::Boolean:
        {
            std::string lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
            {
                value = "true";
                return true;
            }
            if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
            {
                value = "false";
                return true;
            }
            error = "invalid-boolean";
            return false;
        }
        case app_config::ValueType::Slug:
            if (!looks_like_slug(value))
            {
                error = "invalid-slug";
                return false;
            }
            return true;
        }
        return true;
    }
}

namespace app_config
{
    const std::vector<SettingDef> &definitions()
    {
        static const std::vector<SettingDef> defs = {
            {kAzureTenantId, "AZURE_TENANT_ID", "", false, true, ValueType::Text, 0, 0},
            {kAzureClientId, "AZURE_CLIENT_ID", "", false, true, ValueType::Text, 0, 0},
            {kAzureClientSecret, "AZURE_CLIENT_SECRET", "", true, false, ValueType::Text, 0, 0},
            {kVapidPublicKey, "DPW_VAPID_PUBLIC_KEY", "", true, false, ValueType::Text, 0, 0},
            {kVapidPrivateKey, "DPW_VAPID_PRIVATE_KEY", "", true, false, ValueType::Text, 0, 0},
            {kVapidSubject, "DPW_VAPID_SUBJECT", "mailto:admin@example.com", false, true, ValueType::Text, 0, 0},
            {kPublicBaseUrl, "DPW_PUBLIC_URL", "", false, true, ValueType::Url, 0, 0},
            {kAutosaveIntervalMs, "AUTOSAVE_INTERVAL", "1500", false, true, ValueType::Integer, 250, 600000},
            {kAutosaveDebounce, "AUTOSAVE_DEBOUNCE", "true", false, true, ValueType::Boolean, 0, 0},
            {kMidataWeatherRefreshIntervalMs, "MIDATA_WEATHER_REFRESH_INTERVAL", "900000", false, true, ValueType::Integer, 10000, 86400000},
            {kGitHubToken, "GITHUB_TOKEN", "", true, false, ValueType::Text, 0, 0},
            {kGitHubRepo, "GITHUB_REPO", "reicham2/DPW", false, true, ValueType::Slug, 0, 0},
            {kMidataApiKey, "MIDATA_API_KEY", "", true, false, ValueType::Text, 0, 0},
            {kMidataApiUrlTemplate, "MIDATA_API_URL_TEMPLATE", "https://db.scout.ch/de/groups/{group_id}/people.json", false, true, ValueType::Url, 0, 0},
            {kMidataApiTimeoutMs, "MIDATA_API_TIMEOUT_MS", "8000", false, true, ValueType::Integer, 500, 120000},
            {kWpUrl, "DPW_WP_URL", "", false, true, ValueType::Url, 0, 0},
            {kWpUser, "DPW_WP_USER", "", false, true, ValueType::Text, 0, 0},
            {kWpAppPassword, "DPW_WP_APP_PASSWORD", "", true, false, ValueType::Text, 0, 0},
        };
        return defs;
    }

    std::optional<std::string> get(Database &db, const std::string &key)
    {
        const auto *def = find_def(key);
        if (!def)
            return std::nullopt;

        auto env_value = getenv_trimmed(def->env_name);
        if (env_value)
            return env_value;

        if (def->is_secret)
        {
            std::string ek = encryption_key();
            if (!ek.empty())
                return db.get_app_setting(def->key, true, ek);
            return std::nullopt;
        }

        return db.get_app_setting(def->key, false);
    }

    std::string get_or(Database &db, const std::string &key, const std::string &fallback)
    {
        auto v = get(db, key);
        return v ? *v : fallback;
    }

    int get_int_or(Database &db, const std::string &key, int fallback, int min_value, int max_value)
    {
        auto v = get(db, key);
        if (!v)
            return fallback;
        try
        {
            int parsed = std::stoi(*v);
            if (parsed < min_value)
                return min_value;
            if (parsed > max_value)
                return max_value;
            return parsed;
        }
        catch (...)
        {
            return fallback;
        }
    }

    nlohmann::json list_for_admin(Database &db)
    {
        nlohmann::json out = nlohmann::json::array();

        for (const auto &def : definitions())
        {
            nlohmann::json item;
            item["key"] = def.key;
            item["env_name"] = def.env_name;
            item["is_secret"] = def.is_secret;

            auto env_value = getenv_trimmed(def.env_name);
            if (env_value)
            {
                item["source"] = "env";
                item["configured"] = true;
                item["locked_by_env"] = true;
                if (!def.is_secret && def.expose_value)
                    item["value"] = *env_value;
                out.push_back(item);
                continue;
            }

            item["locked_by_env"] = false;

            if (def.is_secret)
            {
                bool configured = db.has_app_setting(def.key, true);
                item["source"] = configured ? "db" : "default";
                item["configured"] = configured;
            }
            else
            {
                auto db_value = db.get_app_setting(def.key, false);
                if (db_value.has_value())
                {
                    item["source"] = "db";
                    item["configured"] = true;
                    if (def.expose_value)
                        item["value"] = *db_value;
                }
                else
                {
                    std::string fallback = default_for_def(def);
                    if (!fallback.empty())
                    {
                        item["source"] = "default";
                        item["configured"] = true;
                        if (def.expose_value)
                            item["value"] = fallback;
                    }
                    else
                    {
                        item["source"] = "default";
                        item["configured"] = false;
                    }
                }
            }

            out.push_back(item);
        }

        return out;
    }

    bool set_from_admin(Database &db,
                        const std::string &key,
                        const std::optional<std::string> &value,
                        std::string &error)
    {
        const auto *def = find_def(key);
        if (!def)
        {
            error = "unknown-setting";
            return false;
        }

        if (getenv_trimmed(def->env_name).has_value())
        {
            error = "locked-by-env";
            return false;
        }

        if (key == kVapidPublicKey || key == kVapidPrivateKey)
        {
            error = "readonly-generated";
            return false;
        }

        if (key == kVapidSubject)
        {
            if (!value || trim_ascii(*value).empty())
            {
                error = "required";
                return false;
            }
        }

        if (!value)
        {
            if (def->is_secret)
                return db.clear_app_setting(def->key, true);

            const std::string fallback = default_for_def(*def);
            if (fallback.empty())
                return db.clear_app_setting(def->key, false);
            return db.set_app_setting(def->key, false, fallback);
        }

        std::string trimmed = trim_ascii(*value);
        if (trimmed.empty())
        {
            if (def->is_secret)
                return db.clear_app_setting(def->key, true);

            const std::string fallback = default_for_def(*def);
            if (fallback.empty())
                return db.clear_app_setting(def->key, false);
            return db.set_app_setting(def->key, false, fallback);
        }

        if (key == kVapidSubject)
        {
            if (trimmed.rfind("mailto:", 0) == 0)
            {
                const std::string email = trimmed.substr(7);
                if (!looks_like_email(email))
                {
                    error = "invalid-email";
                    return false;
                }
            }
            else
            {
                if (!looks_like_email(trimmed))
                {
                    error = "invalid-email";
                    return false;
                }
                trimmed = "mailto:" + trimmed;
            }
        }

        if (!normalize_and_validate_value(*def, trimmed, error))
            return false;

        if (def->is_secret)
        {
            const std::string ek = encryption_key();
            if (ek.empty())
            {
                error = "missing-encryption-key";
                return false;
            }
            return db.set_app_setting(def->key, true, trimmed, ek);
        }

        return db.set_app_setting(def->key, false, trimmed);
    }

    int import_env_overrides_to_db(Database &db, std::vector<std::string> &warnings)
    {
        int imported = 0;
        const std::string ek = encryption_key();

        for (const auto &def : definitions())
        {
            auto env_value = getenv_trimmed(def.env_name);
            if (!env_value)
                continue;

            if (def.is_secret)
            {
                if (ek.empty())
                {
                    warnings.push_back(std::string("Skipped secret ENV import for ") + def.key + ": DPW_CONFIG_ENCRYPTION_KEY missing");
                    continue;
                }
                if (!db.set_app_setting(def.key, true, *env_value, ek))
                {
                    warnings.push_back(std::string("Failed ENV import for ") + def.key);
                    continue;
                }
            }
            else
            {
                if (!db.set_app_setting(def.key, false, *env_value))
                {
                    warnings.push_back(std::string("Failed ENV import for ") + def.key);
                    continue;
                }
            }

            imported += 1;
        }

        return imported;
    }

    bool ensure_generated_vapid(Database &db, std::string &error, bool &generated)
    {
        generated = false;

        const auto env_public = getenv_trimmed("DPW_VAPID_PUBLIC_KEY");
        const auto env_private = getenv_trimmed("DPW_VAPID_PRIVATE_KEY");
        if (env_public.has_value() != env_private.has_value())
        {
            error = "partial-vapid-env";
            return false;
        }
        if (env_public && env_private)
            return true;

        const bool has_public = db.has_app_setting(kVapidPublicKey, true);
        const bool has_private = db.has_app_setting(kVapidPrivateKey, true);
        if (has_public && has_private)
            return true;

        const std::string ek = encryption_key();
        if (ek.empty())
        {
            error = "missing-encryption-key";
            return false;
        }

        auto generated_pair = generate_vapid_keypair_encoded();
        if (!generated_pair)
        {
            error = "vapid-generation-failed";
            return false;
        }

        if (!db.set_app_setting(kVapidPublicKey, true, generated_pair->first, ek) ||
            !db.set_app_setting(kVapidPrivateKey, true, generated_pair->second, ek))
        {
            error = "vapid-store-failed";
            return false;
        }

        generated = true;
        return true;
    }
}
