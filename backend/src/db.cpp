#include "db.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <functional>
#include <curl/curl.h>
#include "json.hpp"

// ---- Constructor / Destructor -----------------------------------------------

Database::Database(const std::string &conn_str)
{
    conn_ = PQconnectdb(conn_str.c_str());
    if (PQstatus(conn_) != CONNECTION_OK)
    {
        std::string err = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("DB connect failed: " + err);
    }

    const char *bootstrap_sql =
        "CREATE EXTENSION IF NOT EXISTS \"pgcrypto\";"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_material_assigned BOOLEAN NOT NULL DEFAULT true;"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_activity_assigned BOOLEAN NOT NULL DEFAULT true;"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_program_assigned BOOLEAN NOT NULL DEFAULT true;"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_mail_own_activity BOOLEAN NOT NULL DEFAULT true;"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_mail_department BOOLEAN NOT NULL DEFAULT true;"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_channel_websocket BOOLEAN NOT NULL DEFAULT true;"
        "ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_channel_email BOOLEAN NOT NULL DEFAULT false;"
        "CREATE TABLE IF NOT EXISTS notifications ("
        "  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),"
        "  user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  category TEXT NOT NULL CHECK (category IN ('material_assigned','activity_assigned','program_assigned','mail_own_activity','mail_department')) ,"
        "  title TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  link TEXT,"
        "  payload JSONB NOT NULL DEFAULT '{}',"
        "  is_read BOOLEAN NOT NULL DEFAULT false,"
        "  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_notifications_user_created ON notifications (user_id, created_at DESC);"
        "CREATE INDEX IF NOT EXISTS idx_notifications_user_unread ON notifications (user_id, is_read, created_at DESC);"
        "CREATE TABLE IF NOT EXISTS push_subscriptions ("
        "  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),"
        "  user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
        "  endpoint TEXT NOT NULL UNIQUE,"
        "  p256dh TEXT NOT NULL,"
        "  auth TEXT NOT NULL,"
        "  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
        "  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_push_subscriptions_user ON push_subscriptions (user_id);";
    PGresult *bootstrap = PQexec(conn_, bootstrap_sql);
    if (PQresultStatus(bootstrap) != PGRES_COMMAND_OK)
    {
        std::string err = PQresultErrorMessage(bootstrap);
        PQclear(bootstrap);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("DB bootstrap failed: " + err);
    }
    PQclear(bootstrap);

    // Migrate: widen notifications.category CHECK to include activity_assigned / program_assigned
    PGresult *migrate = PQexec(conn_,
        "DO $$ BEGIN "
        "  ALTER TABLE notifications DROP CONSTRAINT IF EXISTS notifications_category_check; "
        "  ALTER TABLE notifications ADD CONSTRAINT notifications_category_check "
        "    CHECK (category IN ('material_assigned','activity_assigned','program_assigned','mail_own_activity','mail_department')); "
        "EXCEPTION WHEN OTHERS THEN NULL; END $$;");
    PQclear(migrate);
}

Database::~Database()
{
    if (conn_)
        PQfinish(conn_);
}

void Database::ensure_connected()
{
    if (PQstatus(conn_) != CONNECTION_OK)
    {
        PQreset(conn_);
        if (PQstatus(conn_) != CONNECTION_OK)
            throw std::runtime_error("DB reconnect failed: " + std::string(PQerrorMessage(conn_)));
    }
}

// ---- PostgreSQL array helpers -----------------------------------------------

// Parses PostgreSQL text-mode array like {val1,"val two",val3} into a vector
std::vector<std::string> Database::parse_pg_array(const char *raw)
{
    std::vector<std::string> result;
    if (!raw || raw[0] != '{')
        return result;
    const char *p = raw + 1;
    while (*p && *p != '}')
    {
        std::string token;
        if (*p == '"')
        {
            ++p;
            while (*p && !(*p == '"' && *(p - 1) != '\\'))
            {
                if (*p == '\\' && *(p + 1))
                {
                    ++p;
                }
                token += *p++;
            }
            if (*p == '"')
                ++p;
        }
        else
        {
            while (*p && *p != ',' && *p != '}')
                token += *p++;
        }
        result.push_back(token);
        if (*p == ',')
            ++p;
    }
    return result;
}

// Formats a vector<string> as a JSON array string for use with jsonb_array_elements_text
std::string Database::format_material_param(const std::vector<std::string> &material)
{
    // Produces a JSON array string like ["a","b with \"quotes\""]
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < material.size(); ++i)
    {
        if (i > 0)
            oss << ",";
        oss << "\"";
        for (char c : material[i])
        {
            if (c == '"')
                oss << "\\\"";
            else if (c == '\\')
                oss << "\\\\";
            else
                oss << c;
        }
        oss << "\"";
    }
    oss << "]";
    return oss.str();
}

// Formats vector<MaterialItem> as a JSONB value like [{"name":"a","responsible":["b","c"]},...]
std::string Database::format_material_items_param(const std::vector<MaterialItem> &items)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &m : items)
    {
        nlohmann::json obj = {{"name", m.name}};
        if (!m.responsible.empty())
            obj["responsible"] = m.responsible;
        arr.push_back(obj);
    }
    return arr.dump();
}

// ---- Row mappers ------------------------------------------------------------

Activity Database::row_to_activity(PGresult *res, int row)
{
    Activity a;
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };

    a.id = col("id") ? col("id") : "";
    a.title = col("title") ? col("title") : "";
    a.date = col("date") ? col("date") : "";
    a.start_time = col("start_time") ? col("start_time") : "";
    a.end_time = col("end_time") ? col("end_time") : "";
    a.goal = col("goal") ? col("goal") : "";
    a.location = col("location") ? col("location") : "";
    {
        const char *resp = col("responsible");
        if (resp)
            a.responsible = parse_pg_array(resp);
    }
    a.created_at = col("created_at") ? col("created_at") : "";
    a.updated_at = col("updated_at") ? col("updated_at") : "";

    const char *dept = col("department");
    if (dept)
        a.department = dept;

    const char *bwi = col("bad_weather_info");
    if (bwi)
        a.bad_weather_info = bwi;

    const char *ppe = col("planned_participants_estimate");
    if (ppe)
        a.planned_participants_estimate = std::atoi(ppe);

    // siko_text
    const char *siko_t = col("siko_text");
    if (siko_t)
        a.siko_text = siko_t;

    // material (JSONB column)
    const char *mat = col("material");
    if (mat)
    {
        auto mj = nlohmann::json::parse(mat, nullptr, false);
        if (mj.is_array())
        {
            for (const auto &item : mj)
            {
                MaterialItem mi;
                if (item.is_string())
                {
                    mi.name = item.get<std::string>();
                }
                else if (item.is_object())
                {
                    mi.name = item.value("name", "");
                    if (item.contains("responsible") && item["responsible"].is_array())
                    {
                        for (const auto &r : item["responsible"])
                            if (r.is_string())
                                mi.responsible.push_back(r.get<std::string>());
                    }
                    else if (item.contains("responsible") && item["responsible"].is_string())
                    {
                        std::string rs = item["responsible"].get<std::string>();
                        if (!rs.empty())
                            mi.responsible.push_back(rs);
                    }
                }
                if (!mi.name.empty())
                    a.material.push_back(mi);
            }
        }
    }

    return a;
}

Program Database::row_to_program(PGresult *res, int row)
{
    auto col = [&](const char *name) -> std::string
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return "";
        return PQgetvalue(res, row, c);
    };
    Program p;
    p.id = col("id");
    p.activity_id = col("activity_id");
    {
        std::string dm = col("duration_minutes");
        p.duration_minutes = dm.empty() ? 0 : std::atoi(dm.c_str());
    }
    p.title = col("title");
    p.description = col("description");
    {
        int c = PQfnumber(res, "responsible");
        if (c >= 0 && !PQgetisnull(res, row, c))
            p.responsible = parse_pg_array(PQgetvalue(res, row, c));
    }
    return p;
}

void Database::attach_programs(std::vector<Activity> &activities)
{
    if (activities.empty())
        return;

    // Build array literal: '{uuid1,uuid2,...}'
    std::string arr = "{";
    for (size_t i = 0; i < activities.size(); ++i)
    {
        if (i)
            arr += ",";
        arr += activities[i].id;
    }
    arr += "}";
    const char *params[1] = {arr.c_str()};

    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, duration_minutes, title, description, responsible "
                                 "FROM programs WHERE activity_id = ANY($1::uuid[]) ORDER BY ctid",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return;
    }

    int n = PQntuples(res);
    for (int i = 0; i < n; ++i)
    {
        Program p = row_to_program(res, i);
        for (auto &a : activities)
        {
            if (a.id == p.activity_id)
            {
                a.programs.push_back(p);
                break;
            }
        }
    }
    PQclear(res);
}

void Database::attach_programs_single(Activity &a)
{
    std::vector<Activity> tmp = {a};
    attach_programs(tmp);
    a.programs = std::move(tmp[0].programs);
}

// ---- list_activities --------------------------------------------------------

std::vector<Activity> Database::list_activities()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, title, date::text, start_time, end_time, goal, location, responsible, "
                           "       department, material, siko_text, "
                           "       bad_weather_info, planned_participants_estimate, created_at, updated_at "
                           "FROM activities ORDER BY date DESC, start_time");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_activities: " + err);
    }
    std::vector<Activity> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_activity(res, i));
    PQclear(res);
    attach_programs(out);
    return out;
}

// ---- get_activity_by_id -----------------------------------------------------

std::optional<Activity> Database::get_activity_by_id(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, title, date::text, start_time, end_time, goal, location, responsible, "
                                 "       department, material, siko_text, "
                                 "       bad_weather_info, planned_participants_estimate, created_at, updated_at "
                                 "FROM activities WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    Activity a = row_to_activity(res, 0);
    PQclear(res);
    attach_programs_single(a);
    return a;
}

// ---- get_predefined_locations ------------------------------------------------

std::vector<std::string> Database::get_predefined_locations()
{
    ensure_connected();
    PGresult *res = PQexec(conn_, "SELECT name FROM predefined_locations ORDER BY name");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<std::string> out;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i)
        out.push_back(PQgetvalue(res, i, 0));
    PQclear(res);
    return out;
}

LocationRecord Database::row_to_location(PGresult *res, int row)
{
    auto col = [&](const char *name) -> std::string
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return "";
        return PQgetvalue(res, row, c);
    };
    LocationRecord loc;
    loc.id = col("id");
    loc.name = col("name");
    loc.created_at = col("created_at");
    loc.updated_at = col("updated_at");
    return loc;
}

std::vector<LocationRecord> Database::list_predefined_locations()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, name, created_at, updated_at FROM predefined_locations ORDER BY name");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_predefined_locations: " + err);
    }
    std::vector<LocationRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_location(res, i));
    PQclear(res);
    return out;
}

std::optional<LocationRecord> Database::create_predefined_location(const std::string &name)
{
    ensure_connected();
    const char *params[1] = {name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO predefined_locations (name) VALUES ($1) "
                                 "RETURNING id, name, created_at, updated_at",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto out = row_to_location(res, 0);
    PQclear(res);
    return out;
}

std::optional<LocationRecord> Database::update_predefined_location(const std::string &id, const std::string &name)
{
    ensure_connected();
    const char *params[2] = {id.c_str(), name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE predefined_locations SET name = $2 WHERE id = $1 "
                                 "RETURNING id, name, created_at, updated_at",
                                 2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto out = row_to_location(res, 0);
    PQclear(res);
    return out;
}

bool Database::delete_predefined_location(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM predefined_locations WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK && PQcmdTuples(res)[0] != '0';
    PQclear(res);
    return ok;
}

// ---- Transaction helpers ----------------------------------------------------

static void exec_or_throw(PGconn *conn, const char *sql, const char *context)
{
    PGresult *r = PQexec(conn, sql);
    ExecStatusType s = PQresultStatus(r);
    PQclear(r);
    if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK)
        throw std::runtime_error(std::string(context) + ": " + PQerrorMessage(conn));
}

// Insert programs within an open transaction
static void insert_programs(PGconn *conn,
                            const std::string &activity_id,
                            const std::vector<ProgramInput> &programs,
                            const std::function<std::string(const std::vector<std::string> &)> &fmt)
{
    for (const auto &pi : programs)
    {
        std::string resp_json = fmt(pi.responsible);
        std::string dm_str = std::to_string(pi.duration_minutes);
        const char *params[5] = {
            activity_id.c_str(),
            dm_str.c_str(),
            pi.title.c_str(),
            pi.description.c_str(),
            resp_json.c_str()};
        PGresult *r = PQexecParams(conn,
                                   "INSERT INTO programs (activity_id, duration_minutes, title, description, responsible) "
                                   "VALUES ($1, $2::int, $3, $4, array(select jsonb_array_elements_text($5::jsonb)))",
                                   5, nullptr, params, nullptr, nullptr, 0);
        ExecStatusType s = PQresultStatus(r);
        PQclear(r);
        if (s != PGRES_COMMAND_OK)
            throw std::runtime_error("insert_programs: " + std::string(PQerrorMessage(conn)));
    }
}

// ---- create_activity --------------------------------------------------------

std::optional<Activity> Database::create_activity(const ActivityInput &input)
{
    ensure_connected();

    std::string mat_json = Database::format_material_items_param(input.material);
    std::string resp_json = Database::format_material_param(input.responsible);
    std::string dept_str = input.department ? *input.department : "";
    std::string bwi_str = input.bad_weather_info ? *input.bad_weather_info : "";
    std::string siko_str = input.siko_text ? *input.siko_text : "";
    std::string ppe_str = input.planned_participants_estimate ? std::to_string(*input.planned_participants_estimate) : "";
    const char *dept_param = input.department ? dept_str.c_str() : nullptr;
    const char *siko_param = input.siko_text ? siko_str.c_str() : nullptr;
    const char *ppe_param = input.planned_participants_estimate ? ppe_str.c_str() : nullptr;

    exec_or_throw(conn_, "BEGIN", "create_activity BEGIN");

    try
    {
        const char *p2[12] = {
            input.title.c_str(), input.date.c_str(),
            input.start_time.c_str(), input.end_time.c_str(),
            input.goal.c_str(), input.location.c_str(),
            resp_json.c_str(),
            dept_param,
            mat_json.c_str(),
            siko_param,
            bwi_str.c_str(),
            ppe_param};
        PGresult *r = PQexecParams(conn_,
                                   "INSERT INTO activities "
                                   "(title, date, start_time, end_time, goal, location, responsible, department, material, siko_text, bad_weather_info, planned_participants_estimate) "
                                   "VALUES ($1, $2::date, $3, $4, $5, $6, array(select jsonb_array_elements_text($7::jsonb)), "
                                   "$8, $9::jsonb, $10, $11, $12::int) "
                                   "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                                   "department, material, siko_text, "
                                   "bad_weather_info, planned_participants_estimate, created_at, updated_at",
                                   12, nullptr, p2, nullptr, nullptr, 0);
        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0)
        {
            std::string err = PQresultErrorMessage(r);
            PQclear(r);
            throw std::runtime_error("create_activity INSERT: " + err);
        }
        Activity a = row_to_activity(r, 0);
        PQclear(r);

        insert_programs(conn_, a.id, input.programs, Database::format_material_param);
        exec_or_throw(conn_, "COMMIT", "create_activity COMMIT");
        attach_programs_single(a);
        return a;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

// ---- update_activity --------------------------------------------------------

std::optional<Activity> Database::update_activity(const std::string &id, const ActivityInput &input)
{
    ensure_connected();

    std::string mat_json = Database::format_material_items_param(input.material);
    std::string resp_json = Database::format_material_param(input.responsible);
    std::string dept_str = input.department ? *input.department : "";
    std::string bwi_str = input.bad_weather_info ? *input.bad_weather_info : "";
    std::string siko_str = input.siko_text ? *input.siko_text : "";
    std::string ppe_str = input.planned_participants_estimate ? std::to_string(*input.planned_participants_estimate) : "";
    const char *dept_param = input.department ? dept_str.c_str() : nullptr;
    const char *siko_param = input.siko_text ? siko_str.c_str() : nullptr;
    const char *ppe_param = input.planned_participants_estimate ? ppe_str.c_str() : nullptr;

    exec_or_throw(conn_, "BEGIN", "update_activity BEGIN");

    try
    {
        const char *p[13] = {
            input.title.c_str(), input.date.c_str(),
            input.start_time.c_str(), input.end_time.c_str(),
            input.goal.c_str(), input.location.c_str(),
            resp_json.c_str(),
            dept_param,
            mat_json.c_str(),
            siko_param,
            bwi_str.c_str(),
            ppe_param,
            id.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "UPDATE activities SET "
                                   "title=$1, date=$2::date, start_time=$3, end_time=$4, "
                                   "goal=$5, location=$6, responsible=array(select jsonb_array_elements_text($7::jsonb)), "
                                   "department=$8, material=$9::jsonb, "
                                   "siko_text=$10, bad_weather_info=$11, planned_participants_estimate=$12 "
                                   "WHERE id=$13 "
                                   "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                                   "department, material, siko_text, "
                                   "bad_weather_info, planned_participants_estimate, created_at, updated_at",
                                   13, nullptr, p, nullptr, nullptr, 0);
        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0)
        {
            std::string err = PQresultErrorMessage(r);
            PQclear(r);
            PQexec(conn_, "ROLLBACK");
            return std::nullopt;
        }
        Activity a = row_to_activity(r, 0);
        PQclear(r);

        // Replace programs
        const char *del_params[1] = {id.c_str()};
        PGresult *del = PQexecParams(conn_,
                                     "DELETE FROM programs WHERE activity_id = $1",
                                     1, nullptr, del_params, nullptr, nullptr, 0);
        PQclear(del);

        insert_programs(conn_, a.id, input.programs, Database::format_material_param);
        exec_or_throw(conn_, "COMMIT", "update_activity COMMIT");
        attach_programs_single(a);
        return a;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

// ---- delete_activity --------------------------------------------------------

bool Database::delete_activity(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM activities WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<int> Database::get_activity_midata_children_value(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT midata_children_value FROM activities WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0 || PQgetisnull(res, 0, 0))
    {
        PQclear(res);
        return std::nullopt;
    }
    int value = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    return value;
}

bool Database::set_activity_midata_children_value(const std::string &activity_id, int value)
{
    ensure_connected();
    std::string value_str = std::to_string(value);
    const char *params[2] = {activity_id.c_str(), value_str.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE activities SET midata_children_value = $2::int, midata_children_recorded_at = NOW() WHERE id = $1",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<nlohmann::json> Database::get_activity_weather_snapshot(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT weather_snapshot FROM activities WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0 || PQgetisnull(res, 0, 0))
    {
        PQclear(res);
        return std::nullopt;
    }
    auto parsed = nlohmann::json::parse(PQgetvalue(res, 0, 0), nullptr, false);
    PQclear(res);
    if (parsed.is_discarded())
        return std::nullopt;
    return parsed;
}

bool Database::set_activity_weather_snapshot(const std::string &activity_id, const nlohmann::json &snapshot)
{
    ensure_connected();
    std::string snapshot_str = snapshot.dump();
    const char *params[2] = {activity_id.c_str(), snapshot_str.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE activities SET weather_snapshot = $2::jsonb, weather_recorded_at = NOW() WHERE id = $1",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<std::string> Database::get_activity_weather_location(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT weather_location FROM activities WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0 || PQgetisnull(res, 0, 0))
    {
        PQclear(res);
        return std::nullopt;
    }
    std::string value = PQgetvalue(res, 0, 0);
    PQclear(res);
    return value;
}

bool Database::set_activity_weather_location(const std::string &activity_id, const std::string &location)
{
    ensure_connected();
    const char *params[2] = {activity_id.c_str(), location.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE activities SET weather_location = $2 WHERE id = $1",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ---- User helpers -----------------------------------------------------------

UserRecord Database::row_to_user(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    UserRecord u;
    u.id = col("id") ? col("id") : "";
    u.microsoft_oid = col("microsoft_oid") ? col("microsoft_oid") : "";
    u.email = col("email") ? col("email") : "";
    u.display_name = col("display_name") ? col("display_name") : "";
    u.role = col("role") ? col("role") : "Mitglied";
    u.time_display_mode = col("time_display_mode") ? col("time_display_mode") : "minutes";
    u.notify_material_assigned = col("notify_material_assigned") ? std::string(col("notify_material_assigned")) == "t" : true;
    u.notify_activity_assigned = col("notify_activity_assigned") ? std::string(col("notify_activity_assigned")) == "t" : true;
    u.notify_program_assigned = col("notify_program_assigned") ? std::string(col("notify_program_assigned")) == "t" : true;
    u.notify_mail_own_activity = col("notify_mail_own_activity") ? std::string(col("notify_mail_own_activity")) == "t" : true;
    u.notify_mail_department = col("notify_mail_department") ? std::string(col("notify_mail_department")) == "t" : true;
    u.notify_channel_websocket = col("notify_channel_websocket") ? std::string(col("notify_channel_websocket")) == "t" : true;
    u.notify_channel_email = col("notify_channel_email") ? std::string(col("notify_channel_email")) == "t" : false;
    u.created_at = col("created_at") ? col("created_at") : "";
    u.updated_at = col("updated_at") ? col("updated_at") : "";
    if (col("department"))
        u.department = col("department");
    return u;
}

// ---- list_users -------------------------------------------------------------

std::vector<UserRecord> Database::list_users()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, microsoft_oid, email, display_name, department, role, time_display_mode, "
                           "       notify_material_assigned, notify_activity_assigned, notify_program_assigned, notify_mail_own_activity, notify_mail_department, "
                           "       notify_channel_websocket, notify_channel_email, "
                           "       created_at, updated_at "
                           "FROM users ORDER BY display_name");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_users: " + err);
    }
    std::vector<UserRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_user(res, i));
    PQclear(res);
    return out;
}

// ---- upsert_user ------------------------------------------------------------

std::optional<UserRecord> Database::upsert_user(const std::string &oid,
                                                const std::string &email,
                                                const std::string &display_name,
                                                const std::string &initial_role,
                                                const std::string &initial_dept,
                                                bool force_role)
{
    ensure_connected();

    // Build SQL — when force_role is true (admin email) we also update role on conflict.
    std::string on_conflict;
    if (force_role)
    {
        on_conflict =
            "ON CONFLICT (microsoft_oid) DO UPDATE "
            "SET email = EXCLUDED.email, role = '" +
            initial_role + "', updated_at = NOW() ";
    }
    else
    {
        on_conflict =
            "ON CONFLICT (microsoft_oid) DO UPDATE "
            "SET email = EXCLUDED.email, updated_at = NOW() ";
    }

    std::string sql =
        "INSERT INTO users (microsoft_oid, email, display_name, department, role) "
        "VALUES ($1, $2, $3, '" +
        initial_dept + "', '" + initial_role + "') " +
        on_conflict +
        "RETURNING id, microsoft_oid, email, display_name, department, role, time_display_mode, "
        "          notify_material_assigned, notify_activity_assigned, notify_program_assigned, "
        "          notify_mail_own_activity, notify_mail_department, "
        "          notify_channel_websocket, notify_channel_email, "
        "          created_at, updated_at";

    const char *params[3] = {oid.c_str(), email.c_str(), display_name.c_str()};
    PGresult *res = PQexecParams(conn_, sql.c_str(), 3, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- get_user_by_oid --------------------------------------------------------

std::optional<UserRecord> Database::get_user_by_oid(const std::string &oid)
{
    ensure_connected();
    const char *params[1] = {oid.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, microsoft_oid, email, display_name, department, role, time_display_mode, "
                                 "       notify_material_assigned, notify_activity_assigned, notify_program_assigned, notify_mail_own_activity, notify_mail_department, "
                                 "       notify_channel_websocket, notify_channel_email, "
                                 "       created_at, updated_at "
                                 "FROM users WHERE microsoft_oid = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- get_user_by_id ---------------------------------------------------------

std::optional<UserRecord> Database::get_user_by_id(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, microsoft_oid, email, display_name, department, role, time_display_mode, "
                                 "       notify_material_assigned, notify_activity_assigned, notify_program_assigned, notify_mail_own_activity, notify_mail_department, "
                                 "       notify_channel_websocket, notify_channel_email, "
                                 "       created_at, updated_at "
                                 "FROM users WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- update_user ------------------------------------------------------------

std::optional<UserRecord> Database::update_user(const std::string &oid,
                                                const std::string &display_name,
                                                const std::optional<std::string> &department,
                                                const std::optional<std::string> &time_display_mode,
                                                const std::optional<bool> &notify_material_assigned,
                                                const std::optional<bool> &notify_activity_assigned,
                                                const std::optional<bool> &notify_program_assigned,
                                                const std::optional<bool> &notify_mail_own_activity,
                                                const std::optional<bool> &notify_mail_department,
                                                const std::optional<bool> &notify_channel_websocket,
                                                const std::optional<bool> &notify_channel_email)
{
    ensure_connected();
    std::string dept_str = department ? *department : "";
    std::string tdm_str = time_display_mode ? *time_display_mode : "";

    std::string sql =
        "UPDATE users SET display_name = $1, department = " +
        (department ? ("'" + dept_str + "'") : std::string("NULL"));
    if (time_display_mode)
        sql += ", time_display_mode = '" + tdm_str + "'";
    if (notify_material_assigned.has_value())
        sql += std::string(", notify_material_assigned = ") + (*notify_material_assigned ? "true" : "false");
    if (notify_activity_assigned.has_value())
        sql += std::string(", notify_activity_assigned = ") + (*notify_activity_assigned ? "true" : "false");
    if (notify_program_assigned.has_value())
        sql += std::string(", notify_program_assigned = ") + (*notify_program_assigned ? "true" : "false");
    if (notify_mail_own_activity.has_value())
        sql += std::string(", notify_mail_own_activity = ") + (*notify_mail_own_activity ? "true" : "false");
    if (notify_mail_department.has_value())
        sql += std::string(", notify_mail_department = ") + (*notify_mail_department ? "true" : "false");
    if (notify_channel_websocket.has_value())
        sql += std::string(", notify_channel_websocket = ") + (*notify_channel_websocket ? "true" : "false");
    if (notify_channel_email.has_value())
        sql += std::string(", notify_channel_email = ") + (*notify_channel_email ? "true" : "false");
    sql +=
        " WHERE microsoft_oid = $2 "
        "RETURNING id, microsoft_oid, email, display_name, department, role, time_display_mode, "
        "          notify_material_assigned, notify_activity_assigned, notify_program_assigned, "
        "          notify_mail_own_activity, notify_mail_department, "
        "          notify_channel_websocket, notify_channel_email, "
        "          created_at, updated_at";

    const char *params[2] = {display_name.c_str(), oid.c_str()};
    PGresult *res = PQexecParams(conn_, sql.c_str(), 2, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- update_user_admin ------------------------------------------------------

std::optional<UserRecord> Database::update_user_admin(const std::string &id,
                                                      const std::string &display_name,
                                                      const std::optional<std::string> &department,
                                                      const std::string &role)
{
    ensure_connected();
    std::string dept_str = department ? *department : "";

    std::string sql =
        "UPDATE users SET display_name = $1, department = " +
        (department ? ("'" + dept_str + "'") : std::string("NULL")) +
        ", role = $2"
        " WHERE id = $3 "
        "RETURNING id, microsoft_oid, email, display_name, department, role, time_display_mode, "
        "          notify_material_assigned, notify_activity_assigned, notify_program_assigned, "
        "          notify_mail_own_activity, notify_mail_department, "
        "          notify_channel_websocket, notify_channel_email, "
        "          created_at, updated_at";

    const char *params[3] = {display_name.c_str(), role.c_str(), id.c_str()};
    PGresult *res = PQexecParams(conn_, sql.c_str(), 3, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- delete_user ------------------------------------------------------------

bool Database::delete_user(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM users WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK &&
              std::string(PQcmdTuples(res)) == "1";
    PQclear(res);
    return ok;
}

// ---- Mail template helpers --------------------------------------------------

MailTemplate Database::row_to_mail_template(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    MailTemplate t;
    t.id = col("id") ? col("id") : "";
    t.department = col("department") ? col("department") : "";
    t.subject = col("subject") ? col("subject") : "";
    t.body = col("body") ? col("body") : "";
    if (col("recipients"))
        t.recipients = parse_pg_array(col("recipients"));
    if (col("cc"))
        t.cc = parse_pg_array(col("cc"));
    t.created_at = col("created_at") ? col("created_at") : "";
    t.updated_at = col("updated_at") ? col("updated_at") : "";
    return t;
}

std::vector<MailTemplate> Database::list_mail_templates()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, department, subject, body, recipients, cc, created_at, updated_at "
                           "FROM mail_templates ORDER BY department");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_mail_templates: " + err);
    }
    std::vector<MailTemplate> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_mail_template(res, i));
    PQclear(res);
    return out;
}

std::optional<MailTemplate> Database::get_mail_template_by_department(const std::string &department)
{
    ensure_connected();
    const char *params[1] = {department.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, department, subject, body, recipients, cc, created_at, updated_at "
                                 "FROM mail_templates WHERE department = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailTemplate t = row_to_mail_template(res, 0);
    PQclear(res);
    return t;
}

std::optional<MailTemplate> Database::upsert_mail_template(const std::string &department,
                                                           const std::string &subject,
                                                           const std::string &body,
                                                           const std::vector<std::string> &recipients,
                                                           const std::vector<std::string> &cc)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string recipients_arr = build_arr(recipients);
    std::string cc_arr = build_arr(cc);
    const char *params[5] = {department.c_str(), subject.c_str(), body.c_str(), recipients_arr.c_str(), cc_arr.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO mail_templates (department, subject, body, recipients, cc) "
                                 "VALUES ($1, $2, $3, $4::text[], $5::text[]) "
                                 "ON CONFLICT (department) DO UPDATE SET subject = EXCLUDED.subject, body = EXCLUDED.body, recipients = EXCLUDED.recipients, cc = EXCLUDED.cc "
                                 "RETURNING id, department, subject, body, recipients, cc, created_at, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailTemplate t = row_to_mail_template(res, 0);
    PQclear(res);
    return t;
}

// ---- Event templates ---------------------------------------------------------

EventTemplate Database::row_to_event_template(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    EventTemplate t;
    t.id = col("id") ? col("id") : "";
    t.department = col("department") ? col("department") : "";
    t.title = col("title") ? col("title") : "";
    t.body = col("body") ? col("body") : "";
    t.created_at = col("created_at") ? col("created_at") : "";
    t.updated_at = col("updated_at") ? col("updated_at") : "";
    return t;
}

std::vector<EventTemplate> Database::list_event_templates()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, department, title, body, created_at, updated_at "
                           "FROM event_templates ORDER BY department");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<EventTemplate> out;
    for (int i = 0; i < PQntuples(res); ++i)
        out.push_back(row_to_event_template(res, i));
    PQclear(res);
    return out;
}

std::optional<EventTemplate> Database::get_event_template_by_department(const std::string &department)
{
    ensure_connected();
    const char *params[1] = {department.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, department, title, body, created_at, updated_at "
                                 "FROM event_templates WHERE department = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventTemplate t = row_to_event_template(res, 0);
    PQclear(res);
    return t;
}

std::optional<EventTemplate> Database::upsert_event_template(const std::string &department,
                                                              const std::string &title,
                                                              const std::string &body)
{
    ensure_connected();
    const char *params[3] = {department.c_str(), title.c_str(), body.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO event_templates (department, title, body) "
                                 "VALUES ($1, $2, $3) "
                                 "ON CONFLICT (department) DO UPDATE SET title = EXCLUDED.title, body = EXCLUDED.body "
                                 "RETURNING id, department, title, body, created_at, updated_at",
                                 3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventTemplate t = row_to_event_template(res, 0);
    PQclear(res);
    return t;
}

// ---- Event publications -----------------------------------------------------

EventPublication Database::row_to_event_publication(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    EventPublication p;
    p.id = col("id") ? col("id") : "";
    p.activity_id = col("activity_id") ? col("activity_id") : "";
    p.published_by = col("published_by") ? col("published_by") : "";
    p.title = col("title") ? col("title") : "";
    p.body_html = col("body_html") ? col("body_html") : "";
    p.wp_event_id = col("wp_event_id") ? col("wp_event_id") : "";
    p.published_at = col("published_at") ? col("published_at") : "";
    return p;
}

std::optional<EventPublication> Database::get_event_publication(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, published_by, title, body_html, wp_event_id, published_at "
                                 "FROM event_publications WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventPublication p = row_to_event_publication(res, 0);
    PQclear(res);
    return p;
}

std::optional<EventPublication> Database::upsert_event_publication(const std::string &activity_id,
                                                                    const std::string &published_by,
                                                                    const std::string &title,
                                                                    const std::string &body_html)
{
    ensure_connected();
    const char *params[4] = {activity_id.c_str(), published_by.c_str(), title.c_str(), body_html.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO event_publications (activity_id, published_by, title, body_html) "
                                 "VALUES ($1, $2, $3, $4) "
                                 "ON CONFLICT (activity_id) DO UPDATE SET "
                                 "published_by = EXCLUDED.published_by, title = EXCLUDED.title, "
                                 "body_html = EXCLUDED.body_html, published_at = NOW() "
                                 "RETURNING id, activity_id, published_by, title, body_html, wp_event_id, published_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    EventPublication p = row_to_event_publication(res, 0);
    PQclear(res);
    return p;
}

bool Database::update_event_publication_wp_id(const std::string &activity_id, const std::string &wp_event_id)
{
    ensure_connected();
    const char *params[2] = {wp_event_id.c_str(), activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE event_publications SET wp_event_id = $1 WHERE activity_id = $2",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::delete_event_publication(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM event_publications WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ---- Sent mails log ---------------------------------------------------------

SentMail Database::row_to_sent_mail(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    SentMail m;
    m.id = col("id") ? col("id") : "";
    m.activity_id = col("activity_id") ? col("activity_id") : "";
    m.sender_id = col("sender_id") ? col("sender_id") : "";
    m.sender_email = col("sender_email") ? col("sender_email") : "";
    if (col("to_emails"))
        m.to_emails = parse_pg_array(col("to_emails"));
    if (col("cc_emails"))
        m.cc_emails = parse_pg_array(col("cc_emails"));
    m.subject = col("subject") ? col("subject") : "";
    m.body_html = col("body_html") ? col("body_html") : "";
    m.sent_at = col("sent_at") ? col("sent_at") : "";
    return m;
}

std::optional<SentMail> Database::log_sent_mail(const std::string &activity_id,
                                                const std::string &sender_id,
                                                const std::string &sender_email,
                                                const std::vector<std::string> &to_emails,
                                                const std::vector<std::string> &cc_emails,
                                                const std::string &subject,
                                                const std::string &body_html)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string to_arr = build_arr(to_emails);
    std::string cc_arr = build_arr(cc_emails);
    const char *params[7] = {activity_id.c_str(), sender_id.c_str(), sender_email.c_str(),
                             to_arr.c_str(), cc_arr.c_str(), subject.c_str(), body_html.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO sent_mails (activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html) "
                                 "VALUES ($1, $2, $3, $4::text[], $5::text[], $6, $7) "
                                 "RETURNING id, activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html, sent_at",
                                 7, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        fprintf(stderr, "[log_sent_mail] %s\n", err.c_str());
        return std::nullopt;
    }
    SentMail m = row_to_sent_mail(res, 0);
    PQclear(res);
    return m;
}

std::vector<SentMail> Database::list_sent_mails(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, sender_id, sender_email, to_emails, cc_emails, subject, body_html, sent_at "
                                 "FROM sent_mails WHERE activity_id = $1 ORDER BY sent_at DESC",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<SentMail> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_sent_mail(res, i));
    PQclear(res);
    return out;
}

NotificationRecord Database::row_to_notification(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };

    NotificationRecord n;
    n.id = col("id") ? col("id") : "";
    n.user_id = col("user_id") ? col("user_id") : "";
    n.category = col("category") ? col("category") : "";
    n.title = col("title") ? col("title") : "";
    n.message = col("message") ? col("message") : "";
    if (col("link"))
        n.link = col("link");
    n.payload = nlohmann::json::object();
    if (col("payload"))
    {
        auto parsed = nlohmann::json::parse(col("payload"), nullptr, false);
        if (!parsed.is_discarded())
            n.payload = parsed;
    }
    n.is_read = col("is_read") ? std::string(col("is_read")) == "t" : false;
    n.created_at = col("created_at") ? col("created_at") : "";
    return n;
}

PushSubscriptionRecord Database::row_to_push_subscription(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };

    PushSubscriptionRecord s;
    s.id = col("id") ? col("id") : "";
    s.user_id = col("user_id") ? col("user_id") : "";
    s.endpoint = col("endpoint") ? col("endpoint") : "";
    s.p256dh = col("p256dh") ? col("p256dh") : "";
    s.auth = col("auth") ? col("auth") : "";
    s.created_at = col("created_at") ? col("created_at") : "";
    s.updated_at = col("updated_at") ? col("updated_at") : "";
    return s;
}

void Database::purge_expired_activity_notifications()
{
    PGresult *res = PQexec(conn_,
                           "DELETE FROM notifications n "
                           "USING activities a "
                           "WHERE n.payload ? 'activity_id' "
                           "  AND (n.payload->>'activity_id') = a.id::text "
                           "  AND NOW() >= ((a.date::timestamp) + INTERVAL '1 day')");
    PQclear(res);
}

std::optional<NotificationRecord> Database::create_notification(const std::string &user_id,
                                                                const std::string &category,
                                                                const std::string &title,
                                                                const std::string &message,
                                                                const std::optional<std::string> &link,
                                                                const nlohmann::json &payload)
{
    ensure_connected();
    purge_expired_activity_notifications();
    std::string payload_str = payload.dump();
    std::string link_str = link ? *link : "";
    const char *params[6] = {user_id.c_str(), category.c_str(), title.c_str(), message.c_str(), link_str.c_str(), payload_str.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO notifications (user_id, category, title, message, link, payload) "
                                 "VALUES ($1, $2, $3, $4, NULLIF($5, ''), $6::jsonb) "
                                 "RETURNING id, user_id, category, title, message, link, payload, is_read, created_at",
                                 6, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto out = row_to_notification(res, 0);
    PQclear(res);
    return out;
}

std::vector<NotificationRecord> Database::list_notifications_for_user(const std::string &user_id, int limit)
{
    ensure_connected();
    purge_expired_activity_notifications();
    PGresult *res = nullptr;
    if (limit > 0)
    {
        std::string limit_str = std::to_string(limit);
        const char *params[2] = {user_id.c_str(), limit_str.c_str()};
        res = PQexecParams(conn_,
                           "SELECT id, user_id, category, title, message, link, payload, is_read, created_at "
                           "FROM notifications WHERE user_id = $1 "
                           "ORDER BY created_at DESC LIMIT $2::int",
                           2, nullptr, params, nullptr, nullptr, 0);
    }
    else
    {
        const char *params[1] = {user_id.c_str()};
        res = PQexecParams(conn_,
                           "SELECT id, user_id, category, title, message, link, payload, is_read, created_at "
                           "FROM notifications WHERE user_id = $1 "
                           "ORDER BY created_at DESC",
                           1, nullptr, params, nullptr, nullptr, 0);
    }
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<NotificationRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_notification(res, i));
    PQclear(res);
    return out;
}

bool Database::mark_notification_read(const std::string &user_id, const std::string &notification_id)
{
    ensure_connected();
    purge_expired_activity_notifications();
    const char *params[2] = {user_id.c_str(), notification_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE notifications SET is_read = true "
                                 "WHERE user_id = $1 AND id = $2",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::mark_all_notifications_read(const std::string &user_id)
{
    ensure_connected();
    purge_expired_activity_notifications();
    const char *params[1] = {user_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE notifications SET is_read = true WHERE user_id = $1 AND is_read = false",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<PushSubscriptionRecord> Database::upsert_push_subscription(const std::string &user_id,
                                                                         const std::string &endpoint,
                                                                         const std::string &p256dh,
                                                                         const std::string &auth)
{
    ensure_connected();
    const char *params[4] = {user_id.c_str(), endpoint.c_str(), p256dh.c_str(), auth.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO push_subscriptions (user_id, endpoint, p256dh, auth) "
                                 "VALUES ($1, $2, $3, $4) "
                                 "ON CONFLICT (endpoint) DO UPDATE SET "
                                 "  user_id = EXCLUDED.user_id, "
                                 "  p256dh = EXCLUDED.p256dh, "
                                 "  auth = EXCLUDED.auth, "
                                 "  updated_at = NOW() "
                                 "RETURNING id, user_id, endpoint, p256dh, auth, created_at, updated_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto out = row_to_push_subscription(res, 0);
    PQclear(res);
    return out;
}

bool Database::delete_push_subscription(const std::string &user_id, const std::string &endpoint)
{
    ensure_connected();
    const char *params[2] = {user_id.c_str(), endpoint.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM push_subscriptions WHERE user_id = $1 AND endpoint = $2",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::delete_push_subscription_by_endpoint(const std::string &endpoint)
{
    ensure_connected();
    const char *params[1] = {endpoint.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM push_subscriptions WHERE endpoint = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::vector<PushSubscriptionRecord> Database::list_push_subscriptions_for_user(const std::string &user_id)
{
    ensure_connected();
    const char *params[1] = {user_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, user_id, endpoint, p256dh, auth, created_at, updated_at "
                                 "FROM push_subscriptions WHERE user_id = $1 ORDER BY updated_at DESC",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<PushSubscriptionRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_push_subscription(res, i));
    PQclear(res);
    return out;
}

std::optional<NotificationRecord> Database::get_latest_unread_notification_for_push(const std::string &endpoint,
                                                                                    const std::string &auth)
{
    ensure_connected();
    purge_expired_activity_notifications();
    const char *params[2] = {endpoint.c_str(), auth.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT n.id, n.user_id, n.category, n.title, n.message, n.link, n.payload, n.is_read, n.created_at "
                                 "FROM push_subscriptions ps "
                                 "JOIN notifications n ON n.user_id = ps.user_id "
                                 "WHERE ps.endpoint = $1 AND ps.auth = $2 AND n.is_read = false "
                                 "ORDER BY n.created_at DESC LIMIT 1",
                                 2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    NotificationRecord out = row_to_notification(res, 0);
    PQclear(res);
    return out;
}

// ---- Mail drafts ------------------------------------------------------------

MailDraft Database::row_to_mail_draft(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    MailDraft d;
    d.id = col("id") ? col("id") : "";
    d.activity_id = col("activity_id") ? col("activity_id") : "";
    if (col("recipients"))
        d.recipients = parse_pg_array(col("recipients"));
    if (col("cc"))
        d.cc = parse_pg_array(col("cc"));
    d.subject = col("subject") ? col("subject") : "";
    d.body_html = col("body_html") ? col("body_html") : "";
    d.updated_by = col("updated_by") ? col("updated_by") : "";
    d.updated_at = col("updated_at") ? col("updated_at") : "";
    return d;
}

std::optional<MailDraft> Database::get_mail_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, recipients, cc, subject, body_html, updated_by, updated_at "
                                 "FROM mail_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailDraft d = row_to_mail_draft(res, 0);
    PQclear(res);
    return d;
}

std::optional<MailDraft> Database::upsert_mail_draft(const std::string &activity_id,
                                                     const std::vector<std::string> &recipients,
                                                     const std::vector<std::string> &cc,
                                                     const std::string &subject,
                                                     const std::string &body_html,
                                                     const std::string &updated_by)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string recip_arr = build_arr(recipients);
    std::string cc_arr = build_arr(cc);
    const char *params[6] = {activity_id.c_str(), recip_arr.c_str(), cc_arr.c_str(), subject.c_str(),
                             body_html.c_str(), updated_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO mail_drafts (activity_id, recipients, cc, subject, body_html, updated_by) "
                                 "VALUES ($1, $2::text[], $3::text[], $4, $5, $6) "
                                 "ON CONFLICT (activity_id) DO UPDATE SET recipients = EXCLUDED.recipients, cc = EXCLUDED.cc, "
                                 "subject = EXCLUDED.subject, body_html = EXCLUDED.body_html, updated_by = EXCLUDED.updated_by "
                                 "RETURNING id, activity_id, recipients, cc, subject, body_html, updated_by, updated_at",
                                 6, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        fprintf(stderr, "[upsert_mail_draft] %s\n", err.c_str());
        return std::nullopt;
    }
    MailDraft d = row_to_mail_draft(res, 0);
    PQclear(res);
    return d;
}

bool Database::delete_mail_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM mail_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

FormDraft Database::row_to_form_draft(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    FormDraft d;
    d.id = col("id") ? col("id") : "";
    d.activity_id = col("activity_id") ? col("activity_id") : "";
    d.form_type = col("form_type") ? col("form_type") : "";
    d.title = col("title") ? col("title") : "";
    d.questions_json = col("questions_json") ? col("questions_json") : "[]";
    d.updated_by = col("updated_by") ? col("updated_by") : "";
    d.updated_at = col("updated_at") ? col("updated_at") : "";
    return d;
}

std::optional<FormDraft> Database::get_form_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, form_type, title, questions_json, updated_by, updated_at "
                                 "FROM form_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormDraft d = row_to_form_draft(res, 0);
    PQclear(res);
    return d;
}

std::optional<FormDraft> Database::upsert_form_draft(const std::string &activity_id,
                                                     const std::string &form_type,
                                                     const std::string &title,
                                                     const std::string &questions_json,
                                                     const std::string &updated_by)
{
    ensure_connected();
    const char *params[5] = {activity_id.c_str(), form_type.c_str(), title.c_str(),
                             questions_json.c_str(), updated_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO form_drafts (activity_id, form_type, title, questions_json, updated_by) "
                                 "VALUES ($1, $2, $3, $4::jsonb, $5) "
                                 "ON CONFLICT (activity_id) DO UPDATE SET form_type = EXCLUDED.form_type, "
                                 "title = EXCLUDED.title, questions_json = EXCLUDED.questions_json, updated_by = EXCLUDED.updated_by "
                                 "RETURNING id, activity_id, form_type, title, questions_json, updated_by, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        fprintf(stderr, "[upsert_form_draft] %s\n", err.c_str());
        return std::nullopt;
    }
    FormDraft d = row_to_form_draft(res, 0);
    PQclear(res);
    return d;
}

bool Database::delete_form_draft(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM form_drafts WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ---- send_mail via Microsoft Graph ------------------------------------------

static size_t graph_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *resp = static_cast<std::string *>(userdata);
    resp->append(ptr, size * nmemb);
    return size * nmemb;
}

bool Database::send_mail(const std::string &access_token,
                         const std::string &from_email,
                         const std::vector<std::string> &to_emails,
                         const std::vector<std::string> &cc_emails,
                         const std::string &subject,
                         const std::string &body_html)
{
    (void)from_email; // Graph /me/sendMail sends as the authenticated user

    // Build Graph API JSON payload
    nlohmann::json to_arr = nlohmann::json::array();
    for (auto &email : to_emails)
    {
        to_arr.push_back({{"emailAddress", {{"address", email}}}});
    }
    nlohmann::json cc_arr = nlohmann::json::array();
    for (auto &email : cc_emails)
    {
        cc_arr.push_back({{"emailAddress", {{"address", email}}}});
    }

    nlohmann::json message = {
        {"subject", subject},
        {"body", {{"contentType", "HTML"}, {"content", body_html}}},
        {"toRecipients", to_arr}};
    if (!cc_arr.empty())
        message["ccRecipients"] = cc_arr;
    nlohmann::json payload = {
        {"message", message},
        {"saveToSentItems", true}};

    std::string url = "https://graph.microsoft.com/v1.0/me/sendMail";
    std::string json_body = payload.dump();
    std::string response_body;

    CURL *curl = curl_easy_init();
    if (!curl)
        return false;

    struct curl_slist *headers = nullptr;
    std::string auth_hdr = "Authorization: Bearer " + access_token;
    headers = curl_slist_append(headers, auth_hdr.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, graph_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || (http_code != 202 && http_code != 200))
    {
        fprintf(stderr, "[send_mail] HTTP %ld: %s\n", http_code, response_body.c_str());
        return false;
    }
    return true;
}

// ---- Attachment helpers -----------------------------------------------------

Attachment Database::row_to_attachment(PGresult *res, int row)
{
    auto col = [&](const char *name) -> std::string
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return "";
        return PQgetvalue(res, row, c);
    };
    Attachment att;
    att.id = col("id");
    att.activity_id = col("activity_id");
    att.filename = col("filename");
    att.content_type = col("content_type");
    att.created_at = col("created_at");
    return att;
}

std::vector<Attachment> Database::list_attachments(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, filename, content_type, created_at "
                                 "FROM attachments WHERE activity_id = $1 ORDER BY created_at",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<Attachment> out;
    int n = PQntuples(res);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_attachment(res, i));
    PQclear(res);
    return out;
}

std::optional<Attachment> Database::add_attachment(const std::string &activity_id,
                                                   const std::string &filename,
                                                   const std::string &content_type,
                                                   const std::string &data_base64)
{
    ensure_connected();
    const char *params[4] = {
        activity_id.c_str(),
        filename.c_str(),
        content_type.c_str(),
        data_base64.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO attachments (activity_id, filename, content_type, data) "
                                 "VALUES ($1, $2, $3, decode($4, 'base64')) "
                                 "RETURNING id, activity_id, filename, content_type, created_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    Attachment att = row_to_attachment(res, 0);
    PQclear(res);
    return att;
}

std::optional<AttachmentData> Database::get_attachment_data(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT activity_id, filename, content_type, data FROM attachments WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    AttachmentData ad;
    ad.activity_id = PQgetvalue(res, 0, 0);
    ad.filename = PQgetvalue(res, 0, 1);
    ad.content_type = PQgetvalue(res, 0, 2);
    // data column is bytea returned as hex-escaped text; use PQunescapeBytea
    size_t len = 0;
    unsigned char *unescaped = PQunescapeBytea(
        reinterpret_cast<const unsigned char *>(PQgetvalue(res, 0, 3)), &len);
    if (unescaped)
    {
        ad.data.assign(unescaped, unescaped + len);
        PQfreemem(unescaped);
    }
    PQclear(res);
    return ad;
}

bool Database::delete_attachment(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM attachments WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ── Departments CRUD ────────────────────────────────────────────────────────

DepartmentRecord Database::row_to_department(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    DepartmentRecord d;
    d.name = col("name") ? col("name") : "";
    d.color = col("color") ? col("color") : "#6b7280";
    if (const char *midata_group_id = col("midata_group_id"))
        d.midata_group_id = std::string(midata_group_id);
    if (const char *midata_child_roles = col("midata_child_roles"))
        d.midata_child_roles = parse_pg_array(midata_child_roles);
    return d;
}

std::vector<DepartmentRecord> Database::list_departments()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT name, color, midata_group_id, midata_child_roles FROM departments ORDER BY name");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_departments: " + err);
    }
    std::vector<DepartmentRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_department(res, i));
    PQclear(res);
    return out;
}

std::optional<DepartmentRecord> Database::create_department(const std::string &name,
                                                            const std::string &color,
                                                            const std::optional<std::string> &midata_group_id,
                                                            const std::vector<std::string> &midata_child_roles)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string child_roles_arr = build_arr(midata_child_roles);
    const char *params[4] = {name.c_str(), color.c_str(), midata_group_id ? midata_group_id->c_str() : nullptr, child_roles_arr.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO departments (name, color, midata_group_id, midata_child_roles) VALUES ($1, $2, $3, $4::text[]) "
                                 "RETURNING name, color, midata_group_id, midata_child_roles",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto d = row_to_department(res, 0);
    PQclear(res);
    return d;
}

std::optional<DepartmentRecord> Database::update_department(const std::string &name, const std::string &new_name,
                                                            const std::string &color,
                                                            const std::optional<std::string> &midata_group_id,
                                                            const std::vector<std::string> &midata_child_roles)
{
    ensure_connected();
    auto build_arr = [](const std::vector<std::string> &v)
    {
        std::string s = "{";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i > 0)
                s += ",";
            s += "\"" + v[i] + "\"";
        }
        s += "}";
        return s;
    };
    std::string child_roles_arr = build_arr(midata_child_roles);
    const char *params[5] = {new_name.c_str(), color.c_str(), midata_group_id ? midata_group_id->c_str() : nullptr, child_roles_arr.c_str(), name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE departments SET name = $1, color = $2, midata_group_id = $3, midata_child_roles = $4::text[] WHERE name = $5 "
                                 "RETURNING name, color, midata_group_id, midata_child_roles",
                                 5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto d = row_to_department(res, 0);
    PQclear(res);
    return d;
}

bool Database::delete_department(const std::string &name)
{
    ensure_connected();
    const char *params[1] = {name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM departments WHERE name = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK &&
              std::string(PQcmdTuples(res)) == "1";
    PQclear(res);
    return ok;
}

bool Database::delete_department_with_transfers(
    const std::string &name,
    const std::string &transfer_activities_to,
    bool delete_activities,
    const std::string &transfer_users_to,
    bool delete_users)
{
    ensure_connected();

    PGresult *r = PQexec(conn_, "BEGIN");
    PQclear(r);

    try
    {
        // Transfer or delete activities
        if (!transfer_activities_to.empty())
        {
            const char *p[2] = {transfer_activities_to.c_str(), name.c_str()};
            r = PQexecParams(conn_,
                             "UPDATE activities SET department = $1 WHERE department = $2",
                             2, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::string err = PQerrorMessage(conn_);
                PQclear(r);
                throw std::runtime_error(err);
            }
            PQclear(r);
        }
        else if (delete_activities)
        {
            const char *p[1] = {name.c_str()};
            r = PQexecParams(conn_,
                             "DELETE FROM activities WHERE department = $1",
                             1, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::string err = PQerrorMessage(conn_);
                PQclear(r);
                throw std::runtime_error(err);
            }
            PQclear(r);
        }

        // Transfer or delete users
        if (!transfer_users_to.empty())
        {
            const char *p[2] = {transfer_users_to.c_str(), name.c_str()};
            r = PQexecParams(conn_,
                             "UPDATE users SET department = $1 WHERE department = $2",
                             2, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::string err = PQerrorMessage(conn_);
                PQclear(r);
                throw std::runtime_error(err);
            }
            PQclear(r);
        }
        else if (delete_users)
        {
            const char *p[1] = {name.c_str()};
            r = PQexecParams(conn_,
                             "DELETE FROM users WHERE department = $1",
                             1, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::string err = PQerrorMessage(conn_);
                PQclear(r);
                throw std::runtime_error(err);
            }
            PQclear(r);
        }

        // Delete the department (cascades to mail_templates, role_dept_access)
        const char *p[1] = {name.c_str()};
        r = PQexecParams(conn_,
                         "DELETE FROM departments WHERE name = $1",
                         1, nullptr, p, nullptr, nullptr, 0);
        bool ok = PQresultStatus(r) == PGRES_COMMAND_OK &&
                  std::string(PQcmdTuples(r)) == "1";
        PQclear(r);

        if (!ok)
        {
            PGresult *rb = PQexec(conn_, "ROLLBACK");
            PQclear(rb);
            return false;
        }

        r = PQexec(conn_, "COMMIT");
        PQclear(r);
        return true;
    }
    catch (...)
    {
        PGresult *rb = PQexec(conn_, "ROLLBACK");
        PQclear(rb);
        throw;
    }
}

// ── Roles CRUD ──────────────────────────────────────────────────────────────

RoleRecord Database::row_to_role(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    RoleRecord r;
    r.name = col("name") ? col("name") : "";
    r.color = col("color") ? col("color") : "#6b7280";
    r.sort_order = col("sort_order") ? std::stoi(col("sort_order")) : 0;
    return r;
}

std::vector<RoleRecord> Database::list_roles()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT name, color, sort_order FROM roles ORDER BY sort_order, name");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_roles: " + err);
    }
    std::vector<RoleRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_role(res, i));
    PQclear(res);
    return out;
}

std::optional<RoleRecord> Database::create_role(const std::string &name, const std::string &color)
{
    ensure_connected();
    exec_or_throw(conn_, "BEGIN", "create_role BEGIN");
    try
    {
        PGresult *max_res = PQexec(conn_, "SELECT COALESCE(MAX(sort_order), 0) FROM roles");
        int next_sort_order = 1;
        if (PQresultStatus(max_res) == PGRES_TUPLES_OK && PQntuples(max_res) > 0)
            next_sort_order = std::stoi(PQgetvalue(max_res, 0, 0)) + 1;
        PQclear(max_res);

        std::string sort_order_str = std::to_string(next_sort_order);
        const char *params[3] = {name.c_str(), color.c_str(), sort_order_str.c_str()};
        PGresult *res = PQexecParams(conn_,
                                     "INSERT INTO roles (name, color, sort_order) VALUES ($1, $2, $3::integer) "
                                     "RETURNING name, color, sort_order",
                                     3, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            PQclear(res);
            PQexec(conn_, "ROLLBACK");
            return std::nullopt;
        }
        auto r = row_to_role(res, 0);
        PQclear(res);

        // Also create default role_permissions entry
        const char *rp_params[1] = {name.c_str()};
        PGresult *rp = PQexecParams(conn_,
                                    "INSERT INTO role_permissions (role) VALUES ($1) ON CONFLICT DO NOTHING",
                                    1, nullptr, rp_params, nullptr, nullptr, 0);
        PQclear(rp);

        exec_or_throw(conn_, "COMMIT", "create_role COMMIT");
        return r;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

std::optional<RoleRecord> Database::update_role(const std::string &name, const std::string &new_name,
                                                const std::string &color)
{
    ensure_connected();
    const char *params[3] = {new_name.c_str(), color.c_str(), name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE roles SET name = $1, color = $2 WHERE name = $3 "
                                 "RETURNING name, color, sort_order",
                                 3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto r = row_to_role(res, 0);
    PQclear(res);
    return r;
}

bool Database::move_role(const std::string &name, bool move_up)
{
    ensure_connected();
    exec_or_throw(conn_, "BEGIN", "move_role BEGIN");
    try
    {
        const char *params[1] = {name.c_str()};
        PGresult *current_res = PQexecParams(conn_,
                                             "SELECT sort_order FROM roles WHERE name = $1",
                                             1, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(current_res) != PGRES_TUPLES_OK || PQntuples(current_res) == 0)
        {
            PQclear(current_res);
            PQexec(conn_, "ROLLBACK");
            return false;
        }
        int current_order = std::stoi(PQgetvalue(current_res, 0, 0));
        PQclear(current_res);

        if (name == "admin")
        {
            PQexec(conn_, "ROLLBACK");
            return false;
        }

        std::string direction_sql = move_up ? "<" : ">";
        std::string order_sql = move_up ? "DESC" : "ASC";
        std::string current_order_str = std::to_string(current_order);
        const char *swap_params[1] = {current_order_str.c_str()};
        PGresult *swap_res = PQexecParams(conn_,
                                          ("SELECT name, sort_order FROM roles WHERE name <> 'admin' AND sort_order " + direction_sql + " $1::integer ORDER BY sort_order " + order_sql + " LIMIT 1").c_str(),
                                          1, nullptr, swap_params, nullptr, nullptr, 0);
        if (PQresultStatus(swap_res) != PGRES_TUPLES_OK || PQntuples(swap_res) == 0)
        {
            PQclear(swap_res);
            PGresult *commit = PQexec(conn_, "COMMIT");
            PQclear(commit);
            return true;
        }
        std::string other_name = PQgetvalue(swap_res, 0, 0);
        int other_order = std::stoi(PQgetvalue(swap_res, 0, 1));
        PQclear(swap_res);

        std::string neg_current = std::to_string(-current_order - 1000);
        const char *tmp_params1[2] = {neg_current.c_str(), name.c_str()};
        PGresult *tmp1 = PQexecParams(conn_,
                                      "UPDATE roles SET sort_order = $1::integer WHERE name = $2",
                                      2, nullptr, tmp_params1, nullptr, nullptr, 0);
        PQclear(tmp1);

        std::string current_to_other = std::to_string(current_order);
        const char *tmp_params2[2] = {current_to_other.c_str(), other_name.c_str()};
        PGresult *tmp2 = PQexecParams(conn_,
                                      "UPDATE roles SET sort_order = $1::integer WHERE name = $2",
                                      2, nullptr, tmp_params2, nullptr, nullptr, 0);
        PQclear(tmp2);

        std::string other_to_current = std::to_string(other_order);
        const char *tmp_params3[2] = {other_to_current.c_str(), name.c_str()};
        PGresult *tmp3 = PQexecParams(conn_,
                                      "UPDATE roles SET sort_order = $1::integer WHERE name = $2",
                                      2, nullptr, tmp_params3, nullptr, nullptr, 0);
        PQclear(tmp3);

        exec_or_throw(conn_, "COMMIT", "move_role COMMIT");
        return true;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

bool Database::reorder_roles(const std::vector<std::string> &ordered_names)
{
    ensure_connected();
    exec_or_throw(conn_, "BEGIN", "reorder_roles BEGIN");
    try
    {
        // First pass: set all non-admin roles to high temp values to avoid UNIQUE conflicts
        int tmp = 10000;
        for (const auto &name : ordered_names)
        {
            if (name == "admin")
                continue;
            std::string tmp_str = std::to_string(tmp);
            const char *params[2] = {tmp_str.c_str(), name.c_str()};
            PGresult *r = PQexecParams(conn_,
                                       "UPDATE roles SET sort_order = $1::integer WHERE name = $2",
                                       2, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::string err = PQerrorMessage(conn_);
                PQclear(r);
                throw std::runtime_error("reorder_roles tmp: " + err);
            }
            PQclear(r);
            ++tmp;
        }
        // Second pass: assign correct sort_order values
        int order = 1;
        for (const auto &name : ordered_names)
        {
            if (name == "admin")
                continue;
            std::string order_str = std::to_string(order);
            const char *params[2] = {order_str.c_str(), name.c_str()};
            PGresult *r = PQexecParams(conn_,
                                       "UPDATE roles SET sort_order = $1::integer WHERE name = $2",
                                       2, nullptr, params, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_COMMAND_OK)
            {
                std::string err = PQerrorMessage(conn_);
                PQclear(r);
                throw std::runtime_error("reorder_roles set: " + err);
            }
            PQclear(r);
            ++order;
        }
        exec_or_throw(conn_, "COMMIT", "reorder_roles COMMIT");
        return true;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

bool Database::delete_role(const std::string &name,
                           const std::string &transfer_users_to,
                           bool delete_users)
{
    ensure_connected();
    exec_or_throw(conn_, "BEGIN", "delete_role BEGIN");
    try
    {
        const char *params[1] = {name.c_str()};
        PGresult *order_res = PQexecParams(conn_,
                                           "SELECT sort_order FROM roles WHERE name = $1",
                                           1, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(order_res) != PGRES_TUPLES_OK || PQntuples(order_res) == 0)
        {
            PQclear(order_res);
            PQexec(conn_, "ROLLBACK");
            return false;
        }
        int deleted_order = std::stoi(PQgetvalue(order_res, 0, 0));
        PQclear(order_res);

        // Transfer or delete users
        if (!transfer_users_to.empty())
        {
            const char *xfer_params[2] = {transfer_users_to.c_str(), name.c_str()};
            PGresult *xfer = PQexecParams(conn_,
                                          "UPDATE users SET role = $1 WHERE role = $2",
                                          2, nullptr, xfer_params, nullptr, nullptr, 0);
            PQclear(xfer);
        }
        else if (delete_users)
        {
            PGresult *del = PQexecParams(conn_,
                                         "DELETE FROM users WHERE role = $1",
                                         1, nullptr, params, nullptr, nullptr, 0);
            PQclear(del);
        }
        else
        {
            // Default: set users to 'Mitglied'
            const char *def_params[2] = {"Mitglied", name.c_str()};
            PGresult *def = PQexecParams(conn_,
                                         "UPDATE users SET role = $1 WHERE role = $2",
                                         2, nullptr, def_params, nullptr, nullptr, 0);
            PQclear(def);
        }

        PGresult *res = PQexecParams(conn_,
                                     "DELETE FROM roles WHERE name = $1",
                                     1, nullptr, params, nullptr, nullptr, 0);
        bool ok = PQresultStatus(res) == PGRES_COMMAND_OK &&
                  std::string(PQcmdTuples(res)) == "1";
        PQclear(res);
        if (!ok)
        {
            PQexec(conn_, "ROLLBACK");
            return false;
        }

        std::string deleted_order_str = std::to_string(deleted_order);
        const char *shift_params[1] = {deleted_order_str.c_str()};
        PGresult *shift_res = PQexecParams(conn_,
                                           "UPDATE roles SET sort_order = sort_order - 1 WHERE sort_order > $1::integer",
                                           1, nullptr, shift_params, nullptr, nullptr, 0);
        PQclear(shift_res);

        exec_or_throw(conn_, "COMMIT", "delete_role COMMIT");
        return true;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

// ── Role permissions ────────────────────────────────────────────────────────

RolePermission Database::row_to_role_perm(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    RolePermission rp;
    rp.role = col("role") ? col("role") : "";
    rp.can_read_own_dept = col("can_read_own_dept") && std::string(col("can_read_own_dept")) == "t";
    rp.can_write_own_dept = col("can_write_own_dept") && std::string(col("can_write_own_dept")) == "t";
    rp.can_read_all_depts = col("can_read_all_depts") && std::string(col("can_read_all_depts")) == "t";
    rp.can_write_all_depts = col("can_write_all_depts") && std::string(col("can_write_all_depts")) == "t";
    rp.activity_read_scope = col("activity_read_scope") ? col("activity_read_scope") : "none";
    rp.activity_create_scope = col("activity_create_scope") ? col("activity_create_scope") : "none";
    rp.activity_edit_scope = col("activity_edit_scope") ? col("activity_edit_scope") : "none";
    rp.mail_send_scope = col("mail_send_scope") ? col("mail_send_scope") : "none";
    rp.mail_templates_scope = col("mail_templates_scope") ? col("mail_templates_scope") : "none";
    rp.form_scope = col("form_scope") ? col("form_scope") : "none";
    rp.form_templates_scope = col("form_templates_scope") ? col("form_templates_scope") : "none";
    rp.event_templates_scope = col("event_templates_scope") ? col("event_templates_scope") : "none";
    rp.event_publish_scope = col("event_publish_scope") ? col("event_publish_scope") : "none";
    rp.user_dept_scope = col("user_dept_scope") ? col("user_dept_scope") : "none";
    rp.user_role_scope = col("user_role_scope") ? col("user_role_scope") : "none";
    rp.locations_manage_scope = col("locations_manage_scope") ? col("locations_manage_scope") : "none";
    return rp;
}

std::vector<RolePermission> Database::list_role_permissions()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT role, can_read_own_dept, can_write_own_dept, "
                           "       can_read_all_depts, can_write_all_depts, "
                           "       activity_read_scope, activity_create_scope, "
                           "       activity_edit_scope, "
                           "       mail_send_scope, "
                           "       mail_templates_scope, form_scope, form_templates_scope, "
                           "       event_templates_scope, event_publish_scope, "
                           "       user_dept_scope, user_role_scope, locations_manage_scope "
                           "FROM role_permissions ORDER BY role");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_role_permissions: " + err);
    }
    std::vector<RolePermission> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_role_perm(res, i));
    PQclear(res);
    return out;
}

std::optional<RolePermission> Database::get_role_permission(const std::string &role)
{
    ensure_connected();
    const char *params[] = {role.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT role, can_read_own_dept, can_write_own_dept, "
                                 "       can_read_all_depts, can_write_all_depts, "
                                 "       activity_read_scope, activity_create_scope, "
                                 "       activity_edit_scope, "
                                 "       mail_send_scope, "
                                 "       mail_templates_scope, form_scope, form_templates_scope, "
                                 "       event_templates_scope, "
                                 "       user_dept_scope, user_role_scope, locations_manage_scope "
                                 "FROM role_permissions WHERE role = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("get_role_permission: " + err);
    }
    std::optional<RolePermission> out;
    if (PQntuples(res) > 0)
        out = row_to_role_perm(res, 0);
    PQclear(res);
    return out;
}

bool Database::update_role_permission(const std::string &role,
                                      bool can_read_own_dept,
                                      bool can_write_own_dept,
                                      bool can_read_all_depts,
                                      bool can_write_all_depts,
                                      const std::string &activity_read_scope,
                                      const std::string &activity_create_scope,
                                      const std::string &activity_edit_scope,
                                      const std::string &mail_send_scope,
                                      const std::string &mail_templates_scope,
                                      const std::string &form_scope,
                                      const std::string &form_templates_scope,
                                      const std::string &event_templates_scope,
                                      const std::string &event_publish_scope,
                                      const std::string &user_dept_scope,
                                      const std::string &user_role_scope,
                                      const std::string &locations_manage_scope)
{
    ensure_connected();
    const char *p1 = role.c_str();
    const char *p2 = can_read_own_dept ? "true" : "false";
    const char *p3 = can_write_own_dept ? "true" : "false";
    const char *p4 = can_read_all_depts ? "true" : "false";
    const char *p5 = can_write_all_depts ? "true" : "false";
    const char *p6 = activity_read_scope.c_str();
    const char *p7 = activity_create_scope.c_str();
    const char *p8 = activity_edit_scope.c_str();
    const char *p9 = mail_send_scope.c_str();
    const char *p10 = mail_templates_scope.c_str();
    const char *p11 = form_scope.c_str();
    const char *p12 = form_templates_scope.c_str();
    const char *p13 = event_templates_scope.c_str();
    const char *p14 = event_publish_scope.c_str();
    const char *p15 = user_dept_scope.c_str();
    const char *p16 = user_role_scope.c_str();
    const char *p17 = locations_manage_scope.c_str();
    const char *params[17] = {p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, "
                                 "can_read_all_depts, can_write_all_depts, "
                                 "activity_read_scope, activity_create_scope, activity_edit_scope, "
                                 "mail_send_scope, mail_templates_scope, form_scope, form_templates_scope, "
                                 "event_templates_scope, event_publish_scope, "
                                 "user_dept_scope, user_role_scope, locations_manage_scope) "
                                 "VALUES ($1, $2::boolean, $3::boolean, $4::boolean, $5::boolean, "
                                 "$6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17) "
                                 "ON CONFLICT (role) DO UPDATE SET "
                                 "can_read_own_dept = $2::boolean, can_write_own_dept = $3::boolean, "
                                 "can_read_all_depts = $4::boolean, can_write_all_depts = $5::boolean, "
                                 "activity_read_scope = $6, activity_create_scope = $7, activity_edit_scope = $8, "
                                 "mail_send_scope = $9, mail_templates_scope = $10, "
                                 "form_scope = $11, form_templates_scope = $12, "
                                 "event_templates_scope = $13, event_publish_scope = $14, "
                                 "user_dept_scope = $15, user_role_scope = $16, "
                                 "locations_manage_scope = $17",
                                 17, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ── Role department access ──────────────────────────────────────────────────

RoleDeptAccess Database::row_to_role_dept_access(PGresult *res, int row)
{
    auto col = [&](const char *name) -> const char *
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    };
    RoleDeptAccess rda;
    rda.role = col("role") ? col("role") : "";
    rda.department = col("department") ? col("department") : "";
    rda.can_read = col("can_read") && std::string(col("can_read")) == "t";
    rda.can_write = col("can_write") && std::string(col("can_write")) == "t";
    return rda;
}

std::vector<RoleDeptAccess> Database::list_role_dept_access(const std::string &role)
{
    ensure_connected();
    const char *params[1] = {role.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT role, department, can_read, can_write "
                                 "FROM role_dept_access WHERE role = $1 ORDER BY department",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    std::vector<RoleDeptAccess> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_role_dept_access(res, i));
    PQclear(res);
    return out;
}

bool Database::set_role_dept_access(const std::string &role, const std::string &department,
                                    bool can_read, bool can_write)
{
    ensure_connected();
    const char *r = can_read ? "true" : "false";
    const char *w = can_write ? "true" : "false";
    const char *params[4] = {role.c_str(), department.c_str(), r, w};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO role_dept_access (role, department, can_read, can_write) "
                                 "VALUES ($1, $2, $3::boolean, $4::boolean) "
                                 "ON CONFLICT (role, department) DO UPDATE SET can_read = $3::boolean, can_write = $4::boolean",
                                 4, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ── Forms ─────────────────────────────────────────────────────────────────────

FormQuestion Database::row_to_form_question(PGresult *res, int row)
{
    FormQuestion q;
    q.id = PQgetvalue(res, row, 0);
    q.form_id = PQgetvalue(res, row, 1);
    q.question_text = PQgetvalue(res, row, 2);
    q.question_type = PQgetvalue(res, row, 3);
    q.position = std::stoi(PQgetvalue(res, row, 4));
    q.is_required = std::string(PQgetvalue(res, row, 5)) == "t";
    const char *meta = PQgetvalue(res, row, 6);
    q.metadata = nlohmann::json::parse(meta && *meta ? meta : "{}", nullptr, false);
    if (q.metadata.is_discarded())
        q.metadata = nlohmann::json::object();
    q.created_at = PQgetvalue(res, row, 7);
    return q;
}

SignupForm Database::row_to_signup_form(PGresult *res, int row)
{
    SignupForm f;
    f.id = PQgetvalue(res, row, 0);
    f.activity_id = PQgetvalue(res, row, 1);
    f.public_slug = PQgetvalue(res, row, 2);
    f.form_type = PQgetvalue(res, row, 3);
    f.title = PQgetvalue(res, row, 4);
    f.created_by = PQgetvalue(res, row, 5);
    f.created_at = PQgetvalue(res, row, 6);
    f.updated_at = PQgetvalue(res, row, 7);
    return f;
}

FormResponse Database::row_to_form_response(PGresult *res, int row)
{
    FormResponse r;
    r.id = PQgetvalue(res, row, 0);
    r.form_id = PQgetvalue(res, row, 1);
    r.submission_mode = PQgetvalue(res, row, 2);
    r.submitted_at = PQgetvalue(res, row, 3);
    r.user_agent = PQnfields(res) > 4 ? PQgetvalue(res, row, 4) : "";
    r.ip_address = PQnfields(res) > 5 ? PQgetvalue(res, row, 5) : "";
    return r;
}

FormTemplate Database::row_to_form_template(PGresult *res, int row)
{
    FormTemplate t;
    t.id = PQgetvalue(res, row, 0);
    t.name = PQgetvalue(res, row, 1);
    t.department = PQgetvalue(res, row, 2);
    t.form_type = PQgetvalue(res, row, 3);
    const char *cfg = PQgetvalue(res, row, 4);
    t.template_config = nlohmann::json::parse(cfg && *cfg ? cfg : "[]", nullptr, false);
    if (t.template_config.is_discarded())
        t.template_config = nlohmann::json::array();
    t.is_default = std::string(PQgetvalue(res, row, 5)) == "t";
    t.created_by = PQgetvalue(res, row, 6);
    t.created_at = PQgetvalue(res, row, 7);
    t.updated_at = PQgetvalue(res, row, 8);
    return t;
}

void Database::attach_questions_single(SignupForm &f)
{
    ensure_connected();
    const char *params[1] = {f.id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, form_id, question_text, question_type, position, is_required, metadata, created_at "
                                 "FROM form_questions WHERE form_id = $1 ORDER BY position",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return;
    }
    int n = PQntuples(res);
    f.questions.reserve(n);
    for (int i = 0; i < n; ++i)
        f.questions.push_back(row_to_form_question(res, i));
    PQclear(res);
}

std::optional<SignupForm> Database::get_form_for_activity(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at "
                                 "FROM signup_forms WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);
    attach_questions_single(f);
    return f;
}

std::optional<SignupForm> Database::get_form_by_id(const std::string &form_id)
{
    ensure_connected();
    const char *params[1] = {form_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at "
                                 "FROM signup_forms WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);
    attach_questions_single(f);
    return f;
}

std::optional<SignupForm> Database::get_form_for_public_slug(const std::string &public_slug)
{
    ensure_connected();
    const char *params[1] = {public_slug.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at "
                                 "FROM signup_forms WHERE public_slug = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);
    attach_questions_single(f);
    return f;
}

std::optional<SignupForm> Database::create_form(const std::string &activity_id,
                                                const std::string &form_type,
                                                const std::string &title,
                                                const std::string &created_by,
                                                const std::vector<FormQuestion> &questions)
{
    ensure_connected();
    const char *params[4] = {activity_id.c_str(), form_type.c_str(), title.c_str(), created_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO signup_forms (activity_id, form_type, title, created_by) "
                                 "VALUES ($1, $2, $3, $4::uuid) "
                                 "RETURNING id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);

    for (const auto &q : questions)
    {
        std::string meta_str = q.metadata.dump();
        std::string pos_str = std::to_string(q.position);
        const char *req_str = q.is_required ? "true" : "false";
        const char *qparams[6] = {
            f.id.c_str(), q.question_text.c_str(), q.question_type.c_str(),
            pos_str.c_str(), req_str, meta_str.c_str()};
        PGresult *qres = PQexecParams(conn_,
                                      "INSERT INTO form_questions (form_id, question_text, question_type, position, is_required, metadata) "
                                      "VALUES ($1, $2, $3, $4::int, $5::boolean, $6::jsonb) "
                                      "RETURNING id, form_id, question_text, question_type, position, is_required, metadata, created_at",
                                      6, nullptr, qparams, nullptr, nullptr, 0);
        if (PQresultStatus(qres) == PGRES_TUPLES_OK)
            f.questions.push_back(row_to_form_question(qres, 0));
        PQclear(qres);
    }
    return f;
}

std::optional<SignupForm> Database::update_form(const std::string &activity_id,
                                                const std::string &form_type,
                                                const std::string &title,
                                                const std::vector<FormQuestion> &questions)
{
    ensure_connected();
    const char *params[3] = {activity_id.c_str(), form_type.c_str(), title.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE signup_forms SET form_type=$2, title=$3 WHERE activity_id=$1 "
                                 "RETURNING id, activity_id, public_slug, form_type, title, created_by::text, created_at, updated_at",
                                 3, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    SignupForm f = row_to_signup_form(res, 0);
    PQclear(res);

    const char *dparams[1] = {f.id.c_str()};
    PGresult *dres = PQexecParams(conn_,
                                  "DELETE FROM form_questions WHERE form_id = $1",
                                  1, nullptr, dparams, nullptr, nullptr, 0);
    PQclear(dres);

    for (const auto &q : questions)
    {
        std::string meta_str = q.metadata.dump();
        std::string pos_str = std::to_string(q.position);
        const char *req_str = q.is_required ? "true" : "false";
        const char *qparams[6] = {
            f.id.c_str(), q.question_text.c_str(), q.question_type.c_str(),
            pos_str.c_str(), req_str, meta_str.c_str()};
        PGresult *qres = PQexecParams(conn_,
                                      "INSERT INTO form_questions (form_id, question_text, question_type, position, is_required, metadata) "
                                      "VALUES ($1, $2, $3, $4::int, $5::boolean, $6::jsonb) "
                                      "RETURNING id, form_id, question_text, question_type, position, is_required, metadata, created_at",
                                      6, nullptr, qparams, nullptr, nullptr, 0);
        if (PQresultStatus(qres) == PGRES_TUPLES_OK)
            f.questions.push_back(row_to_form_question(qres, 0));
        PQclear(qres);
    }
    return f;
}

bool Database::delete_form(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM signup_forms WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<FormResponse> Database::submit_response(const std::string &form_id,
                                                      const std::string &submission_mode,
                                                      const std::string &user_agent,
                                                      const std::string &ip_address,
                                                      const std::vector<std::pair<std::string, std::string>> &answers)
{
    ensure_connected();
    const char *params[4] = {form_id.c_str(), submission_mode.c_str(),
                             user_agent.empty() ? nullptr : user_agent.c_str(),
                             ip_address.empty() ? nullptr : ip_address.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO form_responses (form_id, submission_mode, user_agent, ip_address) "
                                 "VALUES ($1, $2, $3, $4) "
                                 "RETURNING id, form_id, submission_mode, submitted_at",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormResponse r = row_to_form_response(res, 0);
    PQclear(res);

    for (const auto &[qid, val] : answers)
    {
        const char *aparams[3] = {r.id.c_str(), qid.c_str(), val.c_str()};
        PGresult *ares = PQexecParams(conn_,
                                      "INSERT INTO response_answers (response_id, question_id, answer_value) VALUES ($1, $2, $3)",
                                      3, nullptr, aparams, nullptr, nullptr, 0);
        PQclear(ares);
    }
    r.answers = answers;
    return r;
}

std::vector<FormResponse> Database::list_responses(const std::string &form_id)
{
    ensure_connected();
    const char *params[1] = {form_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, form_id, submission_mode, submitted_at FROM form_responses "
                                 "WHERE form_id = $1 ORDER BY submitted_at DESC",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    int n = PQntuples(res);
    std::vector<FormResponse> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_form_response(res, i));
    PQclear(res);
    return out;
}

std::optional<FormResponse> Database::get_response(const std::string &response_id)
{
    ensure_connected();
    const char *params[1] = {response_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, form_id, submission_mode, submitted_at FROM form_responses WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormResponse r = row_to_form_response(res, 0);
    PQclear(res);

    const char *aparams[1] = {response_id.c_str()};
    PGresult *ares = PQexecParams(conn_,
                                  "SELECT question_id::text, answer_value FROM response_answers WHERE response_id = $1",
                                  1, nullptr, aparams, nullptr, nullptr, 0);
    if (PQresultStatus(ares) == PGRES_TUPLES_OK)
    {
        int n = PQntuples(ares);
        r.answers.reserve(n);
        for (int i = 0; i < n; ++i)
            r.answers.emplace_back(PQgetvalue(ares, i, 0), PQgetvalue(ares, i, 1));
    }
    PQclear(ares);
    return r;
}

bool Database::delete_response(const std::string &response_id)
{
    ensure_connected();
    const char *params[1] = {response_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM form_responses WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

nlohmann::json Database::get_form_stats(const std::string &form_id)
{
    ensure_connected();
    nlohmann::json stats;

    const char *params[1] = {form_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT submission_mode, COUNT(*) FROM form_responses WHERE form_id=$1 GROUP BY submission_mode",
                                 1, nullptr, params, nullptr, nullptr, 0);
    int total = 0;
    nlohmann::json by_mode = nlohmann::json::object();
    if (PQresultStatus(res) == PGRES_TUPLES_OK)
    {
        for (int i = 0; i < PQntuples(res); ++i)
        {
            std::string mode = PQgetvalue(res, i, 0);
            int cnt = std::stoi(PQgetvalue(res, i, 1));
            by_mode[mode] = cnt;
            total += cnt;
        }
    }
    PQclear(res);
    stats["total"] = total;
    stats["by_mode"] = by_mode;
    int registration_count = by_mode.contains("registration") ? by_mode["registration"].get<int>() : 0;
    int deregistration_count = by_mode.contains("deregistration") ? by_mode["deregistration"].get<int>() : 0;
    stats["registration_count"] = registration_count;
    stats["deregistration_count"] = deregistration_count;
    stats["expected_current"] = registration_count - deregistration_count;

    PGresult *qres = PQexecParams(conn_,
                                  "SELECT fq.id::text, fq.question_text, fq.question_type, ra.answer_value, COUNT(*) "
                                  "FROM form_questions fq "
                                  "JOIN response_answers ra ON ra.question_id = fq.id "
                                  "JOIN form_responses fr ON fr.id = ra.response_id "
                                  "WHERE fq.form_id = $1 "
                                  "GROUP BY fq.id, fq.question_text, fq.question_type, ra.answer_value "
                                  "ORDER BY fq.position",
                                  1, nullptr, params, nullptr, nullptr, 0);
    nlohmann::json questions_stats = nlohmann::json::object();
    if (PQresultStatus(qres) == PGRES_TUPLES_OK)
    {
        for (int i = 0; i < PQntuples(qres); ++i)
        {
            std::string qid = PQgetvalue(qres, i, 0);
            std::string qtext = PQgetvalue(qres, i, 1);
            std::string qtype = PQgetvalue(qres, i, 2);
            std::string val = PQgetvalue(qres, i, 3);
            int cnt = std::stoi(PQgetvalue(qres, i, 4));
            if (!questions_stats.contains(qid))
            {
                questions_stats[qid] = {
                    {"question_text", qtext},
                    {"question_type", qtype},
                    {"answers", nlohmann::json::object()}};
            }
            questions_stats[qid]["answers"][val] = cnt;
        }
    }
    PQclear(qres);
    stats["questions"] = questions_stats;
    return stats;
}

// ── Form Templates ────────────────────────────────────────────────────────────

std::vector<FormTemplate> Database::list_form_templates(const std::string &department)
{
    ensure_connected();
    const char *params[1] = {department.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, name, department, form_type, template_config, is_default, created_by::text, created_at, updated_at "
                                 "FROM form_templates WHERE department = $1 ORDER BY name",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return {};
    }
    int n = PQntuples(res);
    std::vector<FormTemplate> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_form_template(res, i));
    PQclear(res);
    return out;
}

std::optional<FormTemplate> Database::create_form_template(const std::string &name,
                                                           const std::string &department,
                                                           const std::string &form_type,
                                                           const nlohmann::json &template_config,
                                                           const std::string &created_by,
                                                           bool is_default)
{
    ensure_connected();
    // If setting as default, clear any existing default for this department
    if (is_default)
    {
        const char *dp[1] = {department.c_str()};
        PGresult *dr = PQexecParams(conn_,
                                    "UPDATE form_templates SET is_default = false WHERE department = $1 AND is_default = true",
                                    1, nullptr, dp, nullptr, nullptr, 0);
        PQclear(dr);
    }
    std::string cfg_str = template_config.dump();
    const char *def_str = is_default ? "true" : "false";
    const char *params[6] = {name.c_str(), department.c_str(), form_type.c_str(), cfg_str.c_str(), created_by.c_str(), def_str};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO form_templates (name, department, form_type, template_config, created_by, is_default) "
                                 "VALUES ($1, $2, $3, $4::jsonb, $5::uuid, $6::boolean) "
                                 "RETURNING id, name, department, form_type, template_config, is_default, created_by::text, created_at, updated_at",
                                 6, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormTemplate t = row_to_form_template(res, 0);
    PQclear(res);
    return t;
}

std::optional<FormTemplate> Database::update_form_template(const std::string &id,
                                                           const std::string &name,
                                                           const std::string &form_type,
                                                           const nlohmann::json &template_config,
                                                           bool is_default)
{
    ensure_connected();
    // If setting as default, clear any existing default for this department
    if (is_default)
    {
        const char *ip[1] = {id.c_str()};
        PGresult *dr = PQexecParams(conn_,
                                    "UPDATE form_templates SET is_default = false "
                                    "WHERE department = (SELECT department FROM form_templates WHERE id = $1) "
                                    "AND is_default = true AND id != $1",
                                    1, nullptr, ip, nullptr, nullptr, 0);
        PQclear(dr);
    }
    std::string cfg_str = template_config.dump();
    const char *def_str = is_default ? "true" : "false";
    const char *params[5] = {id.c_str(), name.c_str(), form_type.c_str(), cfg_str.c_str(), def_str};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE form_templates SET name=$2, form_type=$3, template_config=$4::jsonb, is_default=$5::boolean WHERE id=$1 "
                                 "RETURNING id, name, department, form_type, template_config, is_default, created_by::text, created_at, updated_at",
                                 5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    FormTemplate t = row_to_form_template(res, 0);
    PQclear(res);
    return t;
}

bool Database::delete_form_template(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM form_templates WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

// ---- Activity share links ---------------------------------------------------

std::optional<ActivityShareLink> Database::create_share_link(const std::string &activity_id,
                                                             const std::string &created_by)
{
    ensure_connected();
    const char *params[2] = {activity_id.c_str(), created_by.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO activity_share_links (activity_id, created_by) "
                                 "VALUES ($1, NULLIF($2, '')::uuid) "
                                 "ON CONFLICT (activity_id) WHERE true "
                                 "DO UPDATE SET activity_id = activity_share_links.activity_id "
                                 "RETURNING id, activity_id, share_token, created_by::text, created_at",
                                 2, nullptr, params, nullptr, nullptr, 0);
    // If UPSERT fails (no unique on activity_id), try simple approach
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        // Check if one already exists
        auto existing = get_share_link(activity_id);
        if (existing)
            return existing;
        // Insert without ON CONFLICT
        res = PQexecParams(conn_,
                           "INSERT INTO activity_share_links (activity_id, created_by) "
                           "VALUES ($1, NULLIF($2, '')::uuid) "
                           "RETURNING id, activity_id, share_token, created_by::text, created_at",
                           2, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            PQclear(res);
            return std::nullopt;
        }
    }
    ActivityShareLink link;
    link.id = PQgetvalue(res, 0, 0);
    link.activity_id = PQgetvalue(res, 0, 1);
    link.share_token = PQgetvalue(res, 0, 2);
    link.created_by = PQgetisnull(res, 0, 3) ? "" : PQgetvalue(res, 0, 3);
    link.created_at = PQgetvalue(res, 0, 4);
    PQclear(res);
    return link;
}

std::optional<ActivityShareLink> Database::get_share_link(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, activity_id, share_token, created_by::text, created_at "
                                 "FROM activity_share_links WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    ActivityShareLink link;
    link.id = PQgetvalue(res, 0, 0);
    link.activity_id = PQgetvalue(res, 0, 1);
    link.share_token = PQgetvalue(res, 0, 2);
    link.created_by = PQgetisnull(res, 0, 3) ? "" : PQgetvalue(res, 0, 3);
    link.created_at = PQgetvalue(res, 0, 4);
    PQclear(res);
    return link;
}

bool Database::delete_share_link(const std::string &activity_id)
{
    ensure_connected();
    const char *params[1] = {activity_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM activity_share_links WHERE activity_id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

std::optional<Activity> Database::get_activity_by_share_token(const std::string &token)
{
    ensure_connected();
    const char *params[1] = {token.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT a.id, a.title, a.date::text, a.start_time, a.end_time, a.goal, "
                                 "       a.location, a.responsible, a.department, a.material, a.siko_text, "
                                 "       a.bad_weather_info, a.planned_participants_estimate, a.created_at, a.updated_at "
                                 "FROM activities a "
                                 "JOIN activity_share_links s ON s.activity_id = a.id "
                                 "WHERE s.share_token = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    Activity a = row_to_activity(res, 0);
    PQclear(res);
    attach_programs_single(a);
    return a;
}
