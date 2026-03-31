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

    // Non-copyable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    std::vector<Activity> list_activities();
    std::optional<Activity> create_activity(const std::string& text);
    std::optional<Activity> update_activity(const std::string& id, const std::string& text);
    bool delete_activity(const std::string& id);

private:
    PGconn* conn_{nullptr};
    Activity row_to_activity(PGresult* res, int row);
    void ensure_connected();
};
