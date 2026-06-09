#pragma once
#include <string>
#include <optional>
#include "core/models/activity.hpp"

struct ActivityShareLink
{
    std::string id;
    std::string activity_id;
    std::string share_token;
    std::string created_by;
    std::string created_at;
};

struct DeletedActivityRecord
{
    Activity activity;
    std::string deleted_at;
    std::optional<std::string> deleted_by_user_id;
    std::optional<std::string> deleted_by_display_name;
    std::optional<std::string> deleted_by_email;
};
