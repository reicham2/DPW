#pragma once
#include <optional>
#include <string>
#include "json.hpp"

struct AuditAction
{
    std::string label;
    std::string resource_type;
};

std::optional<AuditAction> audit_action(const std::string &method, const std::string &route);
std::string audit_outcome(int status, bool uncertain = false);
nlohmann::json audit_change_details(const std::string &resource_type,
                                   const nlohmann::json &before, const nlohmann::json &after);
int audit_retention_days(const std::string &value);
