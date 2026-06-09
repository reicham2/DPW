#pragma once
#include <string>
#include <optional>
#include "json.hpp"

struct NotificationRecord
{
    std::string id;
    std::string user_id;
    std::string category;
    std::string title;
    std::string message;
    std::optional<std::string> link;
    nlohmann::json payload;
    bool is_read{false};
    std::string created_at;
};

struct PushSubscriptionRecord
{
    std::string id;
    std::string user_id;
    std::string endpoint;
    std::string p256dh;
    std::string auth;
    std::string created_at;
    std::string updated_at;
};
