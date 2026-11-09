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

    // Simulate delayed data delivery: with the given probability a sample is
    // delivered `delay` late. Its timestamp stays nominal, and later samples
    // queue up behind it (head-of-line blocking), so delays arrive as bursts.
    // Call before start().
    void setDeliveryDelay(double probability,
                          std::chrono::milliseconds delay,
                          unsigned seed = 1);

    void start(std::chrono::steady_clock::time_point simulationStart);
    void stop();
    std::size_t producedSamples() const;
    std::size_t delayedSamples() const;

private:
    void run(std::chrono::steady_clock::time_point simulationStart);

    MeasurementBuffer& buffer_;
    std::chrono::milliseconds sampleInterval_;
    std::thread worker_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<std::size_t> producedSamples_{0};
    std::atomic<std::size_t> delayedSamples_{0};
    double delayProbability_ = 0.0;
    std::chrono::milliseconds delay_{0};
    unsigned seed_ = 1;
};
