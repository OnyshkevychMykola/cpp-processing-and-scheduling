#include "PeriodicScheduler.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

PeriodicScheduler::PeriodicScheduler(
    std::chrono::milliseconds tickInterval,
    std::chrono::milliseconds duration)
    : tickInterval_(tickInterval), duration_(duration)
{
    if (tickInterval_ <= std::chrono::milliseconds::zero() ||
        duration_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Tick interval and duration must be positive");
    }
}

SchedulerStats PeriodicScheduler::run(const TickCallback& callback)
{
    return run(callback, std::chrono::steady_clock::now());
}

SchedulerStats PeriodicScheduler::run(
    const TickCallback& callback,
    std::chrono::steady_clock::time_point startTime)
{
    using Clock = std::chrono::steady_clock;

    SchedulerStats stats;
    const auto testEnd = startTime + duration_;
    auto nextTick = startTime;

    // Collect timing without logging in the hot path. Reserve memory so
    // vector growth does not periodically interrupt the scheduler.
    std::vector<double> absoluteIntervalJitterMs;
    absoluteIntervalJitterMs.reserve(
        static_cast<std::size_t>(duration_ / tickInterval_));

    std::optional<Clock::time_point> previousActualStart;
    std::optional<Clock::time_point> previousScheduledStart;
    double totalStartLatenessMs = 0.0;
    double totalAbsoluteIntervalJitterMs = 0.0;

    while (nextTick < testEnd)
    {
        std::this_thread::sleep_until(nextTick);
        const auto actualStart = Clock::now();

        if (actualStart >= testEnd)
        {
            break;
        }

        if (actualStart >= nextTick + tickInterval_)
        {
            while (nextTick <= actualStart && nextTick < testEnd)
            {
                nextTick += tickInterval_;
                ++stats.skippedTicks;
            }
            continue;
        }

        // Measure actual tick START before invoking the callback. A skipped
        // tick is not a sample: only executed ticks enter these statistics.
        const double startLatenessMs =
            std::chrono::duration<double, std::milli>(
                actualStart - nextTick).count();
        totalStartLatenessMs += startLatenessMs;
        stats.timing.maximumStartLatenessMs = std::max(
            stats.timing.maximumStartLatenessMs, startLatenessMs);

        if (previousActualStart && previousScheduledStart)
        {
            const double actualIntervalMs =
                std::chrono::duration<double, std::milli>(
                    actualStart - *previousActualStart).count();
            const double expectedIntervalMs =
                std::chrono::duration<double, std::milli>(
                    nextTick - *previousScheduledStart).count();

            const double absoluteJitterMs =
                std::abs(actualIntervalMs - expectedIntervalMs);
            absoluteIntervalJitterMs.push_back(absoluteJitterMs);
            totalAbsoluteIntervalJitterMs += absoluteJitterMs;
            stats.timing.maximumAbsoluteIntervalJitterMs = std::max(
                stats.timing.maximumAbsoluteIntervalJitterMs, absoluteJitterMs);
        }

        previousActualStart = actualStart;
        previousScheduledStart = nextTick;

        const auto tickIndex = static_cast<std::size_t>(
            (nextTick - startTime) / tickInterval_);

        TickContext context{
            tickIndex,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                nextTick - startTime).count(),
            startTime
        };

        callback(context);
        const auto processingEnd = Clock::now();

        if (processingEnd > nextTick + tickInterval_)
        {
            ++stats.deadlineMisses;
        }
        ++stats.executedTicks;
        nextTick += tickInterval_;
        while (nextTick <= processingEnd && nextTick < testEnd)
        {
            nextTick += tickInterval_;
            ++stats.skippedTicks;
        }
    }

    stats.elapsedMs = std::chrono::duration<double, std::milli>(
        Clock::now() - startTime).count();

    if (stats.executedTicks > 0)
    {
        stats.timing.averageStartLatenessMs =
            totalStartLatenessMs / static_cast<double>(stats.executedTicks);
    }

    if (!absoluteIntervalJitterMs.empty())
    {
        std::sort(absoluteIntervalJitterMs.begin(), absoluteIntervalJitterMs.end());
        const auto count = absoluteIntervalJitterMs.size();
        stats.timing.intervalSamples = count;
        stats.timing.averageAbsoluteIntervalJitterMs =
            totalAbsoluteIntervalJitterMs / static_cast<double>(count);

        // Nearest-rank percentiles, e.g. P95 is the 95th percentile value.
        const auto percentile = [&](double fraction) {
            const auto rank = static_cast<std::size_t>(
                std::ceil(fraction * static_cast<double>(count)));
            return absoluteIntervalJitterMs.at(std::max<std::size_t>(1, rank) - 1);
        };
        stats.timing.p95AbsoluteIntervalJitterMs = percentile(0.95);
        stats.timing.p99AbsoluteIntervalJitterMs = percentile(0.99);
    }

    return stats;
}
