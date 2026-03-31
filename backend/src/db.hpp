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

    std::vector<Activity> list_activities();
    std::optional<Activity> get_activity_by_id(const std::string& id);
    std::optional<Activity> create_activity(const ActivityInput& input);
    std::optional<Activity> update_activity(const std::string& id, const ActivityInput& input);
    bool delete_activity(const std::string& id);

private:
    PGconn* conn_{nullptr};
    Activity row_to_activity(PGresult* res, int row);
    void ensure_connected();
};
