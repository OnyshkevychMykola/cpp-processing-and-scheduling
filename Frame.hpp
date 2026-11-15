#pragma once

#include "Packet.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Wire format (little-endian):
//   [0xAA 0x55][uint16 payloadLen][uint32 seq][int64 timestampMs][payload][uint16 checksum]
// checksum = 16-bit wrapping sum of all bytes from payloadLen through the end of the payload.
constexpr std::size_t MaxPayloadBytes = 4096;

std::vector<std::uint8_t> encodeFrame(const Packet& packet);

// Turns a raw byte stream back into packets. Bytes may arrive in any split;
// garbage and corrupt frames are skipped by resyncing on the next marker.
class FrameDecoder
{
public:
    void feed(const std::uint8_t* data, std::size_t size);

    // A decoded packet, or nullopt when more bytes are needed.
    std::optional<Packet> next();

    // Call when no more bytes will come: a corrupt length may be holding back
    // the frames behind it. Drops one byte; false once the buffer is empty.
    bool skipByte();

    std::size_t badFrames() const { return badFrames_; }

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t badFrames_ = 0;
};
