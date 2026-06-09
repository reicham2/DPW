#pragma once
#include <string>
#include <vector>
#include <optional>

struct DepartmentRecord
{
    std::string name;
    std::string color;
    std::optional<std::string> midata_group_id;
    std::vector<std::string> midata_child_roles;
};

struct RoleRecord
{
    std::string name;
    std::string color;
    int sort_order;
};

struct RolePermission
{
    std::string role;
    bool can_read_own_dept;
    bool can_write_own_dept;
    bool can_read_all_depts;
    bool can_write_all_depts;
    std::string activity_read_scope;
    std::string activity_create_scope;
    std::string activity_edit_scope;
    std::string mail_send_scope;
    std::string mail_templates_scope;
    std::string form_scope;
    std::string form_templates_scope;
    std::string event_templates_scope;
    std::string event_publish_scope;
    std::string user_dept_scope;
    std::string user_role_scope;
    std::string locations_manage_scope;
    std::string materials_manage_scope;
    std::string ideenkiste_scope;
    std::string ideenkiste_add_scope;
    std::string ideenkiste_delete_scope;
};

struct RoleDeptAccess
{
    std::string role;
    std::string department;
    bool can_read;
    bool can_write;
};
