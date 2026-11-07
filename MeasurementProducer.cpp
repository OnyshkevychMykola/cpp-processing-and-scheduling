#include "MeasurementProducer.hpp"

#include <cstdint>
#include <stdexcept>
#include <thread>

MeasurementProducer::MeasurementProducer(
    MeasurementBuffer& buffer,
    std::chrono::milliseconds sampleInterval)
    : buffer_(buffer), sampleInterval_(sampleInterval)
{
    if (sampleInterval_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Sample interval must be positive");
    }
}

MeasurementProducer::~MeasurementProducer()
{
    stop();
}

void MeasurementProducer::start(
    std::chrono::steady_clock::time_point simulationStart)
{
    if (worker_.joinable())
    {
        throw std::logic_error("Producer is already running");
    }

    stopRequested_.store(false);
    producedSamples_.store(0);
    worker_ = std::thread([this, simulationStart] {
        run(simulationStart);
    });
}

void MeasurementProducer::stop()
{
    stopRequested_.store(true);
    if (worker_.joinable())
    {
        worker_.join();
    }
}

std::size_t MeasurementProducer::producedSamples() const
{
    return producedSamples_.load();
}

void MeasurementProducer::run(
    std::chrono::steady_clock::time_point simulationStart)
{
    std::int64_t sampleIndex = 0;

    while (!stopRequested_.load())
    {
        const auto scheduledTime = simulationStart + sampleIndex * sampleInterval_;
        std::this_thread::sleep_until(scheduledTime);

        if (stopRequested_.load())
        {
            break;
        }

        const auto timestampMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                scheduledTime - simulationStart
            ).count();

        buffer_.push({100 + static_cast<int>(sampleIndex % 1000), timestampMs});
        ++sampleIndex;
        ++producedSamples_;
    }
}
