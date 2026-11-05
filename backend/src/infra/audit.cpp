#include "infra/audit.hpp"
#include "db/database.hpp"
#include "core/request_helpers.hpp"
#include "infra/app_config.hpp"
#include "utils/strings.hpp"
#include <cstdio>
#include <unordered_map>

namespace
{
    struct PendingAudit
    {
        Database *db;
        std::string id;
        bool uncertain = false;
    };
    // The HTTP event loop owns these contexts; asynchronous body callbacks use
    // the response key, never a thread-local actor that another request can replace.
    std::unordered_map<HttpRes *, PendingAudit> pending;

    bool uuid(const std::string &value)
    {
        if (value.size() != 36) return false;
        for (size_t i = 0; i < value.size(); ++i)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23)
            {
                if (value[i] != '-') return false;
            }
            else if (!((value[i] >= '0' && value[i] <= '9') ||
                       (value[i] >= 'a' && value[i] <= 'f') ||
                       (value[i] >= 'A' && value[i] <= 'F')))
                return false;
        }
        return true;
    }
}

bool audit_begin(HttpRes *res, HttpReq *req, Database &db,
                 const std::string &route, const AuditAction &action)
{
    try
    {
        std::string resource_id;
        // UUID parameters only: never include a share token, arbitrary name or key.
        for (const char *key : {"id", "rid"})
        {
            if (route.find(std::string(":") + key) == std::string::npos) continue;
            std::string value{req->getParameter(key)};
            if (uuid(value))
            {
                if (!resource_id.empty()) resource_id += " / ";
                resource_id += value;
            }
        }
        if (route == "/admin/app-settings/:key")
        {
            auto key = url_decode(std::string{req->getParameter("key")});
            for (const auto &definition : app_config::definitions())
                if (key == definition.key) resource_id = key;
        }
        auto id = db.begin_audit_event(action.label, action.resource_type, resource_id);
        pending.insert_or_assign(res, PendingAudit{&db, id});
        res->onAborted([] {});
        return true;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "[audit] Cannot start journal; operation blocked: %s\n", e.what());
        send_json(res, 503, R"({"error":"Audit Log nicht verfügbar; Aktion nicht ausgeführt"})");
        return false;
    }
}

void audit_attribute(HttpRes *res, const TokenClaims &claims)
{
    auto it = pending.find(res);
    if (it == pending.end()) return;
    auto user = resolve_user(*it->second.db, claims);
    if (claims.oid.rfind("debug:", 0) == 0 && !user)
        return;
    it->second.db->attribute_audit_event(
        it->second.id, user ? user->id : claims.oid,
        user ? user->display_name : claims.display_name);
}

bool audit_finish(HttpRes *res, int status, const std::string &body)
{
    auto it = pending.find(res);
    if (it == pending.end()) return true;
    auto context = it->second;
    pending.erase(it);
    try
    {
        std::string created_id;
        if (status >= 200 && status < 300)
        {
            auto j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_object() && j.contains("id") && j["id"].is_string())
            {
                auto value = j["id"].get<std::string>();
                if (uuid(value)) created_id = value;
            }
        }
        context.db->finish_audit_event(context.id, audit_outcome(status, context.uncertain), status, created_id);
        return true;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "[audit] Cannot finalize journal; operation may have completed: %s\n", e.what());
        return false;
    }
}

void audit_mark_uncertain(HttpRes *res)
{
    auto it = pending.find(res);
    if (it != pending.end()) it->second.uncertain = true;
}

void audit_activity_resource(HttpRes *res, const std::string &activity_id)
{
    auto it = pending.find(res);
    if (it != pending.end() && uuid(activity_id))
        it->second.db->set_audit_resource(it->second.id, "activity", activity_id);
}

void audit_disconnect(HttpRes *res)
{
    auto it = pending.find(res);
    if (it == pending.end()) return;
    auto context = it->second;
    pending.erase(it);
    try
    {
        context.db->finish_audit_event(context.id, "unknown", 0, "");
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "[audit] Cannot journal disconnect; outcome remains pending: %s\n", e.what());
    }
}
