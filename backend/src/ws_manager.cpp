#include "ws_manager.hpp"
#include <cstdio>

void WebSocketManager::on_open(WsHandle ws) {
    clients_.insert(ws);
    printf("[WS] client connected  (total: %zu)\n", clients_.size());
}

void WebSocketManager::on_close(WsHandle ws) {
    clients_.erase(ws);
    printf("[WS] client disconnected (total: %zu)\n", clients_.size());
}

void WebSocketManager::broadcast(const std::string& message) {
    std::string_view sv(message);
    for (auto* ws : clients_) {
        ws->send(sv, uWS::OpCode::TEXT);
    }
}
