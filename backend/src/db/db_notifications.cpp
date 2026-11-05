#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include "json.hpp"
#include "infra/app_config.hpp"
#include "utils/strings.hpp"

// ---- Notification helpers ---------------------------------------------------

void Database::create_due_activity_reminders()
{
    ensure_connected();
    const std::string base = trim_ascii(app_config::get_or(*this, app_config::kPublicBaseUrl, ""));
    if (base.empty())
        throw std::runtime_error("activity-reminders: public base URL is not configured");
    const char *params[] = {base.c_str()};
    PGresult *res = PQexecParams(conn_, "SELECT create_due_activity_reminders(NOW(), $1)",
                                1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("create_due_activity_reminders: " + error);
    }
    PQclear(res);
}

std::vector<NotificationRecord> Database::claim_activity_reminder_deliveries()
{
    ensure_connected();
    PGresult *res = PQexec(conn_, "SELECT * FROM claim_activity_reminder_deliveries()");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("claim_activity_reminder_deliveries: " + error);
    }
    std::vector<NotificationRecord> notes;
    for (int i = 0; i < PQntuples(res); ++i)
        notes.push_back(row_to_notification(res, i));
    PQclear(res);
    return notes;
}

void Database::finish_activity_reminder_delivery(const std::string &notification_id, bool delivered)
{
    ensure_connected();
    const char *params[] = {notification_id.c_str(), delivered ? "true" : "false"};
    PGresult *res = PQexecParams(conn_,
        "UPDATE activity_reminders SET browser_delivered = $2::boolean "
        "WHERE notification_id = $1::uuid", 2, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("finish_activity_reminder_delivery: " + error);
    }
    PQclear(res);
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

void Database::purge_expired_activity_notifications()
{
    ensure_connected();
    PGresult *res = PQexec(conn_,
                           "DELETE FROM notifications n "
                           "USING activities a "
                           "WHERE n.payload ? 'activity_id' "
                           "  AND (n.payload->>'activity_id') = a.id::text "
                           "  AND NOW() >= ((a.date::timestamp) + INTERVAL '1 day')");
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("purge_expired_activity_notifications: " + error);
    }
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
    PGresult *res = nullptr;
    if (limit > 0)
    {
        std::string limit_str = std::to_string(limit);
        const char *params[2] = {user_id.c_str(), limit_str.c_str()};
        res = PQexecParams(conn_,
                           "SELECT id, user_id, category, title, message, link, payload, is_read, created_at "
                           "FROM active_notifications WHERE user_id = $1 "
                           "ORDER BY created_at DESC LIMIT $2::int",
                           2, nullptr, params, nullptr, nullptr, 0);
    }
    else
    {
        const char *params[1] = {user_id.c_str()};
        res = PQexecParams(conn_,
                           "SELECT id, user_id, category, title, message, link, payload, is_read, created_at "
                           "FROM active_notifications WHERE user_id = $1 "
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
    const char *params[2] = {user_id.c_str(), notification_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE active_notifications SET is_read = true "
                                 "WHERE user_id = $1 AND id = $2",
                                 2, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}

bool Database::mark_all_notifications_read(const std::string &user_id)
{
    ensure_connected();
    const char *params[1] = {user_id.c_str()};
    PGresult *res = PQexecParams(conn_,
                                 "UPDATE active_notifications SET is_read = true WHERE user_id = $1 AND is_read = false",
                                 1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}
