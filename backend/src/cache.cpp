#include "cache.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
    std::string trim_ascii(std::string s)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c)
                                        { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c)
                             { return !std::isspace(c); })
                    .base(),
                s.end());
        return s;
    }

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
        ssize_t n = send(fd, buffer.data() + sent, buffer.size() - sent, 0);
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

std::optional<RedisCache::RedisReply> RedisCache::command(const std::vector<std::string> &args)
{
    int fd = connect_socket();
    if (fd < 0)
        return std::nullopt;

    std::string payload;
    payload.reserve(128);
    payload += "*" + std::to_string(args.size()) + "\r\n";
    for (const auto &arg : args)
    {
        payload += "$" + std::to_string(arg.size()) + "\r\n";
        payload += arg;
        payload += "\r\n";
    }

    if (!write_all(fd, payload))
    {
        close(fd);
        return std::nullopt;
    }

    auto reply = read_reply(fd);
    close(fd);
    return reply;
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

void cache_bump_version(const std::string &domain)
{
    (void)redis_cache().incr(cache_version_key(domain));
}

std::string cache_key(const std::string &domain, const std::string &scope)
{
    auto v = redis_cache().get(cache_version_key(domain));
    return "dpw:cache:" + domain + ":v" + (v ? *v : "0") + ":" + scope;
}

std::string cache_user_scope(const TokenClaims &claims, const std::optional<UserRecord> &user)
{
    if (user)
        return "u:" + user->id;
    return "oid:" + claims.oid;
}