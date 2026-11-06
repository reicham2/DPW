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
        std::string resource_type;
        nlohmann::json details;
        std::string attributed_oid;
        bool needs_resource_id = false;
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
        // UUID parameters only; named administrative resources are allowlisted below.
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
        if (route == "/admin/roles/:name" || route == "/admin/roles/:name/move" ||
            route == "/admin/departments/:name")
        {
            resource_id = url_decode(std::string{req->getParameter("name")});
            if (resource_id.size() > 300)
            {
                size_t end = 300;
                while (end && (static_cast<unsigned char>(resource_id[end]) & 0xc0) == 0x80) --end;
                resource_id.resize(end);
            }
        }
        if (route == "/admin/cron-jobs/:id")
            for (const char *id : {"expired-notifications", "activity-trash", "audit-retention"})
                if (req->getParameter("id") == id) resource_id = id;
        auto id = db.begin_audit_event(action.label, action.resource_type, resource_id);
        pending.insert_or_assign(res, PendingAudit{&db, id, false, action.resource_type, nullptr, "", resource_id.empty()});
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

void audit_attribute(HttpRes *res, const TokenClaims &claims, bool refresh)
{
    auto it = pending.find(res);
    if (it == pending.end()) return;
    if (!refresh && !claims.oid.empty() && it->second.attributed_oid == claims.oid) return;
    auto user = resolve_user(*it->second.db, claims);
    if (claims.oid.rfind("debug:", 0) == 0 && !user)
        return;
    it->second.db->attribute_audit_event(
        it->second.id, user ? user->id : claims.oid,
        user ? user->display_name : claims.display_name);
    it->second.attributed_oid = claims.oid;
}

bool audit_finish(HttpRes *res, int status, const std::string &body)
{
    auto it = pending.find(res);
    if (it == pending.end()) return true;
    auto context = std::move(it->second);
    pending.erase(it);
    try
    {
        std::string created_id;
        if (status >= 200 && status < 300 && (context.needs_resource_id || context.details.is_null()))
        {
            auto j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_object() && j.contains("id") && j["id"].is_string())
            {
                auto value = j["id"].get<std::string>();
                if (uuid(value)) created_id = value;
            }
            if (context.details.is_null() && j.is_object())
            {
                context.details = audit_change_details(context.resource_type, nullptr, j);
                context.details["kind"] = "saved";
            }
        }
        context.db->finish_audit_event(context.id, audit_outcome(status, context.uncertain), status, created_id,
                                      context.details.is_null() ? nlohmann::json::object() : context.details);
        return true;
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "[audit] Cannot finalize journal; operation may have completed: %s\n", e.what());
        return false;
    }
}

void audit_record_change(HttpRes *res, const nlohmann::json &before, const nlohmann::json &after)
{
    auto it = pending.find(res);
    if (it != pending.end())
        it->second.details = audit_change_details(it->second.resource_type, before, after);
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
    auto context = std::move(it->second);
    pending.erase(it);
    try
    {
        context.db->finish_audit_event(context.id, "unknown", 0, "",
                                      context.details.is_null() ? nlohmann::json::object() : context.details);
    }
    catch (const std::exception &e)
    {
        std::fprintf(stderr, "[audit] Cannot journal disconnect; outcome remains pending: %s\n", e.what());
    }
}
