#include "db/database.hpp"
#include "infra/app_config.hpp"

// Stubs for symbols referenced by activity_helpers.cpp but not exercised in unit tests.

std::optional<std::string> Database::get_app_setting(const std::string &, bool, const std::string &)
{
    return std::nullopt;
}

namespace app_config
{
    const std::vector<SettingDef> &definitions()
    {
        static const std::vector<SettingDef> empty;
        return empty;
    }

    std::optional<std::string> get(Database &, const std::string &)
    {
        return std::nullopt;
    }

    std::string get_or(Database &, const std::string &, const std::string &fallback)
    {
        return fallback;
    }

    int get_int_or(Database &, const std::string &, int fallback, int, int)
    {
        return fallback;
    }
}
