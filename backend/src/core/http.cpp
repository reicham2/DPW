#include "core/http.hpp"
#include "utils/strings.hpp"

void set_cors(HttpRes *res)
{
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
}

void send_json(HttpRes *res, int status, const std::string &body)
{
    res->writeStatus(status_text(status));
    set_cors(res);
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}
