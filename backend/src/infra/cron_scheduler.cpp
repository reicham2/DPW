#include "infra/cron_scheduler.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace
{
    long long wall_now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    }

    nlohmann::json timestamp(long long value)
    {
        return value == 0 ? nlohmann::json(nullptr) : nlohmann::json(value);
    }
}

CronScheduler::CronScheduler(Now now) : now_(std::move(now)), started_at_(wall_now()) {}
CronScheduler::~CronScheduler() { stop(); }

void CronScheduler::add(const std::string &id, const std::string &name, int interval_minutes,
                        std::function<void()> task)
{
    static const std::vector<int> intervals{1, 5, 15, 30, 60, 1440};
    std::lock_guard lock(mutex_);
    if (active_ || id.empty() || !task ||
        std::find(intervals.begin(), intervals.end(), interval_minutes) == intervals.end() ||
        std::any_of(jobs_.begin(), jobs_.end(), [&](const Job &job) { return job.id == id; }))
        throw std::invalid_argument("Invalid cron registration");
    jobs_.push_back(Job{id, name, interval_minutes, std::move(task), now_()});
}

void CronScheduler::start()
{
    std::lock_guard lock(mutex_);
    if (active_)
        return;
    stopping_ = false;
    worker_ = std::thread([this]
    {
        for (;;)
        {
            {
                std::unique_lock lock(mutex_);
                if (stopping_)
                    break;
            }
            run_due();
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_; });
        }
    });
    active_ = true;
}

void CronScheduler::stop()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
    std::lock_guard lock(mutex_);
    active_ = false;
}

void CronScheduler::run_due()
{
    // One dispatch lane prevents overlapping jobs and keeps callbacks off the HTTP loop.
    std::unique_lock dispatch(dispatch_mutex_, std::try_to_lock);
    if (!dispatch.owns_lock())
        return;
    size_t count;
    {
        std::lock_guard lock(mutex_);
        count = jobs_.size();
    }
    for (size_t i = 0; i < count; ++i)
    {
        std::function<void()> task;
        std::string id;
        {
            std::lock_guard lock(mutex_);
            auto &job = jobs_[i];
            auto now = now_();
            if (stopping_)
                return;
            if (!job.enabled || now < job.next)
                continue;
            auto interval = std::chrono::minutes(job.interval_minutes);
            auto skipped = (now - job.next) / interval;
            job.missed += static_cast<unsigned long long>(skipped);
            job.next += interval * (skipped + 1);
            job.running = true;
            job.started = now;
            job.last_started = wall_now();
            ++job.runs;
            task = job.task;
            id = job.id;
        }
        bool failed = false;
        try
        {
            task();
        }
        catch (const std::exception &e)
        {
            failed = true;
            std::fprintf(stderr, "[cron] %s failed: %s\n", id.c_str(), e.what());
        }
        catch (...)
        {
            failed = true;
            std::fprintf(stderr, "[cron] %s failed: unknown exception\n", id.c_str());
        }
        {
            std::lock_guard lock(mutex_);
            auto &job = jobs_[i];
            job.running = false;
            job.failed = failed;
            job.last_finished = wall_now();
            job.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_() - job.started).count();
            if (failed)
                ++job.failures;
            else
                job.last_success = job.last_finished;
        }
    }
}

bool CronScheduler::set_enabled(const std::string &id, bool enabled)
{
    std::lock_guard lock(mutex_);
    for (auto &job : jobs_)
    {
        if (job.id != id)
            continue;
        if (job.enabled != enabled)
        {
            job.enabled = enabled;
            if (enabled)
                job.next = now_();
        }
        return true;
    }
    return false;
}

nlohmann::json CronScheduler::status() const
{
    std::lock_guard lock(mutex_);
    auto jobs = nlohmann::json::array();
    const auto now = now_();
    const auto wall = wall_now();
    for (const auto &job : jobs_)
    {
        const auto interval = std::chrono::minutes(job.interval_minutes);
        const bool overdue = job.enabled && now >= job.next + std::chrono::seconds(5);
        const auto pending_missed = job.enabled && now >= job.next
            ? static_cast<unsigned long long>((now - job.next) / interval) : 0;
        const std::string health = overdue ? "overdue" : job.running ? "running" :
            !job.enabled ? "paused" : job.failed ? "failed" : job.runs == 0 ? "waiting" : "ok";
        jobs.push_back({
            {"id", job.id}, {"name", job.name}, {"interval_minutes", job.interval_minutes},
            {"enabled", job.enabled}, {"running", job.running}, {"health", health},
            {"next_run", job.enabled ? nlohmann::json(wall + std::chrono::duration_cast<std::chrono::milliseconds>(job.next - now).count()) : nlohmann::json(nullptr)},
            {"last_started", timestamp(job.last_started)}, {"last_finished", timestamp(job.last_finished)},
            {"last_success", timestamp(job.last_success)}, {"duration_ms", job.duration_ms},
            {"runs", job.runs}, {"failures", job.failures}, {"missed_runs", job.missed + pending_missed},
            {"last_error", job.failed ? nlohmann::json("Job fehlgeschlagen. Details in den Backend-Logs.") : nlohmann::json(nullptr)}
        });
    }
    return {{"active", active_ && !stopping_}, {"started_at", started_at_},
            {"intervals_minutes", {1, 5, 15, 30, 60, 1440}}, {"jobs", jobs}};
}
