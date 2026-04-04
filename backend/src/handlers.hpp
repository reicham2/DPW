#pragma once
#include "App.h"
#include "auth.hpp"
#include "db.hpp"
#include "ws_manager.hpp"

using HttpRes = uWS::HttpResponse<false>;
using HttpReq = uWS::HttpRequest;

void send_json(HttpRes* res, int status, const std::string& body);
void set_cors(HttpRes* res);

// Returns true and populates out_claims if the request carries a valid Bearer token.
// On failure sends a 401 response and returns false.
bool require_auth(HttpRes* res, HttpReq* req, TokenClaims& out_claims);

// Extracts the Bearer token from the Authorization header string (used in onData lambdas).
// On failure sends a 401 response and returns empty string.
std::string auth_token_from_header(HttpRes* res, const std::string& auth_header);

void handle_get_departments (HttpRes* res, HttpReq* req);
void handle_get_activities  (HttpRes* res, HttpReq* req, Database& db);
void handle_get_activity    (HttpRes* res, HttpReq* req, Database& db);
void handle_get_siko        (HttpRes* res, HttpReq* req, Database& db);
void handle_post_activity   (HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm);
void handle_patch_activity  (HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm);
void handle_delete_activity (HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm);

// Auth + user endpoints
void handle_post_auth_me    (HttpRes* res, HttpReq* req, Database& db);
void handle_get_me          (HttpRes* res, HttpReq* req, Database& db);
void handle_patch_me        (HttpRes* res, HttpReq* req, Database& db);
