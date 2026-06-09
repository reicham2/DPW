#include "utils/permissions.hpp"
#include "utils/strings.hpp"

// ── Permission helpers ──────────────────────────────────────────────────────

bool is_admin(const UserRecord &user) { return user.role == "admin"; }

bool has_dept_access(const std::vector<RoleDeptAccess> &dept_access,
                     const std::string &dept, bool write)
{
    for (const auto &da : dept_access)
    {
        if (da.department == dept && (write ? da.can_write : da.can_read))
            return true;
    }
    return false;
}

bool can_create_dept(const RolePermission &perm, const UserRecord &user,
                     const std::vector<RoleDeptAccess> &dept_access,
                     const std::string &dept)
{
    if (is_admin(user))
        return true;
    if (perm.activity_create_scope == "all")
        return true;
    if (perm.activity_create_scope == "own_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_access(dept_access, dept, true);
}

bool can_read_dept(const RolePermission &perm, const UserRecord &user,
                   const std::vector<RoleDeptAccess> &dept_access,
                   const std::string &dept)
{
    if (is_admin(user))
        return true;
    if (perm.activity_read_scope == "all")
        return true;
    if (perm.activity_read_scope == "same_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_access(dept_access, dept, false);
}

bool is_activity_responsible(const Activity &activity, const UserRecord &user,
                             const std::string &email)
{
    for (const auto &responsible : activity.responsible)
    {
        const std::string trimmed = trim_ascii(responsible);
        if (trimmed.empty())
            continue;
        if (trimmed == user.id)
            return true;
    }

    auto normalize = [](const std::string &value)
    {
        return to_lower_ascii(trim_ascii(value));
    };

    const std::string user_name = normalize(user.display_name);
    const std::string user_email = normalize(email);
    std::string user_email_local;
    auto at = user_email.find('@');
    if (at != std::string::npos)
        user_email_local = user_email.substr(0, at);

    for (const auto &responsible : activity.responsible)
    {
        const std::string candidate = normalize(responsible);
        if (candidate.empty())
            continue;
        if (candidate == user_name || candidate == user_email || (!user_email_local.empty() && candidate == user_email_local))
            return true;
    }
    return false;
}

bool can_read_activity(const RolePermission &perm, const UserRecord &user,
                       const std::vector<RoleDeptAccess> &dept_access,
                       const Activity &activity)
{
    if (is_admin(user))
        return true;
    if (perm.activity_read_scope == "all")
        return true;
    if (!activity.department)
        return perm.activity_read_scope != "none";
    const std::string &dept = *activity.department;
    if (perm.activity_read_scope == "same_dept" && user.department && *user.department == dept)
        return true;
    return has_dept_access(dept_access, dept, false);
}

bool can_edit_activity(const RolePermission &perm, const UserRecord &user,
                       const Activity &activity, const std::string &email)
{
    if (is_admin(user))
        return true;
    if (perm.activity_edit_scope == "all")
        return true;
    if (perm.activity_edit_scope == "same_dept")
        return (activity.department && user.department && *activity.department == *user.department) ||
               is_activity_responsible(activity, user, email);
    if (perm.activity_edit_scope == "own")
        return is_activity_responsible(activity, user, email);
    return false;
}

bool can_publish_event(const RolePermission &perm, const UserRecord &user,
                       const Activity &activity, const std::string &email)
{
    if (is_admin(user))
        return true;
    if (perm.event_publish_scope == "all")
        return true;
    if (perm.event_publish_scope == "own_dept")
        return (activity.department && user.department && *activity.department == *user.department) ||
               is_activity_responsible(activity, user, email);
    if (perm.event_publish_scope == "own")
        return is_activity_responsible(activity, user, email);
    return false;
}
