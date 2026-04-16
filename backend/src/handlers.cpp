#include "handlers.hpp"
#include "models.hpp"
#include "json.hpp"
#include "graph.hpp"
#include <string>
#include <memory>
#include <algorithm>
#include <ctime>
#include <map>
#include <curl/curl.h>

static const std::string PFADI_HUE_GROUP_ID = "17fcb1fa-9fa2-45f2-96cc-3804d7097311";

static const char *status_text(int code)
{
    {
                                                user_dept_scope, user_role_scope, locations_manage_scope);
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

// URL-decode a percent-encoded string (e.g. "W%C3%B6lfe" → "Wölfe")
static std::string url_decode(const std::string &src)
{
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i)
    {
        if (src[i] == '%' && i + 2 < src.size())
        {
            unsigned int ch = 0;
            if (sscanf(src.c_str() + i + 1, "%2x", &ch) == 1)
            {
                out += static_cast<char>(ch);
                i += 2;
                continue;
            }
        }
        out += (src[i] == '+') ? ' ' : src[i];
    }
    return out;
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

    if (j.contains("department") && j["department"].is_string())
        input.department = j["department"].get<std::string>();

    if (j.contains("bad_weather_info") && j["bad_weather_info"].is_string())
    {
        std::string bwi = j["bad_weather_info"].get<std::string>();
        if (!bwi.empty())
            input.bad_weather_info = bwi;
    }

    if (j.contains("siko_text") && j["siko_text"].is_string())
    {
        std::string st = j["siko_text"].get<std::string>();
        if (!st.empty())
            input.siko_text = st;
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
                if (m.contains("responsible") && m["responsible"].is_array())
                {
                    for (auto &r : m["responsible"])
                        if (r.is_string())
                            mi.responsible.push_back(r.get<std::string>());
                }
                else if (m.contains("responsible") && m["responsible"].is_string())
                {
                    std::string rs = m["responsible"].get<std::string>();
                    if (!rs.empty())
                        mi.responsible.push_back(rs);
                }
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
            if (p.contains("responsible") && p["responsible"].is_array())
            {
                for (auto &r : p["responsible"])
                    if (r.is_string())
                        pi.responsible.push_back(r.get<std::string>());
            }
            else if (p.contains("responsible") && p["responsible"].is_string())
            {
                std::string rs = p["responsible"].get<std::string>();
                if (!rs.empty())
                    pi.responsible.push_back(rs);
            }
            input.programs.push_back(pi);
        }
    }

    return input;
}

// ---- GET /departments -------------------------------------------------------

void handle_get_departments(HttpRes *res, HttpReq * /*req*/, Database &db)
{
    try
    {
        auto depts = db.list_departments();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &d : depts)
            arr.push_back({{"name", d.name}, {"color", d.color}});
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ── Permission helpers ───────────────────────────────────────────────────────

static bool has_dept_read_access(const std::vector<RoleDeptAccess> &dept_access, const std::string &dept)
{
    for (const auto &da : dept_access)
    {
        if (da.department == dept && da.can_read)
            return true;
    }
    return false;
}

static bool has_dept_create_access(const std::vector<RoleDeptAccess> &dept_access, const std::string &dept)
{
    for (const auto &da : dept_access)
    {
        if (da.department == dept && da.can_write)
            return true;
    }
    return false;
}

static bool can_create_dept(const RolePermission &perm, const UserRecord &user,
                            const std::vector<RoleDeptAccess> &dept_access,
                            const std::string &dept)
{
    if (perm.activity_create_scope == "all")
        return true;
    if (perm.activity_create_scope == "own_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_create_access(dept_access, dept);
}

static bool can_read_dept(const RolePermission &perm, const UserRecord &user,
                          const std::vector<RoleDeptAccess> &dept_access,
                          const std::string &dept)
{
    if (perm.activity_read_scope == "all")
        return true;
    if (perm.activity_read_scope == "same_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_read_access(dept_access, dept);
}

static bool is_activity_responsible(const Activity &activity, const UserRecord &user, const TokenClaims &claims)
{
    for (const auto &responsible : activity.responsible)
    {
        if (responsible == user.display_name || responsible == claims.email)
            return true;
    }
    return false;
}

static bool can_read_activity(const RolePermission &perm, const UserRecord &user,
                              const std::vector<RoleDeptAccess> &dept_access,
                              const Activity &activity, const TokenClaims & /*claims*/)
{
    if (perm.activity_read_scope == "all")
        return true;

    if (!activity.department)
        return perm.activity_read_scope != "none";

    const std::string &dept = *activity.department;
    if (perm.activity_read_scope == "same_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_read_access(dept_access, dept);
}

static bool can_edit_activity(const RolePermission &perm, const UserRecord &user,
                              const Activity &activity, const TokenClaims &claims)
{
    if (perm.activity_edit_scope == "all")
        return true;
    if (perm.activity_edit_scope == "same_dept")
        return activity.department && user.department && *activity.department == *user.department;
    if (perm.activity_edit_scope == "own")
        return is_activity_responsible(activity, user, claims);
    return false;
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

        // Load permissions to determine which departments the user can read
        std::optional<RolePermission> perm;
        std::vector<RoleDeptAccess> dept_access;
        if (current_user)
        {
            perm = db.get_role_permission(current_user->role);
            dept_access = db.list_role_dept_access(current_user->role);
        }

        nlohmann::json arr = nlohmann::json::array();
        for (auto &a : activities)
        {
            if (current_user && perm)
            {
                if (!can_read_activity(*perm, *current_user, dept_access, a, claims))
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
        auto current_user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (current_user)
        {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity, claims))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        send_json(res, 200, to_json(*activity).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /locations ---------------------------------------------------------

void handle_get_locations(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto locs = db.get_predefined_locations();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : locs)
            arr.push_back(l);
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// Helper: require locations_manage_scope == 'all'
static std::optional<UserRecord> require_locations_admin(HttpRes *res, HttpReq *req, Database &db)
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
    auto perm = db.get_role_permission(user->role);
    if (!perm || perm->locations_manage_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
}

static nlohmann::json location_to_json(const LocationRecord &loc)
{
    return {{"id", loc.id}, {"name", loc.name}, {"created_at", loc.created_at}, {"updated_at", loc.updated_at}};
}

// ---- GET /admin/locations ---------------------------------------------------

void handle_get_locations_admin(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    try
    {
        auto locs = db.list_predefined_locations();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : locs)
            arr.push_back(location_to_json(l));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /admin/locations --------------------------------------------------

void handle_post_location(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db](std::string_view chunk, bool last) mutable
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        try {
            auto loc = db.create_predefined_location(name);
            if (!loc) { send_json(res, 409, R"({"error":"Ort existiert bereits"})"); return; }
            send_json(res, 201, location_to_json(*loc).dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- PATCH /admin/locations/:id ---------------------------------------------

void handle_patch_location(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    std::string id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, id](std::string_view chunk, bool last) mutable
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (!j.is_object()) { send_json(res, 400, R"({"error":"Ungültiges JSON-Format"})"); return; }
        std::string name = j.value("name", "");
        if (name.empty()) { send_json(res, 400, R"({"error":"name erforderlich"})"); return; }
        try {
            auto loc = db.update_predefined_location(id, name);
            if (!loc) { send_json(res, 404, R"({"error":"Nicht gefunden"})"); return; }
            send_json(res, 200, location_to_json(*loc).dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- DELETE /admin/locations/:id --------------------------------------------

void handle_delete_location(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_locations_admin(res, req, db))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        bool ok = db.delete_predefined_location(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 204, "");
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/attachments ----------------------------------------

void handle_get_attachments(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (current_user)
        {
            auto activity = db.get_activity_by_id(id);
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!activity || !perm || !can_read_activity(*perm, *current_user, dept_access, *activity, claims))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        auto atts = db.list_attachments(id);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &a : atts)
            arr.push_back(attachment_to_json(a));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /activities/:id/attachments ---------------------------------------

void handle_post_attachment(HttpRes *res, HttpReq *req, Database &db)
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
    std::string activity_id{req->getParameter(0)};

    auto activity = db.get_activity_by_id(activity_id);
    if (!activity)
    {
        send_json(res, 404, R"({"error":"Nicht gefunden"})");
        return;
    }

    auto perm = db.get_role_permission(current_user->role);
    auto dept_access = db.list_role_dept_access(current_user->role);
    if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity, claims))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    res->onAborted([] {});
    std::string body;
    res->onData([res, &db, activity_id, body = std::move(body)](std::string_view chunk, bool last) mutable
                {
        body.append(chunk);
        if (!last) return;

        auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }
        std::string filename = j.value("filename", "");
        std::string content_type = j.value("content_type", "application/octet-stream");
        std::string data_base64 = j.value("data", "");
        if (filename.empty() || data_base64.empty()) {
            send_json(res, 400, R"({"error":"filename and data required"})");
            return;
        }
        try {
            auto att = db.add_attachment(activity_id, filename, content_type, data_base64);
            if (!att) {
                send_json(res, 500, R"({"error":"failed to add attachment"})");
                return;
            }
            send_json(res, 201, attachment_to_json(*att).dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ---- GET /attachments/:id/download ------------------------------------------

void handle_get_attachment_download(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto ad = db.get_attachment_data(id);
        if (!ad || ad->data.empty())
        {
            send_json(res, 404, R"({"error":"Keine SiKo-Datei vorhanden"})");
            return;
        }

        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto activity = db.get_activity_by_id(ad->activity_id);
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);
        if (!activity || !perm || !can_read_activity(*perm, *current_user, dept_access, *activity, claims))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        set_cors(res);
        res->writeStatus("200 OK");
        res->writeHeader("Content-Type", ad->content_type);
        std::string disp = "inline; filename=\"" + ad->filename + "\"";
        res->writeHeader("Content-Disposition", disp);
        res->end(std::string_view(
            reinterpret_cast<const char *>(ad->data.data()), ad->data.size()));
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- DELETE /attachments/:id ------------------------------------------------

void handle_delete_attachment(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::string id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto attachment = db.get_attachment_data(id);
        if (!attachment)
        {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }

        auto activity = db.get_activity_by_id(attachment->activity_id);
        auto perm = db.get_role_permission(current_user->role);
        if (!activity || !perm || !can_edit_activity(*perm, *current_user, *activity, claims))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        if (!db.delete_attachment(id))
        {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
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

        // Check create permission for the target department.
        if (current_user && input.department) {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!perm || !can_create_dept(*perm, *current_user, dept_access, *input.department)) {
                send_json(res, 403, R"({"error":"Keine Schreibberechtigung für diese Stufe"})");
                return;
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
    std::optional<std::string> current_department;

    // Permission check: user needs activity edit permission for this activity.
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    {
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        bool allowed = perm && can_edit_activity(*perm, *current_user, *activity, claims);
        current_department = activity->department;
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, id, &db, &wm, current_user, current_department](std::string_view chunk, bool last)
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

        if (current_user && input.department != current_department) {
            auto perm = db.get_role_permission(current_user->role);
            auto dept_access = db.list_role_dept_access(current_user->role);
            if (!input.department || !perm || !can_create_dept(*perm, *current_user, dept_access, *input.department)) {
                send_json(res, 403, R"({"error":"Keine Schreibberechtigung für diese Stufe"})");
                return;
            }
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

    // Permission checks are resolved dynamically from the activity edit scope.
    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    {
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        bool allowed = perm && can_edit_activity(*perm, *current_user, *activity, claims);
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
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
        if (claims.oid.rfind("debug:", 0) == 0)
        {
            auto existing = resolve_user(db, claims);
            if (!existing)
            {
                send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                return;
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
                    initial_dept = "Allgemein";
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
                initial_dept = "Allgemein";
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
                        // Explicitly NOT in the group → keep default Mitglied role
                    }
                    // If nullopt (check failed) or true → keep Mitglied defaults
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
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        // Determine visibility based on user management scopes
        bool can_see_all = perm->user_dept_scope == "all" || perm->user_role_scope == "all";
        bool can_see_dept = perm->user_dept_scope == "own_dept" || perm->user_role_scope == "own_dept";

        auto users = db.list_users();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &u : users)
        {
            if (can_see_all)
            {
                arr.push_back(user_to_json(u));
            }
            else if (can_see_dept)
            {
                // own_dept: only users in same department + self
                if (u.id == current_user->id ||
                    (current_user->department && u.department && *current_user->department == *u.department))
                    arr.push_back(user_to_json(u));
            }
            else
            {
                // own or none: only self
                if (u.id == current_user->id)
                    arr.push_back(user_to_json(u));
            }
        }
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
    if (!current_perm || (current_perm->user_dept_scope == "none" && current_perm->user_role_scope == "none"))
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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
    if (!current_perm || (current_perm->user_dept_scope == "none" && current_perm->user_role_scope == "none"))
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

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        // Fetch current user to check permissions before allowing department change.
        auto current_user = resolve_user(db, claims);
        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string()) {
            std::string new_dept = j["department"].get<std::string>();
            // Only users with user_dept_scope 'all' or 'own' may change their own department.
            auto perm = current_user ? db.get_role_permission(current_user->role) : std::nullopt;
            if (!perm || (perm->user_dept_scope != "all" && perm->user_dept_scope != "own")) {
                send_json(res, 403, R"({"error":"Stufe kann nicht selbst geändert werden"})");
                return;
            }
            department = new_dept;
        } else if (j.contains("department") && j["department"].is_null()) {
            auto perm = current_user ? db.get_role_permission(current_user->role) : std::nullopt;
            if (!perm || (perm->user_dept_scope != "all" && perm->user_dept_scope != "own")) {
                send_json(res, 403, R"({"error":"Stufe kann nicht selbst geändert werden"})");
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
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || perm->mail_templates_scope == "none")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto templates = db.list_mail_templates();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
        {
            // own_dept: only templates for user's own department
            if (perm->mail_templates_scope == "own_dept")
            {
                if (!current_user->department || t.department != *current_user->department)
                    continue;
            }
            arr.push_back(template_to_json(t));
        }
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
        if (!perm || perm->mail_templates_scope == "none")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (perm->mail_templates_scope == "own_dept")
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

    std::string department = url_decode(std::string{req->getParameter(0)});

    // Permission check: mail_templates_scope controls access
    auto current_user = resolve_user(db, claims);
    if (current_user)
    {
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || perm->mail_templates_scope == "none")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        if (perm->mail_templates_scope == "own_dept")
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

    // Permission check: mail_send_scope
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm_check = db.get_role_permission(current_user->role);
    if (!perm_check || perm_check->mail_send_scope == "none")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

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
                    db.delete_mail_draft(activity_id);
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
    if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity, claims))
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
    if (!perm || perm->mail_send_scope == "none")
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

    nlohmann::json j = {
        {"id", draft->id},
        {"activity_id", draft->activity_id},
        {"recipients", recip_arr},
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
    if (!perm || perm->mail_send_scope == "none")
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

        std::string subject = j.value("subject", "");
        std::string body_html = j.value("body_html", "");

        auto draft = db.upsert_mail_draft(activity_id, recipients, subject, body_html, user_id);
        if (!draft)
        {
            send_json(res, 500, R"({"error":"Entwurf konnte nicht gespeichert werden"})");
            return;
        }

        nlohmann::json recip_arr = nlohmann::json::array();
        for (auto &r : draft->recipients)
            recip_arr.push_back(r);

        nlohmann::json out = {
            {"id", draft->id},
            {"activity_id", draft->activity_id},
            {"recipients", recip_arr},
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
    if (!perm || perm->mail_send_scope == "none")
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
        {"user_dept_scope", rp.user_dept_scope},
        {"user_role_scope", rp.user_role_scope},
        {"locations_manage_scope", rp.locations_manage_scope}};
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
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
    auto perm = db.get_role_permission(user->role);
    if (!perm || perm->user_role_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
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
        try {
            auto d = db.create_department(name, color);
            if (!d) { send_json(res, 409, R"({"error":"Abteilung existiert bereits"})"); return; }
            send_json(res, 201, nlohmann::json{{"name", d->name}, {"color", d->color}}.dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        try {
            auto d = db.update_department(name, new_name, color);
            if (!d) { send_json(res, 404, R"({"error":"Abteilung nicht gefunden"})"); return; }
            send_json(res, 200, nlohmann::json{{"name", d->name}, {"color", d->color}}.dump());
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// DELETE /admin/departments/:name
void handle_delete_department(HttpRes *res, HttpReq *req, Database &db)
{
    std::string name = url_decode(std::string{req->getParameter("name")});
    if (!require_admin(res, req, db))
        return;

    if (name == "Allgemein")
    {
        send_json(res, 403, R"({"error":"Die Standard-Stufe kann nicht gelöscht werden"})");
        return;
    }

    res->onAborted([] {});
    res->onData([res, &db, name](std::string_view chunk, bool last) mutable
                {
        static thread_local std::string body;
        body.append(chunk);
        if (!last) return;
        std::string buf = std::move(body);
        body.clear();

        auto j = nlohmann::json::parse(buf, nullptr, false);
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
            send_json(res, 200, R"({"ok":true})");
        }
        catch (std::exception &e)
        {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// DELETE /admin/roles/:name
void handle_delete_role(HttpRes *res, HttpReq *req, Database &db)
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
    res->onData([res, buf, &db, name](std::string_view chunk, bool last)
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
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        std::string user_dept_scope = j.value("user_dept_scope", "none");
        std::string user_role_scope = j.value("user_role_scope", "none");
        std::string locations_manage_scope = j.value("locations_manage_scope", "none");

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
                                                user_dept_scope, user_role_scope, locations_manage_scope);
            if (!ok) {
                send_json(res, 404, R"({"error":"Rolle nicht gefunden"})");
                return;
            }
            send_json(res, 200, R"({"ok":true})");
        } catch (std::exception &e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
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
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        } });
}

// ── Forms ─────────────────────────────────────────────────────────────────────
//
// Permission model: reuses activity-based dept access.
//  - Read form / responses / stats: can_read_dept on activity's department
//  - Create / update / delete form / responses: can_write_dept on activity's department
//  - Templates: can_write_all_depts (admin) or own department write access

// Helper: check form_scope-based read access for an activity
static bool can_form_access(Database &db, const RolePermission &perm, const UserRecord &user,
                            const TokenClaims &claims, const std::string &activity_id, bool /*write*/)
{
    if (perm.form_scope == "none")
        return false;
    auto act = db.get_activity_by_id(activity_id);
    if (!act)
        return false;
    if (perm.form_scope == "all")
        return true;
    if (perm.form_scope == "same_dept" && act->department && user.department && *act->department == *user.department)
        return true;
    if (perm.form_scope == "own" && is_activity_responsible(*act, user, claims))
        return true;
    return false;
}

// Helper: resolve activity department from activity_id
static std::optional<std::string> get_activity_dept(Database &db, const std::string &activity_id)
{
    auto act = db.get_activity_by_id(activity_id);
    if (!act)
        return std::nullopt;
    return act->department;
}

// Helper: parse questions array from JSON
static std::vector<FormQuestion> parse_questions(const nlohmann::json &arr)
{
    std::vector<FormQuestion> questions;
    if (!arr.is_array())
        return questions;
    int pos = 0;
    for (auto &q : arr)
    {
        FormQuestion fq;
        fq.question_text = q.value("question_text", "");
        fq.question_type = q.value("question_type", "text_input");
        fq.position = q.contains("position") ? q["position"].get<int>() : pos;
        fq.is_required = q.value("is_required", true);
        fq.metadata = q.contains("metadata") ? q["metadata"] : nlohmann::json::object();
        questions.push_back(fq);
        ++pos;
    }
    return questions;
}

// ---- GET /activities/:id/form -----------------------------------------------

void handle_get_activity_form(HttpRes *res, HttpReq *req, Database &db)
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
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }
        send_json(res, 200, signup_form_to_json(*form).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /activities/:id/form ----------------------------------------------

void handle_post_activity_form(HttpRes *res, HttpReq *req, Database &db)
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

    std::string activity_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    // Check: exactly one form per activity
    auto existing = db.get_form_for_activity(activity_id);
    if (existing)
    {
        send_json(res, 409, R"({"error":"Formular existiert bereits f\u00fcr diese Aktivit\u00e4t"})");
        return;
    }

    std::string user_id = current_user->id;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, user_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }
        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        if (title.empty()) { send_json(res, 400, R"({"error":"Titel erforderlich"})"); return; }
        auto questions = parse_questions(j.value("questions", nlohmann::json::array()));
        try {
            auto form = db.create_form(activity_id, form_type, title, user_id, questions);
            if (!form) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, signup_form_to_json(*form).dump());
        } catch (std::exception &e) { send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump()); } });
}

// ---- PUT /activities/:id/form -----------------------------------------------

void handle_put_activity_form(HttpRes *res, HttpReq *req, Database &db)
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

    std::string activity_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, activity_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }
        std::string form_type = j.value("form_type", "registration");
        std::string title = j.value("title", "");
        if (title.empty()) { send_json(res, 400, R"({"error":"Titel erforderlich"})"); return; }
        auto questions = parse_questions(j.value("questions", nlohmann::json::array()));
        try {
            auto form = db.update_form(activity_id, form_type, title, questions);
            if (!form) { send_json(res, 404, R"({"error":"Formular nicht gefunden"})"); return; }
            send_json(res, 200, signup_form_to_json(*form).dump());
        } catch (std::exception &e) { send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump()); } });
}

// ---- DELETE /activities/:id/form --------------------------------------------

void handle_delete_activity_form(HttpRes *res, HttpReq *req, Database &db)
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
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_form(activity_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Formular nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/form/responses -------------------------------------

void handle_get_form_responses(HttpRes *res, HttpReq *req, Database &db)
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
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }

        auto responses = db.list_responses(form->id);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &r : responses)
            arr.push_back(form_response_to_json(r));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/form/responses/:rid --------------------------------

void handle_get_form_response(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    std::string response_id{req->getParameter(1)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, false))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto response = db.get_response(response_id);
        if (!response)
        {
            send_json(res, 404, R"({"error":"Antwort nicht gefunden"})");
            return;
        }
        send_json(res, 200, form_response_to_json(*response, true).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- DELETE /activities/:id/form/responses/:rid -----------------------------

void handle_delete_form_response(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string activity_id{req->getParameter(0)};
    std::string response_id{req->getParameter(1)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || !can_form_access(db, *perm, *current_user, claims, activity_id, true))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool ok = db.delete_response(response_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Antwort nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/form/stats -----------------------------------------

void handle_get_form_stats(HttpRes *res, HttpReq *req, Database &db)
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
        auto perm = db.get_role_permission(current_user->role);
        auto dept_access = db.list_role_dept_access(current_user->role);

        auto act_dept = get_activity_dept(db, activity_id);
        bool allowed = false;
        if (perm)
        {
            if (act_dept)
                allowed = can_read_dept(*perm, *current_user, dept_access, *act_dept);
            else
                allowed = perm->can_read_all_depts || perm->can_read_own_dept;
        }
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto form = db.get_form_for_activity(activity_id);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Kein Formular gefunden"})");
            return;
        }

        auto stats = db.get_form_stats(form->id);
        stats["form_type"] = form->form_type;
        send_json(res, 200, stats.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /forms/:slug (public, no auth) --------------------------------

// ---- Template variable resolution -------------------------------------------

static std::string format_date_long_de(const std::string &iso_date)
{
    // "2026-04-15" → "Mittwoch, 15. April 2026"
    if (iso_date.size() < 10)
        return iso_date;
    struct tm t{};
    t.tm_year = std::stoi(iso_date.substr(0, 4)) - 1900;
    t.tm_mon = std::stoi(iso_date.substr(5, 2)) - 1;
    t.tm_mday = std::stoi(iso_date.substr(8, 2));
    std::mktime(&t);
    static const char *wdays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    static const char *months[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
    std::string r = wdays[t.tm_wday];
    r += ", ";
    r += std::to_string(t.tm_mday);
    r += ". ";
    r += months[t.tm_mon];
    r += " ";
    r += iso_date.substr(0, 4);
    return r;
}

static std::string format_date_short_de(const std::string &iso_date)
{
    // "2026-04-15" → "15.04.2026"
    if (iso_date.size() < 10)
        return iso_date;
    return iso_date.substr(8, 2) + "." + iso_date.substr(5, 2) + "." + iso_date.substr(0, 4);
}

static std::string replace_template_vars(const std::string &text, const Activity &act)
{
    if (text.find("{{") == std::string::npos)
        return text;
    std::map<std::string, std::string> vars;
    vars["titel"] = act.title;
    vars["datum"] = format_date_long_de(act.date);
    vars["datum_kurz"] = format_date_short_de(act.date);
    vars["startzeit"] = act.start_time;
    vars["endzeit"] = act.end_time;
    vars["ort"] = act.location;
    {
        std::string r;
        for (size_t i = 0; i < act.responsible.size(); ++i)
        {
            if (i)
                r += ", ";
            r += act.responsible[i];
        }
        vars["verantwortlich"] = r;
    }
    vars["abteilung"] = act.department.value_or("—");
    vars["ziel"] = act.goal;
    {
        std::string m;
        for (size_t i = 0; i < act.material.size(); ++i)
        {
            if (i)
                m += ", ";
            m += act.material[i].name;
        }
        vars["material"] = m.empty() ? "—" : m;
    }
    vars["schlechtwetter"] = act.bad_weather_info.value_or("—");
    {
        std::string p;
        for (auto &pr : act.programs)
        {
            if (!p.empty())
                p += "\n";
            p += (!pr.time.empty() ? pr.time + " min" : std::string("—"));
            p += " – " + pr.title;
            if (!pr.responsible.empty())
            {
                std::string rj;
                for (size_t ri = 0; ri < pr.responsible.size(); ++ri)
                {
                    if (ri)
                        rj += ", ";
                    rj += pr.responsible[ri];
                }
                p += " (" + rj + ")";
            }
            if (!pr.description.empty())
                p += ": " + pr.description;
        }
        vars["programm"] = p.empty() ? "—" : p;
    }

    std::string result = text;
    for (auto &[key, val] : vars)
    {
        std::string placeholder = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos)
        {
            result.replace(pos, placeholder.size(), val);
            pos += val.size();
        }
    }
    return result;
}

void handle_get_public_form(HttpRes *res, HttpReq *req, Database &db)
{
    set_cors(res);
    std::string public_slug{req->getParameter(0)};
    try
    {
        auto form = db.get_form_for_public_slug(public_slug);
        if (!form)
        {
            send_json(res, 404, R"({"error":"Formular nicht gefunden"})");
            return;
        }

        // Resolve template variables in question texts using activity data
        auto act = db.get_activity_by_id(form->activity_id);
        if (act)
        {
            form->title = replace_template_vars(form->title, *act);
            for (auto &q : form->questions)
            {
                q.question_text = replace_template_vars(q.question_text, *act);
                // Also resolve subtitle in section metadata
                if (q.metadata.contains("subtitle") && q.metadata["subtitle"].is_string())
                {
                    q.metadata["subtitle"] = replace_template_vars(q.metadata["subtitle"].get<std::string>(), *act);
                }
                // Resolve choice labels
                if (q.metadata.contains("choices") && q.metadata["choices"].is_array())
                {
                    for (auto &c : q.metadata["choices"])
                    {
                        if (c.contains("label") && c["label"].is_string())
                        {
                            c["label"] = replace_template_vars(c["label"].get<std::string>(), *act);
                        }
                    }
                }
            }
        }

        send_json(res, 200, signup_form_to_json(*form).dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /forms/:slug/submit (public, no auth) -----------------------

void handle_post_form_submit(HttpRes *res, HttpReq *req, Database &db)
{
    set_cors(res);
    std::string public_slug{req->getParameter(0)};

    std::string user_agent{req->getHeader("user-agent")};
    // IP is not available directly via uWS in this context; pass empty
    std::string ip_address{};

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, public_slug, user_agent, ip_address, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        try {
            auto form = db.get_form_for_public_slug(public_slug);
            if (!form) { send_json(res, 404, R"({"error":"Formular nicht gefunden"})"); return; }

            // Parse answers: array of { question_id, answer_value }
            std::vector<std::pair<std::string, std::string>> answers;
            if (j.contains("answers") && j["answers"].is_array())
            {
                for (auto &a : j["answers"])
                {
                    std::string qid = a.value("question_id", "");
                    std::string val = a.value("answer_value", "");
                    if (!qid.empty())
                        answers.emplace_back(qid, val);
                }
            }

            // Validate required questions
            for (const auto &q : form->questions)
            {
                if (q.question_type == "section" || !q.is_required) continue;
                bool answered = false;
                for (auto &[qid, val] : answers)
                    if (qid == q.id && !val.empty()) { answered = true; break; }
                if (!answered)
                {
                    send_json(res, 400, nlohmann::json{{"error", "Pflichtfeld fehlt: " + q.question_text}}.dump());
                    return;
                }
            }

            auto resp = db.submit_response(form->id, form->form_type, user_agent, ip_address, answers);
            if (!resp) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, form_response_to_json(*resp, true).dump());
        } catch (std::exception &e) { send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump()); } });
}

// ---- GET /form-templates?department=... -------------------------------------

void handle_get_form_templates(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string department = url_decode(std::string{req->getQuery("department")});
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto templates = db.list_form_templates(department);
        nlohmann::json arr = nlohmann::json::array();
        for (auto &t : templates)
            arr.push_back(form_template_to_json(t));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /form-templates ---------------------------------------------------

void handle_post_form_template(HttpRes *res, HttpReq *req, Database &db)
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
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || perm->form_templates_scope == "none")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    std::string user_id = current_user->id;
    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, user_id, current_user, perm, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        std::string name = j.value("name", "");
        std::string department = j.value("department", "");
        std::string form_type = j.value("form_type", "registration");
        nlohmann::json config = j.contains("template_config") ? j["template_config"] : nlohmann::json::array();
        bool is_default = j.value("is_default", false);

        if (name.empty() || department.empty()) {
            send_json(res, 400, R"({"error":"name und department erforderlich"})");
            return;
        }
        // Scope check: own_dept only allows own department
        if (perm->form_templates_scope == "own_dept")
        {
            if (!current_user->department || *current_user->department != department) {
                send_json(res, 403, R"({"error":"Keine Berechtigung f\u00fcr dieses Department"})");
                return;
            }
        }
        try {
            auto tpl = db.create_form_template(name, department, form_type, config, user_id, is_default);
            if (!tpl) { send_json(res, 500, R"({"error":"Datenbankfehler"})"); return; }
            send_json(res, 201, form_template_to_json(*tpl).dump());
        } catch (std::exception &e) { send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump()); } });
}

// ---- PUT /form-templates/:id ------------------------------------------------

void handle_put_form_template(HttpRes *res, HttpReq *req, Database &db)
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

    std::string tpl_id{req->getParameter(0)};

    auto current_user = resolve_user(db, claims);
    if (!current_user)
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }
    auto perm = db.get_role_permission(current_user->role);
    if (!perm || perm->form_templates_scope == "none")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return;
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, tpl_id, &db](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) { send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON"})"); return; }

        std::string name = j.value("name", "");
        std::string form_type = j.value("form_type", "registration");
        nlohmann::json config = j.contains("template_config") ? j["template_config"] : nlohmann::json::array();
        bool is_default = j.value("is_default", false);

        try {
            auto tpl = db.update_form_template(tpl_id, name, form_type, config, is_default);
            if (!tpl) { send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})"); return; }
            send_json(res, 200, form_template_to_json(*tpl).dump());
        } catch (std::exception &e) { send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump()); } });
}

// ---- DELETE /form-templates/:id ---------------------------------------------

void handle_delete_form_template(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string tpl_id{req->getParameter(0)};
    try
    {
        auto current_user = resolve_user(db, claims);
        if (!current_user)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        auto perm = db.get_role_permission(current_user->role);
        if (!perm || perm->form_templates_scope == "none")
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        bool ok = db.delete_form_template(tpl_id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Vorlage nicht gefunden"})");
            return;
        }
        send_json(res, 200, R"({"ok":true})");
    }
    catch (std::exception &e)
    {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}
