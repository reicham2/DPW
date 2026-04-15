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
         .get("/debug/users", [&](auto *res, auto *req)
              { handle_debug_get_users(res, req, db); })
         .patch("/me", [&](auto *res, auto *req)
                { handle_patch_me(res, req, db); })
         .get("/my-permissions", [&](auto *res, auto *req)
              { handle_get_my_permissions(res, req, db); })

         // Admin endpoints
         .patch("/admin/users/:id", [&](auto *res, auto *req)
                { handle_patch_admin_user(res, req, db); })
         .del("/admin/users/:id", [&](auto *res, auto *req)
              { handle_delete_admin_user(res, req, db); })

         // Permission management (admin only)
         .get("/admin/roles", [&](auto *res, auto *req)
              { handle_get_roles(res, req, db); })
         .post("/admin/roles", [&](auto *res, auto *req)
               { handle_post_role(res, req, db); })
         .patch("/admin/roles/:name", [&](auto *res, auto *req)
                { handle_patch_role(res, req, db); })
         .del("/admin/roles/:name", [&](auto *res, auto *req)
              { handle_delete_role(res, req, db); })
         .get("/admin/departments", [&](auto *res, auto *req)
              { handle_get_departments(res, req, db); })
         .post("/admin/departments", [&](auto *res, auto *req)
               { handle_post_department(res, req, db); })
         .patch("/admin/departments/:name", [&](auto *res, auto *req)
                { handle_patch_department(res, req, db); })
         .del("/admin/departments/:name", [&](auto *res, auto *req)
              { handle_delete_department(res, req, db); })
         .get("/admin/role-permissions", [&](auto *res, auto *req)
              { handle_get_role_permissions(res, req, db); })
         .put("/admin/role-permissions", [&](auto *res, auto *req)
              { handle_put_role_permission(res, req, db); })
         .get("/admin/role-dept-access", [&](auto *res, auto *req)
              { handle_get_role_dept_access(res, req, db); })
         .put("/admin/role-dept-access", [&](auto *res, auto *req)
              { handle_put_role_dept_access(res, req, db); })

         // Static data
         .get("/departments", [&](auto *res, auto *req)
              { handle_get_departments(res, req, db); })

         // Activities list + create
         .get("/activities", [&](auto *res, auto *req)
              { handle_get_activities(res, req, db); })
         .post("/activities", [&](auto *res, auto *req)
               { handle_post_activity(res, req, db, wm); })

         // Predefined locations
         .get("/locations", [&](auto *res, auto *req)
              { handle_get_locations(res, req, db); })

         // Attachments — must be registered BEFORE /activities/:id
         .get("/activities/:id/attachments", [&](auto *res, auto *req)
              { handle_get_attachments(res, req, db); })
         .post("/activities/:id/attachments", [&](auto *res, auto *req)
               { handle_post_attachment(res, req, db); })
         .get("/attachments/:id/download", [&](auto *res, auto *req)
              { handle_get_attachment_download(res, req, db); })
         .del("/attachments/:id", [&](auto *res, auto *req)
              { handle_delete_attachment(res, req, db); })

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
              { handle_put_mail_template(res, req, db, wm); })
         .post("/send-mail", [&](auto *res, auto *req)
               { handle_post_send_mail(res, req, db); })
         .get("/activities/:id/sent-mails", [&](auto *res, auto *req)
              { handle_get_sent_mails(res, req, db); })

         // Bug report — creates a GitHub issue
         .post("/bug-report", [&](auto *res, auto *req)
               { handle_post_bug_report(res, req, db); })

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
