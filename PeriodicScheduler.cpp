#include "PeriodicScheduler.hpp"

#include <stdexcept>
#include <thread>

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

void PeriodicScheduler::run(
    const TickCallback& callback,
    std::chrono::steady_clock::time_point startTime)
{
    using Clock = std::chrono::steady_clock;

    const auto testEnd = startTime + duration_;
    auto nextTick = startTime;

    while (nextTick < testEnd)
    {
        // Sleep to the absolute deadline, so errors do not accumulate.
        std::this_thread::sleep_until(nextTick);

        const auto tickIndex = static_cast<std::size_t>(
            (nextTick - startTime) / tickInterval_);
        const auto scheduledTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                nextTick - startTime).count();

        callback({tickIndex, scheduledTimeMs});

        // Next tick; skip any tick whose time has already passed.
        nextTick += tickInterval_;
        const auto now = Clock::now();
        while (nextTick <= now && nextTick < testEnd)
        {
            nextTick += tickInterval_;
        }
    }
}
