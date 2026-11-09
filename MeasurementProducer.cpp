#include "MeasurementProducer.hpp"

#include <cstdint>
#include <random>
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

void MeasurementProducer::setDeliveryDelay(
    double probability, std::chrono::milliseconds delay, unsigned seed)
{
    if (worker_.joinable())
    {
        throw std::logic_error("Configure delay before start()");
    }
    if (probability < 0.0 || probability > 1.0 ||
        delay < std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Invalid delay configuration");
    }
    delayProbability_ = probability;
    delay_ = delay;
    seed_ = seed;
}

std::size_t MeasurementProducer::delayedSamples() const
{
    return delayedSamples_.load();
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
    std::mt19937 rng(seed_);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

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

        if (delayProbability_ > 0.0 && uniform(rng) < delayProbability_)
        {
            ++delayedSamples_;
            std::this_thread::sleep_for(delay_);
        }

        buffer_.push({100 + static_cast<int>(sampleIndex % 1000), timestampMs});
        ++sampleIndex;
        ++producedSamples_;
    }
}
