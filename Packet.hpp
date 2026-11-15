#pragma once

#include <cstdint>
#include <vector>

// One chunk of the file on its way to the other side.
// An empty payload marks the end of the stream.
struct Packet
{
    std::uint32_t seq = 0;
    std::int64_t timestampMs = 0;
    std::vector<std::uint8_t> bytes;
};
