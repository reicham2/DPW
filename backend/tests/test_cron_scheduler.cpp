#include "test_harness.hpp"
#include "infra/cron_scheduler.hpp"
#include <atomic>
#include <future>

TEST(cron_all_intervals_and_fixed_cadence)
{
    auto now = CronScheduler::Clock::time_point{};
    CronScheduler cron([&] { return now; });
    const std::vector<int> intervals{1, 5, 15, 30, 60, 1440};
    std::vector<int> runs(intervals.size());
    for (size_t i = 0; i < intervals.size(); ++i)
        cron.add(std::to_string(i), "test", intervals[i], [&, i] { ++runs[i]; });
    ASSERT_EQ(cron.status()["jobs"][0]["health"], "waiting");
    cron.run_due();
    for (int run : runs) ASSERT_EQ(run, 1);
    now += std::chrono::seconds(59);
    cron.run_due();
    for (int run : runs) ASSERT_EQ(run, 1);
    for (int minute = 1; minute <= 1440; ++minute)
    {
        now = CronScheduler::Clock::time_point{} + std::chrono::minutes(minute);
        cron.run_due();
        for (size_t i = 0; i < intervals.size(); ++i)
            ASSERT_EQ(runs[i], 1 + minute / intervals[i]);
    }
    ASSERT_EQ(cron.status()["jobs"][0]["missed_runs"], 0);
}

TEST(cron_failure_isolation_and_recovery)
{
    auto now = CronScheduler::Clock::time_point{};
    CronScheduler cron([&] { return now; });
    bool fail = true;
    int other = 0;
    cron.add("failure", "test", 1, [&] { if (fail) throw std::runtime_error("expected failure"); });
    cron.add("other", "test", 1, [&] { ++other; });
    cron.run_due();
    ASSERT_EQ(other, 1);
    ASSERT_EQ(cron.status()["jobs"][0]["health"], "failed");
    ASSERT_TRUE(cron.status()["jobs"][0]["last_success"].is_null());
    ASSERT_EQ(cron.status()["jobs"][0]["failures"], 1);
    fail = false;
    now += std::chrono::minutes(1);
    cron.run_due();
    ASSERT_EQ(cron.status()["jobs"][0]["health"], "ok");
    ASSERT_TRUE(cron.status()["jobs"][0]["last_error"].is_null());
    ASSERT_FALSE(cron.status()["jobs"][0]["last_success"].is_null());
    ASSERT_EQ(cron.status()["jobs"][0]["failures"], 1);
}

TEST(cron_missed_runs_and_overdue)
{
    auto now = CronScheduler::Clock::time_point{};
    CronScheduler cron([&] { return now; });
    int runs = 0;
    cron.add("test", "test", 5, [&] { ++runs; });
    now += std::chrono::minutes(16);
    ASSERT_EQ(cron.status()["jobs"][0]["health"], "overdue");
    ASSERT_EQ(cron.status()["jobs"][0]["missed_runs"], 3);
    cron.run_due();
    ASSERT_EQ(runs, 1);
    ASSERT_EQ(cron.status()["jobs"][0]["missed_runs"], 3);
    now += std::chrono::minutes(4);
    cron.run_due();
    ASSERT_EQ(runs, 2);
}

TEST(cron_pause_resume_without_replay)
{
    auto now = CronScheduler::Clock::time_point{};
    CronScheduler cron([&] { return now; });
    int runs = 0;
    cron.add("test", "test", 1, [&] { ++runs; });
    ASSERT_FALSE(cron.set_enabled("missing", false));
    ASSERT_TRUE(cron.set_enabled("test", false));
    now += std::chrono::hours(2);
    cron.run_due();
    ASSERT_EQ(runs, 0);
    ASSERT_EQ(cron.status()["jobs"][0]["health"], "paused");
    ASSERT_TRUE(cron.status()["jobs"][0]["next_run"].is_null());
    cron.set_enabled("test", true);
    cron.run_due();
    ASSERT_EQ(runs, 1);
    ASSERT_EQ(cron.status()["jobs"][0]["missed_runs"], 0);
}

TEST(cron_running_overlap_and_slow_job_health)
{
    auto now = CronScheduler::Clock::time_point{};
    CronScheduler cron([&] { return now; });
    int runs = 0;
    cron.add("test", "test", 1, [&]
    {
        ++runs;
        ASSERT_EQ(cron.status()["jobs"][0]["health"], "running");
        now += std::chrono::seconds(125);
        ASSERT_EQ(cron.status()["jobs"][0]["health"], "overdue");
        ASSERT_TRUE(cron.status()["jobs"][0]["running"].get<bool>());
        std::thread concurrent([&] { cron.run_due(); });
        concurrent.join();
        ASSERT_EQ(runs, 1);
        cron.set_enabled("test", false);
    });
    cron.run_due();
    ASSERT_EQ(runs, 1);
    ASSERT_EQ(cron.status()["jobs"][0]["duration_ms"], 125000);
    ASSERT_EQ(cron.status()["jobs"][0]["health"], "paused");
}

TEST(cron_registration_validation)
{
    CronScheduler cron;
    cron.add("test", "test", 1, [] {});
    for (int interval : {0, 2, 1441})
    {
        bool rejected = false;
        try { cron.add("invalid", "invalid", interval, [] {}); }
        catch (const std::invalid_argument &) { rejected = true; }
        ASSERT_TRUE(rejected);
    }
    bool duplicate = false;
    try { cron.add("test", "test", 5, [] {}); }
    catch (const std::invalid_argument &) { duplicate = true; }
    ASSERT_TRUE(duplicate);
}

TEST(cron_lifecycle_start_stop)
{
    CronScheduler cron;
    std::atomic<int> runs{0};
    cron.add("test", "test", 1, [&] { ++runs; });
    ASSERT_FALSE(cron.status()["active"].get<bool>());
    cron.start();
    cron.start();
    ASSERT_TRUE(cron.status()["active"].get<bool>());
    for (int i = 0; i < 100 && runs == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    cron.stop();
    ASSERT_EQ(runs.load(), 1);
    ASSERT_FALSE(cron.status()["active"].get<bool>());
    cron.run_due();
    ASSERT_EQ(runs.load(), 1);
    cron.stop();
}

TEST(cron_shutdown_waits_for_running_job_and_skips_pending_jobs)
{
    CronScheduler cron;
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future();
    std::atomic<int> pending_runs{0};
    std::atomic<bool> stopped{false};
    cron.add("running", "test", 1, [&]
    {
        entered.set_value();
        released.wait();
    });
    cron.add("pending", "test", 1, [&] { ++pending_runs; });
    cron.start();
    entered.get_future().wait();
    std::thread shutdown([&]
    {
        cron.stop();
        stopped = true;
    });
    for (int i = 0; i < 100 && cron.status()["active"].get<bool>(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool returned_early = stopped;
    release.set_value();
    shutdown.join();
    ASSERT_FALSE(returned_early);
    ASSERT_TRUE(stopped.load());
    ASSERT_EQ(pending_runs.load(), 0);
}
