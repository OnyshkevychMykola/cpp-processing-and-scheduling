#pragma once

#include "MeasurementBuffer.hpp"

#include <atomic>
#include <chrono>
#include <thread>

// Thread that pushes one measurement into a buffer every `sampleInterval`.
class MeasurementProducer
{
public:
    MeasurementProducer(MeasurementBuffer& buffer,
                        std::chrono::milliseconds sampleInterval);
    ~MeasurementProducer();

    MeasurementProducer(const MeasurementProducer&) = delete;
    MeasurementProducer& operator=(const MeasurementProducer&) = delete;

    // Simulate delayed data delivery: with the given probability a sample is
    // delivered `delay` late. Its timestamp stays nominal, and later samples
    // queue up behind it, so delays arrive as bursts. Call before start().
    void setDeliveryDelay(double probability,
                          std::chrono::milliseconds delay,
                          unsigned seed = 1);

    void start(std::chrono::steady_clock::time_point simulationStart);
    void stop();

private:
    void run(std::chrono::steady_clock::time_point simulationStart);

    MeasurementBuffer& buffer_;
    std::chrono::milliseconds sampleInterval_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    double delayProbability_ = 0.0;
    std::chrono::milliseconds delay_{0};
    unsigned seed_ = 1;
};
