// Sends data over a serial port (one end of a socat pty pair), one packet per tick,
// and measures how long each tick takes.
//
// File mode (default): cut <file> into --chunk byte packets and send it once.
// Channel mode (--channels N): emulate an ECG-like frontend. Every tick takes one
// 2-byte sample from each of N channels and sends them as one packet, for --duration
// seconds. Each channel reads <file> the way DDPS ChildMetricReader does:
//   --read per-tick  one std::ifstream per channel, read() on every tick (default)
//   --read preload   whole file loaded into memory up front (baseline)
//
// Usage: sender <port> <file> [--chunk BYTES] [--period MS]
//                             [--channels N] [--read per-tick|preload] [--duration S]

#include "Frame.hpp"
#include "Packet.hpp"
#include "PeriodicScheduler.hpp"
#include "SerialPort.hpp"
#include "Stats.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

double microsBetween(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::micro>(to - from).count();
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

constexpr std::size_t SampleBytes = 2;

// One channel of samples; wraps around at the end of the data like ChildMetricReader.
class ChannelReader
{
public:
    virtual ~ChannelReader() = default;
    virtual void readSample(std::uint8_t* out) = 0;
};

// Same access pattern as DDPS ChildMetricReader: own ifstream, read() per sample, seekg(0) on wrap.
class FileChannelReader final : public ChannelReader
{
public:
    explicit FileChannelReader(const std::string& path)
        : path_(path),
          sampleCount_(std::filesystem::file_size(path) / SampleBytes),
          file_(path, std::ios::binary)
    {
        if (!file_ || sampleCount_ == 0)
        {
            throw std::runtime_error("cannot read samples from " + path);
        }
    }

    void readSample(std::uint8_t* out) override
    {
        if (!file_.read(reinterpret_cast<char*>(out), SampleBytes))
        {
            throw std::runtime_error("cannot read a sample from " + path_);
        }
        if (++position_ == sampleCount_)
        {
            position_ = 0;
            file_.seekg(0);
        }
    }

private:
    std::string path_;
    std::size_t sampleCount_;
    std::ifstream file_;
    std::size_t position_ = 0;
};

class MemoryChannelReader final : public ChannelReader
{
public:
    explicit MemoryChannelReader(const std::vector<std::uint8_t>& data)
        : data_(data), sampleCount_(data.size() / SampleBytes)
    {
        if (sampleCount_ == 0)
        {
            throw std::runtime_error("file holds no complete sample");
        }
    }

    void readSample(std::uint8_t* out) override
    {
        std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(position_ * SampleBytes), SampleBytes, out);
        if (++position_ == sampleCount_)
        {
            position_ = 0;
        }
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t sampleCount_;
    std::size_t position_ = 0;
};

struct Options
{
    std::string port;
    std::string file;
    std::size_t chunk = 64;
    long periodMs = 100;
    std::size_t channels = 0; // 0 = file mode
    bool preload = false;
    double durationS = 10.0;
};

[[noreturn]] void usage(const std::string& error = {})
{
    if (!error.empty())
    {
        std::cerr << "sender: " << error << "\n";
    }
    std::cerr << "Usage: sender <port> <file> [--chunk BYTES] [--period MS]\n"
                 "                            [--channels N] [--read per-tick|preload] [--duration S]\n";
    std::exit(2);
}

Options parseOptions(int argc, char* argv[])
{
    if (argc < 3)
    {
        usage();
    }
    Options options;
    options.port = argv[1];
    options.file = argv[2];
    for (int i = 3; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (i + 1 >= argc)
        {
            usage("missing value for " + arg);
        }
        const std::string value = argv[++i];
        try
        {
            if (arg == "--chunk")
                options.chunk = std::stoul(value);
            else if (arg == "--period")
                options.periodMs = std::stol(value);
            else if (arg == "--channels")
                options.channels = std::stoul(value);
            else if (arg == "--duration")
                options.durationS = std::stod(value);
            else if (arg == "--read" && (value == "per-tick" || value == "preload"))
                options.preload = value == "preload";
            else
                usage("unknown option " + arg + " " + value);
        }
        catch (const std::logic_error&) // invalid_argument / out_of_range from sto*
        {
            usage("not a number: " + value);
        }
    }
    if (options.chunk == 0 || options.chunk > MaxPayloadBytes || options.periodMs <= 0 ||
        options.durationS <= 0 || options.channels * SampleBytes + 8 > MaxPayloadBytes)
    {
        usage("chunk must be 1.." + std::to_string(MaxPayloadBytes) +
              ", period and duration must be > 0, channels must fit in one payload");
    }
    return options;
}

struct TickTimings
{
    std::vector<double> latenessUs; // actual tick start - scheduled tick start
    std::vector<double> readUs;     // getting the data for this tick
    std::vector<double> writeUs;    // encode + write to the port
    std::vector<double> tickUs;     // whole callback

    void reserve(std::size_t n)
    {
        latenessUs.reserve(n);
        readUs.reserve(n);
        writeUs.reserve(n);
        tickUs.reserve(n);
    }
};

void printReport(const SchedulerStats& stats, const TickTimings& timings, long periodMs,
                 const std::string& readLabel)
{
    std::cout << "\n=== sender timing (period " << periodMs << " ms = " << periodMs * 1000 << " us) ===\n"
              << "ticks executed=" << stats.executedTicks << " skipped=" << stats.skippedTicks
              << " deadlineMisses=" << stats.deadlineMisses << "\n";
    printStats(std::cout, "tick start lateness", timings.latenessUs);
    printStats(std::cout, readLabel, timings.readUs);
    printStats(std::cout, "encode + write", timings.writeUs);
    printStats(std::cout, "whole tick", timings.tickUs);
}

void sendEndOfStream(SerialPort& port, std::uint32_t seq)
{
    Packet end;
    end.seq = seq;
    end.timestampUs = monotonicUs();
    const auto frame = encodeFrame(end);
    port.writeAll(frame.data(), frame.size());
}

void sendPacket(SerialPort& port, Packet& packet)
{
    packet.timestampUs = monotonicUs();
    const auto frame = encodeFrame(packet);
    port.writeAll(frame.data(), frame.size());
}

int runFileMode(const Options& options, SerialPort& port)
{
    const auto data = readFile(options.file);
    std::vector<Packet> packets;
    for (std::size_t offset = 0; offset < data.size(); offset += options.chunk)
    {
        Packet packet;
        packet.seq = static_cast<std::uint32_t>(packets.size());
        const auto end = std::min(offset + options.chunk, data.size());
        packet.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                            data.begin() + static_cast<std::ptrdiff_t>(end));
        packets.push_back(std::move(packet));
    }

    std::cout << "sending " << data.size() << " bytes from " << options.file << " as " << packets.size()
              << " packets every " << options.periodMs << " ms\n";

    TickTimings timings;
    timings.reserve(packets.size());
    std::size_t nextPacket = 0;
    const std::chrono::milliseconds period(options.periodMs);
    PeriodicScheduler scheduler(period, period * static_cast<long>(packets.size() + 1));
    const auto stats = scheduler.run([&](const TickContext& tick) {
        if (nextPacket >= packets.size())
        {
            return;
        }
        const auto start = Clock::now();
        timings.latenessUs.push_back(microsBetween(tick.experimentStart + period * tick.tickIndex, start));
        sendPacket(port, packets[nextPacket++]);
        const auto end = Clock::now();
        timings.readUs.push_back(0.0); // file is already in memory
        timings.writeUs.push_back(microsBetween(start, end));
        timings.tickUs.push_back(microsBetween(start, end));
    });
    // Ticks the scheduler skipped must not lose file data: flush the rest.
    while (nextPacket < packets.size())
    {
        sendPacket(port, packets[nextPacket++]);
    }
    sendEndOfStream(port, static_cast<std::uint32_t>(packets.size()));

    std::cout << "sent packets=" << packets.size() << " bytes=" << data.size() << "\n";
    printReport(stats, timings, options.periodMs, "read (preloaded)");
    return 0;
}

int runChannelMode(const Options& options, SerialPort& port)
{
    const auto fileData = readFile(options.file);
    std::vector<std::unique_ptr<ChannelReader>> channels;
    for (std::size_t c = 0; c < options.channels; ++c)
    {
        if (options.preload)
            channels.push_back(std::make_unique<MemoryChannelReader>(fileData));
        else
            channels.push_back(std::make_unique<FileChannelReader>(options.file));
    }

    const std::chrono::milliseconds period(options.periodMs);
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration<double>(options.durationS));
    const auto expectedTicks = static_cast<std::size_t>(duration / period);

    std::cout << "channel mode: " << options.channels << " channels, read=" << (options.preload ? "preload" : "per-tick")
              << ", period " << options.periodMs << " ms, duration " << options.durationS << " s (~"
              << expectedTicks << " ticks)\n";

    // ECG-like payload: [uint16 vector counter][uint16 sample counter][N x int16 samples][uint32 pace/inop bits]
    Packet packet;
    packet.bytes.assign(4 + options.channels * SampleBytes + 4, 0);
    const std::array<std::uint8_t, 4> allLeadsConnected{0xAA, 0xAA, 0x0A, 0x00};
    std::copy(allLeadsConnected.begin(), allLeadsConnected.end(), packet.bytes.end() - 4);

    TickTimings timings;
    timings.reserve(expectedTicks + 1);
    std::uint32_t seq = 0;
    std::uint16_t sampleCounter = 0;

    PeriodicScheduler scheduler(period, duration);
    const auto stats = scheduler.run([&](const TickContext& tick) {
        const auto start = Clock::now();
        timings.latenessUs.push_back(microsBetween(tick.experimentStart + period * tick.tickIndex, start));

        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            channels[c]->readSample(packet.bytes.data() + 4 + c * SampleBytes);
        }
        const auto afterRead = Clock::now();

        packet.bytes[2] = static_cast<std::uint8_t>(sampleCounter);
        packet.bytes[3] = static_cast<std::uint8_t>(sampleCounter >> 8);
        ++sampleCounter;
        packet.seq = seq++;
        sendPacket(port, packet);
        const auto end = Clock::now();

        timings.readUs.push_back(microsBetween(start, afterRead));
        timings.writeUs.push_back(microsBetween(afterRead, end));
        timings.tickUs.push_back(microsBetween(start, end));
    });
    sendEndOfStream(port, seq);

    std::cout << "sent packets=" << seq << " payload=" << packet.bytes.size() << " B\n";
    printReport(stats, timings, options.periodMs,
                "read " + std::to_string(options.channels) + " ch (" + (options.preload ? "preload" : "per-tick") + ")");
    return 0;
}
} // namespace

int main(int argc, char* argv[])
{
    const Options options = parseOptions(argc, argv);
    try
    {
        SerialPort port(options.port);
        return options.channels == 0 ? runFileMode(options, port) : runChannelMode(options, port);
    }
    catch (const std::exception& e)
    {
        std::cerr << "sender: " << e.what() << "\n";
        return 1;
    }
}
