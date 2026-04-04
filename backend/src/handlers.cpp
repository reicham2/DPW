#include "handlers.hpp"
#include "models.hpp"
#include "json.hpp"
#include <string>
#include <memory>

static const char* status_text(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 201: return "201 Created";
        case 204: return "204 No Content";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 404: return "404 Not Found";
        case 500: return "500 Internal Server Error";
        default:  return "200 OK";
    }
}

void set_cors(HttpRes* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type,Authorization");
}

bool require_auth(HttpRes* res, HttpReq* req, TokenClaims& out_claims) {
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty()) {
        send_json(res, 401, R"({"error":"Unauthorized"})");
        return false;
    }
    try {
        out_claims = validate_microsoft_token(token);
        return true;
    } catch (std::exception& e) {
        send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump());
        return false;
    }
}

std::string auth_token_from_header(HttpRes* res, const std::string& auth_header) {
    std::string token = extract_bearer_token(auth_header);
    if (token.empty()) {
        send_json(res, 401, R"({"error":"Unauthorized"})");
    }
    return token;
}

void send_json(HttpRes* res, int status, const std::string& body) {
    res->writeStatus(status_text(status));
    set_cors(res);
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}

static std::string str_field(const nlohmann::json& j, const char* key, const std::string& def = "") {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

// Parse ActivityInput from JSON body
static ActivityInput parse_activity_input(const nlohmann::json& j) {
    ActivityInput input;
    input.title        = str_field(j, "title");
    input.date         = str_field(j, "date");
    input.start_time   = str_field(j, "start_time");
    input.end_time     = str_field(j, "end_time");
    input.goal         = str_field(j, "goal");
    input.location     = str_field(j, "location");
    input.responsible  = str_field(j, "responsible");
    input.needs_siko   = j.value("needs_siko", false);

    if (j.contains("department") && j["department"].is_string())
        input.department = j["department"].get<std::string>();

    if (j.contains("bad_weather_info") && j["bad_weather_info"].is_string()) {
        std::string bwi = j["bad_weather_info"].get<std::string>();
        if (!bwi.empty()) input.bad_weather_info = bwi;
    }

    if (j.contains("siko_base64") && j["siko_base64"].is_string()) {
        std::string sb = j["siko_base64"].get<std::string>();
        if (!sb.empty()) input.siko_base64 = sb;
    }

    if (j.contains("material") && j["material"].is_array()) {
        for (auto& m : j["material"])
            if (m.is_string()) input.material.push_back(m.get<std::string>());
    }

    if (j.contains("programs") && j["programs"].is_array()) {
        for (auto& p : j["programs"]) {
            ProgramInput pi;
            pi.time        = str_field(p, "time");
            pi.title       = str_field(p, "title");
            pi.description = str_field(p, "description");
            pi.responsible = str_field(p, "responsible");
            input.programs.push_back(pi);
        }
    }

    return input;
}

// ---- GET /departments -------------------------------------------------------

void handle_get_departments(HttpRes* res, HttpReq* /*req*/) {
    static const std::string body =
        R"(["Leiter","Pio","Pfadi","Wölfe","Biber"])";
    send_json(res, 200, body);
}

// ---- GET /activities --------------------------------------------------------

void handle_get_activities(HttpRes* res, HttpReq* req, Database& db) {
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    try {
        auto activities = db.list_activities();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& a : activities) arr.push_back(to_json(a));
        send_json(res, 200, arr.dump());
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id ----------------------------------------------------

void handle_get_activity(HttpRes* res, HttpReq* req, Database& db) {
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    std::string id{req->getParameter(0)};
    try {
        auto activity = db.get_activity_by_id(id);
        if (!activity) {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }
        send_json(res, 200, to_json(*activity).dump());
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /activities/:id/siko -----------------------------------------------

void handle_get_siko(HttpRes* res, HttpReq* req, Database& db) {
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    std::string id{req->getParameter(0)};
    try {
        auto bytes = db.get_siko(id);
        if (!bytes || bytes->empty()) {
            send_json(res, 404, R"({"error":"no SiKo file"})");
            return;
        }
        std::string disp = "attachment; filename=\"siko-" + id + ".pdf\"";
        set_cors(res);
        res->writeStatus("200 OK");
        res->writeHeader("Content-Type", "application/pdf");
        res->writeHeader("Content-Disposition", disp);
        res->end(std::string_view(
            reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /activities -------------------------------------------------------

void handle_post_activity(HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm) {
    // Read auth header synchronously (req is only valid here, not inside onData)
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty()) { send_json(res, 401, R"({"error":"Unauthorized"})"); return; }
    try { validate_microsoft_token(token); }
    catch (std::exception& e) { send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump()); return; }

    auto buf = std::make_shared<std::string>();
    res->onAborted([]{ });
    res->onData([res, buf, &db, &wm](std::string_view chunk, bool last) {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);
        if (input.title.empty()) {
            send_json(res, 400, R"({"error":"title is required"})");
            return;
        }

        try {
            auto activity = db.create_activity(input);
            if (!activity) {
                send_json(res, 500, R"({"error":"db error"})");
                return;
            }
            nlohmann::json msg = {{"event", "created"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            send_json(res, 201, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        }
    });
}

// ---- PATCH /activities/:id --------------------------------------------------

void handle_patch_activity(HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm) {
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty()) { send_json(res, 401, R"({"error":"Unauthorized"})"); return; }
    try { validate_microsoft_token(token); }
    catch (std::exception& e) { send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump()); return; }

    std::string id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();
    res->onAborted([]{ });
    res->onData([res, buf, id, &db, &wm](std::string_view chunk, bool last) {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        ActivityInput input = parse_activity_input(j);

        try {
            auto activity = db.update_activity(id, input);
            if (!activity) {
                send_json(res, 404, R"({"error":"not found"})");
                return;
            }
            nlohmann::json msg = {{"event", "updated"}, {"activity", to_json(*activity)}};
            wm.broadcast(msg.dump());
            send_json(res, 200, to_json(*activity).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        }
    });
}

// ---- DELETE /activities/:id -------------------------------------------------

void handle_delete_activity(HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm) {
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    std::string id{req->getParameter(0)};
    try {
        bool ok = db.delete_activity(id);
        if (!ok) {
            send_json(res, 404, R"({"error":"not found"})");
            return;
        }
        nlohmann::json msg = {{"event", "deleted"}, {"id", id}};
        wm.broadcast(msg.dump());
        set_cors(res);
        res->writeStatus("204 No Content");
        res->end();
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- User helpers -----------------------------------------------------------

static nlohmann::json user_to_json(const UserRecord& u) {
    nlohmann::json j;
    j["id"]            = u.id;
    j["microsoft_oid"] = u.microsoft_oid;
    j["email"]         = u.email;
    j["display_name"]  = u.display_name;
    j["department"]    = u.department ? nlohmann::json(*u.department) : nlohmann::json(nullptr);
    j["created_at"]    = u.created_at;
    j["updated_at"]    = u.updated_at;
    return j;
}

// ---- POST /auth/me ----------------------------------------------------------
// Called by the frontend immediately after Microsoft login.
// Validates the token and upserts the user record.

void handle_post_auth_me(HttpRes* res, HttpReq* req, Database& db) {
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    try {
        auto user = db.upsert_user(claims.oid, claims.email, claims.display_name);
        if (!user) {
            send_json(res, 500, R"({"error":"db error"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- GET /me ----------------------------------------------------------------

void handle_get_me(HttpRes* res, HttpReq* req, Database& db) {
    TokenClaims claims;
    if (!require_auth(res, req, claims)) return;
    try {
        auto user = db.get_user_by_oid(claims.oid);
        if (!user) {
            send_json(res, 404, R"({"error":"user not found"})");
            return;
        }
        send_json(res, 200, user_to_json(*user).dump());
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- PATCH /me --------------------------------------------------------------

void handle_patch_me(HttpRes* res, HttpReq* req, Database& db) {
    // Read auth synchronously before onData
    std::string auth_header{req->getHeader("authorization")};
    std::string token = extract_bearer_token(auth_header);
    if (token.empty()) { send_json(res, 401, R"({"error":"Unauthorized"})"); return; }

    TokenClaims claims;
    try { claims = validate_microsoft_token(token); }
    catch (std::exception& e) { send_json(res, 401, nlohmann::json{{"error", e.what()}}.dump()); return; }

    auto buf = std::make_shared<std::string>();
    res->onAborted([]{ });
    res->onData([res, buf, claims, &db](std::string_view chunk, bool last) {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded()) {
            send_json(res, 400, R"({"error":"invalid JSON"})");
            return;
        }

        std::string display_name = j.value("display_name", "");
        if (display_name.empty()) {
            send_json(res, 400, R"({"error":"display_name is required"})");
            return;
        }

        std::optional<std::string> department;
        if (j.contains("department") && j["department"].is_string())
            department = j["department"].get<std::string>();

        try {
            auto user = db.update_user(claims.oid, display_name, department);
            if (!user) {
                send_json(res, 404, R"({"error":"user not found"})");
                return;
            }
            send_json(res, 200, user_to_json(*user).dump());
        } catch (std::exception& e) {
            send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
        }
    });
}
