#include "db.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>

// ---- Constructor / Destructor -----------------------------------------------

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

// ---- PostgreSQL array helpers -----------------------------------------------

// Parses PostgreSQL text-mode array like {val1,"val two",val3} into a vector
std::vector<std::string> Database::parse_pg_array(const char* raw) {
    std::vector<std::string> result;
    if (!raw || raw[0] != '{') return result;
    const char* p = raw + 1;
    while (*p && *p != '}') {
        std::string token;
        if (*p == '"') {
            ++p;
            while (*p && !(*p == '"' && *(p-1) != '\\')) {
                if (*p == '\\' && *(p+1)) { ++p; }
                token += *p++;
            }
            if (*p == '"') ++p;
        } else {
            while (*p && *p != ',' && *p != '}') token += *p++;
        }
        result.push_back(token);
        if (*p == ',') ++p;
    }
    return result;
}

// Formats a vector<string> as a JSON array string for use with jsonb_array_elements_text
std::string Database::format_material_param(const std::vector<std::string>& material) {
    // Produces a JSON array string like ["a","b with \"quotes\""]
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < material.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "\"";
        for (char c : material[i]) {
            if (c == '"')  oss << "\\\"";
            else if (c == '\\') oss << "\\\\";
            else oss << c;
        }
        oss << "\"";
    }
    oss << "]";
    return oss.str();
}

// ---- Row mappers ------------------------------------------------------------

Activity Database::row_to_activity(PGresult* res, int row) {
    Activity a;
    auto col = [&](const char* name) -> const char* {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c)) return nullptr;
        return PQgetvalue(res, row, c);
    };

    a.id           = col("id")         ? col("id")         : "";
    a.title        = col("title")      ? col("title")      : "";
    a.date         = col("date")       ? col("date")       : "";
    a.start_time   = col("start_time") ? col("start_time") : "";
    a.end_time     = col("end_time")   ? col("end_time")   : "";
    a.goal         = col("goal")       ? col("goal")       : "";
    a.location     = col("location")   ? col("location")   : "";
    a.responsible  = col("responsible")? col("responsible"): "";
    a.created_at   = col("created_at") ? col("created_at") : "";
    a.updated_at   = col("updated_at") ? col("updated_at") : "";

    const char* dept = col("department");
    if (dept) a.department = dept;

    const char* bwi = col("bad_weather_info");
    if (bwi) a.bad_weather_info = bwi;

    // needs_siko
    const char* ns = col("needs_siko");
    a.needs_siko = (ns && (ns[0] == 't' || ns[0] == '1'));

    // has_siko: check if column is non-null (we don't fetch bytes in list/get queries)
    int siko_col = PQfnumber(res, "has_siko");
    if (siko_col >= 0 && !PQgetisnull(res, row, siko_col)) {
        const char* hs = PQgetvalue(res, row, siko_col);
        if (hs && (hs[0] == 't' || hs[0] == '1')) {
            a.siko.push_back(0); // sentinel: non-empty means has_siko=true
        }
    }

    // material
    const char* mat = col("material");
    if (mat) a.material = parse_pg_array(mat);

    return a;
}

Program Database::row_to_program(PGresult* res, int row) {
    auto col = [&](const char* name) -> std::string {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c)) return "";
        return PQgetvalue(res, row, c);
    };
    Program p;
    p.id          = col("id");
    p.activity_id = col("activity_id");
    p.time        = col("time");
    p.title       = col("title");
    p.description = col("description");
    p.responsible = col("responsible");
    return p;
}

void Database::attach_programs(std::vector<Activity>& activities) {
    if (activities.empty()) return;

    // Build array literal: '{uuid1,uuid2,...}'
    std::string arr = "{";
    for (size_t i = 0; i < activities.size(); ++i) {
        if (i) arr += ",";
        arr += activities[i].id;
    }
    arr += "}";
    const char* params[1] = { arr.c_str() };

    PGresult* res = PQexecParams(conn_,
        "SELECT id, activity_id, time, title, description, responsible "
        "FROM programs WHERE activity_id = ANY($1::uuid[]) ORDER BY ctid",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) { PQclear(res); return; }

    int n = PQntuples(res);
    for (int i = 0; i < n; ++i) {
        Program p = row_to_program(res, i);
        for (auto& a : activities) {
            if (a.id == p.activity_id) { a.programs.push_back(p); break; }
        }
    }
    PQclear(res);
}

void Database::attach_programs_single(Activity& a) {
    std::vector<Activity> tmp = { a };
    attach_programs(tmp);
    a.programs = std::move(tmp[0].programs);
}

// ---- list_activities --------------------------------------------------------

std::vector<Activity> Database::list_activities() {
    ensure_connected();
    PGresult* res = PQexec(conn_,
        "SELECT id, title, date::text, start_time, end_time, goal, location, responsible, "
        "       department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
        "       bad_weather_info, created_at, updated_at "
        "FROM activities ORDER BY date DESC, start_time");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_activities: " + err);
    }
    std::vector<Activity> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i) out.push_back(row_to_activity(res, i));
    PQclear(res);
    attach_programs(out);
    return out;
}

// ---- get_activity_by_id -----------------------------------------------------

std::optional<Activity> Database::get_activity_by_id(const std::string& id) {
    ensure_connected();
    const char* params[1] = { id.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT id, title, date::text, start_time, end_time, goal, location, responsible, "
        "       department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
        "       bad_weather_info, created_at, updated_at "
        "FROM activities WHERE id = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    Activity a = row_to_activity(res, 0);
    PQclear(res);
    attach_programs_single(a);
    return a;
}

// ---- get_siko ---------------------------------------------------------------

std::optional<std::vector<uint8_t>> Database::get_siko(const std::string& activity_id) {
    ensure_connected();
    const char* params[1] = { activity_id.c_str() };
    // Request siko in binary format (resultFormat=1)
    int paramFormats[1] = {0};
    PGresult* res = PQexecParams(conn_,
        "SELECT siko FROM activities WHERE id = $1 AND siko IS NOT NULL",
        1, nullptr, params, nullptr, paramFormats, 1);  // last arg: result binary

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    int len = PQgetlength(res, 0, 0);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(PQgetvalue(res, 0, 0));
    std::vector<uint8_t> bytes(data, data + len);
    PQclear(res);
    return bytes;
}

// ---- Transaction helpers ----------------------------------------------------

static void exec_or_throw(PGconn* conn, const char* sql, const char* context) {
    PGresult* r = PQexec(conn, sql);
    ExecStatusType s = PQresultStatus(r);
    PQclear(r);
    if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK)
        throw std::runtime_error(std::string(context) + ": " + PQerrorMessage(conn));
}

// Insert programs within an open transaction
static void insert_programs(PGconn* conn,
                             const std::string& activity_id,
                             const std::vector<ProgramInput>& programs) {
    for (const auto& pi : programs) {
        const char* params[5] = {
            activity_id.c_str(),
            pi.time.c_str(),
            pi.title.c_str(),
            pi.description.c_str(),
            pi.responsible.c_str()
        };
        PGresult* r = PQexecParams(conn,
            "INSERT INTO programs (activity_id, time, title, description, responsible) "
            "VALUES ($1, $2, $3, $4, $5)",
            5, nullptr, params, nullptr, nullptr, 0);
        ExecStatusType s = PQresultStatus(r);
        PQclear(r);
        if (s != PGRES_COMMAND_OK)
            throw std::runtime_error("insert_programs: " + std::string(PQerrorMessage(conn)));
    }
}

// ---- create_activity --------------------------------------------------------

std::optional<Activity> Database::create_activity(const ActivityInput& input) {
    ensure_connected();

    std::string mat_json = Database::format_material_param(input.material);
    std::string dept_str = input.department ? *input.department : "";
    std::string bwi_str  = input.bad_weather_info ? *input.bad_weather_info : "";
    const char* needs_s  = input.needs_siko ? "true" : "false";

    exec_or_throw(conn_, "BEGIN", "create_activity BEGIN");

    try {
        Activity a;

        if (input.siko_base64 && !input.siko_base64->empty()) {
            // Insert with SiKo
            // dept_val is embedded in the SQL string below

            // Can't use parameterised for the enum cast + conditional, so build safely:
            // All user values go via $n params; only dept which is an enum needs casting.
            // Use a fixed SQL with the enum value embedded for dept (safe as it's validated against enum).
            std::string sql =
                "INSERT INTO activities "
                "(title, date, start_time, end_time, goal, location, responsible, department, material, needs_siko, siko) "
                "VALUES ($1, $2::date, $3, $4, $5, $6, $7, " +
                (input.department ? ("'" + dept_str + "'::department_enum") : std::string("NULL")) +
                ", array(select jsonb_array_elements_text($8::jsonb)), $9, decode($10, 'base64')) "
                "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                "department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
                "bad_weather_info, created_at, updated_at";

            const char* p2[10] = {
                input.title.c_str(), input.date.c_str(),
                input.start_time.c_str(), input.end_time.c_str(),
                input.goal.c_str(), input.location.c_str(),
                input.responsible.c_str(),
                mat_json.c_str(), needs_s,
                input.siko_base64->c_str()
            };
            PGresult* r = PQexecParams(conn_, sql.c_str(), 10, nullptr, p2, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
                std::string err = PQresultErrorMessage(r); PQclear(r);
                throw std::runtime_error("create_activity INSERT: " + err);
            }
            a = row_to_activity(r, 0); PQclear(r);
        } else {
            // Insert without SiKo
            std::string sql =
                "INSERT INTO activities "
                "(title, date, start_time, end_time, goal, location, responsible, department, material, needs_siko, bad_weather_info) "
                "VALUES ($1, $2::date, $3, $4, $5, $6, $7, " +
                (input.department ? ("'" + dept_str + "'::department_enum") : std::string("NULL")) +
                ", array(select jsonb_array_elements_text($8::jsonb)), $9, $10) "
                "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                "department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
                "bad_weather_info, created_at, updated_at";

            const char* p[10] = {
                input.title.c_str(), input.date.c_str(),
                input.start_time.c_str(), input.end_time.c_str(),
                input.goal.c_str(), input.location.c_str(),
                input.responsible.c_str(),
                mat_json.c_str(), needs_s,
                bwi_str.c_str()
            };
            PGresult* r = PQexecParams(conn_, sql.c_str(), 10, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
                std::string err = PQresultErrorMessage(r); PQclear(r);
                throw std::runtime_error("create_activity INSERT: " + err);
            }
            a = row_to_activity(r, 0); PQclear(r);
        }

        insert_programs(conn_, a.id, input.programs);
        exec_or_throw(conn_, "COMMIT", "create_activity COMMIT");
        attach_programs_single(a);
        return a;

    } catch (...) {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

// ---- update_activity --------------------------------------------------------

std::optional<Activity> Database::update_activity(const std::string& id, const ActivityInput& input) {
    ensure_connected();

    std::string mat_json = Database::format_material_param(input.material);
    std::string dept_str = input.department ? *input.department : "";
    std::string bwi_str  = input.bad_weather_info ? *input.bad_weather_info : "";
    const char* needs_s  = input.needs_siko ? "true" : "false";

    exec_or_throw(conn_, "BEGIN", "update_activity BEGIN");

    try {
        Activity a;

        // Case 1: new PDF uploaded — store it
        if (input.needs_siko && input.siko_base64 && !input.siko_base64->empty()) {
            std::string sql =
                "UPDATE activities SET "
                "title=$1, date=$2::date, start_time=$3, end_time=$4, "
                "goal=$5, location=$6, responsible=$7, department=" +
                (input.department ? ("'" + dept_str + "'::department_enum") : std::string("NULL")) +
                ", material=array(select jsonb_array_elements_text($8::jsonb)), "
                "needs_siko=$9, siko=decode($10, 'base64'), bad_weather_info=$11 "
                "WHERE id=$12 "
                "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                "department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
                "bad_weather_info, created_at, updated_at";

            const char* p[12] = {
                input.title.c_str(), input.date.c_str(),
                input.start_time.c_str(), input.end_time.c_str(),
                input.goal.c_str(), input.location.c_str(),
                input.responsible.c_str(),
                mat_json.c_str(), needs_s,
                input.siko_base64->c_str(),
                bwi_str.c_str(), id.c_str()
            };
            PGresult* r = PQexecParams(conn_, sql.c_str(), 12, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
                std::string err = PQresultErrorMessage(r); PQclear(r);
                throw std::runtime_error("update_activity UPDATE (with siko): " + err);
            }
            a = row_to_activity(r, 0); PQclear(r);

        // Case 2: needs_siko turned off — explicitly clear the stored file
        } else if (!input.needs_siko) {
            std::string sql =
                "UPDATE activities SET "
                "title=$1, date=$2::date, start_time=$3, end_time=$4, "
                "goal=$5, location=$6, responsible=$7, department=" +
                (input.department ? ("'" + dept_str + "'::department_enum") : std::string("NULL")) +
                ", material=array(select jsonb_array_elements_text($8::jsonb)), "
                "needs_siko=$9, siko=NULL, bad_weather_info=$10 "
                "WHERE id=$11 "
                "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                "department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
                "bad_weather_info, created_at, updated_at";

            const char* p[11] = {
                input.title.c_str(), input.date.c_str(),
                input.start_time.c_str(), input.end_time.c_str(),
                input.goal.c_str(), input.location.c_str(),
                input.responsible.c_str(),
                mat_json.c_str(), needs_s,
                bwi_str.c_str(), id.c_str()
            };
            PGresult* r = PQexecParams(conn_, sql.c_str(), 11, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
                std::string err = PQresultErrorMessage(r); PQclear(r);
                PQexec(conn_, "ROLLBACK");
                return std::nullopt;
            }
            a = row_to_activity(r, 0); PQclear(r);

        // Case 3: needs_siko on but no new file — keep existing siko unchanged
        } else {
            std::string sql =
                "UPDATE activities SET "
                "title=$1, date=$2::date, start_time=$3, end_time=$4, "
                "goal=$5, location=$6, responsible=$7, department=" +
                (input.department ? ("'" + dept_str + "'::department_enum") : std::string("NULL")) +
                ", material=array(select jsonb_array_elements_text($8::jsonb)), "
                "needs_siko=$9, bad_weather_info=$10 "
                "WHERE id=$11 "
                "RETURNING id, title, date::text, start_time, end_time, goal, location, responsible, "
                "department::text, material, needs_siko, (siko IS NOT NULL) AS has_siko, "
                "bad_weather_info, created_at, updated_at";

            const char* p[11] = {
                input.title.c_str(), input.date.c_str(),
                input.start_time.c_str(), input.end_time.c_str(),
                input.goal.c_str(), input.location.c_str(),
                input.responsible.c_str(),
                mat_json.c_str(), needs_s,
                bwi_str.c_str(), id.c_str()
            };
            PGresult* r = PQexecParams(conn_, sql.c_str(), 11, nullptr, p, nullptr, nullptr, 0);
            if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) == 0) {
                std::string err = PQresultErrorMessage(r); PQclear(r);
                PQexec(conn_, "ROLLBACK");
                return std::nullopt;
            }
            a = row_to_activity(r, 0); PQclear(r);
        }

        // Replace programs
        const char* del_params[1] = { id.c_str() };
        PGresult* del = PQexecParams(conn_,
            "DELETE FROM programs WHERE activity_id = $1",
            1, nullptr, del_params, nullptr, nullptr, 0);
        PQclear(del);

        insert_programs(conn_, a.id, input.programs);
        exec_or_throw(conn_, "COMMIT", "update_activity COMMIT");
        attach_programs_single(a);
        return a;

    } catch (...) {
        PQexec(conn_, "ROLLBACK");
        throw;
    }
}

// ---- delete_activity --------------------------------------------------------

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

// ---- User helpers -----------------------------------------------------------

UserRecord Database::row_to_user(PGresult* res, int row) {
    auto col = [&](const char* name) -> const char* {
        int c = PQfnumber(res, name);
        if (c < 0 || PQgetisnull(res, row, c)) return nullptr;
        return PQgetvalue(res, row, c);
    };
    UserRecord u;
    u.id            = col("id")            ? col("id")            : "";
    u.microsoft_oid = col("microsoft_oid") ? col("microsoft_oid") : "";
    u.email         = col("email")         ? col("email")         : "";
    u.display_name  = col("display_name")  ? col("display_name")  : "";
    u.created_at    = col("created_at")    ? col("created_at")    : "";
    u.updated_at    = col("updated_at")    ? col("updated_at")    : "";
    if (col("department")) u.department = col("department");
    return u;
}

// ---- list_users -------------------------------------------------------------

std::vector<UserRecord> Database::list_users() {
    ensure_connected();
    PGresult* res = PQexec(conn_,
        "SELECT id, microsoft_oid, email, display_name, department::text, created_at, updated_at "
        "FROM users ORDER BY display_name");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("list_users: " + err);
    }
    std::vector<UserRecord> out;
    int n = PQntuples(res);
    out.reserve(n);
    for (int i = 0; i < n; ++i) out.push_back(row_to_user(res, i));
    PQclear(res);
    return out;
}

// ---- upsert_user ------------------------------------------------------------

std::optional<UserRecord> Database::upsert_user(const std::string& oid,
                                                  const std::string& email,
                                                  const std::string& display_name) {
    ensure_connected();
    const char* params[3] = { oid.c_str(), email.c_str(), display_name.c_str() };
    PGresult* res = PQexecParams(conn_,
        "INSERT INTO users (microsoft_oid, email, display_name, department) "
        "VALUES ($1, $2, $3, 'Leiter'::department_enum) "
        "ON CONFLICT (microsoft_oid) DO UPDATE SET email = EXCLUDED.email, updated_at = NOW() "
        "RETURNING id, microsoft_oid, email, display_name, department::text, created_at, updated_at",
        3, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- get_user_by_oid --------------------------------------------------------

std::optional<UserRecord> Database::get_user_by_oid(const std::string& oid) {
    ensure_connected();
    const char* params[1] = { oid.c_str() };
    PGresult* res = PQexecParams(conn_,
        "SELECT id, microsoft_oid, email, display_name, department::text, created_at, updated_at "
        "FROM users WHERE microsoft_oid = $1",
        1, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}

// ---- update_user ------------------------------------------------------------

std::optional<UserRecord> Database::update_user(const std::string& oid,
                                                  const std::string& display_name,
                                                  const std::optional<std::string>& department) {
    ensure_connected();
    std::string dept_str = department ? *department : "";

    std::string sql =
        "UPDATE users SET display_name = $1, department = " +
        (department ? ("'" + dept_str + "'::department_enum") : std::string("NULL")) +
        " WHERE microsoft_oid = $2 "
        "RETURNING id, microsoft_oid, email, display_name, department::text, created_at, updated_at";

    const char* params[2] = { display_name.c_str(), oid.c_str() };
    PGresult* res = PQexecParams(conn_, sql.c_str(), 2, nullptr, params, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }
    UserRecord u = row_to_user(res, 0);
    PQclear(res);
    return u;
}
