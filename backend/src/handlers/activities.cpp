#include "handlers/handler_deps.hpp"
#include "infra/audit.hpp"
#include "handlers/program_images.hpp"
#include "utils/program_images.hpp"
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>

static std::string activity_read_cache_scope(const TokenClaims &claims, const ActivityAccess &access)
{
    std::string scope = cache_user_scope(claims, access.user) + ":read:" + access.permission.activity_read_scope;
    // Older cache entries do not contain all fields in the current activity JSON.
    scope += ":activity-json-v2";
    std::vector<std::string> readable_departments;
    for (const auto &entry : access.dept_access)
    {
        if (entry.can_read)
            readable_departments.push_back(entry.department);
    }
    std::sort(readable_departments.begin(), readable_departments.end());
    for (const auto &department : readable_departments)
        scope += ":" + std::to_string(department.size()) + ":" + department;
    return scope;
}

static bool validate_guiding_thread(HttpRes *res, const nlohmann::json &j)
{
    if (j.contains("guiding_thread") && !j["guiding_thread"].is_string() && !j["guiding_thread"].is_null())
    {
        send_json(res, 400, R"({"error":"Roter Faden muss Text oder null sein"})");
        return false;
    }
    return true;
}

static size_t utf8_character_count(const std::string &value)
{
    size_t count = 0;
    for (unsigned char byte : value)
        if ((byte & 0xc0) != 0x80)
            ++count;
    return count;
}

static nlohmann::json activity_evaluation_to_json(const ActivityEvaluation &evaluation,
                                                  const std::string &current_user_id,
                                                  bool can_edit_actuals)
{
    nlohmann::json feedback = nlohmann::json::array();
    nlohmann::json mine = nullptr;
    for (const auto &entry : evaluation.feedback)
    {
        nlohmann::json item = {
            {"author", entry.author_name},
            {"rating", entry.rating ? nlohmann::json(*entry.rating) : nlohmann::json(nullptr)},
            {"positive", entry.positive},
            {"improvements", entry.improvements},
            {"action_items", entry.action_items},
            {"notes", entry.notes},
            {"created_at", entry.created_at},
            {"updated_at", entry.updated_at}};
        feedback.push_back(item);
        if (entry.author_id == current_user_id)
        {
            item.erase("author");
            item.erase("created_at");
            item.erase("updated_at");
            mine = std::move(item);
        }
    }

    return {
        {"actual_participants", evaluation.actual_participants ? nlohmann::json(*evaluation.actual_participants) : nlohmann::json(nullptr)},
        {"actual_start_time", evaluation.actual_start_time ? nlohmann::json(*evaluation.actual_start_time) : nlohmann::json(nullptr)},
        {"actual_end_time", evaluation.actual_end_time ? nlohmann::json(*evaluation.actual_end_time) : nlohmann::json(nullptr)},
        {"can_edit_actuals", can_edit_actuals},
        {"feedback", std::move(feedback)},
        {"mine", std::move(mine)}};
}

// Only people actually assigned as responsible (and admins) record the real values.
static bool can_record_activity_actuals(const UserRecord &user, const Activity &activity, const std::string &email)
{
    return is_admin(user) || is_activity_responsible(activity, user, email);
}

static bool valid_clock_time(const std::string &value)
{
    return value.size() == 5 && value[2] == ':' &&
           std::isdigit(static_cast<unsigned char>(value[0])) && std::isdigit(static_cast<unsigned char>(value[1])) &&
           std::isdigit(static_cast<unsigned char>(value[3])) && std::isdigit(static_cast<unsigned char>(value[4])) &&
           ((value[0] - '0') * 10 + (value[1] - '0')) < 24 && (value[3] - '0') < 6;
}

void handle_get_activity_evaluation(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    const std::string activity_id{req->getParameter(0)};
    try
    {
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        auto evaluation = db.get_activity_evaluation(activity_id);
        if (!evaluation)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        send_json(res, 200, activity_evaluation_to_json(*evaluation, access->user.id,
                                                        can_record_activity_actuals(access->user, *activity, claims.email)).dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "activity evaluation", e);
    }
}

void handle_put_activity_evaluation(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    const std::string activity_id{req->getParameter(0)};
    try
    {
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }
        const bool can_actuals = can_record_activity_actuals(access->user, *activity, claims.email);

        auto buffer = std::make_shared<std::string>();
        res->onAborted([] {});
        res->onData([res, buffer, activity_id, user = access->user, can_actuals, &db](std::string_view chunk, bool last) mutable
                    {
            buffer->append(chunk.data(), chunk.size());
            if (!last)
                return;

            auto body = nlohmann::json::parse(*buffer, nullptr, false);
            if (body.is_discarded() || !body.is_object())
            {
                send_json(res, 400, R"({"error":"Ungueltiges JSON"})");
                return;
            }

            if (body.contains("actual_participants") || body.contains("actual_start_time") ||
                body.contains("actual_end_time"))
            {
                send_json(res, 400, R"({"error":"Tatsächliche Werte werden separat gespeichert"})");
                return;
            }

            std::optional<int> rating;
            if (!body.contains("rating"))
            {
                send_json(res, 400, R"({"error":"Bewertung fehlt"})");
                return;
            }
            if (!body["rating"].is_null())
            {
                if (!body["rating"].is_number_integer() && !body["rating"].is_number_unsigned())
                {
                    send_json(res, 400, R"({"error":"Bewertung muss eine ganze Zahl von 0 bis 10 sein"})");
                    return;
                }
                const auto value = body["rating"].is_number_unsigned()
                                       ? static_cast<int64_t>(std::min<uint64_t>(body["rating"].get<uint64_t>(), 1000))
                                       : body["rating"].get<int64_t>();
                if (value < 0 || value > 10)
                {
                    send_json(res, 400, R"({"error":"Bewertung muss zwischen 0 und 10 liegen"})");
                    return;
                }
                rating = static_cast<int>(value);
            }

            const char *fields[] = {"positive", "improvements", "action_items", "notes"};
            std::string values[4];
            for (size_t i = 0; i < 4; ++i)
            {
                const std::string key = fields[i];
                if (!body.contains(key) || !body[key].is_string())
                {
                    send_json(res, 400, R"({"error":"Feedback muss Text enthalten"})");
                    return;
                }
                values[i] = trim_ascii(body[key].get<std::string>());
                if (utf8_character_count(values[i]) > 4000)
                {
                    send_json(res, 400, R"({"error":"Feedback darf höchstens 4000 Zeichen pro Feld enthalten"})");
                    return;
                }
            }

            try
            {
                if (!db.save_activity_feedback(activity_id, user.id, user.display_name,
                                               rating, values[0], values[1], values[2], values[3]))
                {
                    send_json(res, 404, R"({"error":"Nicht gefunden"})");
                    return;
                }
                auto evaluation = db.get_activity_evaluation(activity_id);
                if (!evaluation)
                {
                    send_json(res, 404, R"({"error":"Nicht gefunden"})");
                    return;
                }
                send_json(res, 200, activity_evaluation_to_json(*evaluation, user.id, can_actuals).dump());
            }
            catch (std::exception &e)
            {
                send_internal_error(res, "activity evaluation", e);
            }
        });
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "activity evaluation", e);
    }
}

void handle_put_activity_actuals(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    const std::string activity_id{req->getParameter(0)};
    try
    {
        auto user = resolve_user(db, claims);
        auto activity = db.get_activity_by_id(activity_id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        if (!user || !can_record_activity_actuals(*user, *activity, claims.email))
        {
            send_json(res, 403, R"({"error":"Nur Aktivitätsverantwortliche dürfen tatsächliche Werte erfassen"})");
            return;
        }

        auto buffer = std::make_shared<std::string>();
        res->onAborted([] {});
        res->onData([res, buffer, activity_id, user = *user, &db](std::string_view chunk, bool last) mutable
                    {
            buffer->append(chunk.data(), chunk.size());
            if (!last)
                return;

            auto body = nlohmann::json::parse(*buffer, nullptr, false);
            if (body.is_discarded() || !body.is_object())
            {
                send_json(res, 400, R"({"error":"Ungueltiges JSON"})");
                return;
            }
            for (const char *key : {"actual_participants", "actual_start_time", "actual_end_time"})
            {
                if (!body.contains(key))
                {
                    send_json(res, 400, R"({"error":"Tatsächliche Werte sind unvollständig"})");
                    return;
                }
            }

            std::optional<int> actual_participants;
            const auto &participant_value = body["actual_participants"];
            if (!participant_value.is_null())
            {
                if (!participant_value.is_number_integer() && !participant_value.is_number_unsigned())
                {
                    send_json(res, 400, R"({"error":"Ist-Teilnehmendenzahl muss eine ganze Zahl sein"})");
                    return;
                }
                if (participant_value.is_number_unsigned())
                {
                    auto value = participant_value.get<uint64_t>();
                    if (value > static_cast<uint64_t>(std::numeric_limits<int>::max()))
                    {
                        send_json(res, 400, R"({"error":"Ist-Teilnehmendenzahl ist zu gross"})");
                        return;
                    }
                    actual_participants = static_cast<int>(value);
                }
                else
                {
                    auto value = participant_value.get<int64_t>();
                    if (value < 0 || value > std::numeric_limits<int>::max())
                    {
                        send_json(res, 400, R"({"error":"Ist-Teilnehmendenzahl muss mindestens 0 sein"})");
                        return;
                    }
                    actual_participants = static_cast<int>(value);
                }
            }

            std::optional<std::string> times[2];
            const char *time_keys[] = {"actual_start_time", "actual_end_time"};
            for (size_t i = 0; i < 2; ++i)
            {
                const auto &value = body[time_keys[i]];
                if (value.is_null())
                    continue;
                if (!value.is_string() || !valid_clock_time(value.get<std::string>()))
                {
                    send_json(res, 400, R"({"error":"Tatsächliche Zeiten müssen im Format HH:MM angegeben werden"})");
                    return;
                }
                times[i] = value.get<std::string>();
            }

            try
            {
                if (!db.save_activity_actuals(activity_id, actual_participants, times[0], times[1]))
                {
                    send_json(res, 404, R"({"error":"Nicht gefunden"})");
                    return;
                }
                auto evaluation = db.get_activity_evaluation(activity_id);
                if (!evaluation)
                {
                    send_json(res, 404, R"({"error":"Nicht gefunden"})");
                    return;
                }
                send_json(res, 200, activity_evaluation_to_json(*evaluation, user.id, true).dump());
            }
            catch (std::exception &e)
            {
                send_internal_error(res, "activity actuals", e);
            }
        });
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "activity actuals", e);
    }
}

// ---- GET /departments -------------------------------------------------------

void handle_get_departments(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        const std::string ckey = cache_key(db, "departments", "all");
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
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;

        const std::string ckey = cache_key(db, "activities", activity_read_cache_scope(claims, *access));
        if (auto cached = redis_cache().get(ckey))
        {
            send_json(res, 200, *cached);
            return;
        }

        auto activities = db.list_activities();

        nlohmann::json arr = nlohmann::json::array();
        for (auto &a : activities)
        {
            if (!can_read_activity(access->permission, access->user, access->dept_access, a))
                continue;
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
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;

        const std::string ckey = cache_key(db, "activity", activity_read_cache_scope(claims, *access) + ":" + id);
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

        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
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
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        if (should_use_frozen_values(*activity))
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
        auto count = fetch_midata_children_count_cached(db, *group_id, midata_error);
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
            auto count = fetch_midata_children_count_cached(db, *dept.midata_group_id, midata_error);
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
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
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

static std::atomic<int> weather_fetches_in_flight{0};
constexpr int kMaxWeatherFetchesInFlight = 8;

static void finish_expected_weather_response(HttpRes *res, Database &db, const std::string &id,
                                             std::optional<WeatherResult> &weather,
                                             const std::string &weather_error,
                                             bool frozen_snapshot_missing)
{
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

void handle_get_activity_expected_weather(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    std::string id{req->getParameter(0)};
    try
    {
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;
        auto activity = db.get_activity_by_id(id);
        if (!activity)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }

        if (!can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
        }

        bool frozen_snapshot_missing = false;
        if (should_use_frozen_values(*activity))
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

        if (weather_fetches_in_flight.fetch_add(1) >= kMaxWeatherFetchesInFlight)
        {
            weather_fetches_in_flight.fetch_sub(1);
            send_json(res, 200, nlohmann::json{{"available", false}, {"error", "weather-busy"}}.dump());
            return;
        }

        // Upstream MeteoSwiss downloads can take seconds; run them off the event loop and
        // deliver the response back on the loop thread, where the DB and response live.
        auto aborted = std::make_shared<bool>(false);
        res->onAborted([aborted]() { *aborted = true; });
        uWS::Loop *loop = uWS::Loop::get();
        Activity activity_copy = *activity;
        try
        {
            std::thread([loop, res, aborted, &db, id, activity_copy = std::move(activity_copy),
                         location_input, frozen_snapshot_missing]() mutable {
                std::string weather_error;
                std::optional<WeatherResult> weather;
                try
                {
                    weather = fetch_expected_weather_for_activity(activity_copy, location_input, weather_error);
                }
                catch (const std::exception &e)
                {
                    weather_error = std::string("weather-fetch-failed: ") + e.what();
                }
                catch (...)
                {
                    weather_error = "weather-fetch-failed";
                }
                weather_fetches_in_flight.fetch_sub(1);
                loop->defer([res, aborted, &db, id = std::move(id), weather = std::move(weather),
                             weather_error = std::move(weather_error), frozen_snapshot_missing]() mutable {
                    if (*aborted)
                        return;
                    res->cork([&]() {
                        try
                        {
                            finish_expected_weather_response(res, db, id, weather, weather_error, frozen_snapshot_missing);
                        }
                        catch (std::exception &e)
                        {
                            send_internal_error(res, "handler", e);
                        }
                    });
                });
            }).detach();
        }
        catch (...)
        {
            weather_fetches_in_flight.fetch_sub(1);
            throw;
        }
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
        const std::string ckey = cache_key(db, "locations", "public");
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
        const std::string ckey = cache_key(db, "locations_admin", "all");
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
            cache_bump_version(db, "locations");
            cache_bump_version(db, "locations_admin");
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
            auto previous = db.get_predefined_location_by_id(id);
            auto loc = db.update_predefined_location(id, name);
            if (!loc) { send_json(res, 404, R"({"error":"Nicht gefunden"})"); return; }
            audit_record_change(res, previous ? location_to_json(*previous) : nlohmann::json(nullptr), location_to_json(*loc));
            cache_bump_version(db, "locations");
            cache_bump_version(db, "locations_admin");
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
        auto previous = db.get_predefined_location_by_id(id);
        bool ok = db.delete_predefined_location(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        audit_record_change(res, previous ? location_to_json(*previous) : nlohmann::json(nullptr), nullptr);
        cache_bump_version(db, "locations");
        cache_bump_version(db, "locations_admin");
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
        auto access = require_activity_access(res, db, claims);
        if (!access)
            return;
        auto activity = db.get_activity_by_id(id);
        if (!activity || !can_read_activity(access->permission, access->user, access->dept_access, *activity))
        {
            send_json(res, 403, R"({"error":"Keine Berechtigung"})");
            return;
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

void handle_post_attachment(HttpRes *res, HttpReq *req, Database &db, bool program_image)
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
    res->onData([res, &db, activity_id, program_image, rejected = false, body = std::move(body)](std::string_view chunk, bool last) mutable
                {
        if (rejected) return;
        if (program_image && chunk.size() > kMaxProgramImageBodyBytes - body.size()) {
            rejected = true;
            send_json(res, 413, R"({"error":"Bilder d\u00fcrfen h\u00f6chstens 10 MiB gross sein"})");
            return;
        }
        body.append(chunk);
        if (!last) return;

        auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }
        if (!j.contains("filename") || !j["filename"].is_string()
            || !j.contains("data") || !j["data"].is_string()
            || (j.contains("content_type") && !j["content_type"].is_string())) {
            send_json(res, 400, R"({"error":"filename, content_type and data must be text"})");
            return;
        }
        std::string filename = j["filename"].get<std::string>();
        std::string content_type = j.value("content_type", "application/octet-stream");
        std::string data_base64 = j["data"].get<std::string>();
        if (filename.empty() || data_base64.empty()) {
            send_json(res, 400, R"({"error":"filename and data required"})");
            return;
        }
        try {
            if (program_image) {
                std::vector<uint8_t> bytes;
                if (filename.size() > 255 || !decode_program_image(data_base64, content_type, bytes)) {
                    send_json(res, 400, R"({"error":"Ung\u00fcltiges Bild. Erlaubt: PNG, JPEG, GIF oder WebP bis 10 MiB."})");
                    return;
                }
            }
            auto att = program_image
                ? db.add_program_image(activity_id, filename, content_type, data_base64)
                : db.add_attachment(activity_id, filename, content_type, data_base64);
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

    auto access = require_activity_access(res, db, claims);
    if (!access)
        return;
    auto current_user = std::optional<UserRecord>(access->user);

    auto buf = std::make_shared<std::string>();
    res->onAborted([] {});
    res->onData([res, buf, &db, &wm, current_user, access](std::string_view chunk, bool last)
                {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"Ung\u00fcltiges JSON-Format"})");
            return;
        }

        if (!validate_guiding_thread(res, j)) return;
        ActivityInput input = parse_activity_input(j);
        std::string notification_graph_token = j.value("notification_access_token", "");
        if (input.title.empty() || input.date.empty() || input.start_time.empty() || input.end_time.empty()) {
            send_json(res, 400, R"({"error":"Titel, Datum, Startzeit und Endzeit sind erforderlich"})");
            return;
        }

        // Check create permission for the target department.
        if (!can_create_activity(access->permission, access->user, access->dept_access, input.department)) {
            send_json(res, 403, R"({"error":"Keine Schreibberechtigung für diese Stufe"})");
            return;
        }

        try {
            std::vector<std::string> descriptions;
            for (const auto &program : input.programs) descriptions.push_back(program.description);
            if (!validate_program_image_references(res, db, *current_user, descriptions)) return;
            auto activity = db.create_activity(input);
            if (!activity) {
                send_json(res, 500, R"({"error":"Datenbankfehler"})");
                return;
            }

            if (current_user) {
                auto all_assigned = unique_names_from_material(activity->material);
                auto users = db.list_users();
                for (const auto &u : users) {
                    if (u.id == current_user->id || !u.notify_material_assigned)
                        continue;
                    if (!user_in_assignee_list(all_assigned, u))
                        continue;

                    std::string time_range = activity->start_time + "-" + activity->end_time;
                    std::string date_short = format_date_ddmmyyyy(activity->date);
                    std::string full_link = activity_absolute_link(db, activity->id);
                    auto all_assigned_names = resolve_ids_to_display_names(all_assigned, users);
                    std::string recipients_text = join_display(all_assigned_names);
                    nlohmann::json payload = {
                        {"activity_id", activity->id},
                        {"activity_title", activity->title},
                        {"activity_date", activity->date},
                        {"activity_date_display", date_short},
                        {"activity_time", time_range},
                        {"location", activity->location},
                        {"assigned_users", all_assigned_names},
                        {"recipients", all_assigned_names},
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
                            deliver_web_push_for_user(db, u, *note);
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
                        if (!user_in_assignee_list(activity->responsible, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = format_date_ddmmyyyy(activity->date);
                        std::string full_link = activity_absolute_link(db, activity->id);
                        auto responsible_names = resolve_ids_to_display_names(activity->responsible, users);
                        std::string recipients_text = join_display(responsible_names);
                        nlohmann::json payload = {
                            {"activity_id", activity->id},
                            {"activity_title", activity->title},
                            {"activity_date", activity->date},
                            {"activity_date_display", date_short},
                            {"activity_time", time_range},
                            {"location", activity->location},
                            {"assigned_users", responsible_names},
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
                                deliver_web_push_for_user(db, u, *note);
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
                        if (!user_in_assignee_list(prog.responsible, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = format_date_ddmmyyyy(activity->date);
                        std::string full_link = activity_absolute_link(db, activity->id);
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
                                deliver_web_push_for_user(db, u, *note);
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

            wm.broadcast_activity_created(*activity);
            cache_bump_version(db, "activities");
            cache_bump_version(db, "activity");
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

        if (!validate_guiding_thread(res, j)) return;
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
            std::vector<std::string> descriptions;
            for (const auto &program : input.programs) descriptions.push_back(program.description);
            if (!validate_program_image_references(res, db, *current_user, descriptions, id)) return;
            auto updated = db.update_activity(id, input);
            if (!updated) {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }
            std::optional<Activity> previous_activity{std::move(updated->first)};
            std::optional<Activity> activity{std::move(updated->second)};
            audit_record_change(res, to_json(*previous_activity), to_json(*activity));

            if (current_user) {
                auto new_assigned = unique_names_from_material(activity->material);
                auto newly_assigned = newly_assigned_users_from_material_delta(
                    previous_activity ? previous_activity->material : std::vector<MaterialItem>{},
                    activity->material
                );

                if (!newly_assigned.empty()) {
                    auto users = db.list_users();
                    for (const auto &u : users) {
                        if (u.id == current_user->id || !u.notify_material_assigned)
                            continue;
                        if (!user_in_assignee_list(newly_assigned, u))
                            continue;

                        std::string time_range = activity->start_time + "-" + activity->end_time;
                        std::string date_short = format_date_ddmmyyyy(activity->date);
                        std::string full_link = activity_absolute_link(db, activity->id);
                        auto new_assigned_names = resolve_ids_to_display_names(new_assigned, users);
                        std::string recipients_text = join_display(new_assigned_names);
                        nlohmann::json payload = {
                            {"activity_id", activity->id},
                            {"activity_title", activity->title},
                            {"activity_date", activity->date},
                            {"activity_date_display", date_short},
                            {"activity_time", time_range},
                            {"location", activity->location},
                            {"assigned_users", new_assigned_names},
                            {"newly_assigned_users", resolve_ids_to_display_names(newly_assigned, users)},
                            {"recipients", new_assigned_names},
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
                                deliver_web_push_for_user(db, u, *note);
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
                        if (!previous_activity || !contains_name_ci(previous_activity->responsible, name))
                            newly_responsible.push_back(name);
                    }
                    if (!newly_responsible.empty()) {
                        auto users_list = db.list_users();
                        for (const auto &u : users_list) {
                            if (u.id == current_user->id || !u.notify_activity_assigned)
                                continue;
                            if (!user_in_assignee_list(newly_responsible, u))
                                continue;

                            std::string time_range = activity->start_time + "-" + activity->end_time;
                            std::string date_short = format_date_ddmmyyyy(activity->date);
                            std::string full_link = activity_absolute_link(db, activity->id);
                            auto resp_names = resolve_ids_to_display_names(activity->responsible, users_list);
                            std::string recipients_text = join_display(resp_names);
                            nlohmann::json payload = {
                                {"activity_id", activity->id},
                                {"activity_title", activity->title},
                                {"activity_date", activity->date},
                                {"activity_date_display", date_short},
                                {"activity_time", time_range},
                                {"location", activity->location},
                                {"assigned_users", resp_names},
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
                                    deliver_web_push_for_user(db, u, *note);
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
                                old_prog_pairs.insert(prog_key + "\n" + normalize_assignee_key(name));
                        }
                    }

                    auto users_list = db.list_users();
                    for (const auto &prog : activity->programs) {
                        if (prog.responsible.empty())
                            continue;
                        std::string prog_key = to_lower_ascii(trim_ascii(prog.title));
                        for (const auto &name : prog.responsible) {
                            std::string pair_key = prog_key + "\n" + normalize_assignee_key(name);
                            if (old_prog_pairs.find(pair_key) != old_prog_pairs.end())
                                continue;

                            // This is a new assignment — find matching user
                            for (const auto &u : users_list) {
                                if (u.id == current_user->id || !u.notify_program_assigned)
                                    continue;
                                if (!user_matches_assignee(u, name))
                                    continue;

                                std::string time_range = activity->start_time + "-" + activity->end_time;
                                std::string date_short = format_date_ddmmyyyy(activity->date);
                                std::string full_link = activity_absolute_link(db, activity->id);
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
                                        deliver_web_push_for_user(db, u, *note);
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

            wm.broadcast_activity_updated(*previous_activity, *activity);
            cache_bump_version(db, "activities");
            cache_bump_version(db, "activity");
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
    try
    {
        auto activity = db.get_activity_by_id(id);
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
        bool ok = db.soft_delete_activity(id, current_user->id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        audit_record_change(res, to_json(*activity), nullptr);
        wm.broadcast_activity_deleted(*activity);
        cache_bump_version(db, "activities");
        cache_bump_version(db, "activity");
        send_json(res, 204, "");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

// ---- Predefined Materials ---------------------------------------------------

static std::optional<UserRecord> require_materials_admin(HttpRes *res, HttpReq *req, Database &db)
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
    if (!perm || perm->materials_manage_scope != "all")
    {
        send_json(res, 403, R"({"error":"Keine Berechtigung"})");
        return std::nullopt;
    }
    return user;
}

static nlohmann::json material_name_to_json(const MaterialNameRecord &m)
{
    return {{"id", m.id}, {"name", m.name}, {"created_at", m.created_at}, {"updated_at", m.updated_at}};
}

void handle_get_materials_predefined(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    try
    {
        auto mats = db.get_predefined_materials();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &m : mats)
            arr.push_back(m);
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_get_materials_admin(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_materials_admin(res, req, db))
        return;
    try
    {
        auto mats = db.list_predefined_materials();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &m : mats)
            arr.push_back(material_name_to_json(m));
        send_json(res, 200, arr.dump());
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}

void handle_post_material(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_materials_admin(res, req, db))
        return;
    std::string content_type{req->getHeader("content-type")};
    res->onAborted([] {});
    res->onData([res, &db, content_type](std::string_view data, bool last)
                {
        if (!last) return;
        try {
            auto body = nlohmann::json::parse(data, nullptr, false);
            if (body.is_discarded() || !body.contains("name") || !body["name"].is_string()) {
                send_json(res, 400, R"({"error":"name erforderlich"})");
                return;
            }
            std::string name = body["name"].get<std::string>();
            if (name.empty()) {
                send_json(res, 400, R"({"error":"name darf nicht leer sein"})");
                return;
            }
            auto created = db.create_predefined_material(name);
            if (!created) {
                send_json(res, 409, R"({"error":"Material existiert bereits"})");
                return;
            }
            send_json(res, 201, material_name_to_json(*created).dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

void handle_patch_material(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_materials_admin(res, req, db))
        return;
    std::string id{req->getParameter("id")};
    res->onAborted([] {});
    res->onData([res, &db, id](std::string_view data, bool last)
                {
        if (!last) return;
        try {
            auto body = nlohmann::json::parse(data, nullptr, false);
            if (body.is_discarded() || !body.contains("name") || !body["name"].is_string()) {
                send_json(res, 400, R"({"error":"name erforderlich"})");
                return;
            }
            std::string name = body["name"].get<std::string>();
            if (name.empty()) {
                send_json(res, 400, R"({"error":"name darf nicht leer sein"})");
                return;
            }
            auto previous = db.get_predefined_material_by_id(id);
            auto updated = db.update_predefined_material(id, name);
            if (!updated) {
                send_json(res, 404, R"({"error":"Nicht gefunden"})");
                return;
            }
            audit_record_change(res, previous ? material_name_to_json(*previous) : nlohmann::json(nullptr), material_name_to_json(*updated));
            send_json(res, 200, material_name_to_json(*updated).dump());
        } catch (std::exception &e) {
            send_internal_error(res, "handler", e);
        } });
}

void handle_delete_material(HttpRes *res, HttpReq *req, Database &db)
{
    if (!require_materials_admin(res, req, db))
        return;
    std::string id{req->getParameter("id")};
    try
    {
        auto previous = db.get_predefined_material_by_id(id);
        bool ok = db.delete_predefined_material(id);
        if (!ok)
        {
            send_json(res, 404, R"({"error":"Nicht gefunden"})");
            return;
        }
        audit_record_change(res, previous ? material_name_to_json(*previous) : nlohmann::json(nullptr), nullptr);
        send_json(res, 204, "");
    }
    catch (std::exception &e)
    {
        send_internal_error(res, "handler", e);
    }
}
