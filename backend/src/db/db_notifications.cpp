#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include "json.hpp"

// ---- Notification helpers ---------------------------------------------------

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
