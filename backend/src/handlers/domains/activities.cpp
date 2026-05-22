#include "handlers/internal/shared.hpp"

// ---- GET /departments -------------------------------------------------------

void handle_get_departments(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        const std::string ckey = cache_key("departments", "all");
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }

        auto depts = db.list_departments();
        nlohmann::json arr = nlohmann::json::array();
        for (auto &d : depts)
            arr.push_back({{"name", d.name},
                           {"color", d.color},
                           {"midata_group_id", d.midata_group_id ? nlohmann::json(*d.midata_group_id) : nlohmann::json(nullptr)}});
        std::string body = arr.dump();
        redis_cache().setex(ckey, response_cache_ttl_seconds(), body);
        send_json(res, 200, body);
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
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
        const std::string ckey = cache_key("activities", cache_user_scope(claims, current_user));
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }

        auto activities = db.list_activities();

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
                if (!can_read_activity(*perm, *current_user, dept_access, a))
                    continue;
            }
            arr.push_back(to_json(a));
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
        const std::string ckey = cache_key("activity", cache_user_scope(claims, current_user) + ":" + id);
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }

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
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        std::string body = to_json(*activity).dump();
        redis_cache().setex(ckey, response_cache_ttl_seconds(), body);
        send_json(res, 200, body);
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_activity_midata_children_count(HttpRes *res, HttpReq *req, Database &db)
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
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        if (shared_should_use_frozen_values(*activity))
        {
            auto frozen = db.get_activity_midata_children_value(id);
            if (frozen)
            {
                send_json(res, 200, nlohmann::json{{"configured", true}, {"children_count", *frozen}, {"mode", "frozen"}}.dump());
                return;
            }
            send_json(res, 200, nlohmann::json{{"configured", false}, {"children_count", nullptr}, {"mode", "frozen"}}.dump());
            return;
        }

        if (!activity->department)
        {
            send_json(res, 200, nlohmann::json{{"configured", false}, {"children_count", nullptr}}.dump());
            return;
        }

        auto departments = db.list_departments();
        std::optional<std::string> group_id = std::nullopt;
        for (const auto &dept : departments)
        {
            if (dept.name == *activity->department)
            {
                group_id = dept.midata_group_id;
                break;
            }
        }

        if (!group_id || group_id->empty())
        {
            send_json(res, 200, nlohmann::json{{"configured", false}, {"children_count", nullptr}}.dump());
            return;
        }

        std::string midata_error;
        auto count = shared_fetch_midata_children_count_cached(db, *group_id, midata_error);
        if (!count)
        {
            bool configured = midata_error != "not-configured";
            send_json(res, 200, nlohmann::json{{"configured", configured}, {"children_count", nullptr}, {"error", midata_error}}.dump());
            return;
        }

        db.set_activity_midata_children_value(id, *count);

        send_json(res, 200, nlohmann::json{{"configured", true}, {"children_count", *count}, {"mode", "live"}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_midata_children_counts(HttpRes *res, HttpReq *req, Database &db)
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
        auto dept_access = db.list_role_dept_access(current_user->role);
        if (!perm)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto departments = db.list_departments();
        nlohmann::json by_department = nlohmann::json::object();
        for (const auto &dept : departments)
        {
            if (!can_read_dept(*perm, *current_user, dept_access, dept.name))
                continue;

            if (!dept.midata_group_id || dept.midata_group_id->empty())
            {
                by_department[dept.name] = nlohmann::json{{"configured", false}, {"children_count", nullptr}};
                continue;
            }

            std::string midata_error;
            auto count = shared_fetch_midata_children_count_cached(db, *dept.midata_group_id, midata_error);
            if (!count)
            {
                bool configured = midata_error != "not-configured";
                by_department[dept.name] = nlohmann::json{{"configured", configured}, {"children_count", nullptr}, {"error", midata_error}};
                continue;
            }

            by_department[dept.name] = nlohmann::json{{"configured", true}, {"children_count", *count}};
        }

        send_json(res, 200, nlohmann::json{{"departments", by_department}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_activity_weather_location(HttpRes *res, HttpReq *req, Database &db)
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
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        auto location = db.get_activity_weather_location(id);
        send_json(res, 200, nlohmann::json{{"location", location ? nlohmann::json(*location) : nlohmann::json(nullptr)}}.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_put_activity_weather_location(HttpRes *res, HttpReq *req, Database &db)
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

        auto perm = current_user ? db.get_role_permission(current_user->role) : std::optional<RolePermission>{};
        if (!current_user || !perm || !can_edit_activity(*perm, *current_user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto buf = std::make_shared<std::string>();
        res->onAborted([] {});
        res->onData([res, buf, &db, id](std::string_view chunk, bool last) mutable
                    {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;
        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            send_json(res, 400, R"({"error":"Ungueltiges JSON"})");
            return;
        }

        std::string location = trim_ascii(j.value("location", ""));
        if (location.empty()) {
            send_json(res, 400, R"({"error":"location-required"})");
            return;
        }

        if (!db.set_activity_weather_location(id, location)) {
            send_json(res, 500, R"({"error":"Konnte nicht speichern"})");
            return;
        }

        send_json(res, 200, nlohmann::json{{"location", location}}.dump()); });
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_activity_expected_weather(HttpRes *res, HttpReq *req, Database &db)
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
            if (!perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
            {
                send_json(res, 403, R"({"error":"Keine Berechtigung"})");
                return;
            }
        }

        bool frozen_snapshot_missing = false;
        if (shared_should_use_frozen_values(*activity))
        {
            auto frozen = db.get_activity_weather_snapshot(id);
            if (frozen)
            {
                auto payload = *frozen;
                payload["mode"] = "frozen";
                send_json(res, 200, payload.dump());
                return;
            }

            // Legacy activities can miss frozen snapshots. Continue with fallback computation.
            frozen_snapshot_missing = true;
        }

        std::string location_input = trim_ascii(url_decode(std::string{req->getQuery("location")}));
        if (location_input.empty())
        {
            auto saved_location = db.get_activity_weather_location(id);
            if (saved_location)
                location_input = trim_ascii(*saved_location);
        }
        if (location_input.empty())
        {
            location_input = trim_ascii(activity->location);
        }
        if (location_input.empty())
        {
            send_json(res, 200, nlohmann::json{{"available", false}, {"error", "location-missing"}}.dump());
            return;
        }

        std::string weather_error;
        auto weather = shared_fetch_expected_weather_for_activity(*activity, location_input, weather_error);
        if (!weather)
        {
            send_json(res, 200, nlohmann::json{{"available", false}, {"error", weather_error}}.dump());
            return;
        }

        if (frozen_snapshot_missing)
        {
            if (weather->note.empty())
                weather->note = "Kein gespeicherter Ist-Wert vorhanden; Ersatzwert berechnet.";
            else
                weather->note += " Kein gespeicherter Ist-Wert vorhanden; Ersatzwert berechnet.";
        }

        nlohmann::json payload = {
            {"available", true},
            {"mode", weather->mode},
            {"temperature_c", weather->temperature_c},
            {"temperature_min_c", weather->temperature_min_c ? nlohmann::json(*weather->temperature_min_c) : nlohmann::json(nullptr)},
            {"temperature_max_c", weather->temperature_max_c ? nlohmann::json(*weather->temperature_max_c) : nlohmann::json(nullptr)},
            {"hourly_temps", nlohmann::json::array()},
            {"hourly_rain_probability", nlohmann::json::array()},
            {"rain_probability_percent", weather->rain_probability_percent ? nlohmann::json(*weather->rain_probability_percent) : nlohmann::json(nullptr)},
            {"weather_symbol", weather->weather_symbol.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->weather_symbol)},
            {"season", weather->season.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->season)},
            {"point_name", weather->point_name.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->point_name)},
            {"postal_code", weather->postal_code.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->postal_code)},
            {"source", weather->source},
            {"note", weather->note.empty() ? nlohmann::json(nullptr) : nlohmann::json(weather->note)}};
        for (const auto &p : weather->hourly_temps)
            payload["hourly_temps"].push_back(nlohmann::json{{"ts_unix", p.first}, {"temperature_c", p.second}});
        for (const auto &p : weather->hourly_rain_probability)
            payload["hourly_rain_probability"].push_back(nlohmann::json{{"ts_unix", p.first}, {"probability_percent", p.second}});
        db.set_activity_weather_snapshot(id, payload);
        send_json(res, 200, payload.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
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
        const std::string ckey = cache_key("locations", "public");
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }

        auto locs = db.get_predefined_locations();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : locs)
            arr.push_back(l);
        std::string body = arr.dump();
        redis_cache().setex(ckey, response_cache_ttl_seconds(), body);
        send_json(res, 200, body);
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
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
        const std::string ckey = cache_key("locations_admin", "all");
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }

        auto locs = db.list_predefined_locations();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &l : locs)
            arr.push_back(location_to_json(l));
        std::string body = arr.dump();
        redis_cache().setex(ckey, response_cache_ttl_seconds(), body);
        send_json(res, 200, body);
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
            cache_bump_version("locations");
            cache_bump_version("locations_admin");
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
            cache_bump_version("locations");
            cache_bump_version("locations_admin");
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
        cache_bump_version("locations");
        cache_bump_version("locations_admin");
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
            if (!activity || !perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
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
        send_internal_error(res, "handler", e);
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
    if (!perm || !can_edit_activity(*perm, *current_user, *activity, claims.email))
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
            send_internal_error(res, "handler", e);
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
        if (!activity || !perm || !can_read_activity(*perm, *current_user, dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        set_cors(res);
        res->writeStatus("200 OK");
        res->writeHeader("Content-Type", ad->content_type);
        // Sanitize filename: strip quotes, backslashes, and control chars to prevent header injection
        std::string safe_name;
        for (char c : ad->filename)
        {
            if (c == '"' || c == '\\' || c == '\r' || c == '\n' || static_cast<unsigned char>(c) < 0x20)
                continue;
            safe_name += c;
        }
        if (safe_name.empty())
            safe_name = "attachment";
        std::string disp = "inline; filename=\"" + safe_name + "\"";
        res->writeHeader("Content-Disposition", disp);
        res->end(std::string_view(
            reinterpret_cast<const char *>(ad->data.data()), ad->data.size()));
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
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
        if (!activity || !perm || !can_edit_activity(*perm, *current_user, *activity, claims.email))
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
        send_internal_error(res, "handler", e);
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
        std::string notification_graph_token = j.value("notification_access_token", "");
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

            if (current_user) {
                auto all_assigned = shared_unique_names_from_material(activity->material);
                auto users = db.list_users();
                for (const auto &u : users) {
                    if (u.id == current_user->id || !u.notify_material_assigned)
                        continue;
                    if (!shared_user_in_assignee_list(all_assigned, u))
                        continue;

                    std::string time_range = activity->start_time + "-" + activity->end_time;
                    std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                    std::string full_link = shared_activity_absolute_link(db, activity->id);
                    std::string recipients_text = shared_join_display(all_assigned);
                    nlohmann::json payload = {
                        {"activity_id", activity->id},
                        {"activity_title", activity->title},
                        {"activity_date", activity->date},
                        {"activity_date_display", date_short},
                        {"activity_time", time_range},
                        {"location", activity->location},
                        {"assigned_users", all_assigned},
                        {"recipients", all_assigned},
                        {"notification_recipient_name", u.display_name},
                        {"notification_recipient_email", u.email},
                        {"triggered_by_name", current_user->display_name},
                        {"triggered_by_email", current_user->email},
                        {"activity_url", full_link}
                    };
                    auto note = db.create_notification(
                        u.id,
                        "material_assigned",
                        "Material dir zugewiesen",
                        "Neue Materialverantwortung in \"" + activity->title + "\" am " + date_short + ". Empfänger: " + recipients_text,
                        full_link,
                        payload
                    );
                    if (note) {
                        if (u.notify_channel_websocket) {
                            nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                            wm.send_to_user_ids({u.id}, ws_msg.dump());
                            shared_deliver_web_push_for_user(db, u, *note);
                        }
                        if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                            std::string mail_subject = "[DPWeb] Material dir zugewiesen: " + activity->title;
                            std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dir Material zugewiesen.</p>"
                                "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                "<strong>Ort:</strong> " + activity->location + "<br/>"
                                "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                            db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                        }
                    }
                }

                // Notify users added as responsible to the activity
                if (!activity->responsible.empty()) {
                    for (const auto &u : users) {
                        if (u.id == current_user->id || !u.notify_activity_assigned)
                            continue;
                        if (!shared_user_in_assignee_list(activity->responsible, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                        std::string full_link = shared_activity_absolute_link(db, activity->id);
                        std::string recipients_text = shared_join_display(activity->responsible);
                        nlohmann::json payload = {
                            {"activity_id", activity->id},
                            {"activity_title", activity->title},
                            {"activity_date", activity->date},
                            {"activity_date_display", date_short},
                            {"activity_time", time_range},
                            {"location", activity->location},
                            {"assigned_users", activity->responsible},
                            {"notification_recipient_name", u.display_name},
                            {"notification_recipient_email", u.email},
                            {"triggered_by_name", current_user->display_name},
                            {"triggered_by_email", current_user->email},
                            {"activity_url", full_link}
                        };
                        auto note = db.create_notification(
                            u.id,
                            "activity_assigned",
                            "Aktivität dir zugewiesen",
                            "Du wurdest zur Aktivität \"" + activity->title + "\" am " + date_short + " hinzugefügt. Verantwortliche: " + recipients_text,
                            full_link,
                            payload
                        );
                        if (note) {
                            if (u.notify_channel_websocket) {
                                nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                wm.send_to_user_ids({u.id}, ws_msg.dump());
                                shared_deliver_web_push_for_user(db, u, *note);
                            }
                            if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                                std::string mail_subject = "[DPWeb] Aktivität dir zugewiesen: " + activity->title;
                                std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dich zur Aktivität hinzugefügt.</p>"
                                    "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                    "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                    "<strong>Ort:</strong> " + activity->location + "<br/>"
                                    "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                    "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                                db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                            }
                        }
                    }
                }

                // Notify users added as responsible to program blocks
                for (const auto &prog : activity->programs) {
                    if (prog.responsible.empty())
                        continue;
                    for (const auto &u : users) {
                        if (u.id == current_user->id || !u.notify_program_assigned)
                            continue;
                        if (!shared_user_in_assignee_list(prog.responsible, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                        std::string full_link = shared_activity_absolute_link(db, activity->id);
                        nlohmann::json payload = {
                            {"activity_id", activity->id},
                            {"activity_title", activity->title},
                            {"activity_date", activity->date},
                            {"activity_date_display", date_short},
                            {"activity_time", time_range},
                            {"location", activity->location},
                            {"program_title", prog.title},
                            {"assigned_users", prog.responsible},
                            {"notification_recipient_name", u.display_name},
                            {"notification_recipient_email", u.email},
                            {"triggered_by_name", current_user->display_name},
                            {"triggered_by_email", current_user->email},
                            {"activity_url", full_link}
                        };
                        auto note = db.create_notification(
                            u.id,
                            "program_assigned",
                            "Programmblock dir zugewiesen",
                            "Du wurdest zum Programmblock \"" + prog.title + "\" in \"" + activity->title + "\" am " + date_short + " hinzugefügt.",
                            full_link,
                            payload
                        );
                        if (note) {
                            if (u.notify_channel_websocket) {
                                nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                wm.send_to_user_ids({u.id}, ws_msg.dump());
                                shared_deliver_web_push_for_user(db, u, *note);
                            }
                            if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                                std::string mail_subject = "[DPWeb] Programmblock dir zugewiesen: " + prog.title;
                                std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dich zum Programmblock hinzugefügt.</p>"
                                    "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                    "<strong>Programmblock:</strong> " + prog.title + "<br/>"
                                    "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                    "<strong>Ort:</strong> " + activity->location + "<br/>"
                                    "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                    "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                                db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                            }
                        }
                    }
                }
            }

            nlohmann::json msg = {{"event", "created"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            cache_bump_version("activities");
            cache_bump_version("activity");
            std::string share_created_by = current_user ? current_user->id : "";
            auto share_link = db.create_share_link(activity->id, share_created_by);

            nlohmann::json response = to_json(*activity);
            response["share_token"] = share_link ? nlohmann::json(share_link->share_token) : nlohmann::json(nullptr);
            send_json(res, 201, response.dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
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
    std::optional<Activity> previous_activity;

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
        bool allowed = perm && can_edit_activity(*perm, *current_user, *activity, claims.email);
        current_department = activity->department;
        previous_activity = *activity;
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, id, &db, &wm, current_user, current_department, previous_activity](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        std::string notification_graph_token = j.value("notification_access_token", "");
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

            if (current_user) {
                auto new_assigned = shared_unique_names_from_material(activity->material);
                auto newly_assigned = shared_newly_assigned_users_from_material_delta(
                    previous_activity ? previous_activity->material : std::vector<MaterialItem>{},
                    activity->material
                );

                if (!newly_assigned.empty()) {
                    auto users = db.list_users();
                    for (const auto &u : users) {
                        if (u.id == current_user->id || !u.notify_material_assigned)
                            continue;
                        if (!shared_user_in_assignee_list(newly_assigned, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                        std::string full_link = shared_activity_absolute_link(db, activity->id);
                        std::string recipients_text = shared_join_display(new_assigned);
                        nlohmann::json payload = {
                            {"activity_id", activity->id},
                            {"activity_title", activity->title},
                            {"activity_date", activity->date},
                            {"activity_date_display", date_short},
                            {"activity_time", time_range},
                            {"location", activity->location},
                            {"assigned_users", new_assigned},
                            {"newly_assigned_users", newly_assigned},
                            {"recipients", new_assigned},
                            {"notification_recipient_name", u.display_name},
                            {"notification_recipient_email", u.email},
                            {"triggered_by_name", current_user->display_name},
                            {"triggered_by_email", current_user->email},
                            {"activity_url", full_link}
                        };

                        auto note = db.create_notification(
                            u.id,
                            "material_assigned",
                            "Material dir zugewiesen",
                            "Neue Materialverantwortung in \"" + activity->title + "\" am " + date_short + ". Empfänger: " + recipients_text,
                            full_link,
                            payload
                        );
                        if (note) {
                            if (u.notify_channel_websocket) {
                                nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                wm.send_to_user_ids({u.id}, ws_msg.dump());
                                shared_deliver_web_push_for_user(db, u, *note);
                            }
                            if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                                std::string mail_subject = "[DPWeb] Material dir zugewiesen: " + activity->title;
                                std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dir Material zugewiesen.</p>"
                                    "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                    "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                    "<strong>Ort:</strong> " + activity->location + "<br/>"
                                    "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                    "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                                db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                            }
                        }
                    }
                }

                // Notify users newly added as responsible to the activity
                {
                    std::vector<std::string> newly_responsible;
                    for (const auto &name : activity->responsible) {
                        if (!previous_activity || !shared_contains_name_ci(previous_activity->responsible, name))
                            newly_responsible.push_back(name);
                    }
                    if (!newly_responsible.empty()) {
                        auto users_list = db.list_users();
                        for (const auto &u : users_list) {
                            if (u.id == current_user->id || !u.notify_activity_assigned)
                                continue;
                            if (!shared_user_in_assignee_list(newly_responsible, u))
                                continue;

                            std::string time_range = activity->start_time + "-" + activity->end_time;
                            std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                            std::string full_link = shared_activity_absolute_link(db, activity->id);
                            std::string recipients_text = shared_join_display(activity->responsible);
                            nlohmann::json payload = {
                                {"activity_id", activity->id},
                                {"activity_title", activity->title},
                                {"activity_date", activity->date},
                                {"activity_date_display", date_short},
                                {"activity_time", time_range},
                                {"location", activity->location},
                                {"assigned_users", activity->responsible},
                                {"notification_recipient_name", u.display_name},
                                {"notification_recipient_email", u.email},
                                {"triggered_by_name", current_user->display_name},
                                {"triggered_by_email", current_user->email},
                                {"activity_url", full_link}
                            };
                            auto note = db.create_notification(
                                u.id,
                                "activity_assigned",
                                "Aktivität dir zugewiesen",
                                "Du wurdest zur Aktivität \"" + activity->title + "\" am " + date_short + " hinzugefügt. Verantwortliche: " + recipients_text,
                                full_link,
                                payload
                            );
                            if (note) {
                                if (u.notify_channel_websocket) {
                                    nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                    wm.send_to_user_ids({u.id}, ws_msg.dump());
                                    shared_deliver_web_push_for_user(db, u, *note);
                                }
                                if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                                    std::string mail_subject = "[DPWeb] Aktivität dir zugewiesen: " + activity->title;
                                    std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dich zur Aktivität hinzugefügt.</p>"
                                        "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                        "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                        "<strong>Ort:</strong> " + activity->location + "<br/>"
                                        "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                        "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                                    db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                                }
                            }
                        }
                    }
                }

                // Notify users newly added as responsible to program blocks
                {
                    // Build set of old (program_title, assignee) pairs
                    std::unordered_set<std::string> old_prog_pairs;
                    if (previous_activity) {
                        for (const auto &p : previous_activity->programs) {
                            std::string prog_key = to_lower_ascii(trim_ascii(p.title));
                            for (const auto &name : p.responsible)
                                old_prog_pairs.insert(prog_key + "\n" + shared_normalize_assignee_key(name));
                        }
                    }

                    auto users_list = db.list_users();
                    for (const auto &prog : activity->programs) {
                        if (prog.responsible.empty())
                            continue;
                        std::string prog_key = to_lower_ascii(trim_ascii(prog.title));
                        for (const auto &name : prog.responsible) {
                            std::string pair_key = prog_key + "\n" + shared_normalize_assignee_key(name);
                            if (old_prog_pairs.find(pair_key) != old_prog_pairs.end())
                                continue;

                            // This is a new assignment — find matching user
                            for (const auto &u : users_list) {
                                if (u.id == current_user->id || !u.notify_program_assigned)
                                    continue;
                                if (!shared_user_matches_assignee(u, name))
                                    continue;

                                std::string time_range = activity->start_time + "-" + activity->end_time;
                                std::string date_short = shared_format_date_ddmmyyyy(activity->date);
                                std::string full_link = shared_activity_absolute_link(db, activity->id);
                                nlohmann::json payload = {
                                    {"activity_id", activity->id},
                                    {"activity_title", activity->title},
                                    {"activity_date", activity->date},
                                    {"activity_date_display", date_short},
                                    {"activity_time", time_range},
                                    {"location", activity->location},
                                    {"program_title", prog.title},
                                    {"assigned_users", prog.responsible},
                                    {"notification_recipient_name", u.display_name},
                                    {"notification_recipient_email", u.email},
                                    {"triggered_by_name", current_user->display_name},
                                    {"triggered_by_email", current_user->email},
                                    {"activity_url", full_link}
                                };
                                auto note = db.create_notification(
                                    u.id,
                                    "program_assigned",
                                    "Programmblock dir zugewiesen",
                                    "Du wurdest zum Programmblock \"" + prog.title + "\" in \"" + activity->title + "\" am " + date_short + " hinzugefügt.",
                                    full_link,
                                    payload
                                );
                                if (note) {
                                    if (u.notify_channel_websocket) {
                                        nlohmann::json ws_msg = {{"event", "notification"}, {"notification", notification_to_json(*note)}};
                                        wm.send_to_user_ids({u.id}, ws_msg.dump());
                                        shared_deliver_web_push_for_user(db, u, *note);
                                    }
                                    if (u.notify_channel_email && !notification_graph_token.empty() && !u.email.empty()) {
                                        std::string mail_subject = "[DPWeb] Programmblock dir zugewiesen: " + prog.title;
                                        std::string mail_body = "<p><strong>" + current_user->display_name + "</strong> hat dich zum Programmblock hinzugefügt.</p>"
                                            "<p><strong>Aktivität:</strong> <a href=\"" + full_link + "\">" + activity->title + "</a><br/>"
                                            "<strong>Programmblock:</strong> " + prog.title + "<br/>"
                                            "<strong>Datum:</strong> " + date_short + " " + time_range + "<br/>"
                                            "<strong>Ort:</strong> " + activity->location + "<br/>"
                                            "<strong>Ausgelöst von:</strong> " + current_user->display_name + " (" + current_user->email + ")</p>"
                                            "<p>Weitere Details findest du direkt in der Aktivität.</p>";
                                        db.send_mail(notification_graph_token, current_user->email, {u.email}, {}, mail_subject, mail_body);
                                    }
                                }
                                break; // one notification per user per program block
                            }
                        }
                    }
                }
            }

            nlohmann::json msg = {{"event", "updated"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            cache_bump_version("activities");
            cache_bump_version("activity");
            send_json(res, 200, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_internal_error(res, "handler", e);
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
        bool allowed = perm && can_edit_activity(*perm, *current_user, *activity, claims.email);
        if (!allowed)
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
    }

    try
    {
        bool ok = db.soft_delete_activity(id, current_user->id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        nlohmann::json msg = {{"event", "deleted"}, {"id", id}};
        wm.broadcast(msg.dump());
        cache_bump_version("activities");
        cache_bump_version("activity");
        set_cors(res);
        res->writeStatus("204 No Content");
        res->end();
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
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

nlohmann::json notification_to_json(const NotificationRecord &n)
{
    nlohmann::json j = {
        {"id", n.id},
        {"user_id", n.user_id},
        {"category", n.category},
        {"title", n.title},
        {"message", n.message},
        {"payload", n.payload},
        {"is_read", n.is_read},
        {"created_at", n.created_at},
    };
    j["link"] = n.link ? nlohmann::json(*n.link) : nlohmann::json(nullptr);
    return j;
}

