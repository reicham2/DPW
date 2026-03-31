#pragma once
#include <unordered_set>
#include <string>
#include "App.h"

struct WsUserData {};

using WsHandle = uWS::WebSocket<false, true, WsUserData>*;

class WebSocketManager {
public:
    void on_open(WsHandle ws);
    void on_close(WsHandle ws);
    void broadcast(const std::string& message);
    size_t connection_count() const { return clients_.size(); }

private:
    std::unordered_set<WsHandle> clients_;
};
