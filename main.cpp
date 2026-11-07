#include "MeasurementBuffer.hpp"
#include "MeasurementProcessor.hpp"
#include "MeasurementProducer.hpp"
#include "PeriodicScheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

template <typename Duration>
double toMilliseconds(Duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

struct LatencyStats
{
    double totalMs = 0.0;
    double maximumMs = 0.0;
    std::size_t count = 0;

    void add(double milliseconds)
    {
        totalMs += milliseconds;
        maximumMs = std::max(maximumMs, milliseconds);
        ++count;
    }

    double averageMs() const
    {
        return count ? totalMs / static_cast<double>(count) : 0.0;
    }
};

struct Publication
{
    std::size_t tickIndex;
    int value;
    std::int64_t sampleTimestampMs;
    std::int64_t scheduledTimeMs;
    double actualTimeMs;
    double latenessMs;
    double dataAgeMs;
    bool reused;
};

int main(int argc, char* argv[])
{
    // Usage: ./main sleep | ./main cpu
    ProcessingMode mode = ProcessingMode::Sleep;
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [sleep|cpu]\n";
        return 1;
    }
    if (argc == 2)
    {
        const std::string_view requestedMode{argv[1]};
        if (requestedMode == "cpu")
        {
            mode = ProcessingMode::CpuWork;
        }
        else if (requestedMode != "sleep")
        {
            std::cerr << "Unknown mode: " << requestedMode
                      << "\nUsage: " << argv[0] << " [sleep|cpu]\n";
            return 1;
        }
    }

    // Approach B: preprocessing is independent from the scheduler.
    constexpr auto sampleInterval = 2ms;        // Producer: 500 Hz
    constexpr auto tickInterval = 4ms;          // Publisher: 250 Hz
    constexpr auto processingTime = 12ms;       // Slow processor
    constexpr auto testDuration = 2s;

    MeasurementBuffer rawBuffer(4);
    MeasurementBuffer preparedBuffer(8);

    MeasurementProducer producer(rawBuffer, sampleInterval);
    MeasurementProcessor processor(rawBuffer, preparedBuffer, processingTime, mode);
    PeriodicScheduler scheduler(tickInterval, testDuration);

    // Producer, processor and scheduler share the same virtual time origin.
    const auto sharedStart = Clock::now() + 100ms;
    producer.start(sharedStart);
    processor.start(sharedStart);

    std::vector<Publication> publications;
    publications.reserve(500);

    std::optional<Measurement> lastPublishedData;
    std::size_t ticksWithoutPreparedData = 0;
    std::size_t reusedPublications = 0;
    LatencyStats publicationLateness;
    LatencyStats dataAge;

    const auto schedulerStats = scheduler.run(
        [&](const TickContext& tick)
        {
            // Non-blocking: do not wait for the processor to finish.
            const auto newlyPrepared =
                preparedBuffer.takeLatestUpTo(tick.scheduledTimeMs);

            if (newlyPrepared)
            {
                lastPublishedData = *newlyPrepared;
            }
            else if (!lastPublishedData)
            {
                // At startup no sample has completed its 12ms processing.
                ++ticksWithoutPreparedData;
                return;
            }

            // Our demo policy: publish on each tick, repeating the last
            // prepared measurement when there is no new one.
            const bool reused = !newlyPrepared.has_value();
            if (reused)
            {
                ++reusedPublications;
            }

            const auto publicationTime = Clock::now();
            const auto scheduledTime =
                tick.experimentStart +
                std::chrono::milliseconds(tick.scheduledTimeMs);
            const auto sampleTime =
                tick.experimentStart +
                std::chrono::milliseconds(lastPublishedData->timestampMs);

            const double latenessMs =
                toMilliseconds(publicationTime - scheduledTime);
            const double dataAgeMs =
                toMilliseconds(publicationTime - sampleTime);

            publicationLateness.add(latenessMs);
            dataAge.add(dataAgeMs);

            publications.push_back({
                tick.tickIndex,
                lastPublishedData->value,
                lastPublishedData->timestampMs,
                tick.scheduledTimeMs,
                toMilliseconds(publicationTime - tick.experimentStart),
                latenessMs,
                dataAgeMs,
                reused
            });
        },
        sharedStart);

    // Stop independent workers before reading their final statistics.
    producer.stop();
    processor.stop();
    const ProcessorStats processorStats = processor.stats();

    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < std::min<std::size_t>(publications.size(), 15); ++i)
    {
        const auto& p = publications[i];
        std::cout << "Tick " << p.tickIndex
                  << " | Value " << p.value
                  << " | Sample " << p.sampleTimestampMs << " ms"
                  << " | Scheduled " << p.scheduledTimeMs << " ms"
                  << " | Actual " << p.actualTimeMs << " ms"
                  << " | Lateness " << p.latenessMs << " ms"
                  << " | Data age " << p.dataAgeMs << " ms"
                  << " | " << (p.reused ? "REUSED" : "NEW") << '\n';
    }

    std::cout << "\n========== STATISTICS (APPROACH B) ==========\n"
              << "Processing mode: "
              << (mode == ProcessingMode::CpuWork ? "CpuWork" : "Sleep") << '\n'
              << "Produced samples: " << producer.producedSamples() << '\n'
              << "Processed samples: " << processorStats.processedSamples << '\n'
              << "Published events: " << publications.size() << '\n'
              << "Reused publications: " << reusedPublications << '\n'
              << "Ticks without prepared data: " << ticksWithoutPreparedData << '\n'
              << "Executed ticks: " << schedulerStats.executedTicks << '\n'
              << "Skipped ticks: " << schedulerStats.skippedTicks << '\n'
              << "Deadline misses (scheduler callback): "
              << schedulerStats.deadlineMisses << '\n'
              << "Raw buffer overflow drops: " << rawBuffer.overflowDrops() << '\n'
              << "Raw buffer stale drops: " << rawBuffer.staleDrops() << '\n'
              << "Prepared buffer overflow drops: "
              << preparedBuffer.overflowDrops() << '\n'
              << "Prepared buffer stale drops: "
              << preparedBuffer.staleDrops() << '\n'
              << "Raw buffer size: " << rawBuffer.size() << '\n'
              << "Prepared buffer size: " << preparedBuffer.size() << '\n'
              << "Elapsed time: " << schedulerStats.elapsedMs << " ms\n\n"
              << "========== SCHEDULER TIMING ==========\n"
              << "Tick start lateness: avg "
              << schedulerStats.timing.averageStartLatenessMs
              << " ms | max " << schedulerStats.timing.maximumStartLatenessMs
              << " ms\n"
              << "Interval jitter (absolute error): avg "
              << schedulerStats.timing.averageAbsoluteIntervalJitterMs
              << " ms | p95 " << schedulerStats.timing.p95AbsoluteIntervalJitterMs
              << " ms | p99 " << schedulerStats.timing.p99AbsoluteIntervalJitterMs
              << " ms | max " << schedulerStats.timing.maximumAbsoluteIntervalJitterMs
              << " ms\n"
              << "Measured intervals: " << schedulerStats.timing.intervalSamples
              << "\n\n";

    if (processorStats.processedSamples)
    {
        std::cout << "Processing latency: avg "
                  << processorStats.averageProcessingMs()
                  << " ms | max " << processorStats.maximumProcessingMs
                  << " ms\n";
    }
    if (publicationLateness.count)
    {
        std::cout << "Publication lateness: avg "
                  << publicationLateness.averageMs()
                  << " ms | max " << publicationLateness.maximumMs
                  << " ms\n"
                  << "Data age: avg " << dataAge.averageMs()
                  << " ms | max " << dataAge.maximumMs
                  << " ms\n";
    }
}
