#pragma once
#include "App.h"
#include <string>

using HttpRes = uWS::HttpResponse<false>;
using HttpReq = uWS::HttpRequest;

void send_json(HttpRes *res, int status, const std::string &body);
void set_cors(HttpRes *res);
