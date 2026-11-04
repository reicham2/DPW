#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>

class JwksCache {
public:
    std::string get(const std::string& tenant_id, const std::function<std::string()>& fetch);
    void invalidate(const std::string& tenant_id, const std::string& previous_document);

private:
    std::mutex mutex_;
    std::string tenant_id_;
    std::string document_;
    std::chrono::steady_clock::time_point fetched_at_{};
};
