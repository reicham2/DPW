#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include "json.hpp"

// ── Transaction helper (file-local) ─────────────────────────────────────────

static void exec_or_throw(PGconn *conn, const char *sql, const char *context)
{
    PGresult *r = PQexec(conn, sql);
    ExecStatusType s = PQresultStatus(r);
    PQclear(r);
    if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK)
        throw std::runtime_error(std::string(context) + ": " + PQerrorMessage(conn));
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
    rp.materials_manage_scope = col("materials_manage_scope") ? col("materials_manage_scope") : "none";
    rp.ideenkiste_scope = col("ideenkiste_scope") ? col("ideenkiste_scope") : "none";
    rp.ideenkiste_add_scope = col("ideenkiste_add_scope") ? col("ideenkiste_add_scope") : "none";
    rp.ideenkiste_delete_scope = col("ideenkiste_delete_scope") ? col("ideenkiste_delete_scope") : "none";
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
                           "       user_dept_scope, user_role_scope, locations_manage_scope, "
                           "       materials_manage_scope, "
                           "       ideenkiste_scope, ideenkiste_add_scope, ideenkiste_delete_scope "
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
                                 "       event_templates_scope, event_publish_scope, "
                                 "       user_dept_scope, user_role_scope, locations_manage_scope, "
                                 "       materials_manage_scope, "
                                 "       ideenkiste_scope, ideenkiste_add_scope, ideenkiste_delete_scope "
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
                                      const std::string &locations_manage_scope,
                                      const std::string &materials_manage_scope,
                                      const std::string &ideenkiste_scope,
                                      const std::string &ideenkiste_add_scope,
                                      const std::string &ideenkiste_delete_scope)
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
    const char *p18 = materials_manage_scope.c_str();
    const char *p19 = ideenkiste_scope.c_str();
    const char *p20 = ideenkiste_add_scope.c_str();
    const char *p21 = ideenkiste_delete_scope.c_str();
    const char *params[21] = {p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21};
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, "
                                 "can_read_all_depts, can_write_all_depts, "
                                 "activity_read_scope, activity_create_scope, activity_edit_scope, "
                                 "mail_send_scope, mail_templates_scope, form_scope, form_templates_scope, "
                                 "event_templates_scope, event_publish_scope, "
                                 "user_dept_scope, user_role_scope, locations_manage_scope, materials_manage_scope, "
                                 "ideenkiste_scope, ideenkiste_add_scope, ideenkiste_delete_scope) "
                                 "VALUES ($1, $2::boolean, $3::boolean, $4::boolean, $5::boolean, "
                                 "$6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21) "
                                 "ON CONFLICT (role) DO UPDATE SET "
                                 "can_read_own_dept = $2::boolean, can_write_own_dept = $3::boolean, "
                                 "can_read_all_depts = $4::boolean, can_write_all_depts = $5::boolean, "
                                 "activity_read_scope = $6, activity_create_scope = $7, activity_edit_scope = $8, "
                                 "mail_send_scope = $9, mail_templates_scope = $10, "
                                 "form_scope = $11, form_templates_scope = $12, "
                                 "event_templates_scope = $13, event_publish_scope = $14, "
                                 "user_dept_scope = $15, user_role_scope = $16, "
                                 "locations_manage_scope = $17, materials_manage_scope = $18, "
                                 "ideenkiste_scope = $19, "
                                 "ideenkiste_add_scope = $20, ideenkiste_delete_scope = $21",
                                 21, nullptr, params, nullptr, nullptr, 0);
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
                                 "WHERE s.share_token = $1 AND a.deleted_at IS NULL",
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

// ── App settings ─────────────────────────────────────────────────────────────

bool Database::set_app_setting(const std::string &key,
                               bool is_secret,
                               const std::string &value,
                               const std::string &encryption_key)
{
    ensure_connected();
    if (key.empty() || value.empty())
        return false;
    if (is_secret && encryption_key.empty())
        return false;

    const char *params[4] = {
        key.c_str(),
        is_secret ? "true" : "false",
        value.c_str(),
        encryption_key.c_str(),
    };

    PGresult *res = PQexecParams(
        conn_,
        "INSERT INTO app_settings (key, is_secret, value_text, value_secret) "
        "VALUES ("
        "  $1, "
        "  $2::boolean, "
        "  CASE WHEN $2::boolean THEN NULL ELSE $3 END, "
        "  CASE WHEN $2::boolean THEN pgp_sym_encrypt($3, $4, 'cipher-algo=aes256, compress-algo=0') ELSE NULL END"
        ") "
        "ON CONFLICT (key) DO UPDATE SET "
        "  is_secret = EXCLUDED.is_secret, "
        "  value_text = EXCLUDED.value_text, "
        "  value_secret = EXCLUDED.value_secret, "
        "  updated_at = NOW()",
        4, nullptr, params, nullptr, nullptr, 0);

    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::clear_app_setting(const std::string &key, bool is_secret)
{
    ensure_connected();
    if (key.empty())
        return false;

    const char *params[2] = {
        key.c_str(),
        is_secret ? "true" : "false",
    };
    PGresult *res = PQexecParams(conn_,
                                 "INSERT INTO app_settings (key, is_secret, value_text, value_secret) "
                                 "VALUES ($1, $2::boolean, NULL, NULL) "
                                 "ON CONFLICT (key) DO UPDATE SET "
                                 "  is_secret = EXCLUDED.is_secret, "
                                 "  value_text = NULL, "
                                 "  value_secret = NULL, "
                                 "  updated_at = NOW()",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::has_app_setting(const std::string &key, bool is_secret)
{
    ensure_connected();
    if (key.empty())
        return false;

    const char *params[1] = {key.c_str()};
    const char *sql = is_secret
                          ? "SELECT 1 FROM app_settings WHERE key = $1 AND is_secret = true AND value_secret IS NOT NULL LIMIT 1"
                          : "SELECT 1 FROM app_settings WHERE key = $1 AND is_secret = false AND value_text IS NOT NULL LIMIT 1";

    PGresult *res = PQexecParams(conn_,
                                 sql,
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0;
    PQclear(res);
    return ok;
}

std::optional<std::string> Database::get_app_setting(const std::string &key,
                                                     bool is_secret,
                                                     const std::string &encryption_key)
{
    ensure_connected();
    if (key.empty())
        return std::nullopt;
    if (is_secret && encryption_key.empty())
        return std::nullopt;

    const char *params[2] = {key.c_str(), encryption_key.c_str()};
    PGresult *res = nullptr;
    if (is_secret)
    {
        res = PQexecParams(
            conn_,
            "SELECT pgp_sym_decrypt(value_secret, $2) "
            "FROM app_settings "
            "WHERE key = $1 AND is_secret = true AND value_secret IS NOT NULL",
            2, nullptr, params, nullptr, nullptr, 0);
    }
    else
    {
        res = PQexecParams(
            conn_,
            "SELECT value_text "
            "FROM app_settings "
            "WHERE key = $1 AND is_secret = false AND value_text IS NOT NULL",
            1, nullptr, params, nullptr, nullptr, 0);
    }

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
    {
        PQclear(res);
        return std::nullopt;
    }
    std::string out = PQgetvalue(res, 0, 0);
    PQclear(res);
    return out;
}
