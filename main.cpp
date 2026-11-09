#include "MeasurementBuffer.hpp"
#include "MeasurementProcessor.hpp"
#include "MeasurementProducer.hpp"
#include "PeriodicScheduler.hpp"
#include "SystemTuning.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

template <typename Duration>
double toMilliseconds(Duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

struct LatencyStats
{
    double totalMs = 0.0;
    double maximumMs = 0.0;
    std::size_t count = 0;

    void add(double milliseconds)
    {
        totalMs += milliseconds;
        maximumMs = std::max(maximumMs, milliseconds);
        ++count;
    }

    double averageMs() const
    {
        return count ? totalMs / static_cast<double>(count) : 0.0;
    }
};

struct Publication
{
    std::size_t tickIndex;
    int value;
    std::int64_t sampleTimestampMs;
    std::int64_t scheduledTimeMs;
    double actualTimeMs;
    double latenessMs;
    double dataAgeMs;
    bool reused;
};

struct Config
{
    std::string scenario = "default";
    ProcessingMode mode = ProcessingMode::Sleep;
    int sampleIntervalMs = 2;       // Producer: 500 Hz
    int tickIntervalMs = 4;         // Publisher: 250 Hz
    int processingMs = 12;          // Processor time per sample
    int durationMs = 2000;
    int rawCapacity = 4;
    int preparedCapacity = 8;
    // Delayed data delivery (producer side).
    double delayProbability = 0.0;
    int delayMs = 0;
    // Increasing workload: processing time ramps to rampToMs over the test.
    int rampToMs = 0;               // 0 = no ramp
    unsigned seed = 1;
    // A: processing runs inside the scheduler tick (naive).
    // B: processing runs in an independent thread; the tick only publishes.
    char approach = 'B';
    std::string csvPath;        // per-publication rows
    std::string summaryPath;    // one summary row appended per run
    std::string label;          // free-form tag stored in the summary row

    // Timing-consistency techniques (scheduler thread).
    std::string timing = "sleep";   // sleep | hybrid (sleep, then spin)
    int spinUs = 300;               // spin margin for hybrid
    int rtPriority = 0;             // >0: SCHED_FIFO with this priority
    int pinCpu = -1;                // >=0: pin scheduler thread to this CPU
    bool mlockAll = false;
    // Competing system load.
    int stressThreads = 0;          // -1 = auto (2 x CPUs)
    // Publication policy: withhold data older than this (0 = never).
    int maxAgeMs = 0;
};

void printUsage(const char* program)
{
    std::cerr
        << "Usage: " << program << " [sleep|cpu] [--scenario NAME] [options]\n"
        << "Scenarios (presets, options override them):\n"
        << "  default    12 ms processing (slow processor, original setup)\n"
        << "  baseline   3 ms processing, no delays: scheduling accuracy\n"
        << "  delayed    baseline + 5% of samples delivered 20 ms late\n"
        << "  load-ramp  processing time grows 2 ms -> 20 ms over the test\n"
        << "  stress     baseline + busy background threads on every CPU\n"
        << "Options (all take a value):\n"
        << "  --mode sleep|cpu   --sample-ms N   --tick-ms N\n"
        << "  --processing-ms N  --duration-ms N  --raw-cap N  --prepared-cap N\n"
        << "  --delay-prob P (0..1)  --delay-ms N  --ramp-to-ms N  --seed N\n"
        << "  --approach A|B     A: process inside the tick, B: separate thread (default)\n"
        << "  --csv FILE         write per-publication rows\n"
        << "  --summary FILE     append one summary row (header added if empty)\n"
        << "  --label TEXT       tag stored in the summary row\n"
        << "Timing consistency:\n"
        << "  --timing sleep|hybrid  hybrid = sleep, then spin the last --spin-us (300)\n"
        << "  --rt-priority N    SCHED_FIFO priority for the scheduler thread (0 = off)\n"
        << "  --pin-cpu N        pin scheduler thread to CPU N; load avoids it\n"
        << "  --mlock 0|1        mlockall to avoid page faults\n"
        << "  --stress N|auto    N busy background threads (auto = 2 x CPUs)\n"
        << "Publication policy:\n"
        << "  --max-age-ms N     do not publish data older than N ms (0 = always)\n";
}

bool applyScenario(Config& c, const std::string& name)
{
    c.scenario = name;
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
    if (name == "stress")
    {
        c.processingMs = 3;
        c.stressThreads = -1;
        return true;
    }
    return false;
}

// Returns false (after printing a message) on invalid input.
bool parseArgs(int argc, char* argv[], Config& c)
{
    std::vector<std::string> args(argv + 1, argv + argc);

    // Scenario first so that explicit options can override its preset.
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--scenario")
        {
            if (i + 1 >= args.size() || !applyScenario(c, args[i + 1]))
            {
                std::cerr << "Unknown or missing scenario\n";
                return false;
            }
            ++i;
        }
    }

    try
    {
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string& a = args[i];
            if (a == "sleep" || a == "cpu")
            {
                c.mode = a == "cpu" ? ProcessingMode::CpuWork
                                    : ProcessingMode::Sleep;
                continue;
            }
            if (a.rfind("--", 0) != 0 || i + 1 >= args.size())
            {
                std::cerr << "Unknown argument or missing value: " << a << '\n';
                return false;
            }
            const std::string& v = args[++i];
            if (a == "--scenario") {}
            else if (a == "--mode")
            {
                if (v != "sleep" && v != "cpu")
                {
                    std::cerr << "Unknown mode: " << v << '\n';
                    return false;
                }
                c.mode = v == "cpu" ? ProcessingMode::CpuWork
                                    : ProcessingMode::Sleep;
            }
            else if (a == "--sample-ms") c.sampleIntervalMs = std::stoi(v);
            else if (a == "--tick-ms") c.tickIntervalMs = std::stoi(v);
            else if (a == "--processing-ms") c.processingMs = std::stoi(v);
            else if (a == "--duration-ms") c.durationMs = std::stoi(v);
            else if (a == "--raw-cap") c.rawCapacity = std::stoi(v);
            else if (a == "--prepared-cap") c.preparedCapacity = std::stoi(v);
            else if (a == "--delay-prob") c.delayProbability = std::stod(v);
            else if (a == "--delay-ms") c.delayMs = std::stoi(v);
            else if (a == "--ramp-to-ms") c.rampToMs = std::stoi(v);
            else if (a == "--seed") c.seed = static_cast<unsigned>(std::stoul(v));
            else if (a == "--approach")
            {
                if (v != "A" && v != "B")
                {
                    std::cerr << "Approach must be A or B\n";
                    return false;
                }
                c.approach = v[0];
            }
            else if (a == "--csv") c.csvPath = v;
            else if (a == "--summary") c.summaryPath = v;
            else if (a == "--label") c.label = v;
            else if (a == "--timing")
            {
                if (v != "sleep" && v != "hybrid")
                {
                    std::cerr << "Timing must be sleep or hybrid\n";
                    return false;
                }
                c.timing = v;
            }
            else if (a == "--spin-us") c.spinUs = std::stoi(v);
            else if (a == "--rt-priority") c.rtPriority = std::stoi(v);
            else if (a == "--pin-cpu") c.pinCpu = std::stoi(v);
            else if (a == "--mlock") c.mlockAll = std::stoi(v) != 0;
            else if (a == "--stress")
            {
                c.stressThreads = v == "auto" ? -1 : std::stoi(v);
            }
            else if (a == "--max-age-ms") c.maxAgeMs = std::stoi(v);
            else
            {
                std::cerr << "Unknown option: " << a << '\n';
                return false;
            }
        }
    }
    catch (const std::exception&)
    {
        std::cerr << "Invalid numeric value\n";
        return false;
    }

    if (c.sampleIntervalMs <= 0 || c.tickIntervalMs <= 0 ||
        c.durationMs <= 0 || c.processingMs < 0 || c.rampToMs < 0 ||
        c.rawCapacity <= 0 || c.preparedCapacity <= 0 || c.delayMs < 0 ||
        c.delayProbability < 0.0 || c.delayProbability > 1.0 ||
        c.spinUs < 0 || c.rtPriority < 0 || c.rtPriority > 99 ||
        c.pinCpu < -1 || c.stressThreads < -1 || c.maxAgeMs < 0)
    {
        std::cerr << "Option value out of range\n";
        return false;
    }
    return true;
}

int main(int argc, char* argv[])
{
    Config config;
    if (!parseArgs(argc, argv, config))
    {
        printUsage(argv[0]);
        return 1;
    }
    const ProcessingMode mode = config.mode;

    const bool approachB = config.approach == 'B';
    const auto sampleInterval = std::chrono::milliseconds(config.sampleIntervalMs);
    const auto tickInterval = std::chrono::milliseconds(config.tickIntervalMs);
    const auto processingTime = std::chrono::milliseconds(config.processingMs);
    const auto testDuration = std::chrono::milliseconds(config.durationMs);

    MeasurementBuffer rawBuffer(static_cast<std::size_t>(config.rawCapacity));
    MeasurementBuffer preparedBuffer(
        static_cast<std::size_t>(config.preparedCapacity));

    MeasurementProducer producer(rawBuffer, sampleInterval);
    MeasurementProcessor processor(rawBuffer, preparedBuffer, processingTime, mode);
    PeriodicScheduler scheduler(tickInterval, testDuration);

    if (config.delayProbability > 0.0 && config.delayMs > 0)
    {
        producer.setDeliveryDelay(config.delayProbability,
                                  std::chrono::milliseconds(config.delayMs),
                                  config.seed);
    }
    if (config.rampToMs > 0)
    {
        processor.setLoadRamp(std::chrono::milliseconds(config.rampToMs),
                              testDuration);
    }

    if (config.timing == "hybrid")
    {
        scheduler.setSpinMargin(std::chrono::microseconds(config.spinUs));
    }

    // Competing system load, started first. With --pin-cpu it avoids that CPU
    // (an "isolated core"), otherwise it runs everywhere.
    const int stressThreads = config.stressThreads < 0
                                  ? 2 * availableCpuCount()
                                  : config.stressThreads;
    BackgroundLoad load(stressThreads, config.pinCpu);

    // Producer, processor and scheduler share the same virtual time origin.
    // The longer lead-in lets the load threads settle before the first tick.
    const auto sharedStart = Clock::now() + (stressThreads > 0 ? 300ms : 100ms);
    producer.start(sharedStart);
    if (approachB)
    {
        processor.start(sharedStart);
    }

    // Tune only the scheduler (this) thread, after the workers were created so
    // they do not inherit its real-time class or CPU pin.
    const TuningResult tuning =
        tuneCurrentThread(config.rtPriority, config.pinCpu, config.mlockAll);
    if (!tuning.notes.empty())
    {
        std::cerr << "Warning: " << tuning.notes << '\n';
    }

    std::vector<Publication> publications;
    publications.reserve(500);

    std::optional<Measurement> lastPublishedData;
    std::size_t ticksWithoutPreparedData = 0;
    std::size_t ticksWithoutNewData = 0;
    std::size_t withheldPublications = 0;
    std::size_t reusedPublications = 0;
    LatencyStats publicationLateness;
    LatencyStats dataAge;
    LatencyStats inlineProcessing;  // Approach A only

    const auto schedulerStats = scheduler.run(
        [&](const TickContext& tick)
        {
            std::optional<Measurement> newlyPrepared;
            if (approachB)
            {
                // Non-blocking: do not wait for the processor to finish.
                newlyPrepared = preparedBuffer.takeLatestUpTo(tick.scheduledTimeMs);
            }
            else
            {
                // Approach A: take the newest raw sample and process it right
                // here, inside the tick. Slow processing delays the tick.
                newlyPrepared = rawBuffer.takeLatestUpTo(tick.scheduledTimeMs);
                if (newlyPrepared)
                {
                    const auto processingStart = Clock::now();
                    simulateProcessing(
                        mode,
                        rampedProcessingTime(
                            processingTime,
                            std::chrono::milliseconds(config.rampToMs),
                            config.rampToMs > 0 ? testDuration
                                                : std::chrono::milliseconds(0),
                            tick.scheduledTimeMs));
                    inlineProcessing.add(
                        toMilliseconds(Clock::now() - processingStart));
                }
            }

            if (!newlyPrepared)
            {
                ++ticksWithoutNewData;  // buffer underrun
            }

            if (newlyPrepared)
            {
                lastPublishedData = *newlyPrepared;
            }
            else if (!lastPublishedData)
            {
                // At startup no sample has completed its 12ms processing.
                ++ticksWithoutPreparedData;
                return;
            }

            // Our demo policy: publish on each tick, repeating the last
            // prepared measurement when there is no new one.
            const bool reused = !newlyPrepared.has_value();

            const auto publicationTime = Clock::now();
            const auto scheduledTime =
                tick.experimentStart +
                std::chrono::milliseconds(tick.scheduledTimeMs);
            const auto sampleTime =
                tick.experimentStart +
                std::chrono::milliseconds(lastPublishedData->timestampMs);

            const double latenessMs =
                toMilliseconds(publicationTime - scheduledTime);
            const double dataAgeMs =
                toMilliseconds(publicationTime - sampleTime);

            // Policy: better to publish nothing than data that is too old.
            if (config.maxAgeMs > 0 && dataAgeMs > config.maxAgeMs)
            {
                ++withheldPublications;
                return;
            }
            if (reused)
            {
                ++reusedPublications;
            }

            publicationLateness.add(latenessMs);
            dataAge.add(dataAgeMs);

            publications.push_back({
                tick.tickIndex,
                lastPublishedData->value,
                lastPublishedData->timestampMs,
                tick.scheduledTimeMs,
                toMilliseconds(publicationTime - tick.experimentStart),
                latenessMs,
                dataAgeMs,
                reused
            });
        },
        sharedStart);

    // Stop independent workers before reading their final statistics.
    producer.stop();
    processor.stop();
    const ProcessorStats processorStats = processor.stats();

    // Processing latency: from the processor thread (B) or measured inline (A).
    LatencyStats processing = inlineProcessing;
    if (approachB)
    {
        processing.totalMs = processorStats.totalProcessingMs;
        processing.maximumMs = processorStats.maximumProcessingMs;
        processing.count = processorStats.processedSamples;
    }

    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < std::min<std::size_t>(publications.size(), 15); ++i)
    {
        const auto& p = publications[i];
        std::cout << "Tick " << p.tickIndex
                  << " | Value " << p.value
                  << " | Sample " << p.sampleTimestampMs << " ms"
                  << " | Scheduled " << p.scheduledTimeMs << " ms"
                  << " | Actual " << p.actualTimeMs << " ms"
                  << " | Lateness " << p.latenessMs << " ms"
                  << " | Data age " << p.dataAgeMs << " ms"
                  << " | " << (p.reused ? "REUSED" : "NEW") << '\n';
    }

    // Buffer underrun: the publisher tick found no NEW prepared sample
    // (either nothing was ever ready, or the last value had to be repeated).
    const std::size_t publisherUnderruns = ticksWithoutNewData;
    const std::size_t totalTicks = schedulerStats.executedTicks;
    const double underrunPercent =
        totalTicks ? 100.0 * static_cast<double>(publisherUnderruns) /
                         static_cast<double>(totalTicks)
                   : 0.0;

    std::cout << "\n========== STATISTICS (APPROACH " << config.approach
              << (approachB ? ": separate processing thread" : ": processing in tick")
              << ") ==========\n"
              << "Scenario: " << config.scenario
              << " | tick " << config.tickIntervalMs << " ms"
              << " | sample " << config.sampleIntervalMs << " ms"
              << " | processing " << config.processingMs << " ms";
    if (config.rampToMs > 0)
    {
        std::cout << " -> " << config.rampToMs << " ms (ramp)";
    }
    if (config.delayProbability > 0.0 && config.delayMs > 0)
    {
        std::cout << " | delay " << config.delayProbability * 100.0 << "% x "
                  << config.delayMs << " ms";
    }
    std::cout << '\n'
              << "Processing mode: "
              << (mode == ProcessingMode::CpuWork ? "CpuWork" : "Sleep") << '\n'
              << "Produced samples: " << producer.producedSamples() << '\n'
              << "Delayed samples (injected): " << producer.delayedSamples() << '\n'
              << "Timing: " << config.timing
              << (config.timing == "hybrid"
                      ? " (spin " + std::to_string(config.spinUs) + " us)"
                      : std::string())
              << " | RT " << (tuning.realtimeApplied ? "on" : "off")
              << " | pin "
              << (tuning.pinApplied ? std::to_string(config.pinCpu) : "off")
              << " | mlock " << (tuning.mlockApplied ? "on" : "off")
              << " | stress threads " << stressThreads << '\n'
              << "Withheld publications (data older than "
              << config.maxAgeMs << " ms): " << withheldPublications << '\n'
              << "Buffer underruns (publisher, no new data): "
              << publisherUnderruns << " (" << underrunPercent << "% of ticks)\n"
              << "Buffer underruns (processor input empty): ";
    if (approachB)
    {
        std::cout << processorStats.emptyBufferPolls << '\n';
    }
    else
    {
        std::cout << "n/a (no processor thread)\n";
    }
    std::cout << "Processed samples: " << processing.count << '\n'
              << "Published events: " << publications.size() << '\n'
              << "Reused publications: " << reusedPublications << '\n'
              << "Ticks without prepared data: " << ticksWithoutPreparedData << '\n'
              << "Executed ticks: " << schedulerStats.executedTicks << '\n'
              << "Skipped ticks: " << schedulerStats.skippedTicks << '\n'
              << "Deadline misses (scheduler callback): "
              << schedulerStats.deadlineMisses << '\n'
              << "Raw buffer overflow drops: " << rawBuffer.overflowDrops() << '\n'
              << "Raw buffer stale drops: " << rawBuffer.staleDrops() << '\n'
              << "Prepared buffer overflow drops: "
              << preparedBuffer.overflowDrops() << '\n'
              << "Prepared buffer stale drops: "
              << preparedBuffer.staleDrops() << '\n'
              << "Raw buffer size: " << rawBuffer.size() << '\n'
              << "Prepared buffer size: " << preparedBuffer.size() << '\n'
              << "Elapsed time: " << schedulerStats.elapsedMs << " ms\n\n"
              << "========== SCHEDULER TIMING ==========\n"
              << "Tick start lateness: avg "
              << schedulerStats.timing.averageStartLatenessMs
              << " ms | max " << schedulerStats.timing.maximumStartLatenessMs
              << " ms\n"
              << "Interval jitter (absolute error): avg "
              << schedulerStats.timing.averageAbsoluteIntervalJitterMs
              << " ms | p95 " << schedulerStats.timing.p95AbsoluteIntervalJitterMs
              << " ms | p99 " << schedulerStats.timing.p99AbsoluteIntervalJitterMs
              << " ms | max " << schedulerStats.timing.maximumAbsoluteIntervalJitterMs
              << " ms\n"
              << "Measured intervals: " << schedulerStats.timing.intervalSamples
              << "\n\n";

    if (processing.count)
    {
        std::cout << "Processing latency: avg " << processing.averageMs()
                  << " ms | max " << processing.maximumMs << " ms\n";
    }
    if (publicationLateness.count)
    {
        std::cout << "Publication lateness: avg "
                  << publicationLateness.averageMs()
                  << " ms | max " << publicationLateness.maximumMs
                  << " ms\n"
                  << "Data age: avg " << dataAge.averageMs()
                  << " ms | max " << dataAge.maximumMs
                  << " ms\n";
    }

    // ---- CSV export (plain doubles, '.' decimal separator) ----
    if (!config.csvPath.empty())
    {
        std::ofstream csv(config.csvPath);
        csv << "tick,scheduled_ms,actual_ms,lateness_ms,data_age_ms,reused\n";
        for (const auto& p : publications)
        {
            csv << p.tickIndex << ',' << p.scheduledTimeMs << ','
                << p.actualTimeMs << ',' << p.latenessMs << ','
                << p.dataAgeMs << ',' << (p.reused ? 1 : 0) << '\n';
        }
        if (!csv)
        {
            std::cerr << "Failed to write " << config.csvPath << '\n';
            return 2;
        }
    }
    if (!config.summaryPath.empty())
    {
        bool needHeader;
        {
            std::ifstream probe(config.summaryPath);
            needHeader = !probe.good() || probe.peek() == EOF;
        }
        std::ofstream summary(config.summaryPath, std::ios::app);
        if (needHeader)
        {
            summary << "scenario,approach,mode,seed,tick_ms,sample_ms,processing_ms,"
                       "ramp_to_ms,delay_prob,delay_ms,produced,delayed,published,"
                       "executed_ticks,skipped_ticks,deadline_misses,underruns,"
                       "underrun_pct,raw_overflow_drops,start_late_avg_ms,"
                       "start_late_max_ms,jitter_avg_ms,jitter_p95_ms,jitter_p99_ms,"
                       "jitter_max_ms,proc_avg_ms,proc_max_ms,pub_late_avg_ms,"
                       "pub_late_max_ms,data_age_avg_ms,data_age_max_ms,label,"
                       "timing,spin_us,rt_requested,rt_applied,pin_cpu,"
                       "pin_applied,mlock_applied,stress_threads,max_age_ms,"
                       "withheld\n";
        }
        const auto& t = schedulerStats.timing;
        summary << config.scenario << ',' << config.approach << ','
                << (mode == ProcessingMode::CpuWork ? "cpu" : "sleep") << ','
                << config.seed << ',' << config.tickIntervalMs << ','
                << config.sampleIntervalMs << ',' << config.processingMs << ','
                << config.rampToMs << ',' << config.delayProbability << ','
                << config.delayMs << ',' << producer.producedSamples() << ','
                << producer.delayedSamples() << ',' << publications.size() << ','
                << schedulerStats.executedTicks << ','
                << schedulerStats.skippedTicks << ','
                << schedulerStats.deadlineMisses << ',' << publisherUnderruns << ','
                << underrunPercent << ',' << rawBuffer.overflowDrops() << ','
                << t.averageStartLatenessMs << ',' << t.maximumStartLatenessMs << ','
                << t.averageAbsoluteIntervalJitterMs << ','
                << t.p95AbsoluteIntervalJitterMs << ','
                << t.p99AbsoluteIntervalJitterMs << ','
                << t.maximumAbsoluteIntervalJitterMs << ','
                << processing.averageMs() << ',' << processing.maximumMs << ','
                << publicationLateness.averageMs() << ','
                << publicationLateness.maximumMs << ','
                << dataAge.averageMs() << ',' << dataAge.maximumMs << ','
                << config.label << ',' << config.timing << ',' << config.spinUs
                << ',' << (config.rtPriority > 0 ? 1 : 0) << ','
                << (tuning.realtimeApplied ? 1 : 0) << ',' << config.pinCpu << ','
                << (tuning.pinApplied ? 1 : 0) << ','
                << (tuning.mlockApplied ? 1 : 0) << ',' << stressThreads << ','
                << config.maxAgeMs << ',' << withheldPublications << '\n';
        if (!summary)
        {
            std::cerr << "Failed to write " << config.summaryPath << '\n';
            return 2;
        }
    }
}
