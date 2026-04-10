#include "App.h"
#include "db.hpp"
#include "ws_manager.hpp"
#include "handlers.hpp"
#include <cstdlib>
#include <string>
#include <cstdio>
#include <curl/curl.h>

static std::string env(const char *key, const char *def = "")
{
     const char *v = std::getenv(key);
     return v ? v : def;
}

int main()
{
     curl_global_init(CURL_GLOBAL_DEFAULT);

     std::string conn_str =
         "host=" + env("POSTGRES_HOST", "db") +
         " port=" + env("POSTGRES_PORT", "5432") +
         " dbname=" + env("POSTGRES_DB", "activities") +
         " user=" + env("POSTGRES_USER", "postgres") +
         " password=" + env("POSTGRES_PASSWORD", "");

     Database db(conn_str);
     WebSocketManager wm;

     uWS::App()
         // CORS preflight
         .options("/*", [](auto *res, auto * /*req*/)
                  { res->writeHeader("Access-Control-Allow-Origin", "*")
                        ->writeHeader("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS")
                        ->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization")
                        ->writeStatus("204 No Content")
                        ->end(); })

         // Health check (no auth required — used by Docker healthcheck)
         .get("/health", [](auto *res, auto * /*req*/)
              { res->writeHeader("Access-Control-Allow-Origin", "*")
                    ->writeStatus("200 OK")
                    ->end("ok"); })

         // Auth + user endpoints
         .post("/auth/me", [&](auto *res, auto *req)
               { handle_post_auth_me(res, req, db); })
         .get("/me", [&](auto *res, auto *req)
              { handle_get_me(res, req, db); })
         .get("/users", [&](auto *res, auto *req)
              { handle_get_users(res, req, db); })
         .patch("/me", [&](auto *res, auto *req)
                { handle_patch_me(res, req, db); })

         // Admin endpoints
         .patch("/admin/users/:id", [&](auto *res, auto *req)
                { handle_patch_admin_user(res, req, db); })

         // Static data
         .get("/departments", [&](auto *res, auto *req)
              { handle_get_departments(res, req); })

         // Activities list + create
         .get("/activities", [&](auto *res, auto *req)
              { handle_get_activities(res, req, db); })
         .post("/activities", [&](auto *res, auto *req)
               { handle_post_activity(res, req, db, wm); })

         // SiKo download — must be registered BEFORE /activities/:id
         .get("/activities/:id/siko", [&](auto *res, auto *req)
              { handle_get_siko(res, req, db); })

         // Single activity CRUD
         .get("/activities/:id", [&](auto *res, auto *req)
              { handle_get_activity(res, req, db); })
         .patch("/activities/:id", [&](auto *res, auto *req)
                { handle_patch_activity(res, req, db, wm); })
         .del("/activities/:id", [&](auto *res, auto *req)
              { handle_delete_activity(res, req, db, wm); })

         // Mail templates + send
         .get("/mail-templates", [&](auto *res, auto *req)
              { handle_get_mail_templates(res, req, db); })
         .get("/mail-templates/:department", [&](auto *res, auto *req)
              { handle_get_mail_template(res, req, db); })
         .put("/mail-templates/:department", [&](auto *res, auto *req)
              { handle_put_mail_template(res, req, db); })
         .post("/send-mail", [&](auto *res, auto *req)
               { handle_post_send_mail(res, req, db); })

         // WebSocket endpoint
         .ws<WsUserData>("/ws", {.compression = uWS::SHARED_COMPRESSOR,
                                 .maxPayloadLength = 64 * 1024,
                                 .idleTimeout = 120,
                                 .open = [&](auto *ws)
                                 { wm.on_open(ws); },
                                 .message = [&](auto *ws, std::string_view message, uWS::OpCode)
                                 { wm.on_message(ws, message); },
                                 .close = [&](auto *ws, int, std::string_view)
                                 { wm.on_close(ws); }})

         .listen(8080, [](auto *token)
                 {
            if (token) printf("[server] listening on :8080\n");
            else       printf("[server] FAILED to listen on :8080\n"); })
         .run();
}
