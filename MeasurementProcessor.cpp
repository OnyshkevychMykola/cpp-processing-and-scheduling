#include "MeasurementProcessor.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <thread>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace
{
// Prevent the compiler from removing the computational workload as unused.
std::atomic<std::uint64_t> cpuWorkSink{0};

// Keep the CPU busy for about "duration" of wall-clock time.
// This is NOT a measurement of actual CPU time or a replacement for real work.
void doCpuWorkFor(std::chrono::milliseconds duration)
{
    if (duration <= std::chrono::milliseconds::zero())
    {
        return;
    }

    const auto deadline = Clock::now() + duration;
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;

    do
    {
        // Small nontrivial computation. The state flows into an observable
        // atomic result so -O2 cannot discard the work.
        for (int i = 0; i < 2048; ++i)
        {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
        }
    }
    while (Clock::now() < deadline);

    cpuWorkSink.store(state, std::memory_order_relaxed);
}
} // namespace

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

void MeasurementProcessor::start(Clock::time_point simulationStart)
{
    if (worker_.joinable())
    {
        throw std::logic_error("Processor is already running");
    }

    stats_ = {};
    stopRequested_.store(false);
    worker_ = std::thread([this, simulationStart] {
        run(simulationStart);
    });
}

void MeasurementProcessor::stop()
{
    stopRequested_.store(true);
    if (worker_.joinable())
    {
        worker_.join();
    }
}

ProcessorStats MeasurementProcessor::stats() const
{
    return stats_;
}

void MeasurementProcessor::run(Clock::time_point simulationStart)
{
    std::this_thread::sleep_until(simulationStart);

    while (!stopRequested_.load())
    {
        const auto now = Clock::now();
        const auto simulationTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - simulationStart).count();

        // Discard stale raw samples and take the newest currently available.
        // Never wait for the producer while holding the buffer mutex.
        const auto rawMeasurement = rawBuffer_.takeLatestUpTo(simulationTimeMs);

        if (!rawMeasurement)
        {
            ++stats_.emptyBufferPolls;
            std::this_thread::sleep_for(1ms);
            continue;
        }

        const auto processingStart = Clock::now();

        // Select one of two processing modes; the rest of the pipeline is
        // identical so the scheduler's timing can be compared fairly.
        if (mode_ == ProcessingMode::Sleep)
        {
            std::this_thread::sleep_for(processingTime_);
        }
        else
        {
            doCpuWorkFor(processingTime_);
        }

        // Preserve the source timestamp. Ready time is not sample time.
        // A real processor could transform value here.
        preparedBuffer_.push(*rawMeasurement);

        const auto processingEnd = Clock::now();
        const double processingMs =
            std::chrono::duration<double, std::milli>(
                processingEnd - processingStart).count();

        ++stats_.processedSamples;
        stats_.totalProcessingMs += processingMs;
        stats_.maximumProcessingMs =
            std::max(stats_.maximumProcessingMs, processingMs);
    }
}
