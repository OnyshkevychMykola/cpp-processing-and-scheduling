// Listens on a serial port (one end of a socat pty pair), decodes packets and
// measures how evenly they arrive. Exits after the end-of-stream packet.
//
// Usage: receiver <port> [-v]     (-v prints one line per packet)

#include "Frame.hpp"
#include "Packet.hpp"
#include "SerialPort.hpp"
#include "Stats.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "-v"))
    {
        std::cerr << "Usage: receiver <port> [-v]\n";
        return 2;
    }
    const bool verbose = argc == 3;

    try
    {
        SerialPort port(argv[1]);
        FrameDecoder decoder;
        std::array<std::uint8_t, 4096> buffer{};

        std::size_t packets = 0;
        std::size_t bytes = 0;
        std::size_t gaps = 0;
        std::optional<std::uint32_t> lastSeq;
        std::optional<std::int64_t> lastArrivalUs;
        std::vector<double> intervalsUs;
        std::vector<double> latenciesUs;
        intervalsUs.reserve(1 << 16);
        latenciesUs.reserve(1 << 16);

        auto printTotals = [&](const char* how) {
            std::cout << "\n=== receiver (" << how << ") ===\n"
                      << "packets=" << packets << " bytes=" << bytes << " bad=" << decoder.badFrames()
                      << " gaps=" << gaps << "\n";
            printStats(std::cout, "arrival interval", intervalsUs);
            printStats(std::cout, "delivery latency", latenciesUs);
        };

        // Handles decoded packets; returns true once the end-of-stream packet arrived.
        auto drain = [&](std::int64_t arrivalUs) {
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
                const double latencyUs = static_cast<double>(arrivalUs - packet->timestampUs);
                latenciesUs.push_back(latencyUs);
                if (lastArrivalUs)
                {
                    intervalsUs.push_back(static_cast<double>(arrivalUs - *lastArrivalUs));
                }
                lastArrivalUs = arrivalUs;
                if (verbose)
                {
                    std::cout << "seq=" << packet->seq << " size=" << packet->bytes.size()
                              << " latency=" << latencyUs << "us OK\n";
                }
            }
            return false;
        };

        std::cout << "listening on " << argv[1] << "\n" << std::flush;
        while (true)
        {
            const auto got = port.readSome(buffer.data(), buffer.size());
            const auto arrivalUs = monotonicUs();
            if (got == 0)
            {
                // No more bytes: a corrupt length may be holding back good frames.
                bool ended = false;
                while (!ended && decoder.skipByte())
                {
                    ended = drain(arrivalUs);
                }
                printTotals(ended ? "end of stream" : "port closed before end of stream");
                return ended ? 0 : 1;
            }
            decoder.feed(buffer.data(), got);

            if (drain(arrivalUs))
            {
                printTotals("end of stream");
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
