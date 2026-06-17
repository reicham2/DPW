#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include "json.hpp"

// ── Local helpers ─────────────────────────────────────────────────────────────
namespace
{
    void exec_or_throw_local(PGconn *conn, const char *sql, const char *context)
    {
        PGresult *r = PQexec(conn, sql);
        ExecStatusType s = PQresultStatus(r);
        PQclear(r);
        if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK)
            throw std::runtime_error(std::string(context) + ": " + PQerrorMessage(conn));
    }

    const char *col_val(PGresult *res, int row, const char *name)
    {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c))
            return nullptr;
        return PQgetvalue(res, row, c);
    }

    std::string col_str(PGresult *res, int row, const char *name)
    {
        const char *v = col_val(res, row, name);
        return v ? std::string(v) : std::string();
    }
}

// ── Row mappers ───────────────────────────────────────────────────────────────

Camp Database::row_to_camp(PGresult *res, int row)
{
    Camp c;
    c.id = col_str(res, row, "id");
    c.title = col_str(res, row, "title");
    c.short_title = col_str(res, row, "short_title");
    c.motto = col_str(res, row, "motto");
    c.kind = col_str(res, row, "kind");
    c.organizer = col_str(res, row, "organizer");
    c.address_name = col_str(res, row, "address_name");
    c.address_street = col_str(res, row, "address_street");
    c.address_zipcode = col_str(res, row, "address_zipcode");
    c.address_city = col_str(res, row, "address_city");
    c.coach_name = col_str(res, row, "coach_name");
    c.course_number = col_str(res, row, "course_number");
    c.color = col_str(res, row, "color");
    if (const char *d = col_val(res, row, "department"))
        c.department = d;
    if (const char *cb = col_val(res, row, "created_by"))
        c.created_by = cb;
    if (const char *p = col_val(res, row, "is_prototype"))
        c.is_prototype = (std::strcmp(p, "t") == 0);
    c.created_at = col_str(res, row, "created_at");
    c.updated_at = col_str(res, row, "updated_at");
    return c;
}

CampCollaboration Database::row_to_collaboration(PGresult *res, int row)
{
    CampCollaboration c;
    c.id = col_str(res, row, "id");
    c.camp_id = col_str(res, row, "camp_id");
    if (const char *u = col_val(res, row, "user_id"))
        c.user_id = u;
    c.display_name = col_str(res, row, "display_name");
    c.role = col_str(res, row, "role");
    c.camp_role = col_str(res, row, "camp_role");
    c.abbreviation = col_str(res, row, "abbreviation");
    c.color = col_str(res, row, "color");
    c.status = col_str(res, row, "status");
    c.created_at = col_str(res, row, "created_at");
    c.updated_at = col_str(res, row, "updated_at");
    return c;
}

CampCategory Database::row_to_category(PGresult *res, int row)
{
    CampCategory c;
    c.id = col_str(res, row, "id");
    c.camp_id = col_str(res, row, "camp_id");
    c.short_name = col_str(res, row, "short_name");
    c.name = col_str(res, row, "name");
    c.color = col_str(res, row, "color");
    c.numbering_style = col_str(res, row, "numbering_style");
    if (const char *p = col_val(res, row, "position"))
        c.position = std::atoi(p);
    c.created_at = col_str(res, row, "created_at");
    c.updated_at = col_str(res, row, "updated_at");
    return c;
}

CampPeriod Database::row_to_period(PGresult *res, int row)
{
    CampPeriod p;
    p.id = col_str(res, row, "id");
    p.camp_id = col_str(res, row, "camp_id");
    p.description = col_str(res, row, "description");
    p.start_date = col_str(res, row, "start_date");
    p.end_date = col_str(res, row, "end_date");
    if (const char *pos = col_val(res, row, "position"))
        p.position = std::atoi(pos);
    p.created_at = col_str(res, row, "created_at");
    p.updated_at = col_str(res, row, "updated_at");
    return p;
}

ScheduleEntry Database::row_to_schedule_entry(PGresult *res, int row)
{
    ScheduleEntry s;
    s.id = col_str(res, row, "id");
    s.activity_id = col_str(res, row, "activity_id");
    s.period_id = col_str(res, row, "period_id");
    if (const char *o = col_val(res, row, "period_offset"))
        s.period_offset = std::atoi(o);
    if (const char *l = col_val(res, row, "length"))
        s.length = std::atoi(l);
    if (const char *lf = col_val(res, row, "left_fraction"))
        s.left_fraction = std::atof(lf);
    if (const char *wf = col_val(res, row, "width_fraction"))
        s.width_fraction = std::atof(wf);
    s.created_at = col_str(res, row, "created_at");
    s.updated_at = col_str(res, row, "updated_at");
    return s;
}

ContentNode Database::row_to_content_node(PGresult *res, int row)
{
    ContentNode n;
    n.id = col_str(res, row, "id");
    n.activity_id = col_str(res, row, "activity_id");
    if (const char *p = col_val(res, row, "parent_id"))
        n.parent_id = p;
    n.slot = col_str(res, row, "slot");
    if (const char *pos = col_val(res, row, "position"))
        n.position = std::atoi(pos);
    n.content_type = col_str(res, row, "content_type");
    n.instance_name = col_str(res, row, "instance_name");
    if (const char *r = col_val(res, row, "is_root"))
        n.is_root = (std::strcmp(r, "t") == 0);
    if (const char *d = col_val(res, row, "data"))
        n.data = nlohmann::json::parse(d, nullptr, false);
    if (n.data.is_discarded())
        n.data = nlohmann::json::object();
    n.created_at = col_str(res, row, "created_at");
    n.updated_at = col_str(res, row, "updated_at");
    return n;
}

CampMaterialItem Database::row_to_material_item(PGresult *res, int row)
{
    CampMaterialItem m;
    m.id = col_str(res, row, "id");
    m.material_list_id = col_str(res, row, "material_list_id");
    if (const char *c = col_val(res, row, "content_node_id"))
        m.content_node_id = c;
    if (const char *p = col_val(res, row, "period_id"))
        m.period_id = p;
    m.article_name = col_str(res, row, "article_name");
    if (const char *q = col_val(res, row, "quantity"))
        m.quantity = std::atof(q);
    m.unit = col_str(res, row, "unit");
    m.created_at = col_str(res, row, "created_at");
    m.updated_at = col_str(res, row, "updated_at");
    return m;
}

CampActivity Database::row_to_camp_activity(PGresult *res, int row)
{
    CampActivity a;
    a.id = col_str(res, row, "id");
    a.camp_id = col_str(res, row, "camp_id");
    if (const char *c = col_val(res, row, "category_id"))
        a.category_id = c;
    a.title = col_str(res, row, "title");
    a.location = col_str(res, row, "location");
    if (const char *r = col_val(res, row, "responsible"))
        a.responsible = parse_pg_array(r);
    a.created_at = col_str(res, row, "created_at");
    a.updated_at = col_str(res, row, "updated_at");
    return a;
}

// Loads schedule entries, responsibles and content nodes for an activity.
void Database::hydrate_camp_activity(CampActivity &a)
{
    {
        const char *p[1] = {a.id.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "SELECT id, activity_id, period_id, period_offset, length, "
                                   "left_fraction::text AS left_fraction, width_fraction::text AS width_fraction, "
                                   "created_at, updated_at FROM schedule_entries WHERE activity_id=$1 ORDER BY period_offset",
                                   1, nullptr, p, nullptr, nullptr, 0);
        if (PQresultStatus(r) == PGRES_TUPLES_OK)
            for (int i = 0; i < PQntuples(r); ++i)
                a.schedule_entries.push_back(row_to_schedule_entry(r, i));
        PQclear(r);
    }
    {
        const char *p[1] = {a.id.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "SELECT collaboration_id FROM camp_activity_responsibles WHERE activity_id=$1",
                                   1, nullptr, p, nullptr, nullptr, 0);
        if (PQresultStatus(r) == PGRES_TUPLES_OK)
            for (int i = 0; i < PQntuples(r); ++i)
                a.responsible_collaboration_ids.push_back(PQgetvalue(r, i, 0));
        PQclear(r);
    }
    {
        const char *p[1] = {a.id.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "SELECT id, activity_id, parent_id, slot, position, content_type, "
                                   "instance_name, is_root, data::text AS data, created_at, updated_at "
                                   "FROM content_nodes WHERE activity_id=$1 ORDER BY parent_id NULLS FIRST, position",
                                   1, nullptr, p, nullptr, nullptr, 0);
        if (PQresultStatus(r) == PGRES_TUPLES_OK)
            for (int i = 0; i < PQntuples(r); ++i)
                a.content_nodes.push_back(row_to_content_node(r, i));
        PQclear(r);
    }
}

// ── Camps ─────────────────────────────────────────────────────────────────────

std::vector<Camp> Database::list_camps()
{
    ensure_connected();
    std::vector<Camp> out;
    PGresult *r = PQexec(conn_,
                         "SELECT * FROM camps WHERE deleted_at IS NULL ORDER BY created_at DESC");
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(r); ++i)
            out.push_back(row_to_camp(r, i));
    PQclear(r);
    return out;
}

std::optional<Camp> Database::get_camp_by_id(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT * FROM camps WHERE id=$1 AND deleted_at IS NULL",
                               1, nullptr, p, nullptr, nullptr, 0);
    std::optional<Camp> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_camp(r, 0);
    PQclear(r);
    return out;
}

std::optional<Camp> Database::create_camp(const CampInput &in, const std::string &created_by)
{
    ensure_connected();
    const char *dept = in.department ? in.department->c_str() : nullptr;
    const char *cb = created_by.empty() ? nullptr : created_by.c_str();
    const char *p[14] = {
        in.title.c_str(), in.short_title.c_str(), in.motto.c_str(), in.kind.c_str(),
        in.organizer.c_str(), in.address_name.c_str(), in.address_street.c_str(),
        in.address_zipcode.c_str(), in.address_city.c_str(), in.coach_name.c_str(),
        in.course_number.c_str(), in.color.c_str(), dept, cb};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camps (title, short_title, motto, kind, organizer, address_name, "
                               "address_street, address_zipcode, address_city, coach_name, course_number, color, "
                               "department, created_by) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14) "
                               "RETURNING *",
                               14, nullptr, p, nullptr, nullptr, 0);
    std::optional<Camp> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_camp(r, 0);
    else
        fprintf(stderr, "[db] create_camp: %s\n", PQerrorMessage(conn_));
    PQclear(r);
    return out;
}

std::optional<Camp> Database::update_camp(const std::string &id, const CampInput &in)
{
    ensure_connected();
    const char *dept = in.department ? in.department->c_str() : nullptr;
    const char *p[14] = {
        id.c_str(), in.title.c_str(), in.short_title.c_str(), in.motto.c_str(), in.kind.c_str(),
        in.organizer.c_str(), in.address_name.c_str(), in.address_street.c_str(),
        in.address_zipcode.c_str(), in.address_city.c_str(), in.coach_name.c_str(),
        in.course_number.c_str(), in.color.c_str(), dept};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE camps SET title=$2, short_title=$3, motto=$4, kind=$5, organizer=$6, "
                               "address_name=$7, address_street=$8, address_zipcode=$9, address_city=$10, "
                               "coach_name=$11, course_number=$12, color=$13, department=$14 "
                               "WHERE id=$1 AND deleted_at IS NULL RETURNING *",
                               14, nullptr, p, nullptr, nullptr, 0);
    std::optional<Camp> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_camp(r, 0);
    PQclear(r);
    return out;
}

bool Database::soft_delete_camp(const std::string &id, const std::string &by)
{
    ensure_connected();
    const char *by_p = by.empty() ? nullptr : by.c_str();
    const char *p[2] = {id.c_str(), by_p};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE camps SET deleted_at=NOW(), deleted_by=$2 WHERE id=$1 AND deleted_at IS NULL",
                               2, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

// ── Collaborations ──────────────────────────────────────────────────────────

std::vector<CampCollaboration> Database::list_collaborations(const std::string &camp_id)
{
    ensure_connected();
    std::vector<CampCollaboration> out;
    const char *p[1] = {camp_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT * FROM camp_collaborations WHERE camp_id=$1 ORDER BY created_at",
                               1, nullptr, p, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(r); ++i)
            out.push_back(row_to_collaboration(r, i));
    PQclear(r);
    return out;
}

std::optional<CampCollaboration> Database::create_collaboration(const std::string &camp_id, const CampCollaborationInput &in)
{
    ensure_connected();
    const char *uid = in.user_id ? in.user_id->c_str() : nullptr;
    const char *p[8] = {camp_id.c_str(), uid, in.display_name.c_str(), in.role.c_str(),
                        in.camp_role.c_str(), in.abbreviation.c_str(), in.color.c_str(), in.status.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camp_collaborations (camp_id, user_id, display_name, role, camp_role, "
                               "abbreviation, color, status) VALUES ($1,$2,$3,$4,$5,$6,$7,$8) RETURNING *",
                               8, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampCollaboration> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_collaboration(r, 0);
    else
        fprintf(stderr, "[db] create_collaboration: %s\n", PQerrorMessage(conn_));
    PQclear(r);
    return out;
}

std::optional<CampCollaboration> Database::update_collaboration(const std::string &id, const CampCollaborationInput &in)
{
    ensure_connected();
    const char *uid = in.user_id ? in.user_id->c_str() : nullptr;
    const char *p[8] = {id.c_str(), uid, in.display_name.c_str(), in.role.c_str(),
                        in.camp_role.c_str(), in.abbreviation.c_str(), in.color.c_str(), in.status.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE camp_collaborations SET user_id=$2, display_name=$3, role=$4, camp_role=$5, "
                               "abbreviation=$6, color=$7, status=$8 WHERE id=$1 RETURNING *",
                               8, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampCollaboration> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_collaboration(r, 0);
    PQclear(r);
    return out;
}

bool Database::delete_collaboration(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_collaborations WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

// ── Categories ──────────────────────────────────────────────────────────────

std::vector<CampCategory> Database::list_categories(const std::string &camp_id)
{
    ensure_connected();
    std::vector<CampCategory> out;
    const char *p[1] = {camp_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT * FROM camp_categories WHERE camp_id=$1 ORDER BY position, created_at",
                               1, nullptr, p, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(r); ++i)
            out.push_back(row_to_category(r, i));
    PQclear(r);
    return out;
}

std::optional<CampCategory> Database::create_category(const std::string &camp_id, const CampCategoryInput &in)
{
    ensure_connected();
    std::string pos = std::to_string(in.position);
    const char *p[6] = {camp_id.c_str(), in.short_name.c_str(), in.name.c_str(),
                        in.color.c_str(), in.numbering_style.c_str(), pos.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camp_categories (camp_id, short_name, name, color, numbering_style, position) "
                               "VALUES ($1,$2,$3,$4,$5,$6::int) RETURNING *",
                               6, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampCategory> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_category(r, 0);
    else
        fprintf(stderr, "[db] create_category: %s\n", PQerrorMessage(conn_));
    PQclear(r);
    return out;
}

std::optional<CampCategory> Database::update_category(const std::string &id, const CampCategoryInput &in)
{
    ensure_connected();
    std::string pos = std::to_string(in.position);
    const char *p[6] = {id.c_str(), in.short_name.c_str(), in.name.c_str(),
                        in.color.c_str(), in.numbering_style.c_str(), pos.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE camp_categories SET short_name=$2, name=$3, color=$4, numbering_style=$5, "
                               "position=$6::int WHERE id=$1 RETURNING *",
                               6, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampCategory> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_category(r, 0);
    PQclear(r);
    return out;
}

bool Database::delete_category(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_categories WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

// ── Periods ─────────────────────────────────────────────────────────────────

std::vector<CampPeriod> Database::list_periods(const std::string &camp_id)
{
    ensure_connected();
    std::vector<CampPeriod> out;
    const char *p[1] = {camp_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT id, camp_id, description, start_date::text AS start_date, "
                               "end_date::text AS end_date, position, created_at, updated_at "
                               "FROM camp_periods WHERE camp_id=$1 ORDER BY start_date, position",
                               1, nullptr, p, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(r); ++i)
            out.push_back(row_to_period(r, i));
    PQclear(r);
    return out;
}

std::optional<CampPeriod> Database::create_period(const std::string &camp_id, const CampPeriodInput &in)
{
    ensure_connected();
    std::string pos = std::to_string(in.position);
    const char *p[5] = {camp_id.c_str(), in.description.c_str(), in.start_date.c_str(),
                        in.end_date.c_str(), pos.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camp_periods (camp_id, description, start_date, end_date, position) "
                               "VALUES ($1,$2,$3::date,$4::date,$5::int) "
                               "RETURNING id, camp_id, description, start_date::text AS start_date, "
                               "end_date::text AS end_date, position, created_at, updated_at",
                               5, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampPeriod> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_period(r, 0);
    else
        fprintf(stderr, "[db] create_period: %s\n", PQerrorMessage(conn_));
    PQclear(r);
    return out;
}

std::optional<CampPeriod> Database::update_period(const std::string &id, const CampPeriodInput &in)
{
    ensure_connected();
    std::string pos = std::to_string(in.position);
    const char *p[5] = {id.c_str(), in.description.c_str(), in.start_date.c_str(),
                        in.end_date.c_str(), pos.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE camp_periods SET description=$2, start_date=$3::date, end_date=$4::date, "
                               "position=$5::int WHERE id=$1 "
                               "RETURNING id, camp_id, description, start_date::text AS start_date, "
                               "end_date::text AS end_date, position, created_at, updated_at",
                               5, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampPeriod> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_period(r, 0);
    PQclear(r);
    return out;
}

bool Database::delete_period(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_periods WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

// ── Day responsibles (Tagesverantwortliche) ──────────────────────────────────

std::vector<CampDayResponsible> Database::list_day_responsibles(const std::string &camp_id)
{
    ensure_connected();
    std::vector<CampDayResponsible> out;
    const char *p[1] = {camp_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT dr.id, dr.period_id, dr.day_offset, dr.collaboration_id "
                               "FROM camp_day_responsibles dr "
                               "JOIN camp_periods pe ON pe.id = dr.period_id "
                               "WHERE pe.camp_id=$1 ORDER BY dr.day_offset",
                               1, nullptr, p, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
    {
        for (int i = 0; i < PQntuples(r); ++i)
        {
            CampDayResponsible d;
            d.id = col_str(r, i, "id");
            d.period_id = col_str(r, i, "period_id");
            if (const char *o = col_val(r, i, "day_offset"))
                d.day_offset = std::atoi(o);
            d.collaboration_id = col_str(r, i, "collaboration_id");
            out.push_back(std::move(d));
        }
    }
    PQclear(r);
    return out;
}

std::optional<CampDayResponsible> Database::add_day_responsible(const std::string &period_id, int day_offset,
                                                                const std::string &collaboration_id)
{
    ensure_connected();
    std::string off = std::to_string(day_offset);
    const char *p[3] = {period_id.c_str(), off.c_str(), collaboration_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camp_day_responsibles (period_id, day_offset, collaboration_id) "
                               "VALUES ($1,$2::int,$3) "
                               "ON CONFLICT (period_id, day_offset, collaboration_id) DO UPDATE SET day_offset=EXCLUDED.day_offset "
                               "RETURNING id, period_id, day_offset, collaboration_id",
                               3, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampDayResponsible> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
    {
        CampDayResponsible d;
        d.id = col_str(r, 0, "id");
        d.period_id = col_str(r, 0, "period_id");
        if (const char *o = col_val(r, 0, "day_offset"))
            d.day_offset = std::atoi(o);
        d.collaboration_id = col_str(r, 0, "collaboration_id");
        out = std::move(d);
    }
    else
        fprintf(stderr, "[db] add_day_responsible: %s\n", PQerrorMessage(conn_));
    PQclear(r);
    return out;
}

bool Database::delete_day_responsible(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_day_responsibles WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

// ── Camp Activities (with schedule entries, responsibles, content nodes) ──────
namespace
{
    // Inserts content nodes, remapping client-supplied parent ids to DB ids.
    void insert_content_nodes(PGconn *conn, const std::string &activity_id,
                              const std::vector<ContentNodeInput> &nodes)
    {
        // Map client id (or input index) → generated DB id, so children can
        // reference parents created in the same request.
        std::unordered_map<std::string, std::string> id_map;

        // Two-pass: roots/parents must exist before children. We insert in the
        // given order but resolve parent via id_map; callers should order
        // parents before children (frontend guarantees this).
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto &n = nodes[i];
            std::string client_key = n.id ? *n.id : ("idx:" + std::to_string(i));

            std::string parent_db;
            const char *parent_param = nullptr;
            if (n.parent_id)
            {
                auto it = id_map.find(*n.parent_id);
                parent_db = (it != id_map.end()) ? it->second : *n.parent_id;
                parent_param = parent_db.c_str();
            }

            std::string pos = std::to_string(n.position);
            std::string data_str = n.data.dump();
            const char *is_root = n.is_root ? "true" : "false";
            const char *p[8] = {activity_id.c_str(), parent_param, n.slot.c_str(),
                                pos.c_str(), n.content_type.c_str(), n.instance_name.c_str(),
                                is_root, data_str.c_str()};
            PGresult *r = PQexecParams(conn,
                                       "INSERT INTO content_nodes (activity_id, parent_id, slot, position, "
                                       "content_type, instance_name, is_root, data) "
                                       "VALUES ($1,$2,$3,$4::int,$5,$6,$7::bool,$8::jsonb) RETURNING id",
                                       8, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0)
            {
                std::string err = PQerrorMessage(conn);
                PQclear(r);
                throw std::runtime_error("insert_content_nodes: " + err);
            }
            id_map[client_key] = PQgetvalue(r, 0, 0);
            PQclear(r);
        }
    }

    void insert_schedule_entries(PGconn *conn, const std::string &activity_id,
                                 const std::vector<ScheduleEntryInput> &entries)
    {
        for (const auto &s : entries)
        {
            std::string off = std::to_string(s.period_offset);
            std::string len = std::to_string(s.length);
            std::string lf = std::to_string(s.left_fraction);
            std::string wf = std::to_string(s.width_fraction);
            const char *p[6] = {activity_id.c_str(), s.period_id.c_str(), off.c_str(),
                                len.c_str(), lf.c_str(), wf.c_str()};
            PGresult *r = PQexecParams(conn,
                                       "INSERT INTO schedule_entries (activity_id, period_id, period_offset, length, "
                                       "left_fraction, width_fraction) VALUES ($1,$2,$3::int,$4::int,$5::numeric,$6::numeric)",
                                       6, nullptr, p, nullptr, nullptr, 0);
            ExecStatusType st = PQresultStatus(r);
            PQclear(r);
            if (st != PGRES_COMMAND_OK)
                throw std::runtime_error("insert_schedule_entries: " + std::string(PQerrorMessage(conn)));
        }
    }

    void insert_responsibles(PGconn *conn, const std::string &activity_id,
                             const std::vector<std::string> &collab_ids)
    {
        for (const auto &cid : collab_ids)
        {
            const char *p[2] = {activity_id.c_str(), cid.c_str()};
            PGresult *r = PQexecParams(conn,
                                       "INSERT INTO camp_activity_responsibles (activity_id, collaboration_id) "
                                       "VALUES ($1,$2) ON CONFLICT DO NOTHING",
                                       2, nullptr, p, nullptr, nullptr, 0);
            ExecStatusType st = PQresultStatus(r);
            PQclear(r);
            if (st != PGRES_COMMAND_OK)
                throw std::runtime_error("insert_responsibles: " + std::string(PQerrorMessage(conn)));
        }
    }
}

std::vector<CampActivity> Database::list_camp_activities(const std::string &camp_id)
{
    ensure_connected();
    std::vector<CampActivity> out;
    const char *p[1] = {camp_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT * FROM camp_activities WHERE camp_id=$1 ORDER BY created_at",
                               1, nullptr, p, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
        for (int i = 0; i < PQntuples(r); ++i)
            out.push_back(row_to_camp_activity(r, i));
    PQclear(r);
    for (auto &a : out)
        hydrate_camp_activity(a);
    return out;
}

std::optional<CampActivity> Database::get_camp_activity_by_id(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "SELECT * FROM camp_activities WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampActivity> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_camp_activity(r, 0);
    PQclear(r);
    if (out)
        hydrate_camp_activity(*out);
    return out;
}

std::optional<CampActivity> Database::create_camp_activity(const std::string &camp_id, const CampActivityInput &in)
{
    ensure_connected();
    exec_or_throw_local(conn_, "BEGIN", "create_camp_activity BEGIN");
    try
    {
        const char *cat = in.category_id ? in.category_id->c_str() : nullptr;
        std::string resp_json = Database::format_material_param(in.responsible);
        const char *p[5] = {camp_id.c_str(), cat, in.title.c_str(), in.location.c_str(), resp_json.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "INSERT INTO camp_activities (camp_id, category_id, title, location, responsible) "
                                   "VALUES ($1,$2,$3,$4,array(select jsonb_array_elements_text($5::jsonb))) RETURNING *",
                                   5, nullptr, p, nullptr, nullptr, 0);
        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0)
        {
            std::string err = PQerrorMessage(conn_);
            PQclear(r);
            throw std::runtime_error("create_camp_activity INSERT: " + err);
        }
        CampActivity a = row_to_camp_activity(r, 0);
        PQclear(r);

        insert_schedule_entries(conn_, a.id, in.schedule_entries);
        insert_responsibles(conn_, a.id, in.responsible_collaboration_ids);
        insert_content_nodes(conn_, a.id, in.content_nodes);

        exec_or_throw_local(conn_, "COMMIT", "create_camp_activity COMMIT");
        hydrate_camp_activity(a);
        return a;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

std::optional<CampActivity> Database::update_camp_activity(const std::string &id, const CampActivityInput &in)
{
    ensure_connected();
    exec_or_throw_local(conn_, "BEGIN", "update_camp_activity BEGIN");
    try
    {
        const char *cat = in.category_id ? in.category_id->c_str() : nullptr;
        std::string resp_json = Database::format_material_param(in.responsible);
        const char *p[5] = {id.c_str(), cat, in.title.c_str(), in.location.c_str(), resp_json.c_str()};
        PGresult *r = PQexecParams(conn_,
                                   "UPDATE camp_activities SET category_id=$2, title=$3, location=$4, "
                                   "responsible=array(select jsonb_array_elements_text($5::jsonb)) "
                                   "WHERE id=$1 RETURNING *",
                                   5, nullptr, p, nullptr, nullptr, 0);
        if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0)
        {
            PQclear(r);
            PQexec(conn_, "ROLLBACK");
            return std::nullopt;
        }
        CampActivity a = row_to_camp_activity(r, 0);
        PQclear(r);

        // Replace children wholesale (simplest correct approach).
        const char *pid[1] = {id.c_str()};
        PGresult *d1 = PQexecParams(conn_, "DELETE FROM schedule_entries WHERE activity_id=$1", 1, nullptr, pid, nullptr, nullptr, 0);
        PQclear(d1);
        PGresult *d2 = PQexecParams(conn_, "DELETE FROM camp_activity_responsibles WHERE activity_id=$1", 1, nullptr, pid, nullptr, nullptr, 0);
        PQclear(d2);
        PGresult *d3 = PQexecParams(conn_, "DELETE FROM content_nodes WHERE activity_id=$1", 1, nullptr, pid, nullptr, nullptr, 0);
        PQclear(d3);

        insert_schedule_entries(conn_, a.id, in.schedule_entries);
        insert_responsibles(conn_, a.id, in.responsible_collaboration_ids);
        insert_content_nodes(conn_, a.id, in.content_nodes);

        exec_or_throw_local(conn_, "COMMIT", "update_camp_activity COMMIT");
        hydrate_camp_activity(a);
        return a;
    }
    catch (...)
    {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

bool Database::delete_camp_activity(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_activities WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

std::optional<ScheduleEntry> Database::update_schedule_entry(const std::string &id, const ScheduleEntryInput &in)
{
    ensure_connected();
    std::string off = std::to_string(in.period_offset);
    std::string len = std::to_string(in.length);
    std::string lf = std::to_string(in.left_fraction);
    std::string wf = std::to_string(in.width_fraction);
    const char *p[6] = {id.c_str(), in.period_id.c_str(), off.c_str(), len.c_str(), lf.c_str(), wf.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE schedule_entries SET period_id=$2, period_offset=$3::int, length=$4::int, "
                               "left_fraction=$5::numeric, width_fraction=$6::numeric WHERE id=$1 "
                               "RETURNING id, activity_id, period_id, period_offset, length, "
                               "left_fraction::text AS left_fraction, width_fraction::text AS width_fraction, "
                               "created_at, updated_at",
                               6, nullptr, p, nullptr, nullptr, 0);
    std::optional<ScheduleEntry> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_schedule_entry(r, 0);
    PQclear(r);
    return out;
}

// ── Material lists & items ────────────────────────────────────────────────────

std::vector<CampMaterialList> Database::list_material_lists(const std::string &camp_id)
{
    ensure_connected();
    std::vector<CampMaterialList> out;
    const char *p[1] = {camp_id.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "SELECT * FROM camp_material_lists WHERE camp_id=$1 ORDER BY created_at",
                               1, nullptr, p, nullptr, nullptr, 0);
    if (PQresultStatus(r) == PGRES_TUPLES_OK)
    {
        for (int i = 0; i < PQntuples(r); ++i)
        {
            CampMaterialList l;
            l.id = col_str(r, i, "id");
            l.camp_id = col_str(r, i, "camp_id");
            if (const char *c = col_val(r, i, "collaboration_id"))
                l.collaboration_id = c;
            l.name = col_str(r, i, "name");
            l.created_at = col_str(r, i, "created_at");
            l.updated_at = col_str(r, i, "updated_at");
            out.push_back(std::move(l));
        }
    }
    PQclear(r);

    for (auto &l : out)
    {
        const char *p2[1] = {l.id.c_str()};
        PGresult *ir = PQexecParams(conn_,
                                    "SELECT id, material_list_id, content_node_id, period_id, article_name, "
                                    "quantity::text AS quantity, unit, created_at, updated_at "
                                    "FROM camp_material_items WHERE material_list_id=$1 ORDER BY created_at",
                                    1, nullptr, p2, nullptr, nullptr, 0);
        if (PQresultStatus(ir) == PGRES_TUPLES_OK)
            for (int i = 0; i < PQntuples(ir); ++i)
                l.items.push_back(row_to_material_item(ir, i));
        PQclear(ir);
    }
    return out;
}

std::optional<CampMaterialList> Database::create_material_list(const std::string &camp_id,
                                                               const std::optional<std::string> &collaboration_id,
                                                               const std::string &name)
{
    ensure_connected();
    const char *cid = collaboration_id ? collaboration_id->c_str() : nullptr;
    const char *p[3] = {camp_id.c_str(), cid, name.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camp_material_lists (camp_id, collaboration_id, name) "
                               "VALUES ($1,$2,$3) RETURNING *",
                               3, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampMaterialList> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
    {
        CampMaterialList l;
        l.id = col_str(r, 0, "id");
        l.camp_id = col_str(r, 0, "camp_id");
        if (const char *c = col_val(r, 0, "collaboration_id"))
            l.collaboration_id = c;
        l.name = col_str(r, 0, "name");
        l.created_at = col_str(r, 0, "created_at");
        l.updated_at = col_str(r, 0, "updated_at");
        out = std::move(l);
    }
    PQclear(r);
    return out;
}

bool Database::delete_material_list(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_material_lists WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}

std::optional<CampMaterialItem> Database::create_material_item(const std::string &list_id, const CampMaterialItemInput &in)
{
    ensure_connected();
    const char *node = in.content_node_id ? in.content_node_id->c_str() : nullptr;
    const char *period = in.period_id ? in.period_id->c_str() : nullptr;
    std::string qty;
    const char *qty_p = nullptr;
    if (in.quantity)
    {
        qty = std::to_string(*in.quantity);
        qty_p = qty.c_str();
    }
    const char *p[6] = {list_id.c_str(), node, period, in.article_name.c_str(), qty_p, in.unit.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "INSERT INTO camp_material_items (material_list_id, content_node_id, period_id, "
                               "article_name, quantity, unit) VALUES ($1,$2,$3,$4,$5::numeric,$6) "
                               "RETURNING id, material_list_id, content_node_id, period_id, article_name, "
                               "quantity::text AS quantity, unit, created_at, updated_at",
                               6, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampMaterialItem> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_material_item(r, 0);
    else
        fprintf(stderr, "[db] create_material_item: %s\n", PQerrorMessage(conn_));
    PQclear(r);
    return out;
}

std::optional<CampMaterialItem> Database::update_material_item(const std::string &id, const CampMaterialItemInput &in)
{
    ensure_connected();
    const char *node = in.content_node_id ? in.content_node_id->c_str() : nullptr;
    const char *period = in.period_id ? in.period_id->c_str() : nullptr;
    std::string qty;
    const char *qty_p = nullptr;
    if (in.quantity)
    {
        qty = std::to_string(*in.quantity);
        qty_p = qty.c_str();
    }
    const char *p[6] = {id.c_str(), node, period, in.article_name.c_str(), qty_p, in.unit.c_str()};
    PGresult *r = PQexecParams(conn_,
                               "UPDATE camp_material_items SET content_node_id=$2, period_id=$3, article_name=$4, "
                               "quantity=$5::numeric, unit=$6 WHERE id=$1 "
                               "RETURNING id, material_list_id, content_node_id, period_id, article_name, "
                               "quantity::text AS quantity, unit, created_at, updated_at",
                               6, nullptr, p, nullptr, nullptr, 0);
    std::optional<CampMaterialItem> out;
    if (PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) > 0)
        out = row_to_material_item(r, 0);
    PQclear(r);
    return out;
}

bool Database::delete_material_item(const std::string &id)
{
    ensure_connected();
    const char *p[1] = {id.c_str()};
    PGresult *r = PQexecParams(conn_, "DELETE FROM camp_material_items WHERE id=$1",
                               1, nullptr, p, nullptr, nullptr, 0);
    bool ok = PQresultStatus(r) == PGRES_COMMAND_OK && std::atoi(PQcmdTuples(r)) > 0;
    PQclear(r);
    return ok;
}
