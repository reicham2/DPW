#include "db/database.hpp"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <functional>
#include <chrono>
#include <unistd.h>
#include "json.hpp"

namespace
{
    std::string shell_escape(const std::string &value)
    {
        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('\'');
        for (char c : value)
        {
            if (c == '\'')
                out += "'\\''";
            else
                out.push_back(c);
        }
        out.push_back('\'');
        return out;
    }

    std::string env_or_default(const char *name, const char *fallback)
    {
        const char *v = std::getenv(name);
        return (v && v[0] != '\0') ? std::string(v) : std::string(fallback);
    }

    int run_shell_command(const std::string &command)
    {
        return std::system(command.c_str());
    }

    struct DbRuntimeConfig
    {
        std::string host;
        std::string port;
        std::string dbname;
        std::string user;
        std::string password;
    };

    DbRuntimeConfig runtime_db_config()
    {
        DbRuntimeConfig cfg;
        cfg.host = env_or_default("POSTGRES_HOST", "db");
        cfg.port = env_or_default("POSTGRES_PORT", "5432");
        cfg.dbname = env_or_default("POSTGRES_DB", "activities");
        cfg.user = env_or_default("POSTGRES_USER", "postgres");
        cfg.password = env_or_default("POSTGRES_PASSWORD", "");
        return cfg;
    }

    std::string build_snapshot_file_path()
    {
        char pattern[] = "/tmp/dpw-schema-snapshot-XXXXXX";
        const int fd = mkstemp(pattern);
        if (fd < 0)
            throw std::runtime_error("Failed to create secure temp file for DB snapshot");
        close(fd);
        return std::string(pattern);
    }

    std::string create_snapshot(const DbRuntimeConfig &cfg)
    {
        const std::string snapshot_path = build_snapshot_file_path();
        const std::string cmd =
            "PGPASSWORD=" + shell_escape(cfg.password) + " "
                                                         "pg_dump --format=custom --no-owner --no-privileges "
                                                         "--host=" +
            shell_escape(cfg.host) + " "
                                     "--port=" +
            shell_escape(cfg.port) + " "
                                     "--username=" +
            shell_escape(cfg.user) + " "
                                     "--file=" +
            shell_escape(snapshot_path) + " " +
            shell_escape(cfg.dbname);

        if (run_shell_command(cmd) != 0)
        {
            throw std::runtime_error("Failed to create DB snapshot before init.sql sync");
        }
        return snapshot_path;
    }

    void restore_snapshot(const DbRuntimeConfig &cfg, const std::string &snapshot_path)
    {
        const std::string restore_cmd =
            "PGPASSWORD=" + shell_escape(cfg.password) + " "
                                                         "pg_restore --clean --if-exists --no-owner --no-privileges "
                                                         "--host=" +
            shell_escape(cfg.host) + " "
                                     "--port=" +
            shell_escape(cfg.port) + " "
                                     "--username=" +
            shell_escape(cfg.user) + " " +
            "--dbname=" + shell_escape(cfg.dbname) + " " +
            shell_escape(snapshot_path);

        if (run_shell_command(restore_cmd) != 0)
        {
            throw std::runtime_error("Failed to restore DB snapshot after init.sql sync error");
        }
    }

    void remove_snapshot_file(const std::string &snapshot_path)
    {
        if (!snapshot_path.empty())
        {
            std::error_code ec;
            std::filesystem::remove(snapshot_path, ec);
        }
    }

    void verify_schema_after_sync(PGconn *conn)
    {
        const char *required_tables[] = {
            "activities",
            "users",
            "notifications",
            "role_permissions",
        };

        for (const char *table : required_tables)
        {
            const char *params[1] = {table};
            PGresult *res = PQexecParams(
                conn,
                "SELECT EXISTS ("
                "  SELECT 1 FROM information_schema.tables "
                "  WHERE table_schema='public' AND table_name=$1"
                ");",
                1, nullptr, params, nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
            {
                std::string err = PQresultErrorMessage(res);
                PQclear(res);
                throw std::runtime_error("Schema verification query failed: " + err);
            }

            const bool exists = std::strcmp(PQgetvalue(res, 0, 0), "t") == 0;
            PQclear(res);
            if (!exists)
                throw std::runtime_error("Schema verification failed: missing table " + std::string(table));
        }
    }

    std::string read_text_file(const std::filesystem::path &path)
    {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error("Could not open schema file: " + path.string());

        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    std::string load_init_sql()
    {
        std::vector<std::filesystem::path> candidates;

        const char *env_path = std::getenv("DPW_INIT_SQL_PATH");
        if (env_path && env_path[0] != '\0')
            candidates.emplace_back(env_path);

        candidates.emplace_back("db/init.sql");
        candidates.emplace_back("../db/init.sql");
        candidates.emplace_back("/etc/dpw/init.sql");

        std::ostringstream searched;
        bool first = true;
        for (const auto &path : candidates)
        {
            if (std::filesystem::exists(path))
                return read_text_file(path);

            if (!first)
                searched << ", ";
            searched << path.string();
            first = false;
        }

        throw std::runtime_error("init.sql not found. Tried: " + searched.str());
    }
} // namespace

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

    run_schema_sync();
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

void Database::run_schema_sync()
{
    ensure_connected();
    const DbRuntimeConfig cfg = runtime_db_config();
    std::string snapshot_path;

    // Serializes schema updates so multiple backend replicas cannot race.
    PGresult *lock_res = PQexec(conn_, "SELECT pg_advisory_lock(hashtext('dpw_schema_sync_init_sql')); ");
    if (PQresultStatus(lock_res) != PGRES_TUPLES_OK)
    {
        std::string err = PQresultErrorMessage(lock_res);
        PQclear(lock_res);
        throw std::runtime_error("Failed to acquire schema sync lock: " + err);
    }
    PQclear(lock_res);

    try
    {
        snapshot_path = create_snapshot(cfg);

        PGresult *begin_res = PQexec(conn_, "BEGIN;");
        if (PQresultStatus(begin_res) != PGRES_COMMAND_OK)
        {
            std::string err = PQresultErrorMessage(begin_res);
            PQclear(begin_res);
            throw std::runtime_error("Failed to begin schema sync transaction: " + err);
        }
        PQclear(begin_res);

        const std::string init_sql = load_init_sql();
        PGresult *sync_res = PQexec(conn_, init_sql.c_str());
        if (PQresultStatus(sync_res) != PGRES_COMMAND_OK)
        {
            std::string err = PQresultErrorMessage(sync_res);
            PQclear(sync_res);
            PGresult *rollback_res = PQexec(conn_, "ROLLBACK;");
            PQclear(rollback_res);
            throw std::runtime_error("Schema sync via init.sql failed: " + err);
        }
        PQclear(sync_res);

        PGresult *commit_res = PQexec(conn_, "COMMIT;");
        if (PQresultStatus(commit_res) != PGRES_COMMAND_OK)
        {
            std::string err = PQresultErrorMessage(commit_res);
            PQclear(commit_res);
            throw std::runtime_error("Failed to commit schema sync transaction: " + err);
        }
        PQclear(commit_res);

        verify_schema_after_sync(conn_);
    }
    catch (const std::exception &e)
    {
        if (PQtransactionStatus(conn_) == PQTRANS_INTRANS)
        {
            PGresult *rollback_res = PQexec(conn_, "ROLLBACK;");
            PQclear(rollback_res);
        }

        std::string restore_error;
        if (!snapshot_path.empty())
        {
            try
            {
                restore_snapshot(cfg, snapshot_path);
            }
            catch (const std::exception &restore_ex)
            {
                restore_error = restore_ex.what();
            }
        }

        PGresult *unlock_res = PQexec(conn_, "SELECT pg_advisory_unlock(hashtext('dpw_schema_sync_init_sql')); ");
        PQclear(unlock_res);

        remove_snapshot_file(snapshot_path);

        if (!restore_error.empty())
        {
            throw std::runtime_error(std::string(e.what()) + " | rollback snapshot restore failed: " + restore_error);
        }
        throw;
    }

    PGresult *unlock_res = PQexec(conn_, "SELECT pg_advisory_unlock(hashtext('dpw_schema_sync_init_sql')); ");
    PQclear(unlock_res);
    remove_snapshot_file(snapshot_path);

    std::printf("[db] Schema sync via init.sql completed\n");
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
