// Reads a binary file, cuts it into packets and sends one packet per tick
// over a serial port (one end of a socat pty pair).
//
// Usage: sender <port> <file> [--chunk BYTES] [--period MS]

#include "Frame.hpp"
#include "Packet.hpp"
#include "PeriodicScheduler.hpp"
#include "SerialPort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::uint8_t> readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("cannot open " + path);
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<Packet> splitIntoPackets(const std::vector<std::uint8_t>& data, std::size_t chunk)
{
    std::vector<Packet> packets;
    for (std::size_t offset = 0; offset < data.size(); offset += chunk)
    {
        Packet packet;
        packet.seq = static_cast<std::uint32_t>(packets.size());
        const auto end = std::min(offset + chunk, data.size());
        packet.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                            data.begin() + static_cast<std::ptrdiff_t>(end));
        packets.push_back(std::move(packet));
    }
    // End of stream: empty payload.
    Packet end;
    end.seq = static_cast<std::uint32_t>(packets.size());
    packets.push_back(end);
    return packets;
}

void usage()
{
    std::cerr << "Usage: sender <port> <file> [--chunk BYTES] [--period MS]\n";
    std::exit(2);
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        usage();
    }
    const std::string portPath = argv[1];
    const std::string filePath = argv[2];
    std::size_t chunk = 64;
    long periodMs = 100;
    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        try
        {
            if (arg == "--chunk" && i + 1 < argc)
            {
                chunk = std::stoul(argv[++i]);
            }
            else if (arg == "--period" && i + 1 < argc)
            {
                periodMs = std::stol(argv[++i]);
            }
            else
            {
                usage();
            }
        }
        catch (const std::logic_error&) // invalid_argument / out_of_range from stoul/stol
        {
            std::cerr << "not a number: " << argv[i] << "\n";
            usage();
        }
    }
    if (chunk == 0 || chunk > MaxPayloadBytes || periodMs <= 0)
    {
        std::cerr << "chunk must be 1.." << MaxPayloadBytes << ", period must be > 0\n";
        return 2;
    }

    try
    {
        const auto data = readFile(filePath);
        auto packets = splitIntoPackets(data, chunk);
        SerialPort port(portPath);

        std::size_t nextPacket = 0;
        auto sendNext = [&] {
            Packet& packet = packets[nextPacket++];
            packet.timestampMs = nowMs();
            const auto frame = encodeFrame(packet);
            port.writeAll(frame.data(), frame.size());
        };

        std::cout << "sending " << data.size() << " bytes from " << filePath << " as "
                  << packets.size() - 1 << " packets every " << periodMs << " ms\n";

        // One packet per executed tick. Ticks the scheduler skips do not lose
        // packets: whatever is left afterwards is flushed right away.
        const std::chrono::milliseconds period(periodMs);
        PeriodicScheduler scheduler(period, period * static_cast<long>(packets.size()));
        const auto stats = scheduler.run([&](const TickContext&) {
            if (nextPacket < packets.size())
            {
                sendNext();
            }
        });
        while (nextPacket < packets.size())
        {
            sendNext();
        }

        std::cout << "sent packets=" << packets.size() - 1 << " bytes=" << data.size()
                  << " skippedTicks=" << stats.skippedTicks << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "sender: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
