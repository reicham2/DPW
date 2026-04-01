#pragma once
#include <vector>
#include <optional>
#include <string>
#include <libpq-fe.h>
#include "models.hpp"

class Database {
public:
    explicit Database(const std::string& conn_str);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Activities
    std::vector<Activity>    list_activities();
    std::optional<Activity>  get_activity_by_id(const std::string& id);
    std::optional<Activity>  create_activity(const ActivityInput& input);
    std::optional<Activity>  update_activity(const std::string& id, const ActivityInput& input);
    bool                     delete_activity(const std::string& id);

    // SiKo raw bytes (binary download)
    std::optional<std::vector<uint8_t>> get_siko(const std::string& activity_id);

private:
    PGconn* conn_{nullptr};
    void ensure_connected();

    Activity  row_to_activity(PGresult* res, int row);
    Program   row_to_program(PGresult* res, int row);
    void      attach_programs(std::vector<Activity>& activities);
    void      attach_programs_single(Activity& a);

    static std::vector<std::string> parse_pg_array(const char* raw);
    static std::string format_material_param(const std::vector<std::string>& material);
};
