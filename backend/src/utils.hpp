#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include "models.hpp"
#include "db.hpp"

// ── URL helpers ─────────────────────────────────────────────────────────────

std::string url_decode(const std::string &src);

// ── HTTP status text ────────────────────────────────────────────────────────

const char *status_text(int code);

// ── Date formatting (German) ────────────────────────────────────────────────

std::string format_date_long_de(const std::string &iso_date);
std::string format_date_short_de(const std::string &iso_date);

// ── Template variable substitution ──────────────────────────────────────────

std::string replace_template_vars(const std::string &text, const Activity &act);

// ── Permission helpers ──────────────────────────────────────────────────────

bool is_admin(const UserRecord &user);
bool has_dept_access(const std::vector<RoleDeptAccess> &dept_access,
                     const std::string &dept, bool write);
bool can_create_dept(const RolePermission &perm, const UserRecord &user,
                     const std::vector<RoleDeptAccess> &dept_access,
                     const std::string &dept);
bool can_read_dept(const RolePermission &perm, const UserRecord &user,
                   const std::vector<RoleDeptAccess> &dept_access,
                   const std::string &dept);
bool is_activity_responsible(const Activity &activity, const UserRecord &user,
                             const std::string &email);
bool can_read_activity(const RolePermission &perm, const UserRecord &user,
                       const std::vector<RoleDeptAccess> &dept_access,
                       const Activity &activity);
bool can_edit_activity(const RolePermission &perm, const UserRecord &user,
                       const Activity &activity, const std::string &email);
bool can_publish_event(const RolePermission &perm, const UserRecord &user,
                       const Activity &activity, const std::string &email);

// ── JSON parsing helpers ────────────────────────────────────────────────────

std::string str_field(const nlohmann::json &j, const char *key,
                      const std::string &def = "");
ActivityInput parse_activity_input(const nlohmann::json &j);
