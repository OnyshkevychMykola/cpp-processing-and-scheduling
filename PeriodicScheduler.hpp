#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

struct TickContext
{
    std::size_t tickIndex;
    std::int64_t scheduledTimeMs;
};

// Calls a callback every `tickInterval` until `duration` has passed.
// If a callback runs past the next tick, the missed ticks are skipped.
class PeriodicScheduler
{
public:
    using TickCallback = std::function<void(const TickContext&)>;

    PeriodicScheduler(std::chrono::milliseconds tickInterval,
                      std::chrono::milliseconds duration);

    void run(const TickCallback& callback,
             std::chrono::steady_clock::time_point startTime);

private:
    std::chrono::milliseconds tickInterval_;
    std::chrono::milliseconds duration_;
};
