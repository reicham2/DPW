#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <functional>
#include "json.hpp"

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

    const char *tn_mat = col("tn_material");
    if (tn_mat)
    {
        auto tnj = nlohmann::json::parse(tn_mat, nullptr, false);
        if (tnj.is_array())
        {
            for (const auto &item : tnj)
                if (item.is_string())
                    a.tn_material.push_back(item.get<std::string>());
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

MaterialNameRecord Database::row_to_material_name(PGresult *res, int row)
{
    auto col = [&](const char *name) -> std::string
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return "";
        return PQgetvalue(res, row, c);
    };
    MaterialNameRecord m;
    m.id = col("id");
    m.name = col("name");
    m.created_at = col("created_at");
    m.updated_at = col("updated_at");
    return m;
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
                                 "FROM programs WHERE activity_id = ANY($1::uuid[]) ORDER BY activity_id, position",
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
    for (size_t idx = 0; idx < programs.size(); ++idx)
    {
        const auto &pi = programs[idx];
        std::string resp_json = fmt(pi.responsible);
        std::string dm_str = std::to_string(pi.duration_minutes);
        std::string pos_str = std::to_string(idx);
        const char *params[6] = {
            activity_id.c_str(),
            dm_str.c_str(),
            pi.title.c_str(),
            pi.description.c_str(),
            resp_json.c_str(),
            pos_str.c_str()};
        PGresult *r = PQexecParams(conn,
                                   "INSERT INTO programs (activity_id, duration_minutes, title, description, responsible, position) "
                                   "VALUES ($1, $2::int, $3, $4, array(select jsonb_array_elements_text($5::jsonb)), $6::int)",
                                   6, nullptr, params, nullptr, nullptr, 0);
        ExecStatusType s = PQresultStatus(r);
        PQclear(r);
        if (s != PGRES_COMMAND_OK)
            throw std::runtime_error("insert_programs: " + std::string(PQerrorMessage(conn)));
    }
}

// ---- list_activities --------------------------------------------------------

std::vector<Activity> Database::list_activities()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, title, date::text, start_time, end_time, goal, location, responsible, "
                           "       department, material, tn_material, siko_text, "
                           "       bad_weather_info, planned_participants_estimate, created_at, updated_at "
                           "FROM activities WHERE deleted_at IS NULL ORDER BY date DESC, start_time");

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
                                 "       department, material, tn_material, siko_text, "
                                 "       bad_weather_info, planned_participants_estimate, created_at, updated_at "
                                 "FROM activities WHERE id = $1 AND deleted_at IS NULL",
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

// ---- get_predefined_materials ------------------------------------------------

std::vector<std::string> Database::get_predefined_materials()
{
    ensure_connected();
    PGresult *res = PQexec(conn_, "SELECT name FROM predefined_materials ORDER BY name");
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

std::vector<MaterialNameRecord> Database::list_predefined_materials()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, name, created_at, updated_at FROM predefined_materials ORDER BY name");
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_predefined_materials: " + err);
    }
    std::vector<MaterialNameRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_material_name(res, i));
    PQclear(res);
    return out;
}

std::optional<MaterialNameRecord> Database::create_predefined_material(const std::string &name)
{
    ensure_connected();
    const char *params[1] = {name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO predefined_materials (name) VALUES ($1) "
                                 "RETURNING id, name, created_at, updated_at",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto out = row_to_material_name(res, 0);
    PQclear(res);
    return out;
}

std::optional<MaterialNameRecord> Database::update_predefined_material(const std::string &id, const std::string &name)
{
    ensure_connected();
    const char *params[2] = {id.c_str(), name.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE predefined_materials SET name = $2 WHERE id = $1 "
                                 "RETURNING id, name, created_at, updated_at",
                                 2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    auto out = row_to_material_name(res, 0);
    PQclear(res);
    return out;
}

bool Database::delete_predefined_material(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM predefined_materials WHERE id = $1",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK && PQcmdTuples(res)[0] != '0';
    PQclear(res);
    return ok;
}

// ---- create_activity --------------------------------------------------------

std::optional<Activity> Database::create_activity(const ActivityInput &input)
{
    ensure_connected();

    std::string mat_json = Database::format_material_items_param(input.material);
    std::string tn_mat_json = Database::format_material_param(input.tn_material);
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
        const char *p2[13] = {
            input.title.c_str(), input.date.c_str(),
            input.start_time.c_str(), input.end_time.c_str(),
            input.goal.c_str(), input.location.c_str(),
            resp_json.c_str(),
            dept_param,
            mat_json.c_str(),
            tn_mat_json.c_str(),
            siko_param,
            bwi_str.c_str(),
            ppe_param};
        PGresult *r = PQexecParams(conn_,
                                   "INSERT INTO activities "
                                   "(title, date, start_time, end_time, goal, location, responsible, department, material, tn_material, siko_text, bad_weather_info, planned_participants_estimate) "
                                   "VALUES ($1, $2::date, $3, $4, $5, $6, array(select jsonb_array_elements_text($7::jsonb)), "
                                   "$8, $9::jsonb, $10::jsonb, $11, $12, $13::int) "
                                   "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                                   "department, material, tn_material, siko_text, "
                                   "bad_weather_info, planned_participants_estimate, created_at, updated_at",
                                   13, nullptr, p2, nullptr, nullptr, 0);
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
    std::string tn_mat_json = Database::format_material_param(input.tn_material);
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
        const char *p[14] = {
            input.title.c_str(), input.date.c_str(),
            input.start_time.c_str(), input.end_time.c_str(),
            input.goal.c_str(), input.location.c_str(),
            resp_json.c_str(),
            dept_param,
            mat_json.c_str(),
            tn_mat_json.c_str(),
            siko_param,
            bwi_str.c_str(),
            ppe_param,
            id.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "UPDATE activities SET "
                                   "title=$1, date=$2::date, start_time=$3, end_time=$4, "
                                   "goal=$5, location=$6, responsible=array(select jsonb_array_elements_text($7::jsonb)), "
                                   "department=$8, material=$9::jsonb, tn_material=$10::jsonb, "
                                   "siko_text=$11, bad_weather_info=$12, planned_participants_estimate=$13 "
                                   "WHERE id=$14 "
                                   "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                                   "department, material, tn_material, siko_text, "
                                   "bad_weather_info, planned_participants_estimate, created_at, updated_at",
                                   14, nullptr, p, nullptr, nullptr, 0);
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
    return soft_delete_activity(id, "");
}

bool Database::soft_delete_activity(const std::string &id, const std::string &deleted_by_user_id)
{
    ensure_connected();
    const char *params[2] = {
        id.c_str(),
        deleted_by_user_id.empty() ? nullptr : deleted_by_user_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE activities "
                                 "SET deleted_at = NOW(), deleted_by = $2::uuid "
                                 "WHERE id = $1 AND deleted_at IS NULL",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK && PQcmdTuples(res)[0] != '0';
    PQclear(res);
    return ok;
}

bool Database::restore_activity(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE activities "
                                 "SET deleted_at = NULL, deleted_by = NULL "
                                 "WHERE id = $1 AND deleted_at IS NOT NULL",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK && PQcmdTuples(res)[0] != '0';
    PQclear(res);
    return ok;
}

bool Database::permanently_delete_activity(const std::string &id)
{
    ensure_connected();
    const char *params[1] = {id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM activities "
                                 "WHERE id = $1 AND deleted_at IS NOT NULL",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK && PQcmdTuples(res)[0] != '0';
    PQclear(res);
    return ok;
}

int Database::purge_deleted_activities_older_than_days(int days)
{
    ensure_connected();
    std::string days_text = std::to_string(days);
    const char *params[1] = {days_text.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "DELETE FROM activities "
                                 "WHERE deleted_at IS NOT NULL "
                                 "AND deleted_at < NOW() - ($1::int * INTERVAL '1 day')",
                                 1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("purge_deleted_activities_older_than_days: " + err);
    }

    int deleted = 0;
    const char *tuples = PQcmdTuples(res);
    if (tuples && *tuples)
        deleted = std::atoi(tuples);
    PQclear(res);
    return deleted;
}

std::vector<DeletedActivityRecord> Database::list_deleted_activities()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT a.id, a.title, a.date::text, a.start_time, a.end_time, a.goal, a.location, a.responsible, "
                           "       a.department, a.material, a.siko_text, a.bad_weather_info, a.planned_participants_estimate, "
                           "       a.created_at, a.updated_at, a.deleted_at, a.deleted_by::text, "
                           "       u.display_name AS deleted_by_display_name, u.email AS deleted_by_email "
                           "FROM activities a "
                           "LEFT JOIN users u ON u.id = a.deleted_by "
                           "WHERE a.deleted_at IS NOT NULL "
                           "ORDER BY a.deleted_at DESC");

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_deleted_activities: " + err);
    }

    std::vector<DeletedActivityRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        DeletedActivityRecord rec;
        rec.activity = row_to_activity(res, i);
        int deleted_at_col = PQfnumber(res, "deleted_at");
        if (deleted_at_col >= 0 && !PQgetisnull(res, i, deleted_at_col))
            rec.deleted_at = PQgetvalue(res, i, deleted_at_col);

        int deleted_by_id_col = PQfnumber(res, "deleted_by");
        if (deleted_by_id_col >= 0 && !PQgetisnull(res, i, deleted_by_id_col))
            rec.deleted_by_user_id = std::string(PQgetvalue(res, i, deleted_by_id_col));

        int deleted_by_name_col = PQfnumber(res, "deleted_by_display_name");
        if (deleted_by_name_col >= 0 && !PQgetisnull(res, i, deleted_by_name_col))
            rec.deleted_by_display_name = std::string(PQgetvalue(res, i, deleted_by_name_col));

        int deleted_by_email_col = PQfnumber(res, "deleted_by_email");
        if (deleted_by_email_col >= 0 && !PQgetisnull(res, i, deleted_by_email_col))
            rec.deleted_by_email = std::string(PQgetvalue(res, i, deleted_by_email_col));

        out.push_back(std::move(rec));
    }
    PQclear(res);

    for (auto &rec : out)
        attach_programs_single(rec.activity);

    return out;
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

// ---- Attachments ------------------------------------------------------------

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
