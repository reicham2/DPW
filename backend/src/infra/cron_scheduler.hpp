#pragma once

#include "json.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CronScheduler
{
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    explicit CronScheduler(Now now = [] { return Clock::now(); });
    ~CronScheduler();
    CronScheduler(const CronScheduler &) = delete;
    CronScheduler &operator=(const CronScheduler &) = delete;

    void add(const std::string &id, const std::string &name, int interval_minutes,
             std::function<void()> task);
    void start();
    void stop();
    void run_due();
    bool set_enabled(const std::string &id, bool enabled);
    nlohmann::json status() const;

private:
    struct Job
    {
        std::string id;
        std::string name;
        int interval_minutes;
        std::function<void()> task;
        Clock::time_point next;
        Clock::time_point started{};
        bool enabled{true};
        bool running{false};
        long long last_started{0};
        long long last_finished{0};
        long long last_success{0};
        long long duration_ms{0};
        unsigned long long runs{0};
        unsigned long long failures{0};
        unsigned long long missed{0};
        bool failed{false};
    };

    Now now_;
    mutable std::mutex mutex_;
    std::mutex dispatch_mutex_;
    std::condition_variable wake_;
    std::vector<Job> jobs_;
    std::thread worker_;
    bool stopping_{false};
    bool active_{false};
    long long started_at_;
};
