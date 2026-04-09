#include "ws_manager.hpp"
#include "json.hpp"
#include <cstdio>

using json = nlohmann::json;

void WebSocketManager::on_open(WsHandle ws)
{
    clients_.insert(ws);
    printf("[WS] client connected  (total: %zu)\n", clients_.size());
}

void WebSocketManager::on_close(WsHandle ws)
{
    // Clean up locks and presence before removing
    cleanup_user_locks(ws);
    handle_leave(ws);
    clients_.erase(ws);
    printf("[WS] client disconnected (total: %zu)\n", clients_.size());
}

void WebSocketManager::on_message(WsHandle ws, std::string_view message)
{
    try
    {
        auto j = json::parse(message);
        std::string type = j.value("type", "");

        if (type == "register")
        {
            handle_register(ws, j.value("display_name", ""), j.value("oid", ""));
        }
        else if (type == "join")
        {
            handle_join(ws, j.value("activity_id", ""));
        }
        else if (type == "leave")
        {
            handle_leave(ws);
        }
        else if (type == "lock")
        {
            handle_lock(ws, j.value("activity_id", ""), j.value("section", ""));
        }
        else if (type == "unlock")
        {
            handle_unlock(ws, j.value("activity_id", ""), j.value("section", ""));
        }
    }
    catch (const std::exception &e)
    {
        printf("[WS] bad message: %s\n", e.what());
    }
}

void WebSocketManager::broadcast(const std::string &message)
{
    std::string_view sv(message);
    for (auto *ws : clients_)
    {
        ws->send(sv, uWS::OpCode::TEXT);
    }
}

// ---- Private helpers --------------------------------------------------------

void WebSocketManager::handle_register(WsHandle ws, const std::string &display_name, const std::string &oid)
{
    auto *data = ws->getUserData();
    data->display_name = display_name;
    data->oid = oid;
    printf("[WS] registered user: %s\n", display_name.c_str());
}

void WebSocketManager::handle_join(WsHandle ws, const std::string &activity_id)
{
    if (activity_id.empty())
        return;
    // Leave previous activity first
    handle_leave(ws);
    auto *data = ws->getUserData();
    data->viewing_activity = activity_id;
    activity_viewers_[activity_id].insert(ws);
    send_editors_list(activity_id);
    // Send current lock state to the joining user
    send_locks_state(ws, activity_id);
}

void WebSocketManager::handle_leave(WsHandle ws)
{
    auto *data = ws->getUserData();
    if (data->viewing_activity.empty())
        return;
    std::string aid = data->viewing_activity;
    // Remove all locks held by this user for this activity
    auto it = activity_locks_.find(aid);
    if (it != activity_locks_.end())
    {
        std::vector<std::string> to_unlock;
        for (auto &[section, lock] : it->second)
        {
            if (lock.holder == ws)
                to_unlock.push_back(section);
        }
        for (auto &sec : to_unlock)
        {
            it->second.erase(sec);
            json msg = {{"event", "unlock"}, {"activity_id", aid}, {"section", sec}};
            broadcast_to_activity(aid, msg.dump(), ws);
        }
        if (it->second.empty())
            activity_locks_.erase(it);
    }
    activity_viewers_[aid].erase(ws);
    if (activity_viewers_[aid].empty())
    {
        activity_viewers_.erase(aid);
    }
    else
    {
        send_editors_list(aid);
    }
    data->viewing_activity.clear();
}

void WebSocketManager::handle_lock(WsHandle ws, const std::string &activity_id, const std::string &section)
{
    if (activity_id.empty() || section.empty())
        return;
    auto *data = ws->getUserData();
    if (data->display_name.empty())
        return;

    auto &locks = activity_locks_[activity_id];
    auto it = locks.find(section);
    if (it != locks.end() && it->second.holder != ws)
    {
        // Already locked by someone else — deny
        return;
    }
    locks[section] = {section, data->display_name, ws};
    json msg = {{"event", "lock"}, {"activity_id", activity_id}, {"section", section}, {"user", data->display_name}};
    broadcast_to_activity(activity_id, msg.dump(), ws);
}

void WebSocketManager::handle_unlock(WsHandle ws, const std::string &activity_id, const std::string &section)
{
    if (activity_id.empty() || section.empty())
        return;
    auto it = activity_locks_.find(activity_id);
    if (it == activity_locks_.end())
        return;
    auto sit = it->second.find(section);
    if (sit == it->second.end() || sit->second.holder != ws)
        return; // not the owner
    it->second.erase(sit);
    if (it->second.empty())
        activity_locks_.erase(it);
    json msg = {{"event", "unlock"}, {"activity_id", activity_id}, {"section", section}};
    broadcast_to_activity(activity_id, msg.dump(), ws);
}

void WebSocketManager::broadcast_to_activity(const std::string &activity_id, const std::string &message, WsHandle exclude)
{
    auto it = activity_viewers_.find(activity_id);
    if (it == activity_viewers_.end())
        return;
    std::string_view sv(message);
    for (auto *ws : it->second)
    {
        if (ws != exclude)
            ws->send(sv, uWS::OpCode::TEXT);
    }
}

void WebSocketManager::send_editors_list(const std::string &activity_id)
{
    auto it = activity_viewers_.find(activity_id);
    if (it == activity_viewers_.end())
        return;
    json users = json::array();
    for (auto *ws : it->second)
    {
        auto *data = ws->getUserData();
        if (!data->display_name.empty())
        {
            users.push_back(data->display_name);
        }
    }
    json msg = {{"event", "editors"}, {"activity_id", activity_id}, {"users", users}};
    std::string s = msg.dump();
    std::string_view sv(s);
    for (auto *ws : it->second)
    {
        ws->send(sv, uWS::OpCode::TEXT);
    }
}

void WebSocketManager::send_locks_state(WsHandle ws, const std::string &activity_id)
{
    auto it = activity_locks_.find(activity_id);
    json locks = json::array();
    if (it != activity_locks_.end())
    {
        for (auto &[section, lock] : it->second)
        {
            locks.push_back({{"section", lock.section}, {"user", lock.user_name}});
        }
    }
    json msg = {{"event", "locks_state"}, {"activity_id", activity_id}, {"locks", locks}};
    std::string s = msg.dump();
    ws->send(std::string_view(s), uWS::OpCode::TEXT);
}

void WebSocketManager::cleanup_user_locks(WsHandle ws)
{
    auto *data = ws->getUserData();
    if (data->viewing_activity.empty())
        return;
    // handle_leave will clean up locks
}
