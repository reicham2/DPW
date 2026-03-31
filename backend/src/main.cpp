#include "App.h"
#include "db.hpp"
#include "ws_manager.hpp"
#include "handlers.hpp"
#include <cstdlib>
#include <string>
#include <cstdio>

static std::string env(const char* key, const char* def = "") {
    const char* v = std::getenv(key);
    return v ? v : def;
}

int main() {
    std::string conn_str =
        "host="     + env("POSTGRES_HOST", "db")         +
        " port="    + env("POSTGRES_PORT", "5432")        +
        " dbname="  + env("POSTGRES_DB",   "activities")  +
        " user="    + env("POSTGRES_USER", "postgres")    +
        " password="+ env("POSTGRES_PASSWORD", "");

    Database db(conn_str);
    WebSocketManager wm;

    uWS::App()
        // CORS preflight
        .options("/*", [](auto* res, auto* /*req*/) {
            res->writeHeader("Access-Control-Allow-Origin", "*")
               ->writeHeader("Access-Control-Allow-Methods", "GET,POST,PATCH,DELETE,OPTIONS")
               ->writeHeader("Access-Control-Allow-Headers", "Content-Type")
               ->writeStatus("204 No Content")
               ->end();
        })

        // REST endpoints
        .get("/activities", [&](auto* res, auto* req) {
            handle_get_activities(res, req, db);
        })
        .post("/activities", [&](auto* res, auto* req) {
            handle_post_activity(res, req, db, wm);
        })
        .patch("/activities/:id", [&](auto* res, auto* req) {
            handle_patch_activity(res, req, db, wm);
        })
        .del("/activities/:id", [&](auto* res, auto* req) {
            handle_delete_activity(res, req, db, wm);
        })

        // WebSocket endpoint
        .ws<WsUserData>("/ws", {
            .compression   = uWS::SHARED_COMPRESSOR,
            .maxPayloadLength = 64 * 1024,
            .idleTimeout   = 120,
            .open  = [&](auto* ws)                          { wm.on_open(ws);  },
            .message = [](auto* /*ws*/, std::string_view, uWS::OpCode) { /* read-only clients */ },
            .close = [&](auto* ws, int, std::string_view)  { wm.on_close(ws); }
        })

        .listen(8080, [](auto* token) {
            if (token) printf("[server] listening on :8080\n");
            else       printf("[server] FAILED to listen on :8080\n");
        })
        .run();
}
