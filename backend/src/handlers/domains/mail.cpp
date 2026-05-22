#include "handlers/internal/shared.hpp"

#include <curl/curl.h>

static nlohmann::json template_to_json(const MailTemplate &t)
{
    return {
        {"id", t.id},
        {"department", t.department},
        {"subject", t.subject},
        {"body", t.body},
        {"recipients", t.recipients},
        {"cc", t.cc},
        {"created_at", t.created_at},
        {"updated_at", t.updated_at}};
}

static bool can_send_mail_for_activity(const RolePermission &perm, const UserRecord &user,
                                       const Activity &activity, const std::string &email)
{
    if (is_admin(user))
        return true;
    if (perm.mail_send_scope == "all")
        return true;
    if (perm.mail_send_scope == "same_dept")
    {
        return (activity.department && user.department && *activity.department == *user.department) ||
               is_activity_responsible(activity, user, email);
    }
    if (perm.mail_send_scope == "own")
        return is_activity_responsible(activity, user, email);
    return false;
}

// ---- GET /mail-templates ----------------------------------------------------

void handle_get_mail_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        const std::string ckey = cache_key("mail_templates", cache_user_scope(claims, current_user));
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->mail_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto templates = db.list_mail_templates();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
        {
            // own_dept: only templates for user's own department
            if (!is_admin(*current_user) && perm->mail_templates_scope == "own_dept")
            {
                if (!current_user->department || t.department != *current_user->department)
                    continue;
            }
            arr.push_back(template_to_json(t));
        }
        std::string body = arr.dump();
        redis_cache().setex(ckey, response_cache_ttl_seconds(), body);
        send_json(res, 200, body);
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /mail-templates/:department ----------------------------------------

void handle_get_mail_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string department = url_decode(std::string{req->getParameter(0)});
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->mail_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (!is_admin(*current_user) && perm->mail_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        auto tpl = db.get_mail_template_by_department(department);
        if (!tpl)
        {
            send_json(res, 404, R"({"error":"template not found"})");
            return;
        }
        send_json(res, 200, template_to_json(*tpl).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /activities/:id/mail-context --------------------------------------

void handle_get_mail_composer_context(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};

    try
    {
        auto current_user = resolve_user(db, claims);
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
        if (!perm || !can_send_mail_for_activity(*perm, *current_user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        nlohmann::json out = {
            {"template", nullptr},
            {"form", {{"exists", false}, {"public_slug", nullptr}}},
        };

        if (auto form = db.get_form_for_activity(activity_id))
        {
            out["form"] = {
                {"exists", true},
                {"public_slug", form->public_slug},
            };
        }

        if (activity->department)
        {
            if (auto tpl = db.get_mail_template_by_department(*activity->department))
            {
                out["template"] = template_to_json(*tpl);
            }
        }

        send_json(res, 200, out.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- PUT /mail-templates/:department ----------------------------------------

void handle_put_mail_template(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string department = url_decode(std::string{req->getParameter(0)});

    // Permission check: mail_templates_scope controls access
    auto current_user = resolve_user(db, claims);
    if (current_user)
    {
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->mail_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (!is_admin(*current_user) && perm->mail_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }
        // 'all' → no restriction
    }
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, department, &db, &wm](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string subject = j.value("subject", "");
        std::string body    = j.value("body", "");

        std::vector<std::string> recipients;
        if (j.contains("recipients") && j["recipients"].is_array()) {
            for (auto& e : j["recipients"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    recipients.push_back(e.get<std::string>());
            }
        }
        std::vector<std::string> cc;
        if (j.contains("cc") && j["cc"].is_array()) {
            for (auto& e : j["cc"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    cc.push_back(e.get<std::string>());
            }
        }

        try {
            auto tpl = db.upsert_mail_template(department, subject, body, recipients, cc);
            if (!tpl) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }
            nlohmann::json msg = {{"event", "template_updated"}, {"template", template_to_json(*tpl)}};
            wm.broadcast(msg.dump());
            cache_bump_version("mail_templates");
            send_json(res, 200, template_to_json(*tpl).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- Event templates --------------------------------------------------------

static nlohmann::json event_template_to_json(const EventTemplate &t)
{
    return {
        {"id", t.id},
        {"department", t.department},
        {"title", t.title},
        {"body", t.body},
        {"created_at", t.created_at},
        {"updated_at", t.updated_at}};
}

// Parse "YYYY-MM-DD" date + "HH:MM" time into a Unix timestamp (Europe/Zurich assumed UTC+1/+2).
// Falls back to UTC if parsing fails.
static long parse_datetime_unix(const std::string &date_str, const std::string &time_str)
{
    struct tm t{};
    // Parse date
    if (sscanf(date_str.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) == 3)
    {
        t.tm_year -= 1900;
        t.tm_mon -= 1;
    }
    // Parse time
    if (time_str.size() >= 5)
        sscanf(time_str.c_str(), "%d:%d", &t.tm_hour, &t.tm_min);
    t.tm_isdst = -1;
    // Use mktime (local time) — container should have TZ=Europe/Zurich
    time_t result = mktime(&t);
    return static_cast<long>(result);
}

static nlohmann::json event_publication_to_json(const EventPublication &p)
{
    return {
        {"id", p.id},
        {"activity_id", p.activity_id},
        {"published_by", p.published_by},
        {"title", p.title},
        {"body_html", p.body_html},
        {"wp_event_id", p.wp_event_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(p.wp_event_id)},
        {"published_at", p.published_at}};
}

// ---- GET /event-templates ---------------------------------------------------

void handle_get_event_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 401, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->event_templates_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto templates = db.list_event_templates();

    // Filter by own department if scope is own_dept
    if (!is_admin(*current_user) && perm && perm->event_templates_scope == "own_dept")
    {
        std::vector<EventTemplate> filtered;
        for (auto &t : templates)
        {
            if (current_user->department && t.department == *current_user->department)
                filtered.push_back(std::move(t));
        }
        templates = std::move(filtered);
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto &t : templates)
        arr.push_back(event_template_to_json(t));
    send_json(res, 200, arr.dump());
}

// ---- GET /event-templates/:department ---------------------------------------

void handle_get_event_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 401, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    std::string department = url_decode(std::string{req->getParameter(0)});

    auto perm = db.get_role_permission(current_user->role);
    bool can_templates = perm && perm->event_templates_scope != "none";
    bool can_publish = perm && perm->event_publish_scope != "none";
    if (!is_admin(*current_user) && !can_templates && !can_publish)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    if (!is_admin(*current_user) && !can_publish && perm->event_templates_scope == "own_dept")
    {
        if (!current_user->department || *current_user->department != department)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    auto tpl = db.get_event_template_by_department(department);
    if (!tpl)
    {
        send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})");
        return;
    }
    send_json(res, 200, event_template_to_json(*tpl).dump());
}

// ---- PUT /event-templates/:department ---------------------------------------

void handle_put_event_template(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string department = url_decode(std::string{req->getParameter(0)});

    auto current_user = resolve_user(db, claims);
    if (current_user)
    {
        auto perm = db.get_role_permission(current_user->role);
        if (!is_admin(*current_user) && (!perm || perm->event_templates_scope == "none"))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (!is_admin(*current_user) && perm->event_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }
    }
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, department, &db, &wm](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string title = j.value("title", "");
        std::string body  = j.value("body", "");

        try {
            auto tpl = db.upsert_event_template(department, title, body);
            if (!tpl) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }
            nlohmann::json msg = {{"event", "event_template_updated"}, {"template", event_template_to_json(*tpl)}};
            wm.broadcast(msg.dump());
            send_json(res, 200, event_template_to_json(*tpl).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- GET /activities/:id/event-publication -----------------------------------

void handle_get_event_publication(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 401, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    std::string activity_id{req->getParameter(0)};
    auto pub = db.get_event_publication(activity_id);
    if (!pub)
    {
        send_json(res, 404, R"({"error":"Nicht ver\u00f6ffentlicht"})");
        return;
    }
    send_json(res, 200, event_publication_to_json(*pub).dump());
}

// ---- PUT /activities/:id/event-publication -----------------------------------

void handle_put_event_publication(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 401, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->event_publish_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string activity_id{req->getParameter(0)};

    // Scope check: own / own_dept requires checking the activity
    if (!is_admin(*current_user) && perm && perm->event_publish_scope != "all")
    {
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
            return;
        }
        if (!can_publish_event(*perm, *current_user, *activity, current_user->email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung für diese Aktivität"})");
            return;
        }
    }

    std::string user_id = current_user->id;

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, user_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string title    = j.value("title", "");
        std::string body_html = j.value("body_html", "");

        if (title.empty()) {
            send_json(res, 400, R"({"error":"Titel erforderlich"})");
            return;
        }

        try {
            auto pub = db.upsert_event_publication(activity_id, user_id, title, body_html);
            if (!pub) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }

            // ── WordPress / EventON sync ──────────────────────────────
            if (wp_configured(db)) {
                auto activity = db.get_activity_by_id(activity_id);
                long start_ts = 0, end_ts = 0;
                std::string location, department;
                if (activity) {
                    start_ts = parse_datetime_unix(activity->date, activity->start_time);
                    end_ts   = parse_datetime_unix(activity->date, activity->end_time);
                    location = activity->location;
                    department = activity->department.value_or("");
                }

                if (!pub->wp_event_id.empty()) {
                    auto wp = wp_update_event(pub->wp_event_id, title, body_html, start_ts, end_ts, location, department, db);
                    if (!wp)
                        fprintf(stderr, "[wp_client] update failed for wp_event_id=%s\n", pub->wp_event_id.c_str());
                } else {
                    auto wp = wp_create_event(title, body_html, start_ts, end_ts, location, department, db);
                    if (wp) {
                        db.update_event_publication_wp_id(activity_id, wp->wp_event_id);
                        pub->wp_event_id = wp->wp_event_id;
                    } else {
                        fprintf(stderr, "[wp_client] create failed for activity=%s\n", activity_id.c_str());
                    }
                }
            }

            send_json(res, 200, event_publication_to_json(*pub).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- DELETE /activities/:id/event-publication --------------------------------

void handle_delete_event_publication(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 401, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->event_publish_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string activity_id{req->getParameter(0)};

    // Scope check: own / own_dept requires checking the activity
    if (!is_admin(*current_user) && perm && perm->event_publish_scope != "all")
    {
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Aktivität nicht gefunden"})");
            return;
        }
        if (!can_publish_event(*perm, *current_user, *activity, current_user->email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung für diese Aktivität"})");
            return;
        }
    }

    // Delete WordPress event if it exists
    if (wp_configured(db))
    {
        auto pub = db.get_event_publication(activity_id);
        if (pub && !pub->wp_event_id.empty())
        {
            if (!wp_delete_event(pub->wp_event_id, db))
                fprintf(stderr, "[wp_client] delete failed for wp_event_id=%s\n", pub->wp_event_id.c_str());
        }
    }

    db.delete_event_publication(activity_id);
    send_json(res, 204, "");
}

// ---- POST /send-mail --------------------------------------------------------

void handle_post_send_mail(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);

    // Permission check: mail_send_scope
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm_check = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm_check || perm_check->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, token, &db, &wm, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string subject     = j.value("subject", "");
        std::string body_html   = j.value("body", "");
        std::string from_email  = j.value("from", "");
        std::string graph_token = j.value("access_token", "");
        std::string activity_id = j.value("activity_id", "");

        if (subject.empty() || body_html.empty() || graph_token.empty()) {
            send_json(res, 400, R"({"error":"subject, body and access_token are required"})");
            return;
        }

        std::vector<std::string> to_emails;
        if (j.contains("to") && j["to"].is_array()) {
            for (auto& e : j["to"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    to_emails.push_back(e.get<std::string>());
            }
        }
        if (to_emails.empty()) {
            send_json(res, 400, R"({"error":"at least one recipient required"})");
            return;
        }

        std::vector<std::string> cc_emails;
        if (j.contains("cc") && j["cc"].is_array()) {
            for (auto& e : j["cc"]) {
                if (e.is_string() && !e.get<std::string>().empty())
                    cc_emails.push_back(e.get<std::string>());
            }
        }

        try {
            bool ok = db.send_mail(graph_token, from_email, to_emails, cc_emails, subject, body_html);
            if (ok) {
                // Log sent mail if activity_id was provided
                if (!activity_id.empty()) {
                    std::string sender_id = current_user ? current_user->id : "";
                    std::string sender_email = current_user ? current_user->email : from_email;
                    db.log_sent_mail(activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html);
                    db.delete_mail_draft(activity_id);

                    auto activity = db.get_activity_by_id(activity_id);
                    if (activity && current_user) {
                        auto users = db.list_users();
                        for (const auto &u : users) {
                            if (u.id == current_user->id)
                                continue;

                            std::string link = shared_activity_absolute_link(db, activity->id);
                            std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                            std::string recipients_text = shared_join_display(to_emails);
                            std::string cc_text = shared_join_display(cc_emails);
                            nlohmann::json payload = {
                                {"activity_id", activity->id},
                                {"activity_title", activity->title},
                                {"activity_date", activity->date},
                                {"activity_date_display", date_short},
                                {"activity_department", activity->department ? nlohmann::json(*activity->department) : nlohmann::json(nullptr)},
                                {"mail_subject", subject},
                                {"mail_body_html", body_html},
                                {"to", to_emails},
                                {"cc", cc_emails},
                                {"recipients", to_emails},
                                {"notification_recipient_name", u.display_name},
                                {"notification_recipient_email", u.email},
                                {"triggered_by_name", current_user->display_name},
                                {"triggered_by_email", current_user->email},
                                {"activity_url", link}
                            };

                            bool is_own_activity = shared_contains_name_ci(activity->responsible, u.display_name);
                            bool is_same_department = activity->department && u.department && *activity->department == *u.department;

                            if (is_own_activity && u.notify_mail_own_activity) {
                                auto note = db.create_notification(
                                    u.id,
                                    "mail_own_activity",
                                    "Mail für deine Aktivität versendet",
                                    "Mail zu \"" + activity->title + "\" wurde am " + date_short + " versendet. Empfänger: " + recipients_text + (cc_emails.empty() ? "" : ("; CC: " + cc_text)),
                                    link,
                                    payload
                                );
                                if (note) {
                                    if (u.notify_channel_websocket) {
                                        nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                        wm.send_to_user_ids({u.id}, ws_msg.dump());
                                        shared_deliver_web_push_for_user(db, u, *note);
                                    }
                                    if (u.notify_channel_email && !u.email.empty()) {
                                        std::string subj = "[DPWeb] Mail für deine Aktivität: " + activity->title;
                                        std::string body = "<p><strong>" + current_user->display_name + "</strong> hat eine Mail für deine Aktivität versendet.</p>"
                                            "<p><strong>Aktivität:</strong> <a href=\"" + link + "\">" + activity->title + "</a><br/>"
                                            "<strong>Datum:</strong> " + date_short + "<br/>"
                                            "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                            "<p><strong>Empfänger:</strong> " + recipients_text + (cc_emails.empty() ? "" : ("<br/><strong>CC:</strong> " + cc_text)) + "</p>"
                                            "<p><strong>Betreff:</strong> " + subject + "</p>"
                                            "<hr/>" + body_html +
                                            "<p>Die Aktivität ist direkt im Titel verlinkt.</p>";
                                        db.send_mail(graph_token, current_user->email, {u.email}, {}, subj, body);
                                    }
                                }
                            }

                            else if (is_same_department && u.notify_mail_department) {
                                auto note = db.create_notification(
                                    u.id,
                                    "mail_department",
                                    "Mail in deiner Stufe versendet",
                                    "Mail zu \"" + activity->title + "\" in deiner Stufe wurde am " + date_short + " versendet. Empfänger: " + recipients_text + (cc_emails.empty() ? "" : ("; CC: " + cc_text)),
                                    link,
                                    payload
                                );
                                if (note) {
                                    if (u.notify_channel_websocket) {
                                        nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                        wm.send_to_user_ids({u.id}, ws_msg.dump());
                                        shared_deliver_web_push_for_user(db, u, *note);
                                    }
                                    if (u.notify_channel_email && !u.email.empty()) {
                                        std::string subj = "[DPWeb] Mail in deiner Stufe: " + activity->title;
                                        std::string body = "<p><strong>" + current_user->display_name + "</strong> hat eine Mail in deiner Stufe versendet.</p>"
                                            "<p><strong>Aktivität:</strong> <a href=\"" + link + "\">" + activity->title + "</a><br/>"
                                            "<strong>Datum:</strong> " + date_short + "<br/>"
                                            "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                            "<p><strong>Empfänger:</strong> " + recipients_text + (cc_emails.empty() ? "" : ("<br/><strong>CC:</strong> " + cc_text)) + "</p>"
                                            "<p><strong>Betreff:</strong> " + subject + "</p>"
                                            "<hr/>" + body_html +
                                            "<p>Die Aktivität ist direkt im Titel verlinkt.</p>";
                                        db.send_mail(graph_token, current_user->email, {u.email}, {}, subj, body);
                                    }
                                }
                            }
                        }
                    }
                }
                send_json(res, 200, R"({"status":"sent"})");
            } else {
                send_json(res, 502, R"({"error":"Failed to send mail via Microsoft Graph"})");
            }
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ─── GET /activities/:id/sent-mails ──────────────────────────────────────────

void handle_get_sent_mails(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    // Permission check: user must be able to read the activity
    auto current_user = resolve_user(db, claims);
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

    auto mails = db.list_sent_mails(activity_id);

    nlohmann::json arr = nlohmann::json::array();
    for (auto &m : mails)
    {
        nlohmann::json to_arr = nlohmann::json::array();
        for (auto &e : m.to_emails)
            to_arr.push_back(e);
        nlohmann::json cc_arr = nlohmann::json::array();
        for (auto &e : m.cc_emails)
            cc_arr.push_back(e);

        arr.push_back({
            {"id", m.id},
            {"activity_id", m.activity_id},
            {"sender_id", m.sender_id},
            {"sender_email", m.sender_email},
            {"to_emails", to_arr},
            {"cc_emails", cc_arr},
            {"subject", m.subject},
            {"body_html", m.body_html},
            {"sent_at", m.sent_at},
        });
    }
    send_json(res, 200, arr.dump());
}

// ─── GET /activities/:id/mail-draft ──────────────────────────────────────────

void handle_get_mail_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
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
    if (!is_admin(*current_user) && (!perm || perm->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto draft = db.get_mail_draft(activity_id);
    if (!draft)
    {
        send_json(res, 404, R"({"error":"Kein Entwurf vorhanden"})");
        return;
    }

    nlohmann::json recip_arr = nlohmann::json::array();
    for (auto &r : draft->recipients)
        recip_arr.push_back(r);
    nlohmann::json cc_arr = nlohmann::json::array();
    for (auto &c : draft->cc)
        cc_arr.push_back(c);

    nlohmann::json j = {
        {"id", draft->id},
        {"activity_id", draft->activity_id},
        {"recipients", recip_arr},
        {"cc", cc_arr},
        {"subject", draft->subject},
        {"body_html", draft->body_html},
        {"updated_by", draft->updated_by},
        {"updated_at", draft->updated_at},
    };
    send_json(res, 200, j.dump());
}

// ─── PUT /activities/:id/mail-draft ──────────────────────────────────────────

void handle_put_mail_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string body_buf;
    res->onData([res, &db, activity_id, user_id = current_user->id,
                 body_buf = std::move(body_buf)](std::string_view data, bool last) mutable
                {
        body_buf.append(data);
        if (!last) return;

        nlohmann::json j;
        try { j = nlohmann::json::parse(body_buf); }
        catch (...)
        {
            send_json(res, 400, R"({"error":"Ungültiges JSON"})");
            return;
        }

        std::vector<std::string> recipients;
        if (j.contains("recipients") && j["recipients"].is_array())
        {
            for (auto &r : j["recipients"])
                if (r.is_string() && !r.get<std::string>().empty())
                    recipients.push_back(r.get<std::string>());
        }
        std::vector<std::string> cc;
        if (j.contains("cc") && j["cc"].is_array())
        {
            for (auto &c : j["cc"])
                if (c.is_string() && !c.get<std::string>().empty())
                    cc.push_back(c.get<std::string>());
        }

        std::string subject = j.value("subject", "");
        std::string body_html = j.value("body_html", "");

        auto draft = db.upsert_mail_draft(activity_id, recipients, cc, subject, body_html, user_id);
        if (!draft)
        {
            send_json(res, 500, R"({"error":"Entwurf konnte nicht gespeichert werden"})");
            return;
        }

        nlohmann::json recip_arr = nlohmann::json::array();
        for (auto &r : draft->recipients)
            recip_arr.push_back(r);
        nlohmann::json cc_arr = nlohmann::json::array();
        for (auto &c : draft->cc)
            cc_arr.push_back(c);

        nlohmann::json out = {
            {"id", draft->id},
            {"activity_id", draft->activity_id},
            {"recipients", recip_arr},
            {"cc", cc_arr},
            {"subject", draft->subject},
            {"body_html", draft->body_html},
            {"updated_by", draft->updated_by},
            {"updated_at", draft->updated_at},
        };
        send_json(res, 200, out.dump()); });

    res->onAborted([]() {});
}

// ─── DELETE /activities/:id/mail-draft ───────────────────────────────────────

void handle_delete_mail_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!perm || perm->mail_send_scope == "none"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    db.delete_mail_draft(activity_id);
    send_json(res, 200, R"({"ok":true})");
}

// ─── GET /activities/:id/form-draft ─────────────────────────────────────────

void handle_get_form_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
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

    auto draft = db.get_form_draft(activity_id);
    if (!draft)
    {
        send_json(res, 404, R"({"error":"Kein Entwurf vorhanden"})");
        return;
    }

    nlohmann::json questions;
    try
    {
        questions = nlohmann::json::parse(draft->questions_json);
    }
    catch (...)
    {
        questions = nlohmann::json::array();
    }

    nlohmann::json j = {
        {"id", draft->id},
        {"activity_id", draft->activity_id},
        {"form_type", draft->form_type},
        {"title", draft->title},
        {"questions", questions},
        {"updated_by", draft->updated_by},
        {"updated_at", draft->updated_at},
    };
    send_json(res, 200, j.dump());
}

// ─── PUT /activities/:id/form-draft ─────────────────────────────────────────

void handle_put_form_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string body_buf;
    res->onData([res, &db, activity_id, user_id = current_user->id,
                 body_buf = std::move(body_buf)](std::string_view data, bool last) mutable
                {
        body_buf.append(data);
        if (!last) return;

        nlohmann::json j;
        try { j = nlohmann::json::parse(body_buf); }
        catch (...)
        {
            send_json(res, 400, R"({"error":"Ungültiges JSON"})");
            return;
        }

        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        std::string questions_json = "[]";
        if (j.contains("questions") && j["questions"].is_array())
            questions_json = j["questions"].dump();

        auto draft = db.upsert_form_draft(activity_id, form_type, title, questions_json, user_id);
        if (!draft)
        {
            send_json(res, 500, R"({"error":"Entwurf konnte nicht gespeichert werden"})");
            return;
        }

        nlohmann::json questions;
        try
        {
            questions = nlohmann::json::parse(draft->questions_json);
        }
        catch (...)
        {
            questions = nlohmann::json::array();
        }

        nlohmann::json out = {
            {"id", draft->id},
            {"activity_id", draft->activity_id},
            {"form_type", draft->form_type},
            {"title", draft->title},
            {"questions", questions},
            {"updated_by", draft->updated_by},
            {"updated_at", draft->updated_at},
        };
        send_json(res, 200, out.dump()); });

    res->onAborted([]() {});
}

// ─── DELETE /activities/:id/form-draft ───────────────────────────────────────

void handle_delete_form_draft(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    db.delete_form_draft(activity_id);
    send_json(res, 200, R"({"ok":true})");
}

// ─── GitHub API helpers ───────────────────────────────────────────────────────

static size_t github_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::pair<long, std::string> github_post_issue(
    const std::string &token,
    const std::string &repo,
    const std::string &body_json)
{
    const std::string url = "https://api.github.com/repos/" + repo + "/issues";
    std::string response;

    CURL *curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("curl_easy_init failed");

    struct curl_slist *headers = nullptr;
    std::string auth_hdr = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth_hdr.c_str());
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: DPW-BugReport/1.0");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, github_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("GitHub API call failed: ") + curl_easy_strerror(rc));

    return {status, response};
}

// ─── POST /bug-report ─────────────────────────────────────────────────────────

void handle_post_bug_report(HttpRes *res, HttpReq *req, Database &db)
{
    std::string github_token = app_config::get_or(db, app_config::kGitHubToken, "");
    if (github_token.empty())
    {
        send_json(res, 503, R"({"error":"Bug report service not configured"})");
        return;
    }
    std::string github_repo = app_config::get_or(db, app_config::kGitHubRepo, "reicham2/DPW");

    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto current_user = resolve_user(db, claims);

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, github_token, github_repo, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded())
        {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string description = str_field(j, "description");
        std::string url         = str_field(j, "url");
        std::string user_agent  = str_field(j, "userAgent");
        auto debug_info         = j.contains("debugInfo") ? j["debugInfo"] : nlohmann::json{};

        if (description.empty())
        {
            send_json(res, 400, R"({"error":"description is required"})");
            return;
        }

        // Title: first 80 chars of description
        std::string title = "Bug Report: " + description.substr(0, 80);
        if (description.size() > 80) title += "...";

        // Reporter info
        std::string reporter = current_user
            ? current_user->display_name + " (" + current_user->email + ")"
            : "Unbekannt";

        // Timestamp
        std::time_t now = std::time(nullptr);
        char ts_buf[32];
        std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M UTC", std::gmtime(&now));

        std::string body =
            "## Beschreibung\n\n" + description +
            "\n\n---\n"
            "**Gemeldet von:** " + reporter + "  \n"
            "**URL:** " + url + "  \n"
            "**Browser:** " + user_agent + "  \n"
            "**Zeitpunkt:** " + ts_buf;

        // ─── Debug info section ──────────────────────────────────────
        if (!debug_info.is_null() && debug_info.is_object())
        {
            body += "\n\n---\n<details>\n<summary>Debug-Informationen</summary>\n\n";

            // Session
            body += "### Session\n";
            body += "- **Report Timestamp:** " + debug_info.value("timestamp", "") + "\n";
            body += "- **Session Start:** " + debug_info.value("sessionStart", "") + "\n";
            int dur = debug_info.value("sessionDuration", 0);
            body += "- **Session Duration:** " + std::to_string(dur / 60) + "m " + std::to_string(dur % 60) + "s\n\n";

            // Route
            if (debug_info.contains("route") && debug_info["route"].is_object())
            {
                auto &r = debug_info["route"];
                body += "### Route\n";
                body += "- **Path:** `" + r.value("path", "") + "`\n";
                body += "- **Full Path:** `" + r.value("fullPath", "") + "`\n";
                if (r.contains("params") && !r["params"].empty())
                    body += "- **Params:** `" + r["params"].dump() + "`\n";
                if (r.contains("query") && !r["query"].empty())
                    body += "- **Query:** `" + r["query"].dump() + "`\n";
                body += "\n";
            }

            // User context
            if (debug_info.contains("user") && debug_info["user"].is_object())
            {
                auto &u = debug_info["user"];
                body += "### User\n";
                body += "- **ID:** " + u.value("id", "?") + "\n";
                body += "- **Email:** " + u.value("email", "?") + "\n";
                body += "- **Role:** " + u.value("role", "?") + "\n";
                body += "- **Department:** " + u.value("department", "null") + "\n\n";
            }

            // Viewport
            if (debug_info.contains("viewport") && debug_info["viewport"].is_object())
            {
                auto &v = debug_info["viewport"];
                body += "### Viewport & Device\n";
                body += "- **Window:** " + std::to_string(v.value("width", 0)) + " × " + std::to_string(v.value("height", 0)) + "\n";
                body += "- **Screen:** " + std::to_string(v.value("screenWidth", 0)) + " × " + std::to_string(v.value("screenHeight", 0)) + "\n";
                body += "- **DPR:** " + std::to_string(v.value("devicePixelRatio", 1.0)) + "\n";
                body += "- **Orientation:** " + v.value("orientation", "?") + "\n";
            }
            if (debug_info.contains("inputCapabilities") && debug_info["inputCapabilities"].is_object())
            {
                auto &ic = debug_info["inputCapabilities"];
                body += "- **Touch Points:** " + std::to_string(ic.value("touchPoints", 0)) + "\n";
                body += "- **Pointer:** " + ic.value("pointerType", "?") + "\n";
            }
            body += "\n";

            // DOM
            if (debug_info.contains("dom") && debug_info["dom"].is_object())
            {
                auto &d = debug_info["dom"];
                body += "### DOM\n";
                body += "- **Element Count:** " + std::to_string(d.value("elementCount", 0)) + "\n";
                body += "- **Body Scroll Height:** " + std::to_string(d.value("bodyScrollHeight", 0)) + " px\n";
                if (d.contains("activeElement") && d["activeElement"].is_string())
                    body += "- **Active Element:** `" + d.value("activeElement", "") + "`\n";
                body += "\n";
            }

            // WebSocket
            if (debug_info.contains("webSocket") && debug_info["webSocket"].is_object())
            {
                auto &w = debug_info["webSocket"];
                body += "### WebSocket\n";
                body += "- **Connected:** " + std::string(w.value("connected", false) ? "ja" : "nein") + "\n";
                body += "- **Ready State:** " + std::to_string(w.value("readyState", -1)) + "\n";
                body += "- **Connect Count:** " + std::to_string(w.value("connectCount", 0)) + "\n";
                body += "- **Disconnect Count:** " + std::to_string(w.value("disconnectCount", 0)) + "\n";
                body += "- **Messages Received:** " + std::to_string(w.value("messageCount", 0)) + "\n";
                if (w.contains("lastMessageAt") && w["lastMessageAt"].is_string())
                    body += "- **Last Message:** " + w.value("lastMessageAt", "") + "\n";
                if (w.contains("lastError") && w["lastError"].is_string())
                    body += "- **Last Error:** " + w.value("lastError", "") + "\n";
                body += "- **Registered:** " + std::string(w.value("registered", false) ? "ja" : "nein") + "\n\n";
            }

            // Performance
            if (debug_info.contains("performance") && debug_info["performance"].is_object())
            {
                auto &p = debug_info["performance"];
                body += "### Performance\n";
                body += "- **DOM Content Loaded:** " + std::to_string(p.value("domContentLoaded", 0)) + " ms\n";
                body += "- **Load Complete:** " + std::to_string(p.value("loadComplete", 0)) + " ms\n";
                body += "- **Redirect Count:** " + std::to_string(p.value("redirectCount", 0)) + "\n\n";
            }

            // Memory
            if (debug_info.contains("memory") && debug_info["memory"].is_object())
            {
                auto &m = debug_info["memory"];
                body += "### Memory\n";
                long used = m.value("usedJSHeapSize", 0L);
                long total = m.value("totalJSHeapSize", 0L);
                long limit = m.value("jsHeapSizeLimit", 0L);
                body += "- **Used:** " + std::to_string(used / 1048576) + " MB\n";
                body += "- **Total:** " + std::to_string(total / 1048576) + " MB\n";
                body += "- **Limit:** " + std::to_string(limit / 1048576) + " MB\n\n";
            }

            // Connection
            if (debug_info.contains("connection") && debug_info["connection"].is_object())
            {
                auto &c = debug_info["connection"];
                body += "### Connection\n";
                body += "- **Type:** " + c.value("effectiveType", "?") + "\n";
                body += "- **Downlink:** " + std::to_string(c.value("downlink", 0.0)) + " Mbps\n";
                body += "- **RTT:** " + std::to_string(c.value("rtt", 0)) + " ms\n";
                body += "- **Save Data:** " + std::string(c.value("saveData", false) ? "ja" : "nein") + "\n\n";
            }

            // Misc
            body += "### Environment\n";
            body += "- **Language:** " + debug_info.value("language", "?") + "\n";
            if (debug_info.contains("languages") && debug_info["languages"].is_array())
                body += "- **Languages:** " + debug_info["languages"].dump() + "\n";
            body += "- **Platform:** " + debug_info.value("platform", "?") + "\n";
            body += "- **CPU Cores:** " + std::to_string(debug_info.value("hardwareConcurrency", 0)) + "\n";
            body += "- **Online:** " + std::string(debug_info.value("online", true) ? "ja" : "nein") + "\n";
            body += "- **Cookies:** " + std::string(debug_info.value("cookiesEnabled", true) ? "ja" : "nein") + "\n";
            if (debug_info.contains("referrer") && debug_info["referrer"].is_string())
                body += "- **Referrer:** " + debug_info.value("referrer", "") + "\n";
            body += "- **Document Title:** " + debug_info.value("documentTitle", "") + "\n";
            if (debug_info.contains("localStorage") && debug_info["localStorage"].is_object())
            {
                body += "- **localStorage Count:** " + std::to_string(debug_info["localStorage"].value("count", 0)) + "\n";
                if (debug_info["localStorage"].contains("keys") && debug_info["localStorage"]["keys"].is_array())
                    body += "- **localStorage Keys:** " + debug_info["localStorage"]["keys"].dump() + "\n";
            }
            body += "\n";

            // Breadcrumbs (user interaction trace)
            if (debug_info.contains("breadcrumbs") && debug_info["breadcrumbs"].is_array() && !debug_info["breadcrumbs"].empty())
            {
                body += "### User Interaction Trace\n```\n";
                for (auto &entry : debug_info["breadcrumbs"])
                {
                    body += "[" + entry.value("timestamp", "") + "] "
                          + entry.value("type", "?") + ": "
                          + entry.value("message", "") + "\n";
                }
                body += "```\n\n";
            }

            // All API requests
            if (debug_info.contains("recentApiRequests") && debug_info["recentApiRequests"].is_array() && !debug_info["recentApiRequests"].empty())
            {
                body += "### Recent API Requests\n```\n";
                for (auto &entry : debug_info["recentApiRequests"])
                {
                    body += "[" + entry.value("timestamp", "") + "] "
                          + entry.value("method", "?") + " " + entry.value("url", "?")
                          + " → " + std::to_string(entry.value("status", 0))
                          + " (" + std::to_string(entry.value("duration", 0)) + " ms)\n";
                }
                body += "```\n\n";
            }

            // Console errors/warnings
            if (debug_info.contains("recentConsole") && debug_info["recentConsole"].is_array() && !debug_info["recentConsole"].empty())
            {
                body += "### Console Errors/Warnings\n```\n";
                for (auto &entry : debug_info["recentConsole"])
                {
                    body += "[" + entry.value("level", "?") + " " + entry.value("timestamp", "") + "] "
                          + entry.value("message", "") + "\n";
                    if (entry.contains("stack") && entry["stack"].is_string() && !entry["stack"].empty())
                        body += "  Stack: " + entry.value("stack", "") + "\n";
                }
                body += "```\n\n";
            }

            // JS errors
            if (debug_info.contains("recentJsErrors") && debug_info["recentJsErrors"].is_array() && !debug_info["recentJsErrors"].empty())
            {
                body += "### Uncaught JS Errors\n```\n";
                for (auto &entry : debug_info["recentJsErrors"])
                {
                    if (entry.is_string())
                        body += entry.get<std::string>() + "\n";
                }
                body += "```\n\n";
            }

            // Resource errors
            if (debug_info.contains("recentResourceErrors") && debug_info["recentResourceErrors"].is_array() && !debug_info["recentResourceErrors"].empty())
            {
                body += "### Failed Resources\n```\n";
                for (auto &entry : debug_info["recentResourceErrors"])
                {
                    if (entry.is_string())
                        body += entry.get<std::string>() + "\n";
                }
                body += "```\n\n";
            }

            body += "</details>";
        }

        nlohmann::json issue_payload = {
            {"title", title},
            {"body", body},
            {"labels", nlohmann::json::array({"User-Bug"})}
        };

        try
        {
            auto [create_status, create_resp] = github_post_issue(github_token, github_repo, issue_payload.dump());

            if (create_status != 201)
            {
                send_json(res, 502, nlohmann::json{
                    {"error", "GitHub API Fehler: " + std::to_string(create_status)}
                }.dump());
                return;
            }

            auto resp_j = nlohmann::json::parse(create_resp, nullptr, false);
            std::string issue_url = resp_j.value("html_url", "");
            send_json(res, 200, nlohmann::json{{"issue_url", issue_url}}.dump());
        }
        catch (std::exception &e)
        {
            send_internal_error(res, "handler", e);
        } });
}

