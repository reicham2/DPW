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
    std::string role; // 'admin' | 'Stufenleiter' | 'Leiter' | 'Pio'
    std::string created_at;
    std::string updated_at;
};

struct MailTemplate
{
    std::string id;
    std::string department;
    std::string subject;
    std::string body;
    std::string created_at;
    std::string updated_at;
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

    // SiKo raw bytes (binary download)
    std::optional<std::vector<uint8_t>> get_siko(const std::string &activity_id);

    // Users
    std::vector<UserRecord> list_users();
    // initial_role / initial_dept only used when inserting a new user.
    // force_role=true also updates role on conflict (used for admin-email detection).
    std::optional<UserRecord> upsert_user(const std::string &oid,
                                          const std::string &email,
                                          const std::string &display_name,
                                          const std::string &initial_role = "Leiter",
                                          const std::string &initial_dept = "Leiter",
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

    // Mail templates
    std::vector<MailTemplate> list_mail_templates();
    std::optional<MailTemplate> get_mail_template_by_department(const std::string &department);
    std::optional<MailTemplate> upsert_mail_template(const std::string &department,
                                                     const std::string &subject,
                                                     const std::string &body);

    // Send mail via Microsoft Graph
    bool send_mail(const std::string &access_token,
                   const std::string &from_email,
                   const std::vector<std::string> &to_emails,
                   const std::string &subject,
                   const std::string &body_html);

private:
    PGconn *conn_{nullptr};
    void ensure_connected();

    Activity row_to_activity(PGresult *res, int row);
    Program row_to_program(PGresult *res, int row);
    UserRecord row_to_user(PGresult *res, int row);
    MailTemplate row_to_mail_template(PGresult *res, int row);
    void attach_programs(std::vector<Activity> &activities);
    void attach_programs_single(Activity &a);

    static std::vector<std::string> parse_pg_array(const char *raw);
    static std::string format_material_param(const std::vector<std::string> &material);
    static std::string format_material_items_param(const std::vector<MaterialItem> &items);
};
