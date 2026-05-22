#include "handlers/internal/shared.hpp"

// ── Permission management (admin only) ──────────────────────────────────────

static nlohmann::json role_perm_to_json(const RolePermission &rp)
{
    return {
        {"role", rp.role},
        {"can_read_own_dept", rp.can_read_own_dept},
        {"can_write_own_dept", rp.can_write_own_dept},
        {"can_read_all_depts", rp.can_read_all_depts},
        {"can_write_all_depts", rp.can_write_all_depts},
        {"activity_read_scope", rp.activity_read_scope},
        {"activity_create_scope", rp.activity_create_scope},
        {"activity_edit_scope", rp.activity_edit_scope},
        {"mail_send_scope", rp.mail_send_scope},
        {"mail_templates_scope", rp.mail_templates_scope},
        {"form_scope", rp.form_scope},
        {"form_templates_scope", rp.form_templates_scope},
        {"event_templates_scope", rp.event_templates_scope},
        {"event_publish_scope", rp.event_publish_scope},
        {"user_dept_scope", rp.user_dept_scope},
        {"user_role_scope", rp.user_role_scope},
        {"locations_manage_scope", rp.locations_manage_scope},
        {"ideenkiste_scope", rp.ideenkiste_scope},
        {"ideenkiste_add_scope", rp.ideenkiste_add_scope},
        {"ideenkiste_delete_scope", rp.ideenkiste_delete_scope}};
}

// ── Current user permissions ─────────────────────────────────────────────────

void handle_get_my_permissions(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    auto user = resolve_user(db, claims);
    if (!user)
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }
    try
    {
        if (is_admin(*user))
        {
            nlohmann::json j = {
                {"role", user->role},
                {"can_read_own_dept", true},
                {"can_write_own_dept", true},
                {"can_read_all_depts", true},
                {"can_write_all_depts", true},
                {"activity_read_scope", "all"},
                {"activity_create_scope", "all"},
                {"activity_edit_scope", "all"},
                {"mail_send_scope", "all"},
                {"mail_templates_scope", "all"},
                {"form_scope", "all"},
                {"form_templates_scope", "all"},
                {"event_templates_scope", "all"},
                {"event_publish_scope", "all"},
                {"user_dept_scope", "all"},
                {"user_role_scope", "all"},
                {"locations_manage_scope", "all"},
                {"ideenkiste_scope", "all"},
                {"ideenkiste_add_scope", "all"},
                {"ideenkiste_delete_scope", "all"},
                {"dept_access", nlohmann::json::array()}};
            send_json(res, 200, j.dump());
            return;
        }
        auto perm = db.get_role_permission(user->role);
        if (!perm)
        {
            send_json(res, 200, "{}");
            return;
        }
        auto j = role_perm_to_json(*perm);
        // Include cross-department access entries
        auto dept_access = db.list_role_dept_access(user->role);
        nlohmann::json da_arr = nlohmann::json::array();
        for (auto &da : dept_access)
            da_arr.push_back({{"department", da.department}, {"can_read", da.can_read}, {"can_write", da.can_write}});
        j["dept_access"] = da_arr;
        send_json(res, 200, j.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// Require system-admin permissions (user_role_scope == 'all') — returns user or sends 403
static std::optional<UserRecord> require_admin(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return std::nullopt;
    auto user = resolve_user(db, claims);
    if (!user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    if (is_admin(*user))
        return user;
    auto perm = db.get_role_permission(user->role);
    if (!perm || perm->user_role_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
}

static nlohmann::json deleted_activity_to_json(const DeletedActivityRecord &r)
{
    nlohmann::json j;
    j["activity"] = to_json(r.activity);
    j["deleted_at"] = r.deleted_at;
    j["deleted_by"] = {
        {"id", r.deleted_by_user_id ? nlohmann::json(*r.deleted_by_user_id) : nlohmann::json(nullptr)},
        {"display_name", r.deleted_by_display_name ? nlohmann::json(*r.deleted_by_display_name) : nlohmann::json(nullptr)},
        {"email", r.deleted_by_email ? nlohmann::json(*r.deleted_by_email) : nlohmann::json(nullptr)}};
    return j;
}

static std::optional<UserRecord> require_strict_admin(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return std::nullopt;
    auto user = resolve_user(db, claims);
    if (!user || !is_admin(*user))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
}

struct ShellExecResult
{
    int exit_code{1};
    std::string output;
};

static ShellExecResult run_shell_command(const std::string &command)
{
    ShellExecResult result;
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe)
    {
        result.output = "Failed to start command.";
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        result.output.append(buffer.data());
    }

    int status = pclose(pipe);
    if (status == -1)
    {
        result.exit_code = 1;
    }
    else if (WIFEXITED(status))
    {
        result.exit_code = WEXITSTATUS(status);
    }
    else
    {
        result.exit_code = 1;
    }
    return result;
}

static std::vector<std::string> split_lines_nonempty(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        line = trim_ascii(line);
        if (!line.empty())
            out.push_back(line);
    }
    return out;
}

static std::string detect_compose_project_name()
{
    const char *hostname = std::getenv("HOSTNAME");
    if (!hostname || std::string(hostname).empty())
        return "";

    const auto inspect = run_shell_command(
        "docker inspect " + std::string(hostname) +
        " --format '{{ index .Config.Labels \"com.docker.compose.project\" }}' 2>&1");
    if (inspect.exit_code != 0)
        return "";
    return trim_ascii(inspect.output);
}

void handle_get_admin_container_logs(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_strict_admin(res, req, db))
        return;

    int tail = 300;
    std::string tail_raw = std::string(req->getQuery("tail"));
    if (!tail_raw.empty())
    {
        try
        {
            tail = std::max(50, std::min(3000, std::stoi(tail_raw)));
        }
        catch (...)
        {
            tail = 300;
        }
    }

    const std::string compose_project = detect_compose_project_name();
    if (compose_project.empty())
    {
        send_json(res, 500, nlohmann::json{{"error", "Compose-Projekt konnte nicht ermittelt werden"}}.dump());
        return;
    }

    const auto service_list_exec = run_shell_command(
        "docker ps --filter label=com.docker.compose.project=" + compose_project +
        " --format '{{.Label \"com.docker.compose.service\"}}' | sort -u 2>&1");
    if (service_list_exec.exit_code != 0)
    {
        send_json(res, 500, nlohmann::json{{"error", "Container-Liste konnte nicht gelesen werden"}, {"details", service_list_exec.output}}.dump());
        return;
    }

    auto services = split_lines_nonempty(service_list_exec.output);
    if (services.empty())
    {
        send_json(res, 200, nlohmann::json{{"services", nlohmann::json::array()}, {"selected_service", nullptr}, {"logs", ""}, {"tail", tail}}.dump());
        return;
    }

    std::string requested_service = trim_ascii(std::string(req->getQuery("service")));
    std::string selected_service = services.front();
    if (!requested_service.empty() && std::find(services.begin(), services.end(), requested_service) != services.end())
        selected_service = requested_service;

    std::vector<std::pair<std::string, std::string>> commands = {
        {"docker compose project service", "docker ps --filter label=com.docker.compose.project=" + compose_project +
                                               " --filter label=com.docker.compose.service=" + selected_service +
                                               " --format '{{.Names}}' | xargs -r -I{} docker logs --timestamps --tail " + std::to_string(tail) + " {} 2>&1"},
        {"docker service fallback", "docker ps --filter label=com.docker.compose.service=" + selected_service +
                                        " --format '{{.Names}}' | xargs -r -I{} docker logs --timestamps --tail " + std::to_string(tail) + " {} 2>&1"},
    };

    nlohmann::json attempts = nlohmann::json::array();
    for (const auto &entry : commands)
    {
        const auto exec = run_shell_command(entry.second);
        attempts.push_back({
            {"source", entry.first},
            {"command", entry.second},
            {"exit_code", exec.exit_code},
        });

        if (exec.exit_code == 0)
        {
            send_json(res, 200, nlohmann::json{
                                    {"services", services},
                                    {"selected_service", selected_service},
                                    {"source", entry.first},
                                    {"tail", tail},
                                    {"logs", exec.output},
                                    {"attempts", attempts},
                                }
                                    .dump());
            return;
        }
    }

    send_json(res, 500, nlohmann::json{
                            {"error", "Container-Logs konnten nicht gelesen werden"},
                            {"services", services},
                            {"selected_service", selected_service},
                            {"tail", tail},
                            {"attempts", attempts},
                        }
                            .dump());
}

void handle_get_admin_activity_trash(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_strict_admin(res, req, db))
        return;

    try
    {
        auto deleted = db.list_deleted_activities();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &entry : deleted)
            arr.push_back(deleted_activity_to_json(entry));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_post_admin_activity_restore(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    auto admin_user = require_strict_admin(res, req, db);
    if (!admin_user)
        return;

    std::string id{req->getParameter(0)};
    try
    {
        bool restored = db.restore_activity(id);
        if (!restored)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 500, R"({"error":"Wiederhergestellt, aber nicht lesbar"})");
            return;
        }

        nlohmann::json msg = {{"event", "created"}, {"activity", to_json(*activity)}};
        wm.broadcast(msg.dump());
        cache_bump_version("activities");
        cache_bump_version("activity");
        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_admin_midata_status(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;

    bool api_key_configured = db.has_app_setting(app_config::kMidataApiKey, true);
    std::string api_url_template = app_config::get_or(
        db,
        app_config::kMidataApiUrlTemplate,
        "https://db.scout.ch/de/groups/{group_id}/people.json");
    std::string api_key_header = "X-Token";

    send_json(res, 200, nlohmann::json{{"api_key_configured", api_key_configured}, {"api_key_header", api_key_header}, {"api_url_template", api_url_template}}.dump());
}

void handle_get_admin_app_settings(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;

    try
    {
        send_json(res, 200, app_config::list_for_admin(db).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_put_admin_app_setting(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;

    std::string key = url_decode(std::string{req->getParameter("key")});

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, key, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})");
            return;
        }

        std::optional<std::string> value = std::nullopt;
        if (j.contains("value") && !j["value"].is_null()) {
            if (!j["value"].is_string()) {
                send_json(res, 400, R"({"error":"value muss ein String oder null sein"})");
                return;
            }
            value = j["value"].get<std::string>();
        }

        try {
            const bool is_azure_key =
                key == app_config::kAzureTenantId ||
                key == app_config::kAzureClientId ||
                key == app_config::kAzureClientSecret;
            const bool is_clear_operation =
                !value.has_value() || trim_ascii(*value).empty();

            if (is_azure_key && !is_clear_operation) {
                send_json(res, 409, R"({"error":"Azure-Login kann hier nicht bearbeitet werden. Bitte Setup-Reset verwenden."})");
                return;
            }

            std::string err;
            if (!app_config::set_from_admin(db, key, value, err)) {
                if (err == "locked-by-env") {
                    send_json(res, 409, R"({"error":"Wert wird durch ENV vorgegeben und kann hier nicht geändert werden"})");
                    return;
                }
                if (err == "missing-encryption-key") {
                    send_json(res, 400, R"({"error":"DPW_CONFIG_ENCRYPTION_KEY fehlt"})");
                    return;
                }
                if (err == "unknown-setting") {
                    send_json(res, 404, R"({"error":"Unbekannter Setting-Key"})");
                    return;
                }
                if (err == "invalid-url") {
                    send_json(res, 400, "{\"error\":\"Ungueltige URL (erwartet http:// oder https://)\"}");
                    return;
                }
                if (err == "invalid-integer") {
                    send_json(res, 400, R"({"error":"Ungueltiger Zahlenwert"})");
                    return;
                }
                if (err == "out-of-range") {
                    send_json(res, 400, R"({"error":"Zahlenwert liegt ausserhalb des erlaubten Bereichs"})");
                    return;
                }
                if (err == "invalid-boolean") {
                    send_json(res, 400, "{\"error\":\"Ungueltiger Bool-Wert (true/false)\"}");
                    return;
                }
                if (err == "invalid-slug") {
                    send_json(res, 400, "{\"error\":\"Ungueltiges Repository-Format (owner/repo)\"}");
                    return;
                }
                if (err == "invalid-email") {
                    send_json(res, 400, R"({"error":"Ungueltige E-Mail-Adresse"})");
                    return;
                }
                if (err == "required") {
                    send_json(res, 400, R"({"error":"Wert darf nicht leer sein"})");
                    return;
                }
                if (err == "readonly-generated") {
                    send_json(res, 409, R"({"error":"Dieser Wert wird automatisch erzeugt und kann hier nicht bearbeitet werden"})");
                    return;
                }
                send_json(res, 500, R"({"error":"Einstellung konnte nicht gespeichert werden"})");
                return;
            }

            if (key == app_config::kMidataApiKey ||
                key == app_config::kMidataApiUrlTemplate ||
                key == app_config::kMidataApiTimeoutMs) {
                shared_clear_cached_midata_children_counts();
            }

            if (key == app_config::kAzureTenantId ||
                key == app_config::kAzureClientId ||
                key == app_config::kAzureClientSecret) {
                sync_azure_runtime_env_from_db(db);
            }

            send_json(res, 200, nlohmann::json{{"ok", true}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

void handle_post_admin_reset_azure_auth(HttpRes *res, HttpReq *req, Database &db)
{
    (void)req;
    if (!require_admin(res, req, db))
        return;

    try
    {
        std::string err;
        const bool cleared_tenant = app_config::set_from_admin(db, app_config::kAzureTenantId, std::nullopt, err);
        const bool cleared_client = cleared_tenant && app_config::set_from_admin(db, app_config::kAzureClientId, std::nullopt, err);
        const bool cleared_secret = cleared_client && app_config::set_from_admin(db, app_config::kAzureClientSecret, std::nullopt, err);

        if (!cleared_secret)
        {
            if (err == "locked-by-env")
            {
                send_json(res, 409, R"({"error":"Azure-Login wird durch ENV vorgegeben und kann nicht zurückgesetzt werden"})");
                return;
            }
            send_json(res, 500, R"({"error":"Azure-Login konnte nicht zurückgesetzt werden"})");
            return;
        }

        sync_azure_runtime_env_from_db(db);
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// POST /admin/departments
void handle_post_department(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        std::string color = j.value("color", "#6b7280");
        std::optional<std::string> midata_group_id = std::nullopt;
        if (j.contains("midata_group_id") && j["midata_group_id"].is_string()) {
            std::string value = j["midata_group_id"].get<std::string>();
            if (!value.empty()) midata_group_id = value;
        }
        try {
            auto d = db.create_department(name, color, midata_group_id);
            if (!d) { send_json(res, 409, R"({"error":"Abteilung existiert bereits"})"); return; }
            cache_bump_version("departments");
            cache_bump_version("activities");
            cache_bump_version("activity");
            cache_bump_version("mail_templates");
            send_json(res, 201, nlohmann::json{{"name", d->name}, {"color", d->color}, {"midata_group_id", d->midata_group_id ? nlohmann::json(*d->midata_group_id) : nlohmann::json(nullptr)}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// PATCH /admin/departments/:name
void handle_patch_department(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string new_name = j.value("name", name);
        std::string color = j.value("color", "#6b7280");
        std::optional<std::string> midata_group_id;
        bool has_midata_group_field = j.contains("midata_group_id");
        if (has_midata_group_field) {
            if (j["midata_group_id"].is_string()) {
                std::string value = j["midata_group_id"].get<std::string>();
                if (!value.empty()) midata_group_id = value;
            }
        }
        try {
            if (!has_midata_group_field) {
                auto depts = db.list_departments();
                for (const auto &dept : depts) {
                    if (dept.name == name) {
                        midata_group_id = dept.midata_group_id;
                        break;
                    }
                }
            }
            auto d = db.update_department(name, new_name, color, midata_group_id);
            if (!d) { send_json(res, 404, R"({"error":"Abteilung nicht gefunden"})"); return; }
            cache_bump_version("departments");
            cache_bump_version("activities");
            cache_bump_version("activity");
            cache_bump_version("mail_templates");
            send_json(res, 200, nlohmann::json{{"name", d->name}, {"color", d->color}, {"midata_group_id", d->midata_group_id ? nlohmann::json(*d->midata_group_id) : nlohmann::json(nullptr)}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// DELETE /admin/departments/:name
void handle_delete_department(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "Allgemein")
    {
        send_json(res, 403, R"({"error":"Die Standard-Stufe kann nicht gelöscht werden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, name](std::string_view chunk, bool last) mutable
                {
        buf->append(chunk);
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) j = nlohmann::json::object();

        std::string transfer_activities_to = j.value("transfer_activities_to", "");
        bool delete_activities = j.value("delete_activities", false);
        std::string transfer_users_to = j.value("transfer_users_to", "");
        bool delete_users = j.value("delete_users", false);

        try
        {
            bool ok = db.delete_department_with_transfers(
                name, transfer_activities_to, delete_activities,
                transfer_users_to, delete_users);
            if (!ok)
            {
                send_json(res, 404, R"({"error":"Stufe nicht gefunden"})");
                return;
            }
            nlohmann::json msg = {{"event", "department_deleted"}, {"name", name}};
            wm.broadcast(msg.dump());
            cache_bump_version("departments");
            cache_bump_version("activities");
            cache_bump_version("activity");
            cache_bump_version("mail_templates");
            send_json(res, 200, R"({"ok":true})");
        }
        catch (std::exception &e)
        {
            send_internal_error(res, "handler", e);
        } });
}

// GET /admin/roles
//
// Any authenticated user can list roles — role name + color are required
// throughout the UI to render consistent role badges (avatar dropdown, etc.).
// CRUD on roles remains gated via the per-handler permission checks below.
void handle_get_roles(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto roles = db.list_roles();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &r : roles)
            arr.push_back({{"name", r.name}, {"color", r.color}, {"sort_order", r.sort_order}});
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// POST /admin/roles
void handle_post_role(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        std::string color = j.value("color", "#6b7280");
        try {
            auto r = db.create_role(name, color);
            if (!r) { send_json(res, 409, R"({"error":"Rolle existiert bereits"})"); return; }
            send_json(res, 201, nlohmann::json{{"name", r->name}, {"color", r->color}, {"sort_order", r->sort_order}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// PATCH /admin/roles/:name
void handle_patch_role(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "admin")
    {
        send_json(res, 403, R"({"error":"Die Admin-Rolle kann nicht bearbeitet werden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string new_name = j.value("name", name);
        std::string color = j.value("color", "#6b7280");
        try {
            auto r = db.update_role(name, new_name, color);
            if (!r) { send_json(res, 404, R"({"error":"Rolle nicht gefunden"})"); return; }
            send_json(res, 200, nlohmann::json{{"name", r->name}, {"color", r->color}, {"sort_order", r->sort_order}}.dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// POST /admin/roles/:name/move
void handle_post_role_move(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "admin")
    {
        send_json(res, 403, R"({"error":"Die Admin-Rolle bleibt immer zuoberst"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string direction = j.value("direction", "");
        if (direction != "up" && direction != "down") {
            send_json(res, 400, R"({"error":"direction muss up oder down sein"})");
            return;
        }
        try {
            bool ok = db.move_role(name, direction == "up");
            if (!ok) { send_json(res, 404, R"({"error":"Rolle nicht gefunden"})"); return; }
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// POST /admin/roles/reorder
void handle_post_roles_reorder(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object() || !j.contains("order") || !j["order"].is_array()) {
            send_json(res, 400, R"({"error":"Erwarte {order: [...]}"})");
            return;
        }
        std::vector<std::string> names;
        for (const auto &item : j["order"]) {
            if (!item.is_string()) {
                send_json(res, 400, R"({"error":"order muss ein Array von Strings sein"})");
                return;
            }
            names.push_back(item.get<std::string>());
        }
        try {
            db.reorder_roles(names);
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// DELETE /admin/roles/:name
void handle_delete_role(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "admin")
    {
        send_json(res, 403, R"({"error":"Die Admin-Rolle kann nicht gelöscht werden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, name](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        std::string transfer_users_to;
        bool del_users = false;

        if (!buf->empty())
        {
            auto j = nlohmann::json::parse(*buf, nullptr, false);
            if (j.is_object())
            {
                if (j.contains("transfer_users_to") && j["transfer_users_to"].is_string())
                    transfer_users_to = j["transfer_users_to"].get<std::string>();
                if (j.contains("delete_users") && j["delete_users"].is_boolean())
                    del_users = j["delete_users"].get<bool>();
            }
        }

        try {
            bool ok = db.delete_role(name, transfer_users_to, del_users);
            if (!ok) { send_json(res, 404, R"({"error":"Rolle nicht gefunden"})"); return; }
            nlohmann::json msg = {{"event", "role_deleted"}, {"name", name}};
            wm.broadcast(msg.dump());
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// GET /admin/role-permissions
void handle_get_role_permissions(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    try
    {
        auto perms = db.list_role_permissions();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &p : perms)
            arr.push_back(role_perm_to_json(p));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// PUT /admin/role-permissions
void handle_put_role_permission(HttpRes *res, HttpReq *req, Database &db)
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
    if (!current_perm || current_perm->user_role_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})");
            return;
        }
        std::string role = j.value("role", "");
        if (role.empty()) {
            send_json(res, 400, R"({"error":"role erforderlich"})");
            return;
        }
        if (role == "admin") {
            send_json(res, 403, R"({"error":"Die Berechtigungen der Admin-Rolle können nicht geändert werden"})");
            return;
        }
        std::string activity_read_scope = j.value("activity_read_scope", "none");
        std::string activity_create_scope = j.value("activity_create_scope", "none");
        std::string activity_edit_scope = j.value("activity_edit_scope", "none");
        std::string mail_send_scope = j.value("mail_send_scope", "none");
        std::string mail_templates_scope = j.value("mail_templates_scope", "none");
        std::string form_scope = j.value("form_scope", "none");
        std::string form_templates_scope = j.value("form_templates_scope", "none");
        std::string event_templates_scope = j.value("event_templates_scope", "none");
        std::string event_publish_scope = j.value("event_publish_scope", "none");
        std::string user_dept_scope = j.value("user_dept_scope", "none");
        std::string user_role_scope = j.value("user_role_scope", "none");
        std::string locations_manage_scope = j.value("locations_manage_scope", "none");
        std::string ideenkiste_scope = j.value("ideenkiste_scope", "none");
        std::string ideenkiste_add_scope = j.value("ideenkiste_add_scope", "none");
        std::string ideenkiste_delete_scope = j.value("ideenkiste_delete_scope", "none");

        bool can_read_own_dept = activity_read_scope == "same_dept" || activity_read_scope == "all";
        bool can_read_all_depts = activity_read_scope == "all";
        bool can_write_own_dept = activity_create_scope == "own_dept" || activity_create_scope == "all";
        bool can_write_all_depts = activity_create_scope == "all";

        try {
            bool ok = db.update_role_permission(role, can_read_own_dept, can_write_own_dept,
                                                can_read_all_depts, can_write_all_depts,
                                                activity_read_scope, activity_create_scope, activity_edit_scope,
                                                mail_send_scope, mail_templates_scope,
                                                form_scope, form_templates_scope,
                                                event_templates_scope, event_publish_scope,
                                                user_dept_scope, user_role_scope, locations_manage_scope,
                                                ideenkiste_scope, ideenkiste_add_scope, ideenkiste_delete_scope);
            if (!ok) {
                send_json(res, 404, R"({"error":"Rolle nicht gefunden"})");
                return;
            }
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

// GET /admin/role-dept-access?role=XYZ
void handle_get_role_dept_access(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    std::string role{req->getQuery("role")};
    if (role.empty())
    {
        send_json(res, 400, R"({"error":"role Query-Parameter erforderlich"})");
        return;
    }
    try
    {
        auto access = db.list_role_dept_access(role);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &a : access)
            arr.push_back({{"role", a.role}, {"department", a.department}, {"can_read", a.can_read}, {"can_write", a.can_write}});
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// PUT /admin/role-dept-access
void handle_put_role_dept_access(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON"})"); return; }
        std::string role = j.value("role", "");
        std::string department = j.value("department", "");
        if (role.empty() || department.empty()) {
            send_json(res, 400, R"({"error":"role und department erforderlich"})");
            return;
        }
        bool can_read = j.value("can_read", false);
        bool can_write = j.value("can_write", false);
        try {
            db.set_role_dept_access(role, department, can_read, can_write);
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

