#include "MeasurementProcessor.hpp"

#include <algorithm>
#include <stdexcept>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace
{
// Keeps the result observable so -O2 cannot remove the busy loop.
std::atomic<std::uint64_t> cpuWorkSink{0};

void doCpuWorkFor(std::chrono::milliseconds duration)
{
    const auto deadline = Clock::now() + duration;
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    while (Clock::now() < deadline)
    {
        for (int i = 0; i < 2048; ++i)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
        }
    }
    cpuWorkSink.store(state, std::memory_order_relaxed);
}
} // namespace

void simulateProcessing(ProcessingMode mode, std::chrono::milliseconds duration)
{
    if (mode == ProcessingMode::Sleep)
    {
        std::this_thread::sleep_for(duration);
    }
    else
    {
        doCpuWorkFor(duration);
    }
}

std::chrono::milliseconds rampedProcessingTime(
    std::chrono::milliseconds base,
    std::chrono::milliseconds finalTime,
    std::chrono::milliseconds rampDuration,
    std::int64_t simulationTimeMs)
{
    if (rampDuration <= std::chrono::milliseconds::zero())
    {
        return base;
    }
    const double progress = std::clamp(
        static_cast<double>(simulationTimeMs) /
            static_cast<double>(rampDuration.count()),
        0.0, 1.0);
    const double ms = static_cast<double>(base.count()) +
                      progress * static_cast<double>((finalTime - base).count());
    return std::chrono::milliseconds(static_cast<std::int64_t>(ms + 0.5));
}

MeasurementProcessor::MeasurementProcessor(
    MeasurementBuffer& rawBuffer,
    MeasurementBuffer& preparedBuffer,
    std::chrono::milliseconds processingTime,
    ProcessingMode mode)
    : rawBuffer_(rawBuffer),
      preparedBuffer_(preparedBuffer),
      processingTime_(processingTime),
      mode_(mode)
{
    if (processingTime_ < std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Processing time must not be negative");
    }
}

MeasurementProcessor::~MeasurementProcessor()
{
    stop();
}

void MeasurementProcessor::setLoadRamp(
    std::chrono::milliseconds finalTime, std::chrono::milliseconds rampDuration)
{
    rampFinalTime_ = finalTime;
    rampDuration_ = rampDuration;
}

void MeasurementProcessor::start(Clock::time_point simulationStart)
{
    stopRequested_.store(false);
    worker_ = std::thread([this, simulationStart] { run(simulationStart); });
}

void MeasurementProcessor::stop()
{
    stopRequested_.store(true);
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void MeasurementProcessor::run(Clock::time_point simulationStart)
{
    std::this_thread::sleep_until(simulationStart);

    while (!stopRequested_.load())
    {
        const auto simulationTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - simulationStart).count();

        // Take the newest available raw sample; poll again if there is none.
        const auto raw = rawBuffer_.takeLatestUpTo(simulationTimeMs);
        if (!raw)
        {
            std::this_thread::sleep_for(1ms);
            continue;
        }

        simulateProcessing(
            mode_,
            rampedProcessingTime(processingTime_, rampFinalTime_,
                                 rampDuration_, simulationTimeMs));

        // Keep the source timestamp: ready time is not sample time.
        preparedBuffer_.push(*raw);
    }
}
