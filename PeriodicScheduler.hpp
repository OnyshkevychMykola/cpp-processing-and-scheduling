#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

struct TickContext
{
    std::size_t tickIndex;
    std::int64_t scheduledTimeMs;
    std::chrono::steady_clock::time_point experimentStart;
};

struct SchedulerTimingStats
{
    // Difference between actual start and the scheduled deadline of each
    // executed tick. This is NOT the same as publication lateness.
    double averageStartLatenessMs = 0.0;
    double maximumStartLatenessMs = 0.0;

    // For successive EXECUTED ticks:
    // |(actualStart[i] - actualStart[i-1])
    //   - (scheduledStart[i] - scheduledStart[i-1])|.
    // Gaps caused by skipped ticks are accounted for in the expected interval.
    std::size_t intervalSamples = 0;
    double averageAbsoluteIntervalJitterMs = 0.0;
    double maximumAbsoluteIntervalJitterMs = 0.0;
    double p95AbsoluteIntervalJitterMs = 0.0;
    double p99AbsoluteIntervalJitterMs = 0.0;
};

struct SchedulerStats
{
    double elapsedMs = 0.0;
    std::size_t executedTicks = 0;
    std::size_t skippedTicks = 0;
    std::size_t deadlineMisses = 0;
    SchedulerTimingStats timing;
};

class PeriodicScheduler
{
public:
    using TickCallback = std::function<void(const TickContext&)>;

    PeriodicScheduler(std::chrono::milliseconds tickInterval,
                      std::chrono::milliseconds duration);

    SchedulerStats run(const TickCallback& callback);
    SchedulerStats run(const TickCallback& callback,
                       std::chrono::steady_clock::time_point startTime);

private:
    std::chrono::milliseconds tickInterval_;
    std::chrono::milliseconds duration_;
};
