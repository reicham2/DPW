#pragma once
#include <string>
#include <optional>

struct UserRecord
{
    std::string id;
    std::string microsoft_oid;
    std::string email;
    std::string display_name;
    std::optional<std::string> department;
    std::string role;
    std::string time_display_mode{"minutes"};
    bool notify_material_assigned{true};
    bool notify_activity_assigned{true};
    bool notify_program_assigned{true};
    bool notify_mail_own_activity{true};
    bool notify_mail_department{true};
    bool notify_channel_websocket{true};
    bool notify_channel_email{false};
    std::string created_at;
    std::string updated_at;
};
