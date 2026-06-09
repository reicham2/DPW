#pragma once
#include <string>
#include <vector>
#include "core/models/activity.hpp"
#include "core/models/user.hpp"
#include "core/models/admin.hpp"

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
