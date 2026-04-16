#pragma once
#include <vector>
#include <optional>
#include <string>
#include <libpq-fe.h>
#include "models.hpp"

struct UserRecord
{
    std::string id;
    std::string microsoft_oid;
    std::string email;
    std::string display_name;
    std::optional<std::string> department;
    std::string role;
    std::string created_at;
    std::string updated_at;
};

struct MailTemplate
{
    std::string id;
    std::string department;
    std::string subject;
    std::string body;
    std::vector<std::string> recipients;
    std::string created_at;
    std::string updated_at;
};

struct SentMail
{
    std::string id;
    std::string activity_id;
    std::string sender_id;
    std::string sender_email;
    std::vector<std::string> to_emails;
    std::string subject;
    std::string body_html;
    std::string sent_at;
};

struct DepartmentRecord
{
    std::string name;
    std::string color;
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
    std::string activity_read_scope;   // none|own|same_dept|all
    std::string activity_create_scope; // none|own_dept|all
    std::string activity_edit_scope;   // none|own|same_dept|all
    std::string mail_send_scope;       // none|own|same_dept|all
    std::string mail_templates_scope;  // none|own_dept|all
    std::string user_dept_scope;       // none|own|own_dept|all
    std::string user_role_scope;       // none|own|own_dept|all
};

struct RoleDeptAccess
{
    std::string role;
    std::string department;
    bool can_read;
    bool can_write;
};

class Database
{
public:
    explicit Database(const std::string &conn_str);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Activities
    std::vector<Activity> list_activities();
    std::optional<Activity> get_activity_by_id(const std::string &id);
    std::optional<Activity> create_activity(const ActivityInput &input);
    std::optional<Activity> update_activity(const std::string &id, const ActivityInput &input);
    bool delete_activity(const std::string &id);

    // Predefined locations
    std::vector<std::string> get_predefined_locations();

    // Attachments
    std::optional<Attachment> add_attachment(const std::string &activity_id,
                                             const std::string &filename,
                                             const std::string &content_type,
                                             const std::string &data_base64);
    std::vector<Attachment> list_attachments(const std::string &activity_id);
    std::optional<AttachmentData> get_attachment_data(const std::string &id);
    bool delete_attachment(const std::string &id);

    // Users
    std::vector<UserRecord> list_users();
    // initial_role / initial_dept only used when inserting a new user.
    // force_role=true also updates role on conflict (used for admin-email detection).
    std::optional<UserRecord> upsert_user(const std::string &oid,
                                          const std::string &email,
                                          const std::string &display_name,
                                          const std::string &initial_role = "Mitglied",
                                          const std::string &initial_dept = "Allgemein",
                                          bool force_role = false);
    std::optional<UserRecord> get_user_by_oid(const std::string &oid);
    std::optional<UserRecord> get_user_by_id(const std::string &id);
    // Update own profile (display_name only; department blocked by role on handler level).
    std::optional<UserRecord> update_user(const std::string &oid,
                                          const std::string &display_name,
                                          const std::optional<std::string> &department);
    // Admin: update any user's display_name, department and role.
    std::optional<UserRecord> update_user_admin(const std::string &id,
                                                const std::string &display_name,
                                                const std::optional<std::string> &department,
                                                const std::string &role);
    // Admin: delete a user by id.
    bool delete_user(const std::string &id);

    // Mail templates
    std::vector<MailTemplate> list_mail_templates();
    std::optional<MailTemplate> get_mail_template_by_department(const std::string &department);
    std::optional<MailTemplate> upsert_mail_template(const std::string &department,
                                                     const std::string &subject,
                                                     const std::string &body,
                                                     const std::vector<std::string> &recipients);

    // Send mail via Microsoft Graph
    bool send_mail(const std::string &access_token,
                   const std::string &from_email,
                   const std::vector<std::string> &to_emails,
                   const std::string &subject,
                   const std::string &body_html);

    // Sent mails log
    std::optional<SentMail> log_sent_mail(const std::string &activity_id,
                                          const std::string &sender_id,
                                          const std::string &sender_email,
                                          const std::vector<std::string> &to_emails,
                                          const std::string &subject,
                                          const std::string &body_html);
    std::vector<SentMail> list_sent_mails(const std::string &activity_id);

    // Departments CRUD
    std::vector<DepartmentRecord> list_departments();
    std::optional<DepartmentRecord> create_department(const std::string &name, const std::string &color);
    std::optional<DepartmentRecord> update_department(const std::string &name, const std::string &new_name,
                                                      const std::string &color);
    bool delete_department(const std::string &name);
    bool delete_department_with_transfers(const std::string &name,
                                          const std::string &transfer_activities_to,
                                          bool delete_activities,
                                          const std::string &transfer_users_to,
                                          bool delete_users);

    // Roles CRUD
    std::vector<RoleRecord> list_roles();
    std::optional<RoleRecord> create_role(const std::string &name, const std::string &color);
    std::optional<RoleRecord> update_role(const std::string &name, const std::string &new_name,
                                          const std::string &color);
    bool move_role(const std::string &name, bool move_up);
    bool reorder_roles(const std::vector<std::string> &ordered_names);
    bool delete_role(const std::string &name,
                     const std::string &transfer_users_to = "",
                     bool delete_users = false);

    // Role permissions
    std::vector<RolePermission> list_role_permissions();
    std::optional<RolePermission> get_role_permission(const std::string &role);
    bool update_role_permission(const std::string &role,
                                bool can_read_own_dept,
                                bool can_write_own_dept,
                                bool can_read_all_depts,
                                bool can_write_all_depts,
                                const std::string &activity_read_scope,
                                const std::string &activity_create_scope,
                                const std::string &activity_edit_scope,
                                const std::string &mail_send_scope,
                                const std::string &mail_templates_scope,
                                const std::string &user_dept_scope,
                                const std::string &user_role_scope);

    // Role department access
    std::vector<RoleDeptAccess> list_role_dept_access(const std::string &role);
    bool set_role_dept_access(const std::string &role, const std::string &department,
                              bool can_read, bool can_write);

    // ── Forms ────────────────────────────────────────────────────────────────

    // Form CRUD (one form per activity)
    std::optional<SignupForm> get_form_for_activity(const std::string &activity_id);
    std::optional<SignupForm> create_form(const std::string &activity_id,
                                         const std::string &form_type,
                                         const std::string &title,
                                         const std::string &created_by,
                                         const std::vector<FormQuestion> &questions);
    std::optional<SignupForm> update_form(const std::string &activity_id,
                                         const std::string &form_type,
                                         const std::string &title,
                                         const std::vector<FormQuestion> &questions);
    bool delete_form(const std::string &activity_id);

    // Public form access (no auth)
    std::optional<SignupForm> get_form_by_id(const std::string &form_id);

    // Responses
    std::optional<FormResponse> submit_response(const std::string &form_id,
                                                const std::string &submission_mode,
                                                const std::string &user_agent,
                                                const std::string &ip_address,
                                                const std::vector<std::pair<std::string, std::string>> &answers);
    std::vector<FormResponse> list_responses(const std::string &form_id);
    std::optional<FormResponse> get_response(const std::string &response_id);
    bool delete_response(const std::string &response_id);

    // Stats
    nlohmann::json get_form_stats(const std::string &form_id);

    // Templates (one per department)
    std::optional<FormTemplate> get_form_template(const std::string &department);
    std::optional<FormTemplate> save_form_template(const std::string &department,
                                                    const std::string &name,
                                                    const std::string &form_type,
                                                    const nlohmann::json &template_config,
                                                    const std::string &created_by);
    bool delete_form_template(const std::string &department);

private:
    PGconn *conn_{nullptr};
    void ensure_connected();

    Activity row_to_activity(PGresult *res, int row);
    Program row_to_program(PGresult *res, int row);
    Attachment row_to_attachment(PGresult *res, int row);
    UserRecord row_to_user(PGresult *res, int row);
    MailTemplate row_to_mail_template(PGresult *res, int row);
    SentMail row_to_sent_mail(PGresult *res, int row);
    DepartmentRecord row_to_department(PGresult *res, int row);
    RoleRecord row_to_role(PGresult *res, int row);
    RolePermission row_to_role_perm(PGresult *res, int row);
    RoleDeptAccess row_to_role_dept_access(PGresult *res, int row);
    void attach_programs(std::vector<Activity> &activities);
    void attach_programs_single(Activity &a);

    static std::vector<std::string> parse_pg_array(const char *raw);
    static std::string format_material_param(const std::vector<std::string> &material);
    static std::string format_material_items_param(const std::vector<MaterialItem> &items);

    FormQuestion row_to_form_question(PGresult *res, int row);
    SignupForm row_to_signup_form(PGresult *res, int row);
    FormResponse row_to_form_response(PGresult *res, int row);
    FormTemplate row_to_form_template(PGresult *res, int row);
    void attach_questions(std::vector<SignupForm> &forms);
    void attach_questions_single(SignupForm &f);
};
