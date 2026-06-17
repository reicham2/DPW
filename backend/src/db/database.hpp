#pragma once
#include <vector>
#include <optional>
#include <string>
#include <libpq-fe.h>
#include "core/models/all_models.hpp"
#include "core/models/user.hpp"
#include "core/models/notification.hpp"
#include "core/models/mail.hpp"
#include "core/models/admin.hpp"
#include "core/models/share.hpp"

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
    bool soft_delete_activity(const std::string &id, const std::string &deleted_by_user_id);
    bool restore_activity(const std::string &id);
    bool permanently_delete_activity(const std::string &id);
    int purge_deleted_activities_older_than_days(int days);
    std::vector<DeletedActivityRecord> list_deleted_activities();
    std::optional<int> get_activity_midata_children_value(const std::string &activity_id);
    bool set_activity_midata_children_value(const std::string &activity_id, int value);
    std::optional<nlohmann::json> get_activity_weather_snapshot(const std::string &activity_id);
    bool set_activity_weather_snapshot(const std::string &activity_id, const nlohmann::json &snapshot);
    std::optional<std::string> get_activity_weather_location(const std::string &activity_id);
    bool set_activity_weather_location(const std::string &activity_id, const std::string &location);

    // Predefined locations
    std::vector<std::string> get_predefined_locations();
    std::vector<LocationRecord> list_predefined_locations();
    std::optional<LocationRecord> create_predefined_location(const std::string &name);
    std::optional<LocationRecord> update_predefined_location(const std::string &id, const std::string &name);
    bool delete_predefined_location(const std::string &id);

    std::vector<std::string> get_predefined_materials();
    std::vector<MaterialNameRecord> list_predefined_materials();
    std::optional<MaterialNameRecord> create_predefined_material(const std::string &name);
    std::optional<MaterialNameRecord> update_predefined_material(const std::string &id, const std::string &name);
    bool delete_predefined_material(const std::string &id);

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
                                          const std::optional<std::string> &department,
                                          const std::optional<std::string> &time_display_mode = std::nullopt,
                                          const std::optional<bool> &notify_material_assigned = std::nullopt,
                                          const std::optional<bool> &notify_activity_assigned = std::nullopt,
                                          const std::optional<bool> &notify_program_assigned = std::nullopt,
                                          const std::optional<bool> &notify_mail_own_activity = std::nullopt,
                                          const std::optional<bool> &notify_mail_department = std::nullopt,
                                          const std::optional<bool> &notify_channel_websocket = std::nullopt,
                                          const std::optional<bool> &notify_channel_email = std::nullopt,
                                          const std::optional<std::string> &avatar_color = std::nullopt);
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
                                                     const std::vector<std::string> &recipients,
                                                     const std::vector<std::string> &cc);

    // Event templates
    std::vector<EventTemplate> list_event_templates();
    std::optional<EventTemplate> get_event_template_by_department(const std::string &department);
    std::optional<EventTemplate> upsert_event_template(const std::string &department,
                                                       const std::string &title,
                                                       const std::string &body);

    // Event publications
    std::optional<EventPublication> get_event_publication(const std::string &activity_id);
    std::optional<EventPublication> upsert_event_publication(const std::string &activity_id,
                                                             const std::string &published_by,
                                                             const std::string &title,
                                                             const std::string &body_html);
    bool update_event_publication_wp_id(const std::string &activity_id, const std::string &wp_event_id);
    bool delete_event_publication(const std::string &activity_id);

    // Send mail via Microsoft Graph
    bool send_mail(const std::string &access_token,
                   const std::string &from_email,
                   const std::vector<std::string> &to_emails,
                   const std::vector<std::string> &cc_emails,
                   const std::string &subject,
                   const std::string &body_html);

    // Sent mails log
    std::optional<SentMail> log_sent_mail(const std::string &activity_id,
                                          const std::string &sender_id,
                                          const std::string &sender_email,
                                          const std::vector<std::string> &to_emails,
                                          const std::vector<std::string> &cc_emails,
                                          const std::string &subject,
                                          const std::string &body_html);
    std::vector<SentMail> list_sent_mails(const std::string &activity_id);

    // Notifications
    std::optional<NotificationRecord> create_notification(const std::string &user_id,
                                                          const std::string &category,
                                                          const std::string &title,
                                                          const std::string &message,
                                                          const std::optional<std::string> &link,
                                                          const nlohmann::json &payload = nlohmann::json::object());
    std::vector<NotificationRecord> list_notifications_for_user(const std::string &user_id, int limit = 50);
    bool mark_notification_read(const std::string &user_id, const std::string &notification_id);
    bool mark_all_notifications_read(const std::string &user_id);

    // Web push subscriptions
    std::optional<PushSubscriptionRecord> upsert_push_subscription(const std::string &user_id,
                                                                   const std::string &endpoint,
                                                                   const std::string &p256dh,
                                                                   const std::string &auth);
    bool delete_push_subscription(const std::string &user_id, const std::string &endpoint);
    bool delete_push_subscription_by_endpoint(const std::string &endpoint);
    std::vector<PushSubscriptionRecord> list_push_subscriptions_for_user(const std::string &user_id);
    std::optional<NotificationRecord> get_latest_unread_notification_for_push(const std::string &endpoint,
                                                                              const std::string &auth);

    // Mail drafts
    std::optional<MailDraft> get_mail_draft(const std::string &activity_id);
    std::optional<MailDraft> upsert_mail_draft(const std::string &activity_id,
                                               const std::vector<std::string> &recipients,
                                               const std::vector<std::string> &cc,
                                               const std::string &subject,
                                               const std::string &body_html,
                                               const std::string &updated_by);
    bool delete_mail_draft(const std::string &activity_id);
    // Form drafts
    std::optional<FormDraft> get_form_draft(const std::string &activity_id);
    std::optional<FormDraft> upsert_form_draft(const std::string &activity_id,
                                               const std::string &form_type,
                                               const std::string &title,
                                               const std::string &questions_json,
                                               const std::string &updated_by);
    bool delete_form_draft(const std::string &activity_id);

    // Departments CRUD
    std::vector<DepartmentRecord> list_departments();
    std::optional<DepartmentRecord> create_department(const std::string &name,
                                                      const std::string &color,
                                                      const std::optional<std::string> &midata_group_id = std::nullopt,
                                                      const std::vector<std::string> &midata_child_roles = {});
    std::optional<DepartmentRecord> update_department(const std::string &name, const std::string &new_name,
                                                      const std::string &color,
                                                      const std::optional<std::string> &midata_group_id = std::nullopt,
                                                      const std::vector<std::string> &midata_child_roles = {});
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
                                const std::string &form_scope,
                                const std::string &form_templates_scope,
                                const std::string &event_templates_scope,
                                const std::string &event_publish_scope,
                                const std::string &user_dept_scope,
                                const std::string &user_role_scope,
                                const std::string &locations_manage_scope = "none",
                                const std::string &materials_manage_scope = "none",
                                const std::string &ideenkiste_scope = "none",
                                const std::string &ideenkiste_add_scope = "none",
                                const std::string &ideenkiste_delete_scope = "none");

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
    std::optional<SignupForm> get_form_for_public_slug(const std::string &public_slug);

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

    // Templates
    std::vector<FormTemplate> list_form_templates(const std::string &department);
    std::optional<FormTemplate> create_form_template(const std::string &name,
                                                     const std::string &department,
                                                     const std::string &form_type,
                                                     const nlohmann::json &template_config,
                                                     const std::string &created_by,
                                                     bool is_default = false);
    std::optional<FormTemplate> update_form_template(const std::string &id,
                                                     const std::string &name,
                                                     const std::string &form_type,
                                                     const nlohmann::json &template_config,
                                                     bool is_default = false);
    bool delete_form_template(const std::string &id);

    // Ideenkiste CRUD
    std::vector<IdeenkisteItem> list_ideenkiste(const std::string &dept_filter = "");
    std::optional<IdeenkisteItem> create_ideenkiste_item(const IdeenkisteInput &input);
    std::optional<IdeenkisteItem> update_ideenkiste_item(const std::string &id, const IdeenkisteInput &input);
    bool delete_ideenkiste_item(const std::string &id);

    // Activity share links
    std::optional<ActivityShareLink> create_share_link(const std::string &activity_id,
                                                       const std::string &created_by);
    std::optional<ActivityShareLink> get_share_link(const std::string &activity_id);
    bool delete_share_link(const std::string &activity_id);
    std::optional<Activity> get_activity_by_share_token(const std::string &token);

    // ── Camp Planning ──────────────────────────────────────────────────────────
    std::vector<Camp> list_camps();
    std::optional<Camp> get_camp_by_id(const std::string &id);
    std::optional<Camp> create_camp(const CampInput &input, const std::string &created_by);
    std::optional<Camp> update_camp(const std::string &id, const CampInput &input);
    bool soft_delete_camp(const std::string &id, const std::string &deleted_by_user_id);

    std::vector<CampCollaboration> list_collaborations(const std::string &camp_id);
    std::optional<CampCollaboration> create_collaboration(const std::string &camp_id, const CampCollaborationInput &input);
    std::optional<CampCollaboration> update_collaboration(const std::string &id, const CampCollaborationInput &input);
    bool delete_collaboration(const std::string &id);

    std::vector<CampCategory> list_categories(const std::string &camp_id);
    std::optional<CampCategory> create_category(const std::string &camp_id, const CampCategoryInput &input);
    std::optional<CampCategory> update_category(const std::string &id, const CampCategoryInput &input);
    bool delete_category(const std::string &id);

    std::vector<CampPeriod> list_periods(const std::string &camp_id);
    std::optional<CampPeriod> create_period(const std::string &camp_id, const CampPeriodInput &input);
    std::optional<CampPeriod> update_period(const std::string &id, const CampPeriodInput &input);
    bool delete_period(const std::string &id);

    std::vector<CampDayResponsible> list_day_responsibles(const std::string &camp_id);
    std::optional<CampDayResponsible> add_day_responsible(const std::string &period_id, int day_offset,
                                                          const std::string &responsible);
    bool delete_day_responsible(const std::string &id);

    std::vector<CampActivity> list_camp_activities(const std::string &camp_id);
    std::optional<CampActivity> get_camp_activity_by_id(const std::string &id);
    std::optional<CampActivity> create_camp_activity(const std::string &camp_id, const CampActivityInput &input);
    std::optional<CampActivity> update_camp_activity(const std::string &id, const CampActivityInput &input);
    bool delete_camp_activity(const std::string &id);
    // Lightweight reposition of a single schedule entry (drag/drop, resize).
    std::optional<ScheduleEntry> update_schedule_entry(const std::string &id, const ScheduleEntryInput &input);

    std::vector<CampMaterialList> list_material_lists(const std::string &camp_id);
    std::optional<CampMaterialList> create_material_list(const std::string &camp_id,
                                                         const std::optional<std::string> &collaboration_id,
                                                         const std::string &name);
    bool delete_material_list(const std::string &id);
    std::optional<CampMaterialItem> create_material_item(const std::string &material_list_id, const CampMaterialItemInput &input);
    std::optional<CampMaterialItem> update_material_item(const std::string &id, const CampMaterialItemInput &input);
    bool delete_material_item(const std::string &id);

    // App settings (supports encrypted secret values)
    bool set_app_setting(const std::string &key,
                         bool is_secret,
                         const std::string &value,
                         const std::string &encryption_key = "");
    bool clear_app_setting(const std::string &key, bool is_secret);
    bool has_app_setting(const std::string &key, bool is_secret);
    std::optional<std::string> get_app_setting(const std::string &key,
                                               bool is_secret,
                                               const std::string &encryption_key = "");

private:
    PGconn *conn_{nullptr};
    void ensure_connected();
    void run_schema_sync();

    Activity row_to_activity(PGresult *res, int row);
    Program row_to_program(PGresult *res, int row);
    Attachment row_to_attachment(PGresult *res, int row);
    UserRecord row_to_user(PGresult *res, int row);
    MailTemplate row_to_mail_template(PGresult *res, int row);
    EventTemplate row_to_event_template(PGresult *res, int row);
    EventPublication row_to_event_publication(PGresult *res, int row);
    SentMail row_to_sent_mail(PGresult *res, int row);
    NotificationRecord row_to_notification(PGresult *res, int row);
    PushSubscriptionRecord row_to_push_subscription(PGresult *res, int row);
    void purge_expired_activity_notifications();
    MailDraft row_to_mail_draft(PGresult *res, int row);
    FormDraft row_to_form_draft(PGresult *res, int row);
    DepartmentRecord row_to_department(PGresult *res, int row);
    RoleRecord row_to_role(PGresult *res, int row);
    RolePermission row_to_role_perm(PGresult *res, int row);
    RoleDeptAccess row_to_role_dept_access(PGresult *res, int row);
    LocationRecord row_to_location(PGresult *res, int row);
    MaterialNameRecord row_to_material_name(PGresult *res, int row);
    void attach_programs(std::vector<Activity> &activities);
    void attach_programs_single(Activity &a);

    static std::vector<std::string> parse_pg_array(const char *raw);
    static std::string format_material_param(const std::vector<std::string> &material);
    static std::string format_material_items_param(const std::vector<MaterialItem> &items);

    IdeenkisteItem row_to_ideenkiste(PGresult *res, int row);
    FormQuestion row_to_form_question(PGresult *res, int row);
    SignupForm row_to_signup_form(PGresult *res, int row);
    FormResponse row_to_form_response(PGresult *res, int row);
    FormTemplate row_to_form_template(PGresult *res, int row);
    void attach_questions(std::vector<SignupForm> &forms);
    void attach_questions_single(SignupForm &f);

    // Camp planning row mappers
    Camp row_to_camp(PGresult *res, int row);
    CampCollaboration row_to_collaboration(PGresult *res, int row);
    CampCategory row_to_category(PGresult *res, int row);
    CampPeriod row_to_period(PGresult *res, int row);
    ScheduleEntry row_to_schedule_entry(PGresult *res, int row);
    ContentNode row_to_content_node(PGresult *res, int row);
    CampMaterialItem row_to_material_item(PGresult *res, int row);
    CampActivity row_to_camp_activity(PGresult *res, int row);
    void hydrate_camp_activity(CampActivity &a);
};
