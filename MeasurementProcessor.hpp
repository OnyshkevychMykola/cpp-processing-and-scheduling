#pragma once

#include "MeasurementBuffer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

// Sleep: simulate waiting (almost no CPU load).
// CpuWork: use CPU actively for approximately the requested wall-clock time.
enum class ProcessingMode
{
    Sleep,
    CpuWork
};

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

    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    ProcessorStats stats_;
};
