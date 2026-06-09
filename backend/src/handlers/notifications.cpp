#include "handlers/handler_deps.hpp"

// ---- GET /notifications ----------------------------------------------------

void handle_get_notifications(HttpRes *res, HttpReq *req, Database &db)
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
        int limit = 0;
        std::string limit_raw = std::string(req->getQuery("limit"));
        if (!limit_raw.empty())
        {
            try
            {
                limit = std::max(1, std::min(200, std::stoi(limit_raw)));
            }
            catch (...)
            {
                limit = 0;
            }
        }

        auto notes = db.list_notifications_for_user(current_user->id, limit);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &n : notes)
            arr.push_back(notification_to_json(n));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- PATCH /notifications/:id/read ----------------------------------------

void handle_patch_notification_read(HttpRes *res, HttpReq *req, Database &db)
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
        std::string notification_id{req->getParameter(0)};
        bool ok = db.mark_notification_read(current_user->id, notification_id);
        if (!ok)
        {
            send_json(res, 500, R"({"error":"Konnte Benachrichtigung nicht aktualisieren"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- POST /notifications/read-all -----------------------------------------

void handle_post_notifications_read_all(HttpRes *res, HttpReq *req, Database &db)
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
        bool ok = db.mark_all_notifications_read(current_user->id);
        if (!ok)
        {
            send_json(res, 500, R"({"error":"Konnte Benachrichtigungen nicht aktualisieren"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /push/vapid-public-key -------------------------------------------

void handle_get_push_vapid_public_key(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    std::string pub = app_config::get_or(db, app_config::kVapidPublicKey, "");
    if (pub.empty())
    {
        send_json(res, 503, R"({"error":"Web-Push nicht konfiguriert"})");
        return;
    }
    send_json(res, 200, nlohmann::json{{"publicKey", pub}}.dump());
}

// ---- POST /push/subscriptions ---------------------------------------------

void handle_post_push_subscription(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }

        std::string endpoint = j.value("endpoint", "");
        std::string p256dh;
        std::string auth;
        if (j.contains("keys") && j["keys"].is_object()) {
            p256dh = j["keys"].value("p256dh", "");
            auth = j["keys"].value("auth", "");
        }

        if (endpoint.empty() || p256dh.empty() || auth.empty()) {
            send_json(res, 400, R"({"error":"endpoint, keys.p256dh und keys.auth sind erforderlich"})");
            return;
        }

        auto sub = db.upsert_push_subscription(current_user->id, endpoint, p256dh, auth);
        if (!sub) {
            send_json(res, 500, R"({"error":"Konnte Push-Subscription nicht speichern"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})"); });
}

// ---- DELETE /push/subscriptions -------------------------------------------

void handle_delete_push_subscription(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }

        std::string endpoint = j.value("endpoint", "");
        if (endpoint.empty()) {
            send_json(res, 400, R"({"error":"endpoint ist erforderlich"})");
            return;
        }

        bool ok = db.delete_push_subscription(current_user->id, endpoint);
        if (!ok) {
            send_json(res, 500, R"({"error":"Konnte Push-Subscription nicht löschen"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})"); });
}

// ---- POST /push/payload ----------------------------------------------------

void handle_post_push_payload(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }

        std::string endpoint = j.value("endpoint", "");
        std::string auth = j.value("auth", "");
        if (endpoint.empty() || auth.empty()) {
            send_json(res, 400, R"({"error":"endpoint und auth sind erforderlich"})");
            return;
        }

        auto n = db.get_latest_unread_notification_for_push(endpoint, auth);
        if (!n) {
            set_cors(res);
            res->writeStatus("204 No Content");
            res->end();
            return;
        }

        std::string link = n->link ? *n->link : "/profile";
        std::string date_short;
        if (n->payload.is_object() && n->payload.contains("activity_date_display") && n->payload["activity_date_display"].is_string()) {
            date_short = trim_ascii(n->payload["activity_date_display"].get<std::string>());
        }
        if (date_short.empty()) {
            date_short = format_date_ddmmyyyy(n->created_at);
        }
        nlohmann::json payload = {
            {"id", n->id},
            {"title", n->title},
            {"body", date_short.empty() ? std::string("Neue Benachrichtigung") : std::string("Datum: ") + date_short},
            {"url", link},
            {"notification", notification_to_json(*n)}
        };
        send_json(res, 200, payload.dump()); });
}
