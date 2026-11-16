#pragma once

#include "MeasurementBuffer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

// Sleep: simulate waiting (almost no CPU load).
// CpuWork: keep the CPU busy for about the requested wall-clock time.
enum class ProcessingMode
{
    Sleep,
    CpuWork
};

// One unit of simulated processing work taking about `duration`.
// Shared by both approaches.
void simulateProcessing(ProcessingMode mode, std::chrono::milliseconds duration);

// Processing time at simulation time `simulationTimeMs`: `base` if
// `rampDuration` is zero, otherwise linearly ramped from `base` to `finalTime`.
std::chrono::milliseconds rampedProcessingTime(
    std::chrono::milliseconds base,
    std::chrono::milliseconds finalTime,
    std::chrono::milliseconds rampDuration,
    std::int64_t simulationTimeMs);

// Thread that takes the newest raw measurement, "processes" it and pushes it
// into the prepared buffer (approach B).
class MeasurementProcessor
{
public:
    MeasurementProcessor(MeasurementBuffer& rawBuffer,
                         MeasurementBuffer& preparedBuffer,
                         std::chrono::milliseconds processingTime,
                         ProcessingMode mode);
    ~MeasurementProcessor();

    MeasurementProcessor(const MeasurementProcessor&) = delete;
    MeasurementProcessor& operator=(const MeasurementProcessor&) = delete;

    // Processing time grows linearly to `finalTime` over `rampDuration`.
    // Call before start().
    void setLoadRamp(std::chrono::milliseconds finalTime,
                     std::chrono::milliseconds rampDuration);

    void start(std::chrono::steady_clock::time_point simulationStart);
    void stop();

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
};
