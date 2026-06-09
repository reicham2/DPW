#include "infra/cache.hpp"
#include "infra/app_config.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>
#include <unistd.h>

namespace
{
    std::string env_or(const char *key, const std::string &fallback = "")
    {
        const char *v = std::getenv(key);
        if (!v)
            return fallback;
        std::string out = trim_ascii(v);
        return out.empty() ? fallback : out;
    }

    std::string cache_version_key(const std::string &domain)
    {
        return "dpw:cache:ver:" + domain;
    }

    int cache_version_local_ttl_ms(Database &db)
    {
        return app_config::get_int_or(db, app_config::kCacheVersionLocalTtlMs, 1500, 0, 60000);
    }

    struct LocalVersionEntry
    {
        std::string value;
        std::chrono::steady_clock::time_point expires_at;
    };

    std::mutex g_local_versions_mu;
    std::unordered_map<std::string, LocalVersionEntry> g_local_versions;

    std::optional<std::string> local_cached_version_get(const std::string &domain, int ttl_ms)
    {
        if (ttl_ms <= 0)
            return std::nullopt;

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(g_local_versions_mu);
        auto it = g_local_versions.find(domain);
        if (it == g_local_versions.end())
            return std::nullopt;
        if (it->second.expires_at <= now)
        {
            g_local_versions.erase(it);
            return std::nullopt;
        }
        return it->second.value;
    }

    void local_cached_version_set(const std::string &domain, const std::string &value, int ttl_ms)
    {
        if (ttl_ms <= 0)
            return;

        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(g_local_versions_mu);
        g_local_versions[domain] = LocalVersionEntry{value, now + std::chrono::milliseconds(ttl_ms)};
    }

    void local_cached_version_erase(const std::string &domain)
    {
        std::lock_guard<std::mutex> lock(g_local_versions_mu);
        g_local_versions.erase(domain);
    }
}

struct RedisCache::RedisReply
{
    enum class Type
    {
        Simple,
        Bulk,
        Integer,
        Null,
        Error,
    };

    Type type{Type::Error};
    std::string text;
    long long integer{0};
};

RedisCache::RedisCache()
{
    host_ = env_or("REDIS_HOST", "");
    if (!host_.empty())
    {
        enabled_ = true;
        port_ = std::max(1, std::atoi(env_or("REDIS_PORT", "6379").c_str()));
        timeout_ms_ = std::max(100, std::atoi(env_or("REDIS_TIMEOUT_MS", "250").c_str()));
    }
}

RedisCache::~RedisCache()
{
    std::lock_guard<std::mutex> lock(mu_);
    close_socket_locked();
}

bool RedisCache::enabled() const
{
    return enabled_;
}

std::optional<std::string> RedisCache::get(const std::string &key)
{
    if (!enabled_)
        return std::nullopt;
    auto reply = command({"GET", key});
    if (!reply || reply->type == RedisReply::Type::Null)
        return std::nullopt;
    if (reply->type != RedisReply::Type::Bulk)
        return std::nullopt;
    return reply->text;
}

bool RedisCache::setex(const std::string &key, int ttl_seconds, const std::string &value)
{
    if (!enabled_)
        return false;
    auto reply = command({"SETEX", key, std::to_string(ttl_seconds), value});
    return reply && reply->type == RedisReply::Type::Simple && reply->text == "OK";
}

std::optional<long long> RedisCache::incr(const std::string &key)
{
    if (!enabled_)
        return std::nullopt;
    auto reply = command({"INCR", key});
    if (!reply || reply->type != RedisReply::Type::Integer)
        return std::nullopt;
    return reply->integer;
}

bool RedisCache::write_all(int fd, const std::string &buffer)
{
    size_t sent = 0;
    while (sent < buffer.size())
    {
        ssize_t n = send(fd, buffer.data() + sent, buffer.size() - sent, MSG_NOSIGNAL);
        if (n <= 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RedisCache::read_exact(int fd, size_t len, std::string &out)
{
    out.clear();
    out.reserve(len);
    while (out.size() < len)
    {
        char buf[1024];
        size_t needed = len - out.size();
        size_t chunk = std::min(needed, sizeof(buf));
        ssize_t n = recv(fd, buf, chunk, 0);
        if (n <= 0)
            return false;
        out.append(buf, static_cast<size_t>(n));
    }
    return true;
}

bool RedisCache::read_line(int fd, std::string &line)
{
    line.clear();
    char c = 0;
    while (true)
    {
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0)
            return false;
        if (c == '\r')
        {
            char lf = 0;
            if (recv(fd, &lf, 1, 0) <= 0 || lf != '\n')
                return false;
            break;
        }
        line.push_back(c);
    }
    return true;
}

std::optional<RedisCache::RedisReply> RedisCache::read_reply(int fd)
{
    char prefix = 0;
    if (recv(fd, &prefix, 1, 0) <= 0)
        return std::nullopt;

    RedisReply reply;
    std::string line;
    switch (prefix)
    {
    case '+':
        if (!read_line(fd, line))
            return std::nullopt;
        reply.type = RedisReply::Type::Simple;
        reply.text = std::move(line);
        return reply;
    case '-':
        if (!read_line(fd, line))
            return std::nullopt;
        reply.type = RedisReply::Type::Error;
        reply.text = std::move(line);
        return reply;
    case ':':
        if (!read_line(fd, line))
            return std::nullopt;
        reply.type = RedisReply::Type::Integer;
        reply.integer = std::atoll(line.c_str());
        return reply;
    case '$':
    {
        if (!read_line(fd, line))
            return std::nullopt;
        int len = std::atoi(line.c_str());
        if (len < 0)
        {
            reply.type = RedisReply::Type::Null;
            return reply;
        }
        std::string payload;
        if (!read_exact(fd, static_cast<size_t>(len) + 2, payload))
            return std::nullopt;
        if (payload.size() < 2 || payload[payload.size() - 2] != '\r' || payload[payload.size() - 1] != '\n')
            return std::nullopt;
        reply.type = RedisReply::Type::Bulk;
        reply.text.assign(payload.data(), static_cast<size_t>(len));
        return reply;
    }
    default:
        return std::nullopt;
    }
}

int RedisCache::connect_socket()
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = nullptr;
    std::string port_s = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), port_s.c_str(), &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (auto *p = res; p; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;

        struct timeval tv;
        tv.tv_sec = timeout_ms_ / 1000;
        tv.tv_usec = (timeout_ms_ % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

void RedisCache::close_socket_locked()
{
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
}

std::optional<RedisCache::RedisReply> RedisCache::command(const std::vector<std::string> &args)
{
    std::string payload;
    payload.reserve(128);
    payload += "*" + std::to_string(args.size()) + "\r\n";
    for (const auto &arg : args)
    {
        payload += "$" + std::to_string(arg.size()) + "\r\n";
        payload += arg;
        payload += "\r\n";
    }

    std::lock_guard<std::mutex> lock(mu_);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (fd_ < 0)
            fd_ = connect_socket();
        if (fd_ < 0)
            return std::nullopt;

        if (!write_all(fd_, payload))
        {
            close_socket_locked();
            continue;
        }

        auto reply = read_reply(fd_);
        if (!reply)
        {
            close_socket_locked();
            continue;
        }
        return reply;
    }

    return std::nullopt;
}

RedisCache &redis_cache()
{
    static RedisCache cache;
    return cache;
}

int response_cache_ttl_seconds()
{
    static int ttl = []
    {
        return std::max(5, std::atoi(env_or("DPW_CACHE_TTL_SECONDS", "30").c_str()));
    }();
    return ttl;
}

void cache_bump_version(Database &db, const std::string &domain)
{
    const int ttl_ms = cache_version_local_ttl_ms(db);
    auto v = redis_cache().incr(cache_version_key(domain));
    if (v)
    {
        local_cached_version_set(domain, std::to_string(*v), ttl_ms);
    }
    else
    {
        local_cached_version_erase(domain);
    }
}

std::string cache_key(Database &db, const std::string &domain, const std::string &scope)
{
    const int ttl_ms = cache_version_local_ttl_ms(db);
    auto local = local_cached_version_get(domain, ttl_ms);
    if (local)
        return "dpw:cache:" + domain + ":v" + *local + ":" + scope;

    auto v = redis_cache().get(cache_version_key(domain));
    std::string version = v ? *v : "0";
    local_cached_version_set(domain, version, ttl_ms);
    return "dpw:cache:" + domain + ":v" + version + ":" + scope;
}

std::string cache_user_scope(const TokenClaims &claims, const std::optional<UserRecord> &user)
{
    if (user)
    {
        std::string dept = user->department ? *user->department : "_none";
        return "u:" + user->id + ":r:" + user->role + ":d:" + dept;
    }
    return "oid:" + claims.oid;
}
