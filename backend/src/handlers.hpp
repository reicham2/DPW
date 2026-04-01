#pragma once
#include "App.h"
#include "db.hpp"
#include "ws_manager.hpp"

using HttpRes = uWS::HttpResponse<false>;
using HttpReq = uWS::HttpRequest;

void send_json(HttpRes* res, int status, const std::string& body);
void set_cors(HttpRes* res);

void handle_get_departments (HttpRes* res, HttpReq* req);
void handle_get_activities  (HttpRes* res, HttpReq* req, Database& db);
void handle_get_activity    (HttpRes* res, HttpReq* req, Database& db);
void handle_get_siko        (HttpRes* res, HttpReq* req, Database& db);
void handle_post_activity   (HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm);
void handle_patch_activity  (HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm);
void handle_delete_activity (HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm);
