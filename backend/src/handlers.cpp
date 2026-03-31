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
        case 404: return "404 Not Found";
        case 500: return "500 Internal Server Error";
        default:  return "200 OK";
    }
}

void set_cors(HttpRes* res) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Methods", "GET,POST,PATCH,DELETE,OPTIONS");
    res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
}

void send_json(HttpRes* res, int status, const std::string& body) {
    set_cors(res);
    res->writeStatus(status_text(status));
    res->writeHeader("Content-Type", "application/json");
    res->end(body);
}

// ---- GET /activities -------------------------------------------------------

void handle_get_activities(HttpRes* res, HttpReq* /*req*/, Database& db) {
    try {
        auto activities = db.list_activities();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& a : activities) arr.push_back(to_json(a));
        send_json(res, 200, arr.dump());
    } catch (std::exception& e) {
        send_json(res, 500, nlohmann::json{{"error", e.what()}}.dump());
    }
}

// ---- POST /activities ------------------------------------------------------

void handle_post_activity(HttpRes* res, HttpReq* /*req*/, Database& db, WebSocketManager& wm) {
    auto buf = std::make_shared<std::string>();

    res->onAborted([]{ /* connection closed before full body */ });

    res->onData([res, buf, &db, &wm](std::string_view chunk, bool last) {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded() || !j.contains("text") || !j["text"].is_string()) {
            send_json(res, 400, R"({"error":"body must be {\"text\":\"...\"}"})");
            return;
        }

        try {
            auto activity = db.create_activity(j["text"].get<std::string>());
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

// ---- PATCH /activities/:id -------------------------------------------------

void handle_patch_activity(HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm) {
    std::string id{req->getParameter(0)};
    auto buf = std::make_shared<std::string>();

    res->onAborted([]{ });

    res->onData([res, buf, id, &db, &wm](std::string_view chunk, bool last) {
        buf->append(chunk.data(), chunk.size());
        if (!last) return;

        auto j = nlohmann::json::parse(*buf, nullptr, false);
        if (j.is_discarded() || !j.contains("text") || !j["text"].is_string()) {
            send_json(res, 400, R"({"error":"body must be {\"text\":\"...\"}"})");
            return;
        }

        try {
            auto activity = db.update_activity(id, j["text"].get<std::string>());
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

// ---- DELETE /activities/:id ------------------------------------------------

void handle_delete_activity(HttpRes* res, HttpReq* req, Database& db, WebSocketManager& wm) {
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
