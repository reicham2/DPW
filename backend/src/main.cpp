#include "App.h"
#include "db.hpp"
#include "ws_manager.hpp"
#include "handlers.hpp"
#include <cstdlib>
#include <string>
#include <cstdio>
#include <curl/curl.h>

static std::string url_decode_component(std::string_view src)
{
     std::string out;
     out.reserve(src.size());
     for (size_t i = 0; i < src.size(); ++i)
     {
          if (src[i] == '%' && i + 2 < src.size())
          {
               unsigned int ch = 0;
               if (sscanf(src.data() + i + 1, "%2x", &ch) == 1)
               {
                    out += static_cast<char>(ch);
                    i += 2;
                    continue;
               }
          }
          out += (src[i] == '+') ? ' ' : src[i];
     }
     return out;
}

static std::string env(const char *key, const char *def = "")
{
     const char *v = std::getenv(key);
     return v ? v : def;
}

int main()
{
     // Disable stdout buffering so logs appear immediately in Docker
     setvbuf(stdout, nullptr, _IONBF, 0);

     curl_global_init(CURL_GLOBAL_DEFAULT);

     std::string conn_str =
         "host=" + env("POSTGRES_HOST", "db") +
         " port=" + env("POSTGRES_PORT", "5432") +
         " dbname=" + env("POSTGRES_DB", "activities") +
         " user=" + env("POSTGRES_USER", "postgres") +
         " password=" + env("POSTGRES_PASSWORD", "");

     Database db(conn_str);
     WebSocketManager wm(db);

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

         // Debug-only endpoints (compiled out in Release)
         .get("/debug/users", [&](auto *res, auto *req)
              { handle_debug_get_users(res, req, db); })

         // Auth + user endpoints
         .post("/auth/me", [&](auto *res, auto *req)
               { handle_post_auth_me(res, req, db); })
         .get("/me", [&](auto *res, auto *req)
              { handle_get_me(res, req, db); })
         .get("/users", [&](auto *res, auto *req)
              { handle_get_users(res, req, db); })
         .patch("/me", [&](auto *res, auto *req)
                { handle_patch_me(res, req, db); })
         .get("/my-permissions", [&](auto *res, auto *req)
              { handle_get_my_permissions(res, req, db); })
         .get("/notifications", [&](auto *res, auto *req)
              { handle_get_notifications(res, req, db); })
         .patch("/notifications/:id/read", [&](auto *res, auto *req)
                { handle_patch_notification_read(res, req, db); })
         .post("/notifications/read-all", [&](auto *res, auto *req)
               { handle_post_notifications_read_all(res, req, db); })
         .get("/push/vapid-public-key", [&](auto *res, auto *req)
              { handle_get_push_vapid_public_key(res, req); })
         .post("/push/subscriptions", [&](auto *res, auto *req)
               { handle_post_push_subscription(res, req, db); })
         .del("/push/subscriptions", [&](auto *res, auto *req)
              { handle_delete_push_subscription(res, req, db); })
         .post("/push/payload", [&](auto *res, auto *req)
               { handle_post_push_payload(res, req, db); })

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
         .post("/admin/roles/reorder", [&](auto *res, auto *req)
               { handle_post_roles_reorder(res, req, db); })
         .post("/admin/roles/:name/move", [&](auto *res, auto *req)
               { handle_post_role_move(res, req, db); })
         .patch("/admin/roles/:name", [&](auto *res, auto *req)
                { handle_patch_role(res, req, db); })
         .del("/admin/roles/:name", [&](auto *res, auto *req)
              { handle_delete_role(res, req, db, wm); })
         .get("/admin/departments", [&](auto *res, auto *req)
              { handle_get_departments(res, req, db); })
         .get("/admin/midata-status", [&](auto *res, auto *req)
              { handle_get_admin_midata_status(res, req, db); })
         .post("/admin/departments", [&](auto *res, auto *req)
               { handle_post_department(res, req, db); })
         .patch("/admin/departments/:name", [&](auto *res, auto *req)
                { handle_patch_department(res, req, db); })
         .del("/admin/departments/:name", [&](auto *res, auto *req)
              { handle_delete_department(res, req, db, wm); })
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
         .get("/admin/locations", [&](auto *res, auto *req)
              { handle_get_locations_admin(res, req, db); })
         .post("/admin/locations", [&](auto *res, auto *req)
               { handle_post_location(res, req, db); })
         .patch("/admin/locations/:id", [&](auto *res, auto *req)
                { handle_patch_location(res, req, db); })
         .del("/admin/locations/:id", [&](auto *res, auto *req)
              { handle_delete_location(res, req, db); })

         // Attachments — must be registered BEFORE /activities/:id
         .get("/activities/:id/attachments", [&](auto *res, auto *req)
              { handle_get_attachments(res, req, db); })
         .post("/activities/:id/attachments", [&](auto *res, auto *req)
               { handle_post_attachment(res, req, db); })
         .get("/attachments/:id/download", [&](auto *res, auto *req)
              { handle_get_attachment_download(res, req, db); })
         .del("/attachments/:id", [&](auto *res, auto *req)
              { handle_delete_attachment(res, req, db); })

         // Activity share links — must be registered BEFORE /activities/:id
         .post("/activities/:id/share", [&](auto *res, auto *req)
               { handle_post_share_link(res, req, db); })
         .get("/activities/:id/share", [&](auto *res, auto *req)
              { handle_get_share_link(res, req, db); })
         .del("/activities/:id/share", [&](auto *res, auto *req)
              { handle_delete_share_link(res, req, db); })
         // Public shared activity view (no auth)
         .get("/shared/:token", [&](auto *res, auto *req)
              { handle_get_shared_activity(res, req, db); })

         // MiData child count (auth)
         .get("/activities/:id/midata/children-count", [&](auto *res, auto *req)
              { handle_get_activity_midata_children_count(res, req, db); })
         .get("/activities/:id/midata-children-count", [&](auto *res, auto *req)
              { handle_get_activity_midata_children_count(res, req, db); })
         .get("/midata/children-counts", [&](auto *res, auto *req)
              { handle_get_midata_children_counts(res, req, db); })
         .get("/activities/:id/weather-location", [&](auto *res, auto *req)
              { handle_get_activity_weather_location(res, req, db); })
         .put("/activities/:id/weather-location", [&](auto *res, auto *req)
              { handle_put_activity_weather_location(res, req, db); })
         .get("/activities/:id/weather-expected", [&](auto *res, auto *req)
              { handle_get_activity_expected_weather(res, req, db); })

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
               { handle_post_send_mail(res, req, db, wm); })
         .get("/activities/:id/sent-mails", [&](auto *res, auto *req)
              { handle_get_sent_mails(res, req, db); })
         .get("/activities/:id/mail-draft", [&](auto *res, auto *req)
              { handle_get_mail_draft(res, req, db); })
         .put("/activities/:id/mail-draft", [&](auto *res, auto *req)
              { handle_put_mail_draft(res, req, db); })
         .del("/activities/:id/mail-draft", [&](auto *res, auto *req)
              { handle_delete_mail_draft(res, req, db); })
         .get("/activities/:id/form-draft", [&](auto *res, auto *req)
              { handle_get_form_draft(res, req, db); })
         .put("/activities/:id/form-draft", [&](auto *res, auto *req)
              { handle_put_form_draft(res, req, db); })
         .del("/activities/:id/form-draft", [&](auto *res, auto *req)
              { handle_delete_form_draft(res, req, db); })

         // Bug report — creates a GitHub issue
         .post("/bug-report", [&](auto *res, auto *req)
               { handle_post_bug_report(res, req, db); })

         // ── Forms ────────────────────────────────────────────────────────────
         // Public endpoints (no auth) — must be BEFORE /activities/:id
         .get("/forms/:slug", [&](auto *res, auto *req)
              { handle_get_public_form(res, req, db); })
         .post("/forms/:slug/submit", [&](auto *res, auto *req)
               { handle_post_form_submit(res, req, db); })

         // Admin: form CRUD per activity
         .get("/activities/:id/form", [&](auto *res, auto *req)
              { handle_get_activity_form(res, req, db); })
         .post("/activities/:id/form", [&](auto *res, auto *req)
               { handle_post_activity_form(res, req, db); })
         .put("/activities/:id/form", [&](auto *res, auto *req)
              { handle_put_activity_form(res, req, db); })
         .del("/activities/:id/form", [&](auto *res, auto *req)
              { handle_delete_activity_form(res, req, db); })

         // Admin: responses & stats
         .get("/activities/:id/form/responses", [&](auto *res, auto *req)
              { handle_get_form_responses(res, req, db); })
         .get("/activities/:id/form/responses/:rid", [&](auto *res, auto *req)
              { handle_get_form_response(res, req, db); })
         .del("/activities/:id/form/responses/:rid", [&](auto *res, auto *req)
              { handle_delete_form_response(res, req, db); })
         .get("/activities/:id/form/stats", [&](auto *res, auto *req)
              { handle_get_form_stats(res, req, db); })

         // Admin: form templates
         .get("/form-templates", [&](auto *res, auto *req)
              { handle_get_form_templates(res, req, db); })
         .post("/form-templates", [&](auto *res, auto *req)
               { handle_post_form_template(res, req, db); })
         .put("/form-templates/:id", [&](auto *res, auto *req)
              { handle_put_form_template(res, req, db); })
         .del("/form-templates/:id", [&](auto *res, auto *req)
              { handle_delete_form_template(res, req, db); })

         // WebSocket endpoint
         .ws<WsUserData>("/ws", {.compression = uWS::SHARED_COMPRESSOR,
                                 .maxPayloadLength = 64 * 1024,
                                 .idleTimeout = 120,
                                 .upgrade = [&](auto *res, auto *req, auto *context)
                                 {
                                      std::string token = url_decode_component(req->getQuery("token"));
                                      if (token.empty())
                                      {
                                           res->writeStatus("401 Unauthorized")->end("Missing WebSocket token");
                                           return;
                                      }

                                      WsUserData user_data;
                                      if (!wm.authenticate_upgrade(token, user_data))
                                      {
                                           res->writeStatus("401 Unauthorized")->end("Invalid WebSocket token");
                                           return;
                                      }

                                      res->template upgrade<WsUserData>(
                                          std::move(user_data),
                                          req->getHeader("sec-websocket-key"),
                                          req->getHeader("sec-websocket-protocol"),
                                          req->getHeader("sec-websocket-extensions"),
                                          context); },
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
