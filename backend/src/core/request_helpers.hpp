#pragma once
#include <string>
#include <optional>
#include "core/http.hpp"
#include "infra/auth.hpp"
#include "core/models/user.hpp"
#include "json.hpp"

class Database;

bool require_auth(HttpRes *res, HttpReq *req, TokenClaims &out_claims);
std::string auth_token_from_header(HttpRes *res, const std::string &auth_header);

TokenClaims validate_token(const std::string &token);
std::optional<UserRecord> resolve_user(Database &db, const TokenClaims &claims);
void send_internal_error(HttpRes *res, const char *context, const std::exception &e);

struct MaintenanceState
{
    bool enabled{false};
    bool scheduled_now{false};
    bool active{false};
    std::string message;
    std::string scheduled_start;
    std::string scheduled_end;
    nlohmann::json windows = nlohmann::json::array();
    nlohmann::json upcoming_windows = nlohmann::json::array();
};

MaintenanceState get_maintenance_state(Database &db);
void sync_azure_runtime_env_from_db(Database &db);
