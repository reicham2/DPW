#include "handlers/handler_deps.hpp"

#include <cstdio>

// ── Camp Planning handlers ────────────────────────────────────────────────────
//
// Permission model: reuses the existing activity role scopes.
//   read  → activity_read_scope  != "none" (admins always)
//   write → activity_create_scope/activity_edit_scope != "none" (admins always)
// Camp-level row scoping by department is intentionally coarse for the MVP:
// any user who may read activities may read camps; any user who may create/edit
// activities may mutate camps. Fine-grained per-camp ACLs can layer on later.

namespace
{
    std::optional<UserRecord> resolve_user_local(Database &db, const TokenClaims &claims)
    {
        if (claims.oid.rfind("debug:", 0) == 0)
            return db.get_user_by_id(claims.oid.substr(6));
        return db.get_user_by_oid(claims.oid);
    }

    void send_err(HttpRes *res, const char *context, const std::exception &e)
    {
        fprintf(stderr, "[error] %s: %s\n", context, e.what());
        send_json(res, 500, R"({"error":"Interner Serverfehler"})");
    }

    // Authenticates and authorises. Returns the user on success, otherwise
    // writes an error response and returns nullopt.
    std::optional<UserRecord> require_camp_access(HttpRes *res, HttpReq *req, Database &db, bool write)
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
        if (is_admin(*user))
            return user;
        auto perm = db.get_role_permission(user->role);
        if (!perm)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        bool ok = write
                      ? (perm->activity_create_scope != "none" || perm->activity_edit_scope != "none")
                      : (perm->activity_read_scope != "none");
        if (!ok)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return std::nullopt;
        }
        return user;
    }

    // Reads the full request body, then invokes fn(parsed_json). Handles parse
    // errors and aborts uniformly.
    template <typename Fn>
    void with_json_body(HttpRes *res, Fn fn)
    {
        auto buffer = std::make_shared<std::string>();
        res->onAborted([]() {});
        res->onData([res, buffer, fn = std::move(fn)](std::string_view chunk, bool last) mutable
                    {
            buffer->append(chunk.data(), chunk.size());
            if (!last) return;
            try
            {
                auto j = nlohmann::json::parse(*buffer, nullptr, true);
                fn(j);
            }
            catch (const nlohmann::json::exception &)
            {
                send_json(res, 400, R"({"error":"Ungültiger Request-Body"})");
            }
            catch (const std::exception &e)
            {
                send_err(res, "camp handler", e);
            } });
    }

    std::optional<std::string> opt_str(const nlohmann::json &j, const char *key)
    {
        if (j.contains(key) && !j[key].is_null())
            return j[key].get<std::string>();
        return std::nullopt;
    }

    CampInput parse_camp_input(const nlohmann::json &j)
    {
        CampInput in;
        in.title = j.value("title", "");
        in.short_title = j.value("short_title", "");
        in.motto = j.value("motto", "");
        in.kind = j.value("kind", "");
        in.organizer = j.value("organizer", "");
        in.address_name = j.value("address_name", "");
        in.address_street = j.value("address_street", "");
        in.address_zipcode = j.value("address_zipcode", "");
        in.address_city = j.value("address_city", "");
        in.coach_name = j.value("coach_name", "");
        in.course_number = j.value("course_number", "");
        in.color = j.value("color", "#0080ff");
        in.department = opt_str(j, "department");
        return in;
    }

    CampCollaborationInput parse_collab_input(const nlohmann::json &j)
    {
        CampCollaborationInput in;
        in.user_id = opt_str(j, "user_id");
        in.display_name = j.value("display_name", "");
        in.role = j.value("role", "member");
        in.camp_role = j.value("camp_role", "");
        in.abbreviation = j.value("abbreviation", "");
        in.color = j.value("color", "#6b7280");
        in.status = j.value("status", "established");
        return in;
    }

    CampCategoryInput parse_category_input(const nlohmann::json &j)
    {
        CampCategoryInput in;
        in.short_name = j.value("short_name", "");
        in.name = j.value("name", "");
        in.color = j.value("color", "#0080ff");
        in.numbering_style = j.value("numbering_style", "1");
        in.position = j.value("position", 0);
        return in;
    }

    CampPeriodInput parse_period_input(const nlohmann::json &j)
    {
        CampPeriodInput in;
        in.description = j.value("description", "");
        in.start_date = j.value("start_date", "");
        in.end_date = j.value("end_date", "");
        in.position = j.value("position", 0);
        return in;
    }

    ScheduleEntryInput parse_schedule_input(const nlohmann::json &j)
    {
        ScheduleEntryInput s;
        s.period_id = j.value("period_id", "");
        s.period_offset = j.value("period_offset", 0);
        s.length = j.value("length", 60);
        s.left_fraction = j.value("left_fraction", 0.0);
        s.width_fraction = j.value("width_fraction", 1.0);
        return s;
    }

    ContentNodeInput parse_content_node_input(const nlohmann::json &j)
    {
        ContentNodeInput n;
        n.id = opt_str(j, "id");
        n.parent_id = opt_str(j, "parent_id");
        n.slot = j.value("slot", "");
        n.position = j.value("position", 0);
        n.content_type = j.value("content_type", "SingleText");
        n.instance_name = j.value("instance_name", "");
        n.is_root = j.value("is_root", false);
        if (j.contains("data") && j["data"].is_object())
            n.data = j["data"];
        return n;
    }

    CampActivityInput parse_camp_activity_input(const nlohmann::json &j)
    {
        CampActivityInput in;
        in.category_id = opt_str(j, "category_id");
        in.title = j.value("title", "");
        in.location = j.value("location", "");
        if (j.contains("responsible") && j["responsible"].is_array())
            for (const auto &r : j["responsible"])
                if (r.is_string())
                    in.responsible.push_back(r.get<std::string>());
        if (j.contains("schedule_entries") && j["schedule_entries"].is_array())
            for (const auto &s : j["schedule_entries"])
                in.schedule_entries.push_back(parse_schedule_input(s));
        if (j.contains("responsible_collaboration_ids") && j["responsible_collaboration_ids"].is_array())
            for (const auto &c : j["responsible_collaboration_ids"])
                if (c.is_string())
                    in.responsible_collaboration_ids.push_back(c.get<std::string>());
        if (j.contains("content_nodes") && j["content_nodes"].is_array())
            for (const auto &n : j["content_nodes"])
                in.content_nodes.push_back(parse_content_node_input(n));
        if (j.contains("programs") && j["programs"].is_array())
            for (const auto &p : j["programs"])
            {
                ProgramInput pi;
                pi.duration_minutes = p.value("duration_minutes", 0);
                pi.title = p.value("title", "");
                pi.description = p.value("description", "");
                if (p.contains("responsible") && p["responsible"].is_array())
                    for (const auto &r : p["responsible"])
                        if (r.is_string())
                            pi.responsible.push_back(r.get<std::string>());
                in.programs.push_back(std::move(pi));
            }
        return in;
    }

    CampMaterialItemInput parse_material_item_input(const nlohmann::json &j)
    {
        CampMaterialItemInput in;
        in.content_node_id = opt_str(j, "content_node_id");
        in.period_id = opt_str(j, "period_id");
        in.article_name = j.value("article_name", "");
        if (j.contains("quantity") && j["quantity"].is_number())
            in.quantity = j["quantity"].get<double>();
        in.unit = j.value("unit", "");
        return in;
    }

    void broadcast_camp_event(WebSocketManager &wm, const std::string &camp_id, const char *event)
    {
        nlohmann::json msg = {{"event", "camp_updated"}, {"camp_id", camp_id}, {"kind", event}};
        wm.broadcast(msg.dump());
    }
}

// ── Camps ─────────────────────────────────────────────────────────────────────

void handle_get_camps(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    try
    {
        auto camps = db.list_camps();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &c : camps)
            arr.push_back(to_json(c));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camps", e);
    }
}

void handle_get_camp(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto camp = db.get_camp_by_id(id);
        if (!camp)
        {
            send_json(res, 404, R"({"error":"Lager nicht gefunden"})");
            return;
        }
        // Bundle the full camp graph for the detail page.
        nlohmann::json j = to_json(*camp);
        nlohmann::json periods = nlohmann::json::array();
        for (const auto &p : db.list_periods(id))
            periods.push_back(to_json(p));
        j["periods"] = periods;
        nlohmann::json cats = nlohmann::json::array();
        for (const auto &c : db.list_categories(id))
            cats.push_back(to_json(c));
        j["categories"] = cats;
        nlohmann::json collabs = nlohmann::json::array();
        for (const auto &c : db.list_collaborations(id))
            collabs.push_back(to_json(c));
        j["collaborations"] = collabs;
        nlohmann::json acts = nlohmann::json::array();
        for (const auto &a : db.list_camp_activities(id))
            acts.push_back(to_json(a));
        j["activities"] = acts;
        nlohmann::json lists = nlohmann::json::array();
        for (const auto &l : db.list_material_lists(id))
            lists.push_back(to_json(l));
        j["material_lists"] = lists;
        nlohmann::json drs = nlohmann::json::array();
        for (const auto &d : db.list_day_responsibles(id))
            drs.push_back(to_json(d));
        j["day_responsibles"] = drs;
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp", e);
    }
}

void handle_post_camp(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string uid = user->id;
    with_json_body(res, [res, &db, &wm, uid](const nlohmann::json &j)
                   {
        CampInput in = parse_camp_input(j);
        if (in.title.empty())
        {
            send_json(res, 400, R"({"error":"Titel fehlt"})");
            return;
        }
        auto camp = db.create_camp(in, uid);
        if (!camp)
        {
            send_json(res, 500, R"({"error":"Lager konnte nicht erstellt werden"})");
            return;
        }
        broadcast_camp_event(wm, camp->id, "created");
        send_json(res, 201, to_json(*camp).dump()); });
}

void handle_patch_camp(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, id](const nlohmann::json &j)
                   {
        CampInput in = parse_camp_input(j);
        auto camp = db.update_camp(id, in);
        if (!camp)
        {
            send_json(res, 404, R"({"error":"Lager nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, id, "updated");
        send_json(res, 200, to_json(*camp).dump()); });
}

void handle_delete_camp(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string id{req->getParameter(0)};
    try
    {
        if (!db.soft_delete_camp(id, user->id))
        {
            send_json(res, 404, R"({"error":"Lager nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, id, "deleted");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp", e);
    }
}

// ── Collaborations ──────────────────────────────────────────────────────────

void handle_get_camp_collaborations(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &c : db.list_collaborations(camp_id))
            arr.push_back(to_json(c));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp_collaborations", e);
    }
}

void handle_post_camp_collaboration(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, camp_id](const nlohmann::json &j)
                   {
        auto in = parse_collab_input(j);
        auto c = db.create_collaboration(camp_id, in);
        if (!c)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "collaboration");
        send_json(res, 201, to_json(*c).dump()); });
}

void handle_patch_camp_collaboration(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, id](const nlohmann::json &j)
                   {
        auto in = parse_collab_input(j);
        auto c = db.update_collaboration(id, in);
        if (!c)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "collaboration");
        send_json(res, 200, to_json(*c).dump()); });
}

void handle_delete_camp_collaboration(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_collaboration(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "collaboration");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_collaboration", e);
    }
}

// ── Categories ──────────────────────────────────────────────────────────────

void handle_get_camp_categories(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &c : db.list_categories(camp_id))
            arr.push_back(to_json(c));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp_categories", e);
    }
}

void handle_post_camp_category(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, camp_id](const nlohmann::json &j)
                   {
        auto in = parse_category_input(j);
        auto c = db.create_category(camp_id, in);
        if (!c)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "category");
        send_json(res, 201, to_json(*c).dump()); });
}

void handle_patch_camp_category(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, id](const nlohmann::json &j)
                   {
        auto in = parse_category_input(j);
        auto c = db.update_category(id, in);
        if (!c)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "category");
        send_json(res, 200, to_json(*c).dump()); });
}

void handle_delete_camp_category(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_category(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "category");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_category", e);
    }
}

// ── Periods ─────────────────────────────────────────────────────────────────

void handle_get_camp_periods(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &p : db.list_periods(camp_id))
            arr.push_back(to_json(p));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp_periods", e);
    }
}

void handle_post_camp_period(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, camp_id](const nlohmann::json &j)
                   {
        auto in = parse_period_input(j);
        if (in.start_date.empty() || in.end_date.empty())
        {
            send_json(res, 400, R"({"error":"Start- und Enddatum erforderlich"})");
            return;
        }
        auto p = db.create_period(camp_id, in);
        if (!p)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "period");
        send_json(res, 201, to_json(*p).dump()); });
}

void handle_patch_camp_period(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, id](const nlohmann::json &j)
                   {
        auto in = parse_period_input(j);
        auto p = db.update_period(id, in);
        if (!p)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "period");
        send_json(res, 200, to_json(*p).dump()); });
}

void handle_delete_camp_period(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_period(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "period");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_period", e);
    }
}

// ── Day responsibles (Tagesverantwortliche) ──────────────────────────────────

void handle_get_camp_day_responsibles(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &d : db.list_day_responsibles(camp_id))
            arr.push_back(to_json(d));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp_day_responsibles", e);
    }
}

void handle_post_camp_day_responsible(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, camp_id](const nlohmann::json &j)
                   {
        std::string period_id = j.value("period_id", "");
        int day_offset = j.value("day_offset", 0);
        std::string collab = j.value("collaboration_id", "");
        if (period_id.empty() || collab.empty())
        {
            send_json(res, 400, R"({"error":"period_id und collaboration_id erforderlich"})");
            return;
        }
        auto d = db.add_day_responsible(period_id, day_offset, collab);
        if (!d)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "day_responsible");
        send_json(res, 201, to_json(*d).dump()); });
}

void handle_delete_camp_day_responsible(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_day_responsible(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "day_responsible");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_day_responsible", e);
    }
}

// ── Camp Activities ───────────────────────────────────────────────────────────

void handle_get_camp_activities(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &a : db.list_camp_activities(camp_id))
            arr.push_back(to_json(a));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp_activities", e);
    }
}

void handle_post_camp_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, camp_id](const nlohmann::json &j)
                   {
        auto in = parse_camp_activity_input(j);
        auto a = db.create_camp_activity(camp_id, in);
        if (!a)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "activity");
        send_json(res, 201, to_json(*a).dump()); });
}

void handle_patch_camp_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, id](const nlohmann::json &j)
                   {
        auto in = parse_camp_activity_input(j);
        auto a = db.update_camp_activity(id, in);
        if (!a)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "activity");
        send_json(res, 200, to_json(*a).dump()); });
}

void handle_delete_camp_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_camp_activity(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "activity");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_activity", e);
    }
}

void handle_patch_schedule_entry(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, id](const nlohmann::json &j)
                   {
        auto in = parse_schedule_input(j);
        auto s = db.update_schedule_entry(id, in);
        if (!s)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "schedule");
        send_json(res, 200, to_json(*s).dump()); });
}

// ── Material lists & items ────────────────────────────────────────────────────

void handle_get_camp_material_lists(HttpRes *res, HttpReq *req, Database &db)
{
    auto user = require_camp_access(res, req, db, false);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    try
    {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : db.list_material_lists(camp_id))
            arr.push_back(to_json(l));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_get_camp_material_lists", e);
    }
}

void handle_post_camp_material_list(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    with_json_body(res, [res, &db, &wm, camp_id](const nlohmann::json &j)
                   {
        std::optional<std::string> collab = opt_str(j, "collaboration_id");
        std::string name = j.value("name", "");
        auto l = db.create_material_list(camp_id, collab, name);
        if (!l)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "material");
        send_json(res, 201, to_json(*l).dump()); });
}

void handle_delete_camp_material_list(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_material_list(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "material");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_material_list", e);
    }
}

void handle_post_camp_material_item(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string list_id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, list_id](const nlohmann::json &j)
                   {
        auto in = parse_material_item_input(j);
        auto it = db.create_material_item(list_id, in);
        if (!it)
        {
            send_json(res, 500, R"({"error":"Fehler beim Erstellen"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "material");
        send_json(res, 201, to_json(*it).dump()); });
}

void handle_patch_camp_material_item(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    with_json_body(res, [res, &db, &wm, camp_id, id](const nlohmann::json &j)
                   {
        auto in = parse_material_item_input(j);
        auto it = db.update_material_item(id, in);
        if (!it)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "material");
        send_json(res, 200, to_json(*it).dump()); });
}

void handle_delete_camp_material_item(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto user = require_camp_access(res, req, db, true);
    if (!user)
        return;
    std::string camp_id{req->getParameter(0)};
    std::string id{req->getParameter(1)};
    try
    {
        if (!db.delete_material_item(id))
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        broadcast_camp_event(wm, camp_id, "material");
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_err(res, "handle_delete_camp_material_item", e);
    }
}
