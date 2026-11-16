#include "MeasurementBuffer.hpp"
#include "MeasurementProcessor.hpp"
#include "MeasurementProducer.hpp"
#include "PeriodicScheduler.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <string>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct Config
{
    ProcessingMode mode = ProcessingMode::Sleep;
    // A: processing runs inside the scheduler tick.
    // B: processing runs in its own thread; the tick only publishes.
    char approach = 'B';
    int sampleIntervalMs = 2;    // producer: 500 Hz
    int tickIntervalMs = 4;      // publisher: 250 Hz
    int processingMs = 12;       // processing time per sample
    int rampToMs = 0;            // >0: processing time grows to this value
    double delayProbability = 0; // share of samples delivered late
    int delayMs = 0;
    int durationMs = 200;
};

void printUsage(const char* program)
{
    std::cerr
        << "Usage: " << program
        << " [sleep|cpu] [--approach A|B] [--scenario NAME] [--duration-ms N]\n"
        << "Scenarios:\n"
        << "  default    12 ms processing (slower than the 4 ms tick)\n"
        << "  baseline   3 ms processing, no delays\n"
        << "  delayed    baseline + 5% of samples delivered 20 ms late\n"
        << "  load-ramp  processing time grows 2 ms -> 20 ms over the run\n";
}

bool applyScenario(Config& c, const std::string& name)
{
    if (name == "default")
    {
        return true;
    }
    if (name == "baseline")
    {
        c.processingMs = 3;
        return true;
    }
    if (name == "delayed")
    {
        c.processingMs = 3;
        c.delayProbability = 0.05;
        c.delayMs = 20;
        return true;
    }
    if (name == "load-ramp")
    {
        c.processingMs = 2;
        c.rampToMs = 20;
        return true;
    }
    return false;
}

bool parseArgs(int argc, char* argv[], Config& c)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "sleep" || a == "cpu")
        {
            c.mode = a == "cpu" ? ProcessingMode::CpuWork : ProcessingMode::Sleep;
            continue;
        }
        if (i + 1 >= argc)
        {
            return false;
        }
        const std::string v = argv[++i];
        if (a == "--approach" && (v == "A" || v == "B"))
        {
            c.approach = v[0];
        }
        else if (a == "--scenario" && applyScenario(c, v))
        {
        }
        else if (a == "--duration-ms" && std::stoi(v) > 0)
        {
            c.durationMs = std::stoi(v);
        }
        else
        {
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    Config config;
    try
    {
        if (!parseArgs(argc, argv, config))
        {
            printUsage(argv[0]);
            return 1;
        }
    }
    catch (const std::exception&)
    {
        printUsage(argv[0]);
        return 1;
    }

    const bool approachB = config.approach == 'B';
    const auto processingTime = std::chrono::milliseconds(config.processingMs);
    const auto duration = std::chrono::milliseconds(config.durationMs);
    const auto rampTo = std::chrono::milliseconds(config.rampToMs);
    const auto rampDuration = config.rampToMs > 0 ? duration : 0ms;

    // Producer -> raw buffer -> processor -> prepared buffer -> scheduler.
    MeasurementBuffer rawBuffer(4);
    MeasurementBuffer preparedBuffer(8);

    MeasurementProducer producer(
        rawBuffer, std::chrono::milliseconds(config.sampleIntervalMs));
    producer.setDeliveryDelay(config.delayProbability,
                              std::chrono::milliseconds(config.delayMs));

    MeasurementProcessor processor(rawBuffer, preparedBuffer, processingTime,
                                   config.mode);
    if (config.rampToMs > 0)
    {
        processor.setLoadRamp(rampTo, rampDuration);
    }

    PeriodicScheduler scheduler(
        std::chrono::milliseconds(config.tickIntervalMs), duration);

    // All parts share one time origin, slightly in the future.
    const auto start = Clock::now() + 100ms;
    producer.start(start);
    if (approachB)
    {
        processor.start(start);
    }

    std::cout << "Approach " << config.approach
              << (approachB ? " (separate processing thread)"
                            : " (processing inside the tick)")
              << "\n\n";

    std::optional<Measurement> lastPublished;

    scheduler.run(
        [&](const TickContext& tick)
        {
            std::optional<Measurement> fresh;
            if (approachB)
            {
                // Only pick up finished work; never wait for the processor.
                fresh = preparedBuffer.takeLatestUpTo(tick.scheduledTimeMs);
            }
            else
            {
                // Take a raw sample and process it right here. Slow
                // processing delays this tick and the ticks after it.
                fresh = rawBuffer.takeLatestUpTo(tick.scheduledTimeMs);
                if (fresh)
                {
                    simulateProcessing(
                        config.mode,
                        rampedProcessingTime(processingTime, rampTo,
                                             rampDuration, tick.scheduledTimeMs));
                }
            }

            if (fresh)
            {
                lastPublished = fresh;
            }
            if (!lastPublished)
            {
                std::cout << "Tick " << tick.tickIndex << " @ "
                          << tick.scheduledTimeMs << " ms: no data yet\n";
                return;
            }

            // Publish on every tick; repeat the last value if nothing new.
            std::cout << "Tick " << tick.tickIndex << " @ "
                      << tick.scheduledTimeMs << " ms: value "
                      << lastPublished->value << " (sample from "
                      << lastPublished->timestampMs << " ms)"
                      << (fresh ? "" : " REPEAT") << '\n';
        },
        start);

    producer.stop();
    processor.stop();
    return 0;
}
