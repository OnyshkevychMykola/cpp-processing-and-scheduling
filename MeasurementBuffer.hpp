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

// Mutex-protected bounded queue shared between two threads.
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

    // When the buffer is full, the oldest measurement is dropped.
    void push(const Measurement& measurement)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_.size() == capacity_)
        {
            buffer_.pop_front();
        }
        buffer_.push_back(measurement);
    }

    // Take the newest measurement not newer than `timestampMs` and discard
    // the older ones: consumers always work with the freshest data.
    std::optional<Measurement> takeLatestUpTo(std::int64_t timestampMs)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::optional<Measurement> latest;
        while (!buffer_.empty() && buffer_.front().timestampMs <= timestampMs)
        {
            latest = buffer_.front();
            buffer_.pop_front();
        }
        return latest;
    }

private:
    std::mutex mutex_;
    std::deque<Measurement> buffer_;
    std::size_t capacity_;
};
