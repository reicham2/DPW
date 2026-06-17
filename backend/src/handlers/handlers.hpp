#pragma once
#include "core/http.hpp"
#include "db/database.hpp"
#include "infra/ws_manager.hpp"

void handle_get_departments(HttpRes *res, HttpReq *req, Database &db);
void handle_get_setup_auth_config(HttpRes *res, HttpReq *req, Database &db);
void handle_post_setup_auth_config(HttpRes *res, HttpReq *req, Database &db);
void handle_get_maintenance(HttpRes *res, HttpReq *req, Database &db);
void handle_get_admin_maintenance(HttpRes *res, HttpReq *req, Database &db);
void handle_put_admin_maintenance(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_get_admin_midata_status(HttpRes *res, HttpReq *req, Database &db);
void handle_get_admin_app_settings(HttpRes *res, HttpReq *req, Database &db);
void handle_get_admin_container_logs(HttpRes *res, HttpReq *req, Database &db);
void handle_put_admin_app_setting(HttpRes *res, HttpReq *req, Database &db);
void handle_post_admin_reset_azure_auth(HttpRes *res, HttpReq *req, Database &db);
void handle_post_department(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_department(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_department(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_get_activities(HttpRes *res, HttpReq *req, Database &db);
void handle_get_activity(HttpRes *res, HttpReq *req, Database &db);
void handle_get_activity_midata_children_count(HttpRes *res, HttpReq *req, Database &db);
void handle_get_midata_children_counts(HttpRes *res, HttpReq *req, Database &db);
void handle_get_activity_weather_location(HttpRes *res, HttpReq *req, Database &db);
void handle_put_activity_weather_location(HttpRes *res, HttpReq *req, Database &db);
void handle_get_activity_expected_weather(HttpRes *res, HttpReq *req, Database &db);
void handle_post_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_get_admin_activity_trash(HttpRes *res, HttpReq *req, Database &db);
void handle_post_admin_activity_restore(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_admin_activity_trash(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_locations(HttpRes *res, HttpReq *req, Database &db);
void handle_get_locations_admin(HttpRes *res, HttpReq *req, Database &db);
void handle_post_location(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_location(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_location(HttpRes *res, HttpReq *req, Database &db);

void handle_get_materials_predefined(HttpRes *res, HttpReq *req, Database &db);
void handle_get_materials_admin(HttpRes *res, HttpReq *req, Database &db);
void handle_post_material(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_material(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_material(HttpRes *res, HttpReq *req, Database &db);

void handle_get_attachments(HttpRes *res, HttpReq *req, Database &db);
void handle_post_attachment(HttpRes *res, HttpReq *req, Database &db);
void handle_get_attachment_download(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_attachment(HttpRes *res, HttpReq *req, Database &db);

void handle_post_auth_me(HttpRes *res, HttpReq *req, Database &db);
void handle_get_me(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_me(HttpRes *res, HttpReq *req, Database &db);
void handle_get_users(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_admin_user(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_admin_user(HttpRes *res, HttpReq *req, Database &db);
void handle_debug_get_users(HttpRes *res, HttpReq *req, Database &db);

void handle_get_mail_templates(HttpRes *res, HttpReq *req, Database &db);
void handle_get_mail_template(HttpRes *res, HttpReq *req, Database &db);
void handle_put_mail_template(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_post_send_mail(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_get_sent_mails(HttpRes *res, HttpReq *req, Database &db);
void handle_get_mail_composer_context(HttpRes *res, HttpReq *req, Database &db);

void handle_get_notifications(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_notification_read(HttpRes *res, HttpReq *req, Database &db);
void handle_post_notifications_read_all(HttpRes *res, HttpReq *req, Database &db);
void handle_get_push_vapid_public_key(HttpRes *res, HttpReq *req, Database &db);
void handle_post_push_subscription(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_push_subscription(HttpRes *res, HttpReq *req, Database &db);
void handle_post_push_payload(HttpRes *res, HttpReq *req, Database &db);

void handle_get_event_templates(HttpRes *res, HttpReq *req, Database &db);
void handle_get_event_template(HttpRes *res, HttpReq *req, Database &db);
void handle_put_event_template(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_event_publication(HttpRes *res, HttpReq *req, Database &db);
void handle_put_event_publication(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_event_publication(HttpRes *res, HttpReq *req, Database &db);

void handle_get_mail_draft(HttpRes *res, HttpReq *req, Database &db);
void handle_put_mail_draft(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_mail_draft(HttpRes *res, HttpReq *req, Database &db);
void handle_get_form_draft(HttpRes *res, HttpReq *req, Database &db);
void handle_put_form_draft(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_form_draft(HttpRes *res, HttpReq *req, Database &db);

void handle_post_bug_report(HttpRes *res, HttpReq *req, Database &db);
void handle_get_my_permissions(HttpRes *res, HttpReq *req, Database &db);

void handle_get_activity_form(HttpRes *res, HttpReq *req, Database &db);
void handle_post_activity_form(HttpRes *res, HttpReq *req, Database &db);
void handle_put_activity_form(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_activity_form(HttpRes *res, HttpReq *req, Database &db);

void handle_get_form_responses(HttpRes *res, HttpReq *req, Database &db);
void handle_get_form_response(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_form_response(HttpRes *res, HttpReq *req, Database &db);
void handle_get_form_stats(HttpRes *res, HttpReq *req, Database &db);

void handle_get_public_form(HttpRes *res, HttpReq *req, Database &db);
void handle_post_form_submit(HttpRes *res, HttpReq *req, Database &db);

void handle_get_form_templates(HttpRes *res, HttpReq *req, Database &db);
void handle_post_form_template(HttpRes *res, HttpReq *req, Database &db);
void handle_put_form_template(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_form_template(HttpRes *res, HttpReq *req, Database &db);

void handle_get_roles(HttpRes *res, HttpReq *req, Database &db);
void handle_post_role(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_role(HttpRes *res, HttpReq *req, Database &db);
void handle_post_role_move(HttpRes *res, HttpReq *req, Database &db);
void handle_post_roles_reorder(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_role(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_get_role_permissions(HttpRes *res, HttpReq *req, Database &db);
void handle_put_role_permission(HttpRes *res, HttpReq *req, Database &db);
void handle_get_role_dept_access(HttpRes *res, HttpReq *req, Database &db);
void handle_put_role_dept_access(HttpRes *res, HttpReq *req, Database &db);

void handle_post_share_link(HttpRes *res, HttpReq *req, Database &db);
void handle_get_share_link(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_share_link(HttpRes *res, HttpReq *req, Database &db);
void handle_get_shared_activity(HttpRes *res, HttpReq *req, Database &db);

void handle_get_ideenkiste(HttpRes *res, HttpReq *req, Database &db);
void handle_post_ideenkiste(HttpRes *res, HttpReq *req, Database &db);
void handle_put_ideenkiste(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_ideenkiste(HttpRes *res, HttpReq *req, Database &db);

// ── Camp Planning ───────────────────────────────────────────────────────────
void handle_get_camps(HttpRes *res, HttpReq *req, Database &db);
void handle_get_camp(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_camp(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_camp_collaborations(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp_collaboration(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_camp_collaboration(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_collaboration(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_camp_categories(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp_category(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_camp_category(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_category(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_camp_periods(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp_period(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_camp_period(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_period(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_camp_day_responsibles(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp_day_responsible(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_day_responsible(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_camp_activities(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_camp_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_schedule_entry(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

void handle_get_camp_material_lists(HttpRes *res, HttpReq *req, Database &db);
void handle_post_camp_material_list(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_material_list(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_post_camp_material_item(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_camp_material_item(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_camp_material_item(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
