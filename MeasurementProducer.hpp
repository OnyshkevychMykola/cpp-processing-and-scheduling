#pragma once

#include "MeasurementBuffer.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

class MeasurementProducer
{
public:
    MeasurementProducer(MeasurementBuffer& buffer,
                        std::chrono::milliseconds sampleInterval);
    ~MeasurementProducer();

    MeasurementProducer(const MeasurementProducer&) = delete;
    MeasurementProducer& operator=(const MeasurementProducer&) = delete;

    void start(std::chrono::steady_clock::time_point simulationStart);
    void stop();
    std::size_t producedSamples() const;

private:
    void run(std::chrono::steady_clock::time_point simulationStart);

    MeasurementBuffer& buffer_;
    std::chrono::milliseconds sampleInterval_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<std::size_t> producedSamples_{0};
};
