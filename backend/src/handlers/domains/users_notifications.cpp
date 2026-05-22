#include "handlers/internal/shared.hpp"

static nlohmann::json user_to_json(const UserRecord &u)
{
    nlohmann::json j;
    j["id"] = u.id;
    j["microsoft_oid"] = u.microsoft_oid;
    j["email"] = u.email;
    j["display_name"] = u.display_name;
    j["department"] = u.department ? nlohmann::json(*u.department) : nlohmann::json(nullptr);
    j["role"] = u.role;
    j["time_display_mode"] = u.time_display_mode;
    j["notify_material_assigned"] = u.notify_material_assigned;
    j["notify_activity_assigned"] = u.notify_activity_assigned;
    j["notify_program_assigned"] = u.notify_program_assigned;
    j["notify_mail_own_activity"] = u.notify_mail_own_activity;
    j["notify_mail_department"] = u.notify_mail_department;
    j["notify_channel_websocket"] = u.notify_channel_websocket;
    j["notify_channel_email"] = u.notify_channel_email;
    j["created_at"] = u.created_at;
    j["updated_at"] = u.updated_at;
    return j;
}

// ---- POST /auth/me ----------------------------------------------------------
// Called by the frontend immediately after Microsoft login.
// Validates the token and upserts the user record.

void handle_post_auth_me(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        if (claims.oid.rfind("debug:", 0) == 0)
        {
            auto existing = resolve_user(db, claims);
            if (!existing)
            {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            // Block non-admins in maintenance mode (debug path).
            if (!is_admin(*existing))
            {
                auto maint = get_maintenance_state(db);
                if (maint.active)
                {
                    nlohmann::json j = {
                        {"error", "maintenance"},
                        {"message", maint.message.empty() ? "Die App befindet sich im Wartungsmodus." : maint.message},
                        {"scheduled_end", maint.scheduled_end},
                    };
                    send_json(res, 503, j.dump());
                    return;
                }
            }
            send_json(res, 200, user_to_json(*existing).dump());
            return;
        }

        // Determine initial role / department for new users.
        std::string initial_role = "Mitglied";
        std::string initial_dept = "Allgemein";
        bool force_role = false;

        // Lowercase email once for all comparisons.
        std::string email_lower = claims.email;
        std::transform(email_lower.begin(), email_lower.end(), email_lower.begin(), ::tolower);

        // ── Debug-Mode ──────────────────────────────────────────────────────
        // If DEBUG_ADMIN_EMAIL matches, force admin role (compile-time gated).
#if DPW_ENABLE_DEBUG_AUTH
        {
            const char *dbg_mail = std::getenv("DEBUG_ADMIN_EMAIL");
            if (dbg_mail && !std::string(dbg_mail).empty())
            {
                std::string dbg_mail_lower(dbg_mail);
                std::transform(dbg_mail_lower.begin(), dbg_mail_lower.end(),
                               dbg_mail_lower.begin(), ::tolower);
                if (email_lower == dbg_mail_lower)
                {
                    initial_role = "admin";
                    initial_dept = "Allgemein";
                    force_role = true;
                }
            }
        }
#endif

        // ── Normale Rollen-Zuweisung (nur wenn kein Debug-Override) ─────────
        if (!force_role)
        {
            // Email contains "admin" → always admin role (applied even for existing users).
            if (email_lower.find("admin") != std::string::npos)
            {
                initial_role = "admin";
                initial_dept = "Allgemein";
                force_role = true;
            }
        }

        // Extract scout name: "Marc Reichardt v/o Breeze" → "Breeze"
        std::string effective_name = claims.display_name;
        {
            std::string lower = claims.display_name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            auto pos = lower.find("v/o");
            if (pos != std::string::npos)
            {
                std::string after = claims.display_name.substr(pos + 3);
                size_t start = after.find_first_not_of(" \t");
                if (start != std::string::npos)
                    effective_name = after.substr(start);
            }
        }

        auto user = db.upsert_user(claims.oid, claims.email, effective_name,
                                   initial_role, initial_dept, force_role);
        if (!user)
        {
            send_json(res, 500, R"({"error":"Datenbankfehler"})");
            return;
        }

        // Block non-admins when maintenance mode is active.
        if (!is_admin(*user))
        {
            auto maint = get_maintenance_state(db);
            if (maint.active)
            {
                nlohmann::json j = {
                    {"error", "maintenance"},
                    {"message", maint.message.empty() ? "Die App befindet sich im Wartungsmodus." : maint.message},
                    {"scheduled_end", maint.scheduled_end},
                };
                send_json(res, 503, j.dump());
                return;
            }
        }

        send_json(res, 200, user_to_json(*user).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /me ----------------------------------------------------------------

void handle_get_me(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto user = resolve_user(db, claims);
        if (!user)
        {
            send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- GET /users -------------------------------------------------------------

void handle_get_users(HttpRes *res, HttpReq *req, Database &db)
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

        auto users = db.list_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &u : users)
            arr.push_back(user_to_json(u));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- Debug-only endpoints ---------------------------------------------------

static bool is_debug_mode()
{
#if DPW_ENABLE_DEBUG_AUTH
    return true;
#else
    return false;
#endif
}

// GET /debug/users — list debug-login candidates without auth (debug mode only)
void handle_debug_get_users(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    if (!is_debug_mode())
    {
        send_json(res, 404, R"({"error":"Nicht gefunden"})");
        return;
    }
    try
    {
        auto users = db.list_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &u : users)
            arr.push_back({
                {"id", u.id},
                {"display_name", u.display_name},
                {"department", u.department ? nlohmann::json(*u.department) : nlohmann::json(nullptr)},
                {"role", u.role},
            });
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- PATCH /admin/users/:id -------------------------------------------------

void handle_patch_admin_user(HttpRes *res, HttpReq *req, Database &db)
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

    // Check user management permissions
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto current_perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!current_perm || (current_perm->user_dept_scope == "none" && current_perm->user_role_scope == "none")))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string target_id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, target_id, current_user, current_perm, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        // Check scope-based restrictions
        std::string role = j.value("role", "Mitglied");
        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string())
            department = j["department"].get<std::string>();

        if (current_perm) {
            auto target = db.get_user_by_id(target_id);
            if (!target) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }

            // own scope: only own user record
            if (current_perm->user_dept_scope == "own" || current_perm->user_role_scope == "own") {
                if (target_id != current_user->id) {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }

            // own_dept scope: only users in same department
            if (current_perm->user_dept_scope == "own_dept" || current_perm->user_role_scope == "own_dept") {
                if (!current_user->department || !target->department ||
                    *target->department != *current_user->department) {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }

            // Cannot change role if role scope is not enough
            if (current_perm->user_role_scope != "all" &&
                current_perm->user_role_scope != "own_dept" &&
                current_perm->user_role_scope != "own") {
                role = target->role;
            }
            else if (current_user->role != "admin" && role != target->role) {
                auto roles = db.list_roles();
                int current_role_index = -1;
                int new_role_index = -1;
                for (size_t i = 0; i < roles.size(); ++i) {
                    if (roles[i].name == current_user->role) current_role_index = static_cast<int>(i);
                    if (roles[i].name == role) new_role_index = static_cast<int>(i);
                }
                if (current_role_index < 0 || new_role_index < 0 || new_role_index <= current_role_index) {
                    send_json(res, 403, R"({"error":"Diese Rolle darf nicht vergeben werden"})");
                    return;
                }
            }

            // Cannot change department if dept scope is not enough
            if (current_perm->user_dept_scope != "all" &&
                current_perm->user_dept_scope != "own_dept" &&
                current_perm->user_dept_scope != "own") {
                department = target->department;
            }
        }

        try {
            auto user = db.update_user_admin(target_id, display_name, department, role);
            if (!user) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

// ---- DELETE /admin/users/:id ------------------------------------------------

void handle_delete_admin_user(HttpRes *res, HttpReq *req, Database &db)
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
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto current_perm = db.get_role_permission(current_user->role);
    if (!is_admin(*current_user) && (!current_perm || (current_perm->user_dept_scope == "none" && current_perm->user_role_scope == "none")))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string target_id{req->getParameter(0)};

    // Cannot delete yourself
    if (target_id == current_user->id)
    {
        send_json(res, 400, R"({"error":"Du kannst dich nicht selbst löschen"})");
        return;
    }

    auto target = db.get_user_by_id(target_id);
    if (!target)
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    // own scope: only own user record (self-delete still blocked above)
    if (current_perm->user_dept_scope == "own" || current_perm->user_role_scope == "own")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    // own_dept scope: only users in same department
    if (current_perm->user_dept_scope == "own_dept" || current_perm->user_role_scope == "own_dept")
    {
        if (!current_user->department || !target->department ||
            *target->department != *current_user->department)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    if (db.delete_user(target_id))
    {
        send_json(res, 200, R"({"ok":true})");
    }
    else
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
    }
}

// ---- PATCH /me --------------------------------------------------------------

void handle_patch_me(HttpRes *res, HttpReq *req, Database &db)
{
    // Read auth synchronously before onData
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

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, claims, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        // Fetch current user once; own profile updates require an existing user record.
        auto current_user = resolve_user(db, claims);
        if (!current_user) {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        // display_name is managed by login (Azure AD / v/o extraction), not user-editable
        std::string display_name = current_user->display_name;

        // Keep the current department unless an explicit department key is provided.
        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string()) {
            std::string new_dept = j["department"].get<std::string>();
            department = new_dept;

            // Only enforce permission when the department actually changes.
            if (department != current_user->department) {
                auto perm = db.get_role_permission(current_user->role);
                if (!perm || (perm->user_dept_scope != "all" && perm->user_dept_scope != "own")) {
                    send_json(res, 403, R"({"error":"Stufe kann nicht selbst geändert werden"})");
                    return;
                }
            }
        } else if (j.contains("department") && j["department"].is_null()) {
            // NULL means explicit department removal.
            department = std::nullopt;

            // Only enforce permission when the department actually changes.
            if (department != current_user->department) {
                auto perm = db.get_role_permission(current_user->role);
                if (!perm || (perm->user_dept_scope != "all" && perm->user_dept_scope != "own")) {
                    send_json(res, 403, R"({"error":"Stufe kann nicht selbst geändert werden"})");
                    return;
                }
            }
        } else {
            // department key missing → keep existing department
            department = current_user->department;
        }

        std::optional<std::string> time_display_mode;
        if (j.contains("time_display_mode") && j["time_display_mode"].is_string()) {
            std::string mode = j["time_display_mode"].get<std::string>();
            if (mode != "minutes" && mode != "clock") {
                send_json(res, 400, R"({"error":"Ung\u00fcltiger time_display_mode"})");
                return;
            }
            time_display_mode = mode;
        }

        std::optional<bool> notify_material_assigned;
        if (j.contains("notify_material_assigned") && j["notify_material_assigned"].is_boolean()) {
            notify_material_assigned = j["notify_material_assigned"].get<bool>();
        }

        std::optional<bool> notify_activity_assigned;
        if (j.contains("notify_activity_assigned") && j["notify_activity_assigned"].is_boolean()) {
            notify_activity_assigned = j["notify_activity_assigned"].get<bool>();
        }

        std::optional<bool> notify_program_assigned;
        if (j.contains("notify_program_assigned") && j["notify_program_assigned"].is_boolean()) {
            notify_program_assigned = j["notify_program_assigned"].get<bool>();
        }

        std::optional<bool> notify_mail_own_activity;
        if (j.contains("notify_mail_own_activity") && j["notify_mail_own_activity"].is_boolean()) {
            notify_mail_own_activity = j["notify_mail_own_activity"].get<bool>();
        }

        std::optional<bool> notify_mail_department;
        if (j.contains("notify_mail_department") && j["notify_mail_department"].is_boolean()) {
            notify_mail_department = j["notify_mail_department"].get<bool>();
        }

        std::optional<bool> notify_channel_websocket;
        if (j.contains("notify_channel_websocket") && j["notify_channel_websocket"].is_boolean()) {
            notify_channel_websocket = j["notify_channel_websocket"].get<bool>();
        }

        std::optional<bool> notify_channel_email;
        if (j.contains("notify_channel_email") && j["notify_channel_email"].is_boolean()) {
            notify_channel_email = j["notify_channel_email"].get<bool>();
        }

        try {
            auto user = db.update_user(
                current_user->microsoft_oid,
                display_name,
                department,
                time_display_mode,
                notify_material_assigned,
                notify_activity_assigned,
                notify_program_assigned,
                notify_mail_own_activity,
                notify_mail_department,
                notify_channel_websocket,
                notify_channel_email
            );
            if (!user) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
        } });
}

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
            date_short = shared_format_date_ddmmyyyy(n->created_at);
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

// ---- Mail template helpers --------------------------------------------------

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

