#include "db.hpp"
#include <stdexcept>
#include <cstdio>

Database::Database(const std::string& conn_str) {
    conn_ = PQconnectdb(conn_str.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        PQfinish(conn_);
        conn_ = nullptr;
        throw std::runtime_error("DB connect failed: " + err);
    }
}

Database::~Database() {
    if (conn_) PQfinish(conn_);
}

void Database::ensure_connected() {
    if (PQstatus(conn_) != CONNECTION_OK) {
        PQreset(conn_);
        if (PQstatus(conn_) != CONNECTION_OK)
            throw std::runtime_error("DB reconnect failed: " + std::string(PQerrorMessage(conn_)));
    }
}

Activity Database::row_to_activity(PGresult* res, int row) {
    Activity a;
    a.id          = PQgetvalue(res, row, PQfnumber(res, "id"));
    a.text        = PQgetvalue(res, row, PQfnumber(res, "text"));
    a.title       = PQgetvalue(res, row, PQfnumber(res, "title"));
    a.description = PQgetvalue(res, row, PQfnumber(res, "description"));
    a.responsible = PQgetvalue(res, row, PQfnumber(res, "responsible"));
    a.created_at  = PQgetvalue(res, row, PQfnumber(res, "created_at"));
    a.updated_at  = PQgetvalue(res, row, PQfnumber(res, "updated_at"));
    return a;
}

std::vector<Activity> Database::list_activities() {
    ensure_connected();
    PGresult* res = PQexec(conn_,
        "SELECT id, text, title, description, responsible, created_at, updated_at "
        "FROM activities ORDER BY created_at DESC");
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_activities failed: " + err);
    }
    std::vector<Activity> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(row_to_activity(res, i));
    PQclear(res);
    return out;
}

std::optional<Activity> Database::get_activity_by_id(const std::string& id) {
    ensure_connected();
    const char* params[1] = { id.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT id, text, title, description, responsible, created_at, updated_at "
        "FROM activities WHERE id = $1",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    Activity a = row_to_activity(res, 0);
    PQclear(res);
    return a;
}

std::optional<Activity> Database::create_activity(const ActivityInput& input) {
    ensure_connected();
    const char* params[4] = {
        input.text.c_str(),
        input.title.c_str(),
        input.description.c_str(),
        input.responsible.c_str()
    };
    PGresult* res = PQexecParams(conn_,
        "INSERT INTO activities (text, title, description, responsible) "
        "VALUES ($1, $2, $3, $4) "
        "RETURNING id, text, title, description, responsible, created_at, updated_at",
        4, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    Activity a = row_to_activity(res, 0);
    PQclear(res);
    return a;
}

std::optional<Activity> Database::update_activity(const std::string& id, const ActivityInput& input) {
    ensure_connected();
    const char* params[5] = {
        input.text.c_str(),
        input.title.c_str(),
        input.description.c_str(),
        input.responsible.c_str(),
        id.c_str()
    };
    PGresult* res = PQexecParams(conn_,
        "UPDATE activities SET text=$1, title=$2, description=$3, responsible=$4 "
        "WHERE id=$5 "
        "RETURNING id, text, title, description, responsible, created_at, updated_at",
        5, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    Activity a = row_to_activity(res, 0);
    PQclear(res);
    return a;
}

bool Database::delete_activity(const std::string& id) {
    ensure_connected();
    const char* params[1] = { id.c_str() };
    PGresult* res = PQexecParams(conn_,
        "DELETE FROM activities WHERE id = $1",
        1, nullptr, params, nullptr, nullptr, 0);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    PQclear(res);
    return ok;
}
