// Listens on a serial port (one end of a socat pty pair), decodes packets
// and prints what arrived. Exits after the end-of-stream packet.
//
// Usage: receiver <port>

#include "Frame.hpp"
#include "SerialPort.hpp"

#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <optional>

namespace
{
std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: receiver <port>\n";
        return 2;
    }

    try
    {
        SerialPort port(argv[1]);
        FrameDecoder decoder;
        std::array<std::uint8_t, 1024> buffer{};

        std::size_t packets = 0;
        std::size_t bytes = 0;
        std::size_t gaps = 0;
        std::optional<std::uint32_t> lastSeq;

        auto printTotals = [&] {
            std::cout << "packets=" << packets << " bytes=" << bytes
                      << " bad=" << decoder.badFrames() << " gaps=" << gaps << "\n";
        };

        // Prints decoded packets; returns true once the end-of-stream packet arrived.
        auto drain = [&] {
            while (auto packet = decoder.next())
            {
                if (lastSeq && packet->seq != *lastSeq + 1)
                {
                    ++gaps;
                }
                lastSeq = packet->seq;

                if (packet->bytes.empty())
                {
                    return true;
                }

                ++packets;
                bytes += packet->bytes.size();
                std::cout << "seq=" << packet->seq << " size=" << packet->bytes.size()
                          << " latency=" << (nowMs() - packet->timestampMs) << "ms OK\n"
                          << std::flush;
            }
            return false;
        };

        std::cout << "listening on " << argv[1] << "\n" << std::flush;
        while (true)
        {
            const auto got = port.readSome(buffer.data(), buffer.size());
            if (got == 0)
            {
                // No more bytes: a corrupt length may be holding back good frames.
                bool ended = false;
                while (!ended && decoder.skipByte())
                {
                    ended = drain();
                }
                std::cout << (ended ? "end of stream: " : "port closed before end of stream: ");
                printTotals();
                return ended ? 0 : 1;
            }
            decoder.feed(buffer.data(), got);

            if (drain())
            {
                std::cout << "end of stream: ";
                printTotals();
                return 0;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "receiver: " << e.what() << "\n";
        return 1;
    }
}
