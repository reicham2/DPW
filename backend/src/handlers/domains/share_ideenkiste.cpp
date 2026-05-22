#include "handlers.hpp"

#include "app_config.hpp"
#include "models.hpp"
#include "utils.hpp"

#include <cstdio>

namespace
{
    std::optional<UserRecord> resolve_user_local(Database &db, const TokenClaims &claims)
    {
        if (claims.oid.rfind("debug:", 0) == 0)
            return db.get_user_by_id(claims.oid.substr(6));
        return db.get_user_by_oid(claims.oid);
    }

    std::optional<std::string> configured_public_base_url(Database &db)
    {
        std::string out = app_config::get_or(db, app_config::kPublicBaseUrl, "");
        out = trim_ascii(out);
        while (!out.empty() && out.back() == '/')
            out.pop_back();
        if (out.empty())
            return std::nullopt;
        return out;
    }

    void send_internal_error_local(HttpRes *res, const char *context, const std::exception &e)
    {
        fprintf(stderr, "[error] %s: %s\n", context, e.what());
        if (std::string(e.what()) == "Öffentliche Basis-URL ist nicht konfiguriert")
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }
        send_json(res, 500, R"({"error":"Interner Serverfehler"})");
    }

    enum class IdeenkisteOp
    {
        View,
        Add,
        Delete
    };

    std::optional<std::string> ideenkiste_check(HttpRes *res, HttpReq *req, Database &db,
                                                UserRecord &out_user, IdeenkisteOp op)
    {
        TokenClaims claims;
        if (!require_auth(res, req, claims))
            return std::nullopt;
        auto user = resolve_user_local(db, claims);
        if (!user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        auto perm = is_admin(*user) ? std::optional<RolePermission>{} : db.get_role_permission(user->role);
        std::string scope;
        if (is_admin(*user))
        {
            scope = "all";
        }
        else if (perm)
        {
            switch (op)
            {
            case IdeenkisteOp::View:
                scope = perm->ideenkiste_scope;
                break;
            case IdeenkisteOp::Add:
                scope = perm->ideenkiste_add_scope;
                break;
            case IdeenkisteOp::Delete:
                scope = perm->ideenkiste_delete_scope;
                break;
            }
        }
        else
        {
            scope = "none";
        }
        if (scope == "none")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        out_user = *user;
        return scope;
    }

    bool ideenkiste_item_in_scope(Database &db,
                                  const std::string &item_id,
                                  const std::string &scope,
                                  const std::optional<std::string> &user_dept)
    {
        if (scope == "all")
            return true;

        if (scope == "own_dept")
        {
            if (!user_dept)
                return false;

            auto all_items = db.list_ideenkiste("");
            for (const auto &item : all_items)
            {
                if (item.id == item_id)
                    return item.department.has_value() && *item.department == *user_dept;
            }
        }

        return false;
    }
}

void handle_post_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto base_url = configured_public_base_url(db);
        if (!base_url)
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }

        auto current_user = resolve_user_local(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);
        if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto link = db.create_share_link(activity_id, current_user->id);
        if (!link)
        {
            send_json(res, 500, R"({"error":"Share-Link konnte nicht erstellt werden"})");
            return;
        }
        nlohmann::json j = {
            {"id", link->id},
            {"activity_id", link->activity_id},
            {"share_token", link->share_token},
            {"share_url", *base_url + "/shared/" + link->share_token},
            {"created_at", link->created_at}};
        send_json(res, 201, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_get_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto base_url = configured_public_base_url(db);
        if (!base_url)
        {
            send_json(res, 409, R"({"error":"Öffentliche Basis-URL ist nicht konfiguriert"})");
            return;
        }

        auto link = db.get_share_link(activity_id);
        if (!link)
        {
            send_json(res, 200, R"({"share_token":null})");
            return;
        }
        nlohmann::json j = {
            {"id", link->id},
            {"activity_id", link->activity_id},
            {"share_token", link->share_token},
            {"share_url", *base_url + "/shared/" + link->share_token},
            {"created_at", link->created_at}};
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_delete_share_link(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string activity_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user_local(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        bool ok = db.delete_share_link(activity_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Kein Share-Link vorhanden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_get_shared_activity(HttpRes *res, HttpReq *req, Database &db)
{
    std::string token{req->getParameter(0)};
    try
    {
        auto activity = db.get_activity_by_share_token(token);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_get_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::View);
    if (!scope)
        return;
    try
    {
        std::string dept_filter = (*scope == "own_dept" && user.department) ? *user.department : "";
        auto items = db.list_ideenkiste(dept_filter);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &item : items)
            arr.push_back(ideenkiste_to_json(item));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}

void handle_post_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::Add);
    if (!scope)
        return;
    res->onAborted([]() {});
    res->onData([res, &db, user_dept = user.department, scope = *scope](std::string_view body, bool last)
                {
        if (!last) return;
        try
        {
            auto j = nlohmann::json::parse(body);
            IdeenkisteInput input;
            input.title = j.value("title", "");
            input.duration_minutes = j.value("duration_minutes", 0);
            input.description = j.value("description", "");
            if (j.contains("department") && !j["department"].is_null())
                input.department = j["department"].get<std::string>();
            if (input.title.empty())
            {
                send_json(res, 400, R"({"error":"Titel fehlt"})");
                return;
            }
            if (scope == "own_dept")
                input.department = user_dept;
            auto item = db.create_ideenkiste_item(input);
            if (!item)
            {
                send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
                return;
            }
            send_json(res, 201, ideenkiste_to_json(*item).dump());
        }
        catch (std::exception &e)
        {
            send_internal_error_local(res, "handler", e);
        } });
}

void handle_put_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::Add);
    if (!scope)
        return;
    std::string id{req->getParameter(0)};
    res->onAborted([]() {});
    res->onData([res, &db, id, user_dept = user.department, scope = *scope](std::string_view body, bool last)
                {
        if (!last) return;
        try
        {
            if (!ideenkiste_item_in_scope(db, id, scope, user_dept))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }

            auto j = nlohmann::json::parse(body);
            IdeenkisteInput input;
            input.title = j.value("title", "");
            input.duration_minutes = j.value("duration_minutes", 0);
            input.description = j.value("description", "");
            if (j.contains("department") && !j["department"].is_null())
                input.department = j["department"].get<std::string>();
            if (input.title.empty())
            {
                send_json(res, 400, R"({"error":"Titel fehlt"})");
                return;
            }
            if (scope == "own_dept")
                input.department = user_dept;
            auto item = db.update_ideenkiste_item(id, input);
            if (!item)
            {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }
            send_json(res, 200, ideenkiste_to_json(*item).dump());
        }
        catch (std::exception &e)
        {
            send_internal_error_local(res, "handler", e);
        } });
}

void handle_delete_ideenkiste(HttpRes *res, HttpReq *req, Database &db)
{
    UserRecord user;
    auto scope = ideenkiste_check(res, req, db, user, IdeenkisteOp::Delete);
    if (!scope)
        return;
    std::string id{req->getParameter(0)};
    try
    {
        if (!ideenkiste_item_in_scope(db, id, *scope, user.department))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_ideenkiste_item(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error_local(res, "handler", e);
    }
}