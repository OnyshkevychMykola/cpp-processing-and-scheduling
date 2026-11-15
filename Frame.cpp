#include "Frame.hpp"

#include <algorithm>

namespace
{
constexpr std::uint8_t Marker0 = 0xAA;
constexpr std::uint8_t Marker1 = 0x55;
constexpr std::size_t MarkerBytes = 2;
constexpr std::size_t HeaderBytes = MarkerBytes + 2 + 4 + 8; // marker, len, seq, timestamp
constexpr std::size_t ChecksumBytes = 2;

template <typename T>
void putLE(std::vector<std::uint8_t>& out, T value)
{
    for (std::size_t i = 0; i < sizeof(T); ++i)
    {
        out.push_back(static_cast<std::uint8_t>(
            static_cast<std::uint64_t>(value) >> (8 * i)));
    }
}

template <typename T>
T getLE(const std::uint8_t* in)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
    {
        value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    }
    return static_cast<T>(value);
}

std::uint16_t checksum(const std::uint8_t* begin, const std::uint8_t* end)
{
    std::uint16_t sum = 0;
    for (auto it = begin; it != end; ++it)
    {
        sum = static_cast<std::uint16_t>(sum + *it);
    }
    return sum;
}
} // namespace

std::vector<std::uint8_t> encodeFrame(const Packet& packet)
{
    std::vector<std::uint8_t> out;
    out.reserve(HeaderBytes + packet.bytes.size() + ChecksumBytes);
    out.push_back(Marker0);
    out.push_back(Marker1);
    putLE(out, static_cast<std::uint16_t>(packet.bytes.size()));
    putLE(out, packet.seq);
    putLE(out, packet.timestampMs);
    out.insert(out.end(), packet.bytes.begin(), packet.bytes.end());
    putLE(out, checksum(out.data() + MarkerBytes, out.data() + out.size()));
    return out;
}

void FrameDecoder::feed(const std::uint8_t* data, std::size_t size)
{
    buffer_.insert(buffer_.end(), data, data + size);
}

bool FrameDecoder::skipByte()
{
    if (buffer_.empty())
    {
        return false;
    }
    buffer_.erase(buffer_.begin());
    return true;
}

std::optional<Packet> FrameDecoder::next()
{
    while (true)
    {
        // Drop everything before the next marker.
        std::size_t start = 0;
        while (start + 1 < buffer_.size() &&
               !(buffer_[start] == Marker0 && buffer_[start + 1] == Marker1))
        {
            ++start;
        }
        if (start + 1 >= buffer_.size())
        {
            // Keep a trailing 0xAA: it may be the first half of a marker.
            const bool keepLast = !buffer_.empty() && buffer_.back() == Marker0;
            buffer_.erase(buffer_.begin(), buffer_.end() - (keepLast ? 1 : 0));
            return std::nullopt;
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(start));

        if (buffer_.size() < HeaderBytes)
        {
            return std::nullopt;
        }

        const auto payloadLen = getLE<std::uint16_t>(buffer_.data() + MarkerBytes);
        if (payloadLen > MaxPayloadBytes)
        {
            ++badFrames_;
            buffer_.erase(buffer_.begin()); // resync past this marker
            continue;
        }

        const std::size_t frameBytes = HeaderBytes + payloadLen + ChecksumBytes;
        if (buffer_.size() < frameBytes)
        {
            return std::nullopt;
        }

        const std::uint8_t* frame = buffer_.data();
        const auto expected = getLE<std::uint16_t>(frame + HeaderBytes + payloadLen);
        const auto actual = checksum(frame + MarkerBytes, frame + HeaderBytes + payloadLen);
        if (expected != actual)
        {
            ++badFrames_;
            buffer_.erase(buffer_.begin());
            continue;
        }

        Packet packet;
        packet.seq = getLE<std::uint32_t>(frame + 4);
        packet.timestampMs = getLE<std::int64_t>(frame + 8);
        packet.bytes.assign(frame + HeaderBytes, frame + HeaderBytes + payloadLen);
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frameBytes));
        return packet;
    }
}
