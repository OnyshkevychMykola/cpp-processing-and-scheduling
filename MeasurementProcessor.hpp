#pragma once

#include "MeasurementBuffer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

// Sleep: simulate waiting (almost no CPU load).
// CpuWork: use CPU actively for approximately the requested wall-clock time.
enum class ProcessingMode
{
    Sleep,
    CpuWork
};

// Simulate one unit of processing work taking about `duration` wall-clock time
// (sleep or busy CPU, depending on `mode`). Shared by both approaches.
void simulateProcessing(ProcessingMode mode, std::chrono::milliseconds duration);

// Processing time at simulation time `simulationTimeMs`: `base` if
// `rampDuration` is zero, otherwise linearly ramped from `base` to `finalTime`.
std::chrono::milliseconds rampedProcessingTime(
    std::chrono::milliseconds base,
    std::chrono::milliseconds finalTime,
    std::chrono::milliseconds rampDuration,
    std::int64_t simulationTimeMs);

struct ProcessorStats
{
    std::size_t processedSamples = 0;
    std::size_t emptyBufferPolls = 0;
    double totalProcessingMs = 0.0;
    double maximumProcessingMs = 0.0;

    double averageProcessingMs() const
    {
        return processedSamples == 0
            ? 0.0
            : totalProcessingMs / static_cast<double>(processedSamples);
    }
};

class MeasurementProcessor
{
public:
    MeasurementProcessor(MeasurementBuffer& rawBuffer,
                         MeasurementBuffer& preparedBuffer,
                         std::chrono::milliseconds processingTime,
                         ProcessingMode mode = ProcessingMode::Sleep);
    ~MeasurementProcessor();

    MeasurementProcessor(const MeasurementProcessor&) = delete;
    MeasurementProcessor& operator=(const MeasurementProcessor&) = delete;

    // Increasing workload: processing time grows linearly from the initial
    // value to `finalTime` over `rampDuration`, then stays at `finalTime`.
    // Call before start().
    void setLoadRamp(std::chrono::milliseconds finalTime,
                     std::chrono::milliseconds rampDuration);

    void start(std::chrono::steady_clock::time_point simulationStart);
    void stop();

    // Call only after stop() has joined the processing thread.
    ProcessorStats stats() const;

private:
    void run(std::chrono::steady_clock::time_point simulationStart);

    MeasurementBuffer& rawBuffer_;
    MeasurementBuffer& preparedBuffer_;
    std::chrono::milliseconds processingTime_;
    ProcessingMode mode_;
    std::chrono::milliseconds rampFinalTime_{0};
    std::chrono::milliseconds rampDuration_{0};

    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    ProcessorStats stats_;
};
