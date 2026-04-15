#pragma once
#include "App.h"
#include "auth.hpp"
#include "db.hpp"
#include "ws_manager.hpp"

using HttpRes = uWS::HttpResponse<false>;
using HttpReq = uWS::HttpRequest;

void send_json(HttpRes *res, int status, const std::string &body);
void set_cors(HttpRes *res);

// Returns true and populates out_claims if the request carries a valid Bearer token.
// On failure sends a 401 response and returns false.
bool require_auth(HttpRes *res, HttpReq *req, TokenClaims &out_claims);

// Extracts the Bearer token from the Authorization header string (used in onData lambdas).
// On failure sends a 401 response and returns empty string.
std::string auth_token_from_header(HttpRes *res, const std::string &auth_header);

void handle_get_departments(HttpRes *res, HttpReq *req, Database &db);
void handle_post_department(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_department(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_department(HttpRes *res, HttpReq *req, Database &db);
void handle_get_activities(HttpRes *res, HttpReq *req, Database &db);
void handle_get_activity(HttpRes *res, HttpReq *req, Database &db);
void handle_post_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_patch_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_delete_activity(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);

// Predefined locations
void handle_get_locations(HttpRes *res, HttpReq *req, Database &db);

// Attachments
void handle_get_attachments(HttpRes *res, HttpReq *req, Database &db);
void handle_post_attachment(HttpRes *res, HttpReq *req, Database &db);
void handle_get_attachment_download(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_attachment(HttpRes *res, HttpReq *req, Database &db);

// Auth + user endpoints
void handle_post_auth_me(HttpRes *res, HttpReq *req, Database &db);
void handle_get_me(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_me(HttpRes *res, HttpReq *req, Database &db);
void handle_get_users(HttpRes *res, HttpReq *req, Database &db);

// Admin endpoints (admin role required)
void handle_patch_admin_user(HttpRes *res, HttpReq *req, Database &db);

// Debug-only endpoints (only active when DEBUG=true)
void handle_debug_get_users(HttpRes *res, HttpReq *req, Database &db);
void handle_debug_login(HttpRes *res, HttpReq *req, Database &db);

// Mail template endpoints
void handle_get_mail_templates(HttpRes *res, HttpReq *req, Database &db);
void handle_get_mail_template(HttpRes *res, HttpReq *req, Database &db);
void handle_put_mail_template(HttpRes *res, HttpReq *req, Database &db, WebSocketManager &wm);
void handle_post_send_mail(HttpRes *res, HttpReq *req, Database &db);
void handle_get_sent_mails(HttpRes *res, HttpReq *req, Database &db);

// Bug report (creates GitHub issue)
void handle_post_bug_report(HttpRes *res, HttpReq *req, Database &db);

// Current user permissions (authenticated)
void handle_get_my_permissions(HttpRes *res, HttpReq *req, Database &db);

// Permission management (admin only)
void handle_get_roles(HttpRes *res, HttpReq *req, Database &db);
void handle_post_role(HttpRes *res, HttpReq *req, Database &db);
void handle_patch_role(HttpRes *res, HttpReq *req, Database &db);
void handle_delete_role(HttpRes *res, HttpReq *req, Database &db);
void handle_get_role_permissions(HttpRes *res, HttpReq *req, Database &db);
void handle_put_role_permission(HttpRes *res, HttpReq *req, Database &db);
void handle_get_role_dept_access(HttpRes *res, HttpReq *req, Database &db);
void handle_put_role_dept_access(HttpRes *res, HttpReq *req, Database &db);
