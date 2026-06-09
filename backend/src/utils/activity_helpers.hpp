#pragma once
#include <string>
#include <vector>
#include <optional>
#include "core/models/activity.hpp"
#include "core/models/user.hpp"
#include "core/models/notification.hpp"

class Database;

std::string activity_absolute_link(Database &db, const std::string &activity_id);
std::string format_date_ddmmyyyy(const std::string &iso);
std::string join_display(const std::vector<std::string> &items);
std::vector<std::string> unique_names_from_material(const std::vector<MaterialItem> &material);
bool user_in_assignee_list(const std::vector<std::string> &assignees, const UserRecord &user);
bool contains_name_ci(const std::vector<std::string> &names, const std::string &needle);
std::string normalize_assignee_key(const std::string &name);
bool user_matches_assignee(const UserRecord &user, const std::string &assignee);
std::vector<std::string> newly_assigned_users_from_material_delta(const std::vector<MaterialItem> &old_material,
                                                                   const std::vector<MaterialItem> &new_material);

nlohmann::json notification_to_json(const NotificationRecord &n);
