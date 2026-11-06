#include "db/database.hpp"
#include <memory>
#include <stdexcept>

namespace
{
    using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;

    Result query(PGconn *conn, const char *sql, int count, const char *const *params)
    {
        Result result(PQexecParams(conn, sql, count, nullptr, params, nullptr, nullptr, 0), PQclear);
        if (!result || PQresultStatus(result.get()) != PGRES_TUPLES_OK)
            throw std::runtime_error("Audit database operation failed: " +
                std::string(result ? PQresultErrorMessage(result.get()) : PQerrorMessage(conn)));
        return result;
    }

    void require_row(const Result &result)
    {
        if (PQntuples(result.get()) != 1)
            throw std::runtime_error("Audit event missing or already finalized");
    }
}

std::string Database::begin_audit_event(const std::string &action, const std::string &resource_type,
                                      const std::string &resource_id)
{
    ensure_connected();
    const char *params[] = {action.c_str(), resource_type.c_str(), resource_id.c_str()};
    auto result = query(conn_,
        "INSERT INTO audit_events(action,resource_type,resource_id) VALUES ($1,$2,$3) RETURNING id",
        3, params);
    require_row(result);
    return PQgetvalue(result.get(), 0, 0);
}

void Database::attribute_audit_event(const std::string &id, const std::string &actor_id,
                                    const std::string &actor_name)
{
    ensure_connected();
    const char *params[] = {id.c_str(), actor_id.c_str(), actor_name.c_str()};
    auto result = query(conn_,
        "UPDATE audit_events SET actor_id=$2,actor_name=$3 WHERE id=$1::bigint AND outcome='pending' RETURNING id",
        3, params);
    require_row(result);
}

void Database::finish_audit_event(const std::string &id, const std::string &outcome, int status,
                                 const std::string &created_id, const nlohmann::json &details)
{
    ensure_connected();
    auto status_string = std::to_string(status);
    auto details_string = details.dump();
    const char *params[] = {id.c_str(), outcome.c_str(), status_string.c_str(), created_id.c_str(), details_string.c_str()};
    auto result = query(conn_,
        "UPDATE audit_events SET outcome=$2,http_status=$3::int,finished_at=clock_timestamp(),"
        "resource_id=CASE WHEN resource_id='' THEN $4 ELSE resource_id END,details=$5::jsonb "
        "WHERE id=$1::bigint AND outcome='pending' RETURNING id", 5, params);
    require_row(result);
}

void Database::set_audit_resource(const std::string &id, const std::string &resource_type,
                                 const std::string &resource_id)
{
    ensure_connected();
    const char *params[] = {id.c_str(), resource_type.c_str(), resource_id.c_str()};
    auto result = query(conn_,
        "UPDATE audit_events SET resource_type=$2,resource_id=$3 "
        "WHERE id=$1::bigint AND outcome='pending' RETURNING id", 3, params);
    require_row(result);
}

nlohmann::json Database::list_audit_events(int limit, const std::string &before,
                                         const std::string &actor, const std::string &outcome)
{
    ensure_connected();
    auto take = std::to_string(limit + 1);
    const char *params[] = {take.c_str(), before.c_str(), actor.c_str(), outcome.c_str()};
    auto result = query(conn_,
        "SELECT id::text,to_char(occurred_at AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"'),"
        "actor_id,actor_name,action,resource_type,resource_id,outcome,http_status,details "
        "FROM audit_events "
        "WHERE ($2='' OR id<NULLIF($2,'')::bigint) "
        "AND ($3='' OR actor_id=$3 OR strpos(lower(actor_name),lower($3))>0) "
        "AND ($4='' OR outcome=$4) ORDER BY id DESC LIMIT $1::int", 4, params);
    nlohmann::json entries = nlohmann::json::array();
    auto rows = PQntuples(result.get());
    for (int i = 0; i < rows && i < limit; ++i)
    {
        auto nullable = [&](int col) -> nlohmann::json
        {
            return PQgetisnull(result.get(), i, col) ? nlohmann::json(nullptr) :
                nlohmann::json(PQgetvalue(result.get(), i, col));
        };
        entries.push_back({
            {"id", PQgetvalue(result.get(), i, 0)},
            {"occurred_at", PQgetvalue(result.get(), i, 1)},
            {"actor_id", nullable(2)}, {"actor_name", nullable(3)},
            {"action", PQgetvalue(result.get(), i, 4)},
            {"resource_type", PQgetvalue(result.get(), i, 5)},
            {"resource_id", PQgetvalue(result.get(), i, 6)},
            {"outcome", PQgetvalue(result.get(), i, 7)},
            {"http_status", PQgetisnull(result.get(), i, 8) ? nlohmann::json(nullptr) :
                nlohmann::json(std::stoi(PQgetvalue(result.get(), i, 8)))},
            {"details", nlohmann::json::parse(PQgetvalue(result.get(), i, 9))}
        });
    }

    return {{"entries", entries}, {"next_cursor", rows > limit ?
        nlohmann::json(PQgetvalue(result.get(), limit - 1, 0)) : nlohmann::json(nullptr)}};
}

int Database::purge_audit_events_older_than_days(int days)
{
    if (days < 1 || days > 3650)
        throw std::invalid_argument("Audit retention must be between 1 and 3650 days");
    ensure_connected();
    auto value = std::to_string(days);
    const char *params[] = {value.c_str()};
    auto result = query(conn_, "SELECT purge_audit_events($1::int)", 1, params);
    return std::stoi(PQgetvalue(result.get(), 0, 0));
}
