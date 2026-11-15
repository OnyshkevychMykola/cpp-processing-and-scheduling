#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

// One chunk of data on its way to the other side.
// An empty payload marks the end of the stream.
struct Packet
{
    std::uint32_t seq = 0;
    // steady_clock (CLOCK_MONOTONIC) microseconds: comparable across processes on one machine.
    std::int64_t timestampUs = 0;
    std::vector<std::uint8_t> bytes;
};

inline std::int64_t monotonicUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
