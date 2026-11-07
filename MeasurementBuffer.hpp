#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>

struct Measurement
{
    int value;
    std::int64_t timestampMs;
};

class MeasurementBuffer
{
public:
    explicit MeasurementBuffer(std::size_t capacity) : capacity_(capacity)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("Capacity must be > 0");
        }
    }

    void push(const Measurement& measurement)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.size() == capacity_)
        {
            buffer_.pop_front();
            ++overflowDrops_;
        }
        buffer_.push_back(measurement);
    }

    std::optional<Measurement> takeLatestUpTo(std::int64_t timestampMs)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<Measurement> latest;

        while (!buffer_.empty() && buffer_.front().timestampMs <= timestampMs)
        {
            if (latest)
            {
                ++staleDrops_;
            }
            latest = buffer_.front();
            buffer_.pop_front();
        }
        return latest;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    std::size_t overflowDrops() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return overflowDrops_;
    }

    std::size_t staleDrops() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return staleDrops_;
    }

private:
    mutable std::mutex mutex_;
    std::deque<Measurement> buffer_;
    std::size_t capacity_;
    std::size_t overflowDrops_ = 0;
    std::size_t staleDrops_ = 0;
};
