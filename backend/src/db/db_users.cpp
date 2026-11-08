#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include "json.hpp"

std::string Database::add_proofreading_word(const std::string &user_id, const std::string &word)
{
    ensure_connected();
    PGresult *res = PQexec(conn_, "BEGIN");
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("dictionary begin: " + error);
    }
    PQclear(res);
    try
    {
        const char *params[] = {user_id.c_str(), word.c_str()};
        res = PQexecParams(conn_, "SELECT id FROM users WHERE id=$1 FOR UPDATE", 1, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
        {
            std::string error = PQresultErrorMessage(res);
            PQclear(res);
            throw std::runtime_error("dictionary user lock: " + error);
        }
        PQclear(res);
        res = PQexecParams(conn_,
            "WITH input AS (SELECT normalize($2, NFC) AS word), "
            "status AS (SELECT CASE "
            "WHEN char_length(word) NOT BETWEEN 1 AND 100 OR octet_length(word)>256 "
            "OR word !~ '^[[:alpha:]]+([-''’][[:alpha:]]+)*$' THEN 'invalid' "
            "WHEN EXISTS (SELECT 1 FROM proofreading_dictionary WHERE user_id=$1 AND proofreading_dictionary.word=input.word) THEN 'ok' "
            "WHEN (SELECT count(*) FROM proofreading_dictionary WHERE user_id=$1)>=500 THEN 'limit' "
            "ELSE 'insert' END AS result, word FROM input), "
            "inserted AS (INSERT INTO proofreading_dictionary(user_id,word) "
            "SELECT $1,word FROM status WHERE result='insert' ON CONFLICT DO NOTHING) "
            "SELECT CASE WHEN result='insert' THEN 'ok' ELSE result END FROM status",
            2, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
        {
            std::string error = PQresultErrorMessage(res);
            PQclear(res);
            throw std::runtime_error("dictionary insert: " + error);
        }
        const std::string result = PQgetvalue(res, 0, 0);
        PQclear(res);
        res = PQexec(conn_, "COMMIT");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            std::string error = PQresultErrorMessage(res);
            PQclear(res);
            throw std::runtime_error("dictionary commit: " + error);
        }
        PQclear(res);
        return result;
    }
    catch (...)
    {
        res = PQexec(conn_, "ROLLBACK");
        PQclear(res);
        throw;
    }
}

bool Database::delete_proofreading_word(const std::string &user_id, const std::string &word)
{
    ensure_connected();
    const char *params[] = {user_id.c_str(), word.c_str()};
    PGresult *res = PQexecParams(conn_,
        "DELETE FROM proofreading_dictionary WHERE user_id=$1 AND word=normalize($2, NFC) RETURNING word",
        2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("dictionary delete: " + error);
    }
    const bool removed = PQntuples(res) != 0;
    PQclear(res);
    return removed;
}

std::vector<int> Database::known_proofreading_words(const std::string &user_id, const nlohmann::json &words)
{
    ensure_connected();
    const std::string values = words.dump();
    const char *params[] = {user_id.c_str(), values.c_str()};
    PGresult *res = PQexecParams(conn_,
        "SELECT (candidate.ordinality-1)::integer FROM jsonb_array_elements_text($2::jsonb) "
        "WITH ORDINALITY AS candidate(word,ordinality) "
        "JOIN proofreading_dictionary d ON d.user_id=$1 AND d.word=normalize(candidate.word, NFC)",
        2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("dictionary lookup: " + error);
    }
    std::vector<int> indices;
    for (int i = 0; i < PQntuples(res); ++i)
        indices.push_back(std::stoi(PQgetvalue(res, i, 0)));
    PQclear(res);
    return indices;
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
    u.notify_activity_reminder = col("notify_activity_reminder") ? std::string(col("notify_activity_reminder")) == "t" : true;
    u.activity_reminder_hours = col("activity_reminder_hours") ? std::stoi(col("activity_reminder_hours")) : 24;
    u.notify_program_assigned = col("notify_program_assigned") ? std::string(col("notify_program_assigned")) == "t" : true;
    u.notify_mail_own_activity = col("notify_mail_own_activity") ? std::string(col("notify_mail_own_activity")) == "t" : true;
    u.notify_mail_department = col("notify_mail_department") ? std::string(col("notify_mail_department")) == "t" : true;
    u.notify_channel_websocket = col("notify_channel_websocket") ? std::string(col("notify_channel_websocket")) == "t" : true;
    u.notify_channel_email = col("notify_channel_email") ? std::string(col("notify_channel_email")) == "t" : false;
    u.avatar_color = col("avatar_color") ? col("avatar_color") : "#4f8cff";
    u.created_at = col("created_at") ? col("created_at") : "";
    u.updated_at = col("updated_at") ? col("updated_at") : "";
    if (col("department"))
        u.department = col("department");
    return u;
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

// ---- list_users -------------------------------------------------------------

std::vector<UserRecord> Database::list_users()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "SELECT id, microsoft_oid, email, display_name, department, role, time_display_mode, "
                           "       notify_material_assigned, notify_activity_assigned, notify_program_assigned, notify_mail_own_activity, notify_mail_department, "
                           "       notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
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
            "SET email = EXCLUDED.email, display_name = EXCLUDED.display_name, role = '" +
            initial_role + "', updated_at = NOW() ";
    }
    else
    {
        on_conflict =
            "ON CONFLICT (microsoft_oid) DO UPDATE "
            "SET email = EXCLUDED.email, display_name = EXCLUDED.display_name, updated_at = NOW() ";
    }

    std::string sql =
        "INSERT INTO users (microsoft_oid, email, display_name, department, role) "
        "VALUES ($1, $2, $3, '" +
        initial_dept + "', '" + initial_role + "') " +
        on_conflict +
        "RETURNING id, microsoft_oid, email, display_name, department, role, time_display_mode, "
        "          notify_material_assigned, notify_activity_assigned, notify_program_assigned, "
        "          notify_mail_own_activity, notify_mail_department, "
        "          notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
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
                                 "       notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
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
                                 "       notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
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

// ---- get_users_by_ids -------------------------------------------------------

std::vector<UserRecord> Database::get_users_by_ids(const std::vector<std::string> &ids)
{
    std::string array_literal = "{";
    bool first = true;
    for (const auto &id : ids)
    {
        if (id.empty())
            continue;
        if (!first)
            array_literal += ',';
        first = false;
        array_literal += '"';
        for (char c : id)
        {
            if (c == '"' || c == '\\')
                array_literal += '\\';
            array_literal += c;
        }
        array_literal += '"';
    }
    array_literal += '}';
    if (first)
        return {};

    ensure_connected();
    const char *params[1] = {array_literal.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT id, microsoft_oid, email, display_name, department, role, time_display_mode, "
                                 "       notify_material_assigned, notify_activity_assigned, notify_program_assigned, notify_mail_own_activity, notify_mail_department, "
                                 "       notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
                                 "       created_at, updated_at "
                                 "FROM users WHERE id::text = ANY($1::text[])",
                                 1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("get_users_by_ids: " + err);
    }
    std::vector<UserRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_user(res, i));
    PQclear(res);
    return out;
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
                                                const std::optional<bool> &notify_channel_email,
                                                const std::optional<std::string> &avatar_color,
                                                const std::optional<bool> &notify_activity_reminder,
                                                const std::optional<int> &activity_reminder_hours)
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
    if (notify_activity_reminder.has_value())
        sql += std::string(", notify_activity_reminder = ") + (*notify_activity_reminder ? "true" : "false");
    if (activity_reminder_hours.has_value())
        sql += ", activity_reminder_hours = " + std::to_string(*activity_reminder_hours);
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
    if (avatar_color.has_value())
        sql += ", avatar_color = '" + *avatar_color + "'";
    sql +=
        " WHERE microsoft_oid = $2 "
        "RETURNING id, microsoft_oid, email, display_name, department, role, time_display_mode, "
        "          notify_material_assigned, notify_activity_assigned, notify_program_assigned, "
        "          notify_mail_own_activity, notify_mail_department, "
        "          notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
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
        "          notify_activity_reminder, activity_reminder_hours, notify_channel_websocket, notify_channel_email, avatar_color, "
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

// ---- Push subscriptions -----------------------------------------------------

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
        const std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_push_subscriptions_for_user: " + error);
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
    const char *params[2] = {endpoint.c_str(), auth.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "SELECT n.id, n.user_id, n.category, n.title, n.message, n.link, n.payload, n.is_read, n.created_at "
                                 "FROM push_subscriptions ps "
                                 "JOIN active_notifications n ON n.user_id = ps.user_id "
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
