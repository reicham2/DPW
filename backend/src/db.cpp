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
    p.time = col("time");
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
                                 "SELECT id, activity_id, time, title, description, responsible "
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
                           "       bad_weather_info, created_at, updated_at "
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
                                 "       bad_weather_info, created_at, updated_at "
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
        const char *params[5] = {
            activity_id.c_str(),
            pi.time.c_str(),
            pi.title.c_str(),
            pi.description.c_str(),
            resp_json.c_str()};
        PGresult *r = PQexecParams(conn,
                                   "INSERT INTO programs (activity_id, time, title, description, responsible) "
                                   "VALUES ($1, $2, $3, $4, array(select jsonb_array_elements_text($5::jsonb)))",
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
    const char *dept_param = input.department ? dept_str.c_str() : nullptr;
    const char *siko_param = input.siko_text ? siko_str.c_str() : nullptr;

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
            bwi_str.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "INSERT INTO activities "
                                   "(title, date, start_time, end_time, goal, location, responsible, department, material, siko_text, bad_weather_info) "
                                   "VALUES ($1, $2::date, $3, $4, $5, $6, array(select jsonb_array_elements_text($7::jsonb)), "
                                   "$8, $9::jsonb, $10, $11) "
                                   "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                                   "department, material, siko_text, "
                                   "bad_weather_info, created_at, updated_at",
                                   11, nullptr, p2, nullptr, nullptr, 0);
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
    const char *dept_param = input.department ? dept_str.c_str() : nullptr;
    const char *siko_param = input.siko_text ? siko_str.c_str() : nullptr;

    exec_or_throw(conn_, "BEGIN", "update_activity BEGIN");

    try
    {
        const char *p[12] = {
            input.title.c_str(), input.date.c_str(),
            input.start_time.c_str(), input.end_time.c_str(),
            input.goal.c_str(), input.location.c_str(),
            resp_json.c_str(),
            dept_param,
            mat_json.c_str(),
            siko_param,
            bwi_str.c_str(), id.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "UPDATE activities SET "
                                   "title=$1, date=$2::date, start_time=$3, end_time=$4, "
                                   "goal=$5, location=$6, responsible=array(select jsonb_array_elements_text($7::jsonb)), "
                                   "department=$8, material=$9::jsonb, "
                                   "siko_text=$10, bad_weather_info=$11 "
                                   "WHERE id=$12 "
                                   "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                                   "department, material, siko_text, "
                                   "bad_weather_info, created_at, updated_at",
                                   12, nullptr, p, nullptr, nullptr, 0);
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
                           "SELECT id, microsoft_oid, email, display_name, department, role, created_at, updated_at "
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
        "RETURNING id, microsoft_oid, email, display_name, department, role, created_at, updated_at";

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
                                 "SELECT id, microsoft_oid, email, display_name, department, role, created_at, updated_at "
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
                                 "SELECT id, microsoft_oid, email, display_name, department, role, created_at, updated_at "
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
                                                const std::optional<std::string> &department)
{
    ensure_connected();
    std::string dept_str = department ? *department : "";

    std::string sql =
        "UPDATE users SET display_name = $1, department = " +
        (department ? ("'" + dept_str + "'") : std::string("NULL")) +
        " WHERE microsoft_oid = $2 "
        "RETURNING id, microsoft_oid, email, display_name, department, role, created_at, updated_at";

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
        "RETURNING id, microsoft_oid, email, display_name, department, role, created_at, updated_at";

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
    t.created_at = col("created_at") ? col("created_at") : "";
    t.updated_at = col("updated_at") ? col("updated_at") : "";
    return t;
}

std::vector<MailTemplate> Database::list_mail_templates()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, department, subject, body, recipients, created_at, updated_at "
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
                                 "SELECT id, department, subject, body, recipients, created_at, updated_at "
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
                                                           const std::vector<std::string> &recipients)
{
    ensure_connected();
    // Build PostgreSQL text array literal for recipients
    std::string recipients_arr = "{";
    for (size_t i = 0; i < recipients.size(); ++i)
    {
        if (i > 0)
            recipients_arr += ",";
        recipients_arr += "\"" + recipients[i] + "\"";
    }
    recipients_arr += "}";
    const char *params[4] = {department.c_str(), subject.c_str(), body.c_str(), recipients_arr.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO mail_templates (department, subject, body, recipients) "
                                 "VALUES ($1, $2, $3, $4::text[]) "
                                 "ON CONFLICT (department) DO UPDATE SET subject = EXCLUDED.subject, body = EXCLUDED.body, recipients = EXCLUDED.recipients "
                                 "RETURNING id, department, subject, body, recipients, created_at, updated_at",
                                 4, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    MailTemplate t = row_to_mail_template(res, 0);
    PQclear(res);
    return t;
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
    m.subject = col("subject") ? col("subject") : "";
    m.body_html = col("body_html") ? col("body_html") : "";
    m.sent_at = col("sent_at") ? col("sent_at") : "";
    return m;
}

std::optional<SentMail> Database::log_sent_mail(const std::string &activity_id,
                                                const std::string &sender_id,
                                                const std::string &sender_email,
                                                const std::vector<std::string> &to_emails,
                                                const std::string &subject,
                                                const std::string &body_html)
{
    ensure_connected();
    std::string to_arr = "{";
    for (size_t i = 0; i < to_emails.size(); ++i)
    {
        if (i > 0)
            to_arr += ",";
        to_arr += "\"" + to_emails[i] + "\"";
    }
    to_arr += "}";
    const char *params[6] = {activity_id.c_str(), sender_id.c_str(), sender_email.c_str(),
                             to_arr.c_str(), subject.c_str(), body_html.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO sent_mails (activity_id, sender_id, sender_email, to_emails, subject, body_html) "
                                 "VALUES ($1, $2, $3, $4::text[], $5, $6) "
                                 "RETURNING id, activity_id, sender_id, sender_email, to_emails, subject, body_html, sent_at",
                                 6, nullptr, params, nullptr, nullptr, 0);
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
                                 "SELECT id, activity_id, sender_id, sender_email, to_emails, subject, body_html, sent_at "
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

    nlohmann::json payload = {
        {"message", {{"subject", subject}, {"body", {{"contentType", "HTML"}, {"content", body_html}}}, {"toRecipients", to_arr}}},
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
                                 "SELECT filename, content_type, data FROM attachments WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    AttachmentData ad;
    ad.filename = PQgetvalue(res, 0, 0);
    ad.content_type = PQgetvalue(res, 0, 1);
    // data column is bytea returned as hex-escaped text; use PQunescapeBytea
    size_t len = 0;
    unsigned char *unescaped = PQunescapeBytea(
        reinterpret_cast<const unsigned char *>(PQgetvalue(res, 0, 2)), &len);
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
    d.sort_order = col("sort_order") ? std::stoi(col("sort_order")) : 0;
    return d;
}

std::vector<DepartmentRecord> Database::list_departments()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT name, color, sort_order FROM departments ORDER BY sort_order, name");
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

std::optional<DepartmentRecord> Database::create_department(const std::string &name, const std::string &color, int sort_order)
{
    ensure_connected();
    std::string so = std::to_string(sort_order);
    const char *params[3] = {name.c_str(), color.c_str(), so.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO departments (name, color, sort_order) VALUES ($1, $2, $3::int) "
                                 "RETURNING name, color, sort_order",
                                 3, nullptr, params, nullptr, nullptr, 0);
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
                                                            const std::string &color, int sort_order)
{
    ensure_connected();
    std::string so = std::to_string(sort_order);
    const char *params[4] = {new_name.c_str(), color.c_str(), so.c_str(), name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE departments SET name = $1, color = $2, sort_order = $3::int WHERE name = $4 "
                                 "RETURNING name, color, sort_order",
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

std::optional<RoleRecord> Database::create_role(const std::string &name, const std::string &color, int sort_order)
{
    ensure_connected();
    exec_or_throw(conn_, "BEGIN", "create_role BEGIN");
    try
    {
        std::string so = std::to_string(sort_order);
        const char *params[3] = {name.c_str(), color.c_str(), so.c_str()};
        PGresult *res = PQexecParams(conn_,
                                     "INSERT INTO roles (name, color, sort_order) VALUES ($1, $2, $3::int) "
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
                                                const std::string &color, int sort_order)
{
    ensure_connected();
    std::string so = std::to_string(sort_order);
    const char *params[4] = {new_name.c_str(), color.c_str(), so.c_str(), name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE roles SET name = $1, color = $2, sort_order = $3::int WHERE name = $4 "
                                 "RETURNING name, color, sort_order",
                                 4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto r = row_to_role(res, 0);
    PQclear(res);
    return r;
}

bool Database::delete_role(const std::string &name)
{
    ensure_connected();
    const char *params[1] = {name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM roles WHERE name = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK &&
              std::string(PQcmdTuples(res)) == "1";
    PQclear(res);
    return ok;
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
    rp.mail_send_scope = col("mail_send_scope") ? col("mail_send_scope") : "none";
    rp.mail_templates_scope = col("mail_templates_scope") ? col("mail_templates_scope") : "none";
    rp.user_dept_scope = col("user_dept_scope") ? col("user_dept_scope") : "none";
    rp.user_role_scope = col("user_role_scope") ? col("user_role_scope") : "none";
    return rp;
}

std::vector<RolePermission> Database::list_role_permissions()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT role, can_read_own_dept, can_write_own_dept, mail_send_scope, "
                           "       mail_templates_scope, user_dept_scope, user_role_scope "
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
                                 "SELECT role, can_read_own_dept, can_write_own_dept, mail_send_scope, "
                                 "       mail_templates_scope, user_dept_scope, user_role_scope "
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
                                      const std::string &mail_send_scope,
                                      const std::string &mail_templates_scope,
                                      const std::string &user_dept_scope,
                                      const std::string &user_role_scope)
{
    ensure_connected();
    const char *p1 = role.c_str();
    const char *p2 = can_read_own_dept ? "true" : "false";
    const char *p3 = can_write_own_dept ? "true" : "false";
    const char *p4 = can_read_all_depts ? "true" : "false";
    const char *p5 = can_write_all_depts ? "true" : "false";
    const char *p6 = mail_send_scope.c_str();
    const char *p7 = mail_templates_scope.c_str();
    const char *p8 = user_dept_scope.c_str();
    const char *p9 = user_role_scope.c_str();
    const char *params[9] = {p1, p2, p3, p4, p5, p6, p7, p8, p9};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, "
                                 "can_read_all_depts, can_write_all_depts, "
                                 "mail_send_scope, mail_templates_scope, user_dept_scope, user_role_scope) "
                                 "VALUES ($1, $2::boolean, $3::boolean, $4::boolean, $5::boolean, $6, $7, $8, $9) "
                                 "ON CONFLICT (role) DO UPDATE SET "
                                 "can_read_own_dept = $2::boolean, can_write_own_dept = $3::boolean, "
                                 "can_read_all_depts = $4::boolean, can_write_all_depts = $5::boolean, "
                                 "mail_send_scope = $6, mail_templates_scope = $7, "
                                 "user_dept_scope = $8, user_role_scope = $9",
                                 9, nullptr, params, nullptr, nullptr, 0);
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
