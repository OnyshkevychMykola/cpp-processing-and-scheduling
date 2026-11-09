#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

// Result of tuning the CALLING thread. Everything is best effort: a failed
// step (typically EPERM without CAP_SYS_NICE / RLIMIT_RTPRIO) is reported in
// `notes` and the run continues untuned.
struct TuningResult
{
    bool realtimeApplied = false;
    bool pinApplied = false;
    bool mlockApplied = false;
    std::string notes;
};

// Applies to the calling thread (and threads it creates later):
//   rtPriority > 0  -> SCHED_FIFO with that priority
//   pinCpu >= 0     -> restrict to that CPU
//   mlockAll        -> mlockall(MCL_CURRENT | MCL_FUTURE), no page faults
// Linux only; elsewhere every step reports "unsupported".
TuningResult tuneCurrentThread(int rtPriority, int pinCpu, bool mlockAll);

// Number of CPUs usable by this process (>= 1).
int availableCpuCount();

// Busy-loop threads that simulate other workloads competing for the CPU.
// If avoidCpu >= 0 the load threads never run on that CPU, which models an
// isolated core. Stops and joins in the destructor.
class BackgroundLoad
{
public:
    BackgroundLoad(int threads, int avoidCpu);
    ~BackgroundLoad();

    BackgroundLoad(const BackgroundLoad&) = delete;
    BackgroundLoad& operator=(const BackgroundLoad&) = delete;

    int threadCount() const { return static_cast<int>(workers_.size()); }

private:
    std::atomic<bool> stop_{false};
    std::vector<std::thread> workers_;
};
