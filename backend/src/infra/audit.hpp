#pragma once
#include "core/http.hpp"
#include "infra/auth.hpp"
#include "infra/audit_actions.hpp"
#include "core/request_helpers.hpp"
#include <optional>

class Database;

bool audit_begin(HttpRes *res, HttpReq *req, Database &db,
                 const std::string &route, const AuditAction &action);
void audit_attribute(HttpRes *res, const TokenClaims &claims, bool refresh = false);
void audit_mark_uncertain(HttpRes *res);
void audit_activity_resource(HttpRes *res, const std::string &activity_id);
void audit_record_change(HttpRes *res, const nlohmann::json &before, const nlohmann::json &after);
bool audit_finish(HttpRes *res, int status, const std::string &body);
void audit_disconnect(HttpRes *res);

// Preserve the existing router API while applying one journal mechanism to all
// registered mutations, including bulk/indirect operations inside their handlers.
class AuditApp
{
    Database &db_;
    uWS::App app_;

    template <typename Handler>
    auto mutation(const std::string &method, const std::string &route, Handler handler)
    {
        auto action = audit_action(method, route);
        return [this, route, action, handler = std::move(handler)](HttpRes *res, HttpReq *req) mutable
        {
            if (action && !audit_begin(res, req, db_, route, *action))
                return;
            if (action && route != "/setup/auth-config")
            {
                TokenClaims claims;
                if (!require_auth(res, req, claims))
                    return;
            }
            handler(res, req);
        };
    }

public:
    explicit AuditApp(Database &db) : db_(db)
    {
        app_.filter([](HttpRes *res, int event)
        {
            if (event == -1)
                audit_disconnect(res);
        });
    }

    template <typename H> AuditApp &get(const std::string &route, H handler)
    { app_.get(route, std::move(handler)); return *this; }
    template <typename H> AuditApp &options(const std::string &route, H handler)
    { app_.options(route, std::move(handler)); return *this; }
    template <typename H> AuditApp &post(const std::string &route, H handler)
    { app_.post(route, mutation("POST", route, std::move(handler))); return *this; }
    template <typename H> AuditApp &put(const std::string &route, H handler)
    { app_.put(route, mutation("PUT", route, std::move(handler))); return *this; }
    template <typename H> AuditApp &patch(const std::string &route, H handler)
    { app_.patch(route, mutation("PATCH", route, std::move(handler))); return *this; }
    template <typename H> AuditApp &del(const std::string &route, H handler)
    { app_.del(route, mutation("DELETE", route, std::move(handler))); return *this; }
    template <typename T> AuditApp &ws(const std::string &route, uWS::App::WebSocketBehavior<T> behavior)
    { app_.ws<T>(route, std::move(behavior)); return *this; }
    template <typename H> AuditApp &listen(int port, H handler)
    { app_.listen(port, std::move(handler)); return *this; }
    void run() { app_.run(); }
};
