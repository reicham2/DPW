#pragma once
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>
#include "App.h"

class Database;

struct WsUserData
{
    std::string display_name;
    std::string oid;
    std::string viewing_activity; // activity_id the user is currently viewing
    bool authenticated = false;
};

using WsHandle = uWS::WebSocket<false, true, WsUserData> *;

struct LockInfo
{
    std::string section;
    std::string user_name;
    WsHandle holder;
};

class WebSocketManager
{
public:
    explicit WebSocketManager(Database &db) : db_(db) {}

    bool authenticate_upgrade(const std::string &token, WsUserData &out) const;

    void on_open(WsHandle ws);
    void on_close(WsHandle ws);
    void on_message(WsHandle ws, std::string_view message);
    void broadcast(const std::string &message);
    size_t connection_count() const { return clients_.size(); }

private:
    void handle_register(WsHandle ws, const std::string &token);
    void handle_join(WsHandle ws, const std::string &activity_id);
    void handle_leave(WsHandle ws);
    void handle_lock(WsHandle ws, const std::string &activity_id, const std::string &section);
    void handle_unlock(WsHandle ws, const std::string &activity_id, const std::string &section);
    void broadcast_to_activity(const std::string &activity_id, const std::string &message, WsHandle exclude = nullptr);
    void send_editors_list(const std::string &activity_id);
    void send_locks_state(WsHandle ws, const std::string &activity_id);
    void cleanup_user_locks(WsHandle ws);

    Database &db_;
    std::unordered_set<WsHandle> clients_;
    // activity_id -> set of clients viewing it
    std::unordered_map<std::string, std::unordered_set<WsHandle>> activity_viewers_;
    // activity_id -> section -> LockInfo
    std::unordered_map<std::string, std::unordered_map<std::string, LockInfo>> activity_locks_;
};
