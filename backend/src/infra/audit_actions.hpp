#pragma once
#include <optional>
#include <string>

struct AuditAction
{
    std::string label;
    std::string resource_type;
};

std::optional<AuditAction> audit_action(const std::string &method, const std::string &route);
std::string audit_outcome(int status, bool uncertain = false);
