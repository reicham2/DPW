#include "core/http.hpp"
#include "utils/strings.hpp"
#include "infra/audit.hpp"

void set_cors(HttpRes *res)
{
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization,X-Guest-Edit-Token");
}

void send_json(HttpRes *res, int status, const std::string &body)
{
    if (!audit_finish(res, status, body))
    {
        send_json(res, 500, R"({"error":"Audit Log konnte nicht abgeschlossen werden; Aktion möglicherweise ausgeführt. Vor Wiederholung Zustand prüfen."})");
        return;
    }
    res->writeStatus(status_text(status));
    set_cors(res);
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}
