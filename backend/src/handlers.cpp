#include "handlers.hpp"
#include "models.hpp"
#include "json.hpp"
#include "graph.hpp"
#include <string>
#include <memory>
#include <algorithm>

static const std::string PFADI_HUE_GROUP_ID = "17fcb1fa-9fa2-45f2-96cc-3804d7097311";

static const char *status_text(int code)
{
    switch (code)
    {
    case 200:
        return "200 OK";
    case 201:
        return "201 Created";
    case 204:
        return "204 No Content";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 404:
        return "404 Not Found";
    case 500:
        return "500 Internal Server Error";
    default:
        return "200 OK";
    }
}

void set_cors(HttpRes *res)
{
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
}

bool require_auth(HttpRes *res, HttpReq *req, TokenClaims &out_claims)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return false;
    }
    try
    {
        out_claims = validate_microsoft_token(token);
        return true;
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return false;
    }
}

std::string auth_token_from_header(HttpRes *res, const std::string &auth_header)
{
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
    }
    return token;
}

void send_json(HttpRes *res, int status, const std::string &body)
{
    res->writeStatus(status_text(status));
    set_cors(res);
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}

static std::string str_field(const nlohmann::json &j, const char *key, const std::string &def = "")
{
    if (j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return def;
}

// Parse ActivityInput from JSON body
static ActivityInput parse_activity_input(const nlohmann::json &j)
{
    ActivityInput input;
    input.title = str_field(j, "title");
    input.date = str_field(j, "date");
    input.start_time = str_field(j, "start_time");
    input.end_time = str_field(j, "end_time");
    input.goal = str_field(j, "goal");
    input.location = str_field(j, "location");
    if (j.contains("responsible") && j["responsible"].is_array())
    {
        for (auto &r : j["responsible"])
            if (r.is_string())
                input.responsible.push_back(r.get<std::string>());
    }
    input.needs_siko = j.value("needs_siko", false);

    if (j.contains("department") && j["department"].is_string())
        input.department = j["department"].get<std::string>();

    if (j.contains("bad_weather_info") && j["bad_weather_info"].is_string())
    {
        std::string bwi = j["bad_weather_info"].get<std::string>();
        if (!bwi.empty())
            input.bad_weather_info = bwi;
    }

    if (j.contains("siko_base64") && j["siko_base64"].is_string())
    {
        std::string sb = j["siko_base64"].get<std::string>();
        if (!sb.empty())
            input.siko_base64 = sb;
    }

    if (j.contains("material") && j["material"].is_array())
    {
        for (auto &m : j["material"])
            if (m.is_string())
                input.material.push_back(m.get<std::string>());
    }

    if (j.contains("programs") && j["programs"].is_array())
    {
        for (auto &p : j["programs"])
        {
            ProgramInput pi;
            pi.time = str_field(p, "time");
            pi.title = str_field(p, "title");
            pi.description = str_field(p, "description");
            pi.responsible = str_field(p, "responsible");
            input.programs.push_back(pi);
        }
    }

    return input;
}

// ---- GET /departments -------------------------------------------------------

void handle_get_departments(HttpRes *res, HttpReq * /*req*/)
{
    static const std::string body =
        R"(["Leiter","Pio","Pfadi","Wölfe","Biber"])";
    send_json(res, 200, body);
}

// ---- GET /activities --------------------------------------------------------

void handle_get_activities(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto current_user = db.get_user_by_oid(claims.oid);
        auto activities = db.list_activities();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &a : activities)
        {
            // Pio: only see activities for their own department
            if (current_user && current_user->role == "Pio")
            {
                if (!a.department || !current_user->department ||
                    *a.department != *current_user->department)
                    continue;
            }
            arr.push_back(to_json(a));
        }
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id ----------------------------------------------------

void handle_get_activity(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }
        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/siko -----------------------------------------------

void handle_get_siko(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto bytes = db.get_siko(id);
        if (!bytes || bytes->empty())
        {
            send_json(res, 404, R"({"error":"no SiKo file"})");
            return;
        }
        std::string disp = "attachment; filename=\"siko-" + id + ".pdf\"";
        set_cors(res);
        res->writeStatus("200 OK");
        res->writeHeader("Content-Type", "application/pdf");
        res->writeHeader("Content-Disposition", disp);
        res->end(std::string_view(
            reinterpret_cast<const char *>(bytes->data()), bytes->size()));
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /activities -------------------------------------------------------

void handle_post_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    // Read auth header synchronously (req is only valid here, not inside onData)
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_microsoft_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    // Pio cannot create activities
    auto current_user = db.get_user_by_oid(claims.oid);
    if (current_user && current_user->role == "Pio")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"title, date, start_time and end_time are required"})");
            return;
        }

        try {
            auto activity = db.create_activity(input);
            if (!activity) {
                send_json(res, 500, R"({"error":"db error"})");
                return;
            }
            nlohmann::json msg = {{"event", "created"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            send_json(res, 201, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- PATCH /activities/:id --------------------------------------------------

void handle_patch_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_microsoft_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string id{req->getParameter(0)};

    // Role check: Pio cannot edit; Leiter only if responsible; Stufenleiter only own dept.
    auto current_user = db.get_user_by_oid(claims.oid);
    if (current_user)
    {
        const std::string &role = current_user->role;
        if (role == "Pio")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (role == "Leiter" || role == "Stufenleiter")
        {
            auto activity = db.get_activity_by_id(id);
            if (!activity)
            {
                send_json(res, 404, R"({"error":"not found"})");
                return;
            }
            if (role == "Stufenleiter")
            {
                // Must match department
                if (!current_user->department || !activity->department ||
                    *current_user->department != *activity->department)
                {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }
            else // Leiter: must be in responsible list
            {
                bool is_responsible = false;
                for (auto &r : activity->responsible)
                {
                    if (r == current_user->display_name || r == claims.email)
                    {
                        is_responsible = true;
                        break;
                    }
                }
                if (!is_responsible)
                {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }
        }
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, id, &db, &wm](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"title, date, start_time and end_time are required"})");
            return;
        }

        try {
            auto activity = db.update_activity(id, input);
            if (!activity) {
                send_json(res, 404, R"({"error":"not found"})");
                return;
            }
            nlohmann::json msg = {{"event", "updated"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            send_json(res, 200, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- DELETE /activities/:id -------------------------------------------------

void handle_delete_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};

    // Role check: only admin and Stufenleiter (own dept) may delete.
    auto current_user = db.get_user_by_oid(claims.oid);
    if (current_user)
    {
        const std::string &role = current_user->role;
        if (role == "Pio" || role == "Leiter")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (role == "Stufenleiter")
        {
            auto activity = db.get_activity_by_id(id);
            if (!activity)
            {
                send_json(res, 404, R"({"error":"not found"})");
                return;
            }
            if (!current_user->department || !activity->department ||
                *current_user->department != *activity->department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }
    }

    try
    {
        bool ok = db.delete_activity(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }
        nlohmann::json msg = {{"event", "deleted"}, {"id", id}};
        wm.broadcast(msg.dump());
        set_cors(res);
        res->writeStatus("204 No Content");
        res->end();
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- User helpers -----------------------------------------------------------

static nlohmann::json user_to_json(const UserRecord &u)
{
    nlohmann::json j;
    j["id"] = u.id;
    j["microsoft_oid"] = u.microsoft_oid;
    j["email"] = u.email;
    j["display_name"] = u.display_name;
    j["department"] = u.department ? nlohmann::json(*u.department) : nlohmann::json(nullptr);
    j["role"] = u.role;
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
        // Determine initial role / department for new users.
        std::string initial_role = "Leiter";
        std::string initial_dept = "Leiter";
        bool force_role = false;

        // Lowercase email once for all comparisons.
        std::string email_lower = claims.email;
        std::transform(email_lower.begin(), email_lower.end(), email_lower.begin(), ::tolower);

        // ── Debug-Mode ──────────────────────────────────────────────────────
        // If DEBUG=true and the user's email matches DEBUG_ADMIN_EMAIL,
        // force admin role regardless of any other checks.
        {
            const char *dbg = std::getenv("DEBUG");
            const char *dbg_mail = std::getenv("DEBUG_ADMIN_EMAIL");
            bool debug_mode = dbg && (std::string(dbg) == "true" || std::string(dbg) == "1");
            if (debug_mode && dbg_mail && !std::string(dbg_mail).empty())
            {
                std::string dbg_mail_lower(dbg_mail);
                std::transform(dbg_mail_lower.begin(), dbg_mail_lower.end(),
                               dbg_mail_lower.begin(), ::tolower);
                if (email_lower == dbg_mail_lower)
                {
                    initial_role = "admin";
                    initial_dept = "Leiter";
                    force_role = true;
                }
            }
        }

        // ── Normale Rollen-Zuweisung (nur wenn kein Debug-Override) ─────────
        if (!force_role)
        {
            // Email contains "admin" → always admin role (applied even for existing users).
            if (email_lower.find("admin") != std::string::npos)
            {
                initial_role = "admin";
                initial_dept = "Leiter";
                force_role = true;
            }
            else
            {
                // Only check group membership for potentially new users.
                auto existing = db.get_user_by_oid(claims.oid);
                if (!existing)
                {
                    // New user: check if they are in "Pfadi Hü allgemein" group.
                    auto in_group = is_group_member(claims.oid, PFADI_HUE_GROUP_ID);
                    if (in_group == false)
                    {
                        // Explicitly NOT in the group → Pio
                        initial_role = "Pio";
                        initial_dept = "Pio";
                    }
                    // If nullopt (check failed) or true → keep Leiter defaults
                }
            }
        }

        auto user = db.upsert_user(claims.oid, claims.email, claims.display_name,
                                   initial_role, initial_dept, force_role);
        if (!user)
        {
            send_json(res, 500, R"({"error":"db error"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        auto user = db.get_user_by_oid(claims.oid);
        if (!user)
        {
            send_json(res, 404, R"({"error":"user not found"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        auto users = db.list_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &u : users)
            arr.push_back(user_to_json(u));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- PATCH /admin/users/:id -------------------------------------------------

void handle_patch_admin_user(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_microsoft_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    // Admin only
    auto current_user = db.get_user_by_oid(claims.oid);
    if (!current_user || current_user->role != "admin")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string target_id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, target_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }
        std::string role = j.value("role", "Leiter");

        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string())
            department = j["department"].get<std::string>();

        try {
            auto user = db.update_user_admin(target_id, display_name, department, role);
            if (!user) {
                send_json(res, 404, R"({"error":"user not found"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- PATCH /me --------------------------------------------------------------

void handle_patch_me(HttpRes *res, HttpReq *req, Database &db)
{
    // Read auth synchronously before onData
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return;
    }

    TokenClaims claims;
    try
    {
        claims = validate_microsoft_token(token);
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
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        // Fetch current user to check role before allowing department change.
        auto current_user = db.get_user_by_oid(claims.oid);
        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string()) {
            std::string new_dept = j["department"].get<std::string>();
            // Only admin may change their own department via /me.
            if (current_user && current_user->role != "admin") {
                send_json(res, 403, R"({"error":"Abteilung kann nicht selbst geändert werden"})");
                return;
            }
            department = new_dept;
        } else if (j.contains("department") && j["department"].is_null()) {
            if (current_user && current_user->role != "admin") {
                send_json(res, 403, R"({"error":"Abteilung kann nicht selbst geändert werden"})");
                return;
            }
            // department stays nullopt → will be set to NULL
        } else {
            // department key missing → keep existing department
            department = current_user ? current_user->department : std::optional<std::string>{};
        }

        try {
            auto user = db.update_user(claims.oid, display_name, department);
            if (!user) {
                send_json(res, 404, R"({"error":"user not found"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- Mail template helpers --------------------------------------------------

static nlohmann::json template_to_json(const MailTemplate &t)
{
    return {
        {"id", t.id},
        {"department", t.department},
        {"subject", t.subject},
        {"body", t.body},
        {"created_at", t.created_at},
        {"updated_at", t.updated_at}};
}

// ---- GET /mail-templates ----------------------------------------------------

void handle_get_mail_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto templates = db.list_mail_templates();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
            arr.push_back(template_to_json(t));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /mail-templates/:department ----------------------------------------

void handle_get_mail_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string department{req->getParameter(0)};
    try
    {
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
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- PUT /mail-templates/:department ----------------------------------------

void handle_put_mail_template(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return;
    }
    TokenClaims claims;
    try
    {
        claims = validate_microsoft_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    std::string department{req->getParameter(0)};

    // Role check: admin may edit any; Stufenleiter only their own department.
    auto current_user = db.get_user_by_oid(claims.oid);
    if (current_user)
    {
        const std::string &role = current_user->role;
        if (role != "admin")
        {
            if (role != "Stufenleiter" || !current_user->department ||
                *current_user->department != department)
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }
    }
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, department, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        std::string subject = j.value("subject", "");
        std::string body    = j.value("body", "");

        try {
            auto tpl = db.upsert_mail_template(department, subject, body);
            if (!tpl) {
                send_json(res, 500, R"({"error":"db error"})");
                return;
            }
            send_json(res, 200, template_to_json(*tpl).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- POST /send-mail --------------------------------------------------------

void handle_post_send_mail(HttpRes *res, HttpReq *req, Database &db)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return;
    }
    try
    {
        validate_microsoft_token(token);
    }
    catch (std::exception &e)
    {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, token, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        std::string subject    = j.value("subject", "");
        std::string body_html  = j.value("body", "");
        std::string from_email = j.value("from", "");
        std::string graph_token = j.value("access_token", "");

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

        try {
            bool ok = db.send_mail(graph_token, from_email, to_emails, subject, body_html);
            if (ok) {
                send_json(res, 200, R"({"status":"sent"})");
            } else {
                send_json(res, 502, R"({"error":"Failed to send mail via Microsoft Graph"})");
            }
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}
