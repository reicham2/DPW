#pragma once

#include <optional>
#include <string>
#include <vector>
#include "auth.hpp"
#include "db.hpp"

class RedisCache
{
public:
    RedisCache();

    bool enabled() const;
    std::optional<std::string> get(const std::string &key);
    bool setex(const std::string &key, int ttl_seconds, const std::string &value);
    std::optional<long long> incr(const std::string &key);

private:
    struct RedisReply;

    bool write_all(int fd, const std::string &buffer);
    bool read_exact(int fd, size_t len, std::string &out);
    bool read_line(int fd, std::string &line);
    std::optional<RedisReply> read_reply(int fd);
    int connect_socket();
    std::optional<RedisReply> command(const std::vector<std::string> &args);

    bool enabled_{false};
    std::string host_;
    int port_{6379};
    int timeout_ms_{250};
};

RedisCache &redis_cache();
int response_cache_ttl_seconds();
void cache_bump_version(const std::string &domain);
std::string cache_key(const std::string &domain, const std::string &scope);
std::string cache_user_scope(const TokenClaims &claims, const std::optional<UserRecord> &user);