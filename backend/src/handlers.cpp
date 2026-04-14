#include "handlers.hpp"
#include "models.hpp"
#include "json.hpp"
#include "graph.hpp"
#include <string>
#include <memory>
#include <algorithm>
#include <ctime>
#include <curl/curl.h>

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

// ── Debug token support ─────────────────────────────────────────────────────
// In debug mode, accepts "debug:<user_id>" as a token. Returns TokenClaims
// with oid = user_id so that get_user_by_oid-based lookups still work.
// Falls back to real Microsoft token validation otherwise.
static TokenClaims validate_token(const std::string &token)
{
    const char *dbg = std::getenv("DEBUG");
    bool debug_mode = dbg && (std::string(dbg) == "true" || std::string(dbg) == "1");

    if (debug_mode && token.rfind("debug:", 0) == 0)
    {
        std::string user_id = token.substr(6);
        if (user_id.empty())
            throw std::runtime_error("debug token: user_id missing");
        // Use user_id as OID so downstream get_user_by_oid won't find anything,
        // but we also store it for get_user_by_id lookups.
        TokenClaims c;
        c.oid = "debug:" + user_id;
        c.email = "debug@local";
        c.display_name = "Debug";
        c.tid = "debug";
        return c;
    }
    return validate_microsoft_token(token);
}

bool require_auth(HttpRes *res, HttpReq *req, TokenClaims &out_claims)
{
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty())
    {
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
        return false;
    }
    try
    {
        out_claims = validate_token(token);
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
        send_json(res, 401, R"({"error":"Nicht autorisiert"})");
    }
    return token;
}

// Resolve a user from TokenClaims — supports debug tokens (oid = "debug:<user_id>").
static std::optional<UserRecord> resolve_user(Database &db, const TokenClaims &claims)
{
    if (claims.oid.rfind("debug:", 0) == 0)
        return db.get_user_by_id(claims.oid.substr(6));
    return db.get_user_by_oid(claims.oid);
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
        {
            MaterialItem mi;
            if (m.is_string())
            {
                mi.name = m.get<std::string>();
            }
            else if (m.is_object())
            {
                mi.name = str_field(m, "name");
                mi.responsible = str_field(m, "responsible");
            }
            if (!mi.name.empty())
                input.material.push_back(mi);
        }
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
        auto current_user = resolve_user(db, claims);
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
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
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
            send_json(res, 404, R"({"error":"Keine SiKo-Datei vorhanden"})");
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

    // All authenticated users can create (Pio restricted to own dept — enforced in onData)
    auto current_user = resolve_user(db, claims);

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, current_user](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"Titel, Datum, Startzeit und Endzeit sind erforderlich"})");
            return;
        }

        // Non-admin: can only create for own department or Leiter
        if (current_user && current_user->role != "admin") {
            if (input.department) {
                const std::string &dept = *input.department;
                if (dept != "Leiter") {
                    if (!current_user->department || *current_user->department != dept) {
                        send_json(res, 403, R"({"error":"Nur eigene Abteilung oder Leiter erlaubt"})");
                        return;
                    }
                }
            }
        }

        try {
            auto activity = db.create_activity(input);
            if (!activity) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
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

    std::string id{req->getParameter(0)};

    // Role check: Stufenleiter=own dept; Leiter/Pio=if verantwortlich (Pio also own dept).
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    {
        const std::string &role = current_user->role;
        if (role == "Stufenleiter" || role == "Leiter" || role == "Pio")
        {
            auto activity = db.get_activity_by_id(id);
            if (!activity)
            {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }
            if (role == "Stufenleiter")
            {
                // Stufenleiter: must match department
                if (!current_user->department || !activity->department ||
                    *current_user->department != *activity->department)
                {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }
            else // Leiter and Pio: must be in responsible list
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
                // Pio: additionally must be in own department
                if (role == "Pio" && (!current_user->department || !activity->department ||
                                      *activity->department != *current_user->department))
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
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"Titel, Datum, Startzeit und Endzeit sind erforderlich"})");
            return;
        }

        try {
            auto activity = db.update_activity(id, input);
            if (!activity) {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
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

    // admin: always; Stufenleiter: own dept; Leiter+Pio: if verantwortlich (Pio also own dept).
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    {
        const std::string &role = current_user->role;
        if (role == "Stufenleiter" || role == "Leiter" || role == "Pio")
        {
            auto activity = db.get_activity_by_id(id);
            if (!activity)
            {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }
            if (role == "Stufenleiter")
            {
                if (!current_user->department || !activity->department ||
                    *current_user->department != *activity->department)
                {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }
            else // Leiter and Pio: must be verantwortlich
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
                // Pio: additionally must be in own department
                if (role == "Pio" && (!current_user->department || !activity->department ||
                                      *activity->department != *current_user->department))
                {
                    send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                    return;
                }
            }
        }
    }

    try
    {
        bool ok = db.delete_activity(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
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
                auto existing = resolve_user(db, claims);
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
            send_json(res, 500, R"({"error":"Datenbankfehler"})");
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

// ---- Debug-only endpoints ---------------------------------------------------

static bool is_debug_mode()
{
    const char *dbg = std::getenv("DEBUG");
    return dbg && (std::string(dbg) == "true" || std::string(dbg) == "1");
}

// GET /debug/users — list all users without auth (debug mode only)
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
            arr.push_back(user_to_json(u));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// POST /auth/debug-login — login as any existing user by ID (debug mode only)
void handle_debug_login(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    if (!is_debug_mode())
    {
        send_json(res, 404, R"({"error":"Nicht gefunden"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        try
        {
            auto j = nlohmann::json::parse(*buf, nullptr, false);
            if (j.is_discarded())
            {
                send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
                return;
            }
            std::string user_id;
            if (j.contains("user_id") && j["user_id"].is_string())
                user_id = j["user_id"].get<std::string>();
            if (user_id.empty())
            {
                send_json(res, 400, R"({"error":"Benutzer-ID ist erforderlich"})");;
                return;
            }
            auto user = db.get_user_by_id(user_id);
            if (!user)
            {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        }
        catch (std::exception &e)
        {
            send_json(res, 400, nlohmann::json{{"error", e.what()}}.dump());
        } });
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

    // Admin or Stufenleiter (own dept users only, no role change)
    auto current_user = resolve_user(db, claims);
    if (!current_user || (current_user->role != "admin" && current_user->role != "Stufenleiter"))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string target_id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, target_id, current_user, &db](std::string_view chunk, bool last)
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

        // Stufenleiter: verify target is in own dept; keep existing role
        std::string role = j.value("role", "Leiter");
        if (current_user && current_user->role == "Stufenleiter") {
            auto target = db.get_user_by_id(target_id);
            if (!target || !current_user->department || !target->department ||
                *target->department != *current_user->department) {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
            role = target->role; // Stufenleiter cannot change role
        }

        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string())
            department = j["department"].get<std::string>();

        try {
            auto user = db.update_user_admin(target_id, display_name, department, role);
            if (!user) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
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

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        // Fetch current user to check role before allowing department change.
        auto current_user = resolve_user(db, claims);
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
            auto user = db.update_user(current_user->microsoft_oid, display_name, department);
            if (!user) {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
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
        {"recipients", t.recipients},
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

    std::string department{req->getParameter(0)};

    // Role check: admin may edit any; Stufenleiter only their own department.
    auto current_user = resolve_user(db, claims);
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

        try {
            auto tpl = db.upsert_mail_template(department, subject, body, recipients);
            if (!tpl) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }
            nlohmann::json msg = {{"event", "template_updated"}, {"template", template_to_json(*tpl)}};
            wm.broadcast(msg.dump());
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
    res->onData([res, buf, token, &db, current_user](std::string_view chunk, bool last)
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

        try {
            bool ok = db.send_mail(graph_token, from_email, to_emails, subject, body_html);
            if (ok) {
                // Log sent mail if activity_id was provided
                if (!activity_id.empty()) {
                    std::string sender_id = current_user ? current_user->id : "";
                    std::string sender_email = current_user ? current_user->email : from_email;
                    db.log_sent_mail(activity_id, sender_id, sender_email, to_emails, subject, body_html);
                }
                send_json(res, 200, R"({"status":"sent"})");
            } else {
                send_json(res, 502, R"({"error":"Failed to send mail via Microsoft Graph"})");
            }
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ─── GET /activities/:id/sent-mails ──────────────────────────────────────────

void handle_get_sent_mails(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter("id")};
    auto mails = db.list_sent_mails(activity_id);

    nlohmann::json arr = nlohmann::json::array();
    for (auto &m : mails)
    {
        nlohmann::json to_arr = nlohmann::json::array();
        for (auto &e : m.to_emails)
            to_arr.push_back(e);

        arr.push_back({
            {"id", m.id},
            {"activity_id", m.activity_id},
            {"sender_id", m.sender_id},
            {"sender_email", m.sender_email},
            {"to_emails", to_arr},
            {"subject", m.subject},
            {"body_html", m.body_html},
            {"sent_at", m.sent_at},
        });
    }
    send_json(res, 200, arr.dump());
}

// ─── GitHub API helpers ───────────────────────────────────────────────────────

static size_t github_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::pair<long, std::string> github_post_issue(
    const std::string &token, const std::string &body_json)
{
    const std::string url = "https://api.github.com/repos/reicham2/DPW/issues";
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
    const char *github_token_env = std::getenv("GITHUB_TOKEN");
    if (!github_token_env || std::string(github_token_env).empty())
    {
        send_json(res, 503, R"({"error":"Bug report service not configured"})");
        return;
    }
    std::string github_token = github_token_env;

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
    res->onData([res, buf, github_token, current_user](std::string_view chunk, bool last)
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
            auto [create_status, create_resp] = github_post_issue(github_token, issue_payload.dump());

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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}
