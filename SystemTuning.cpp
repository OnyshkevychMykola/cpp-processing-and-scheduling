#include "SystemTuning.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#endif

namespace
{
std::atomic<std::uint64_t> loadSink{0};

#ifdef __linux__
// CPUs this process may use, in ascending order.
std::vector<int> allowedCpus()
{
    std::vector<int> cpus;
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
        {
            if (CPU_ISSET(cpu, &set))
            {
                cpus.push_back(cpu);
            }
        }
    }
    return cpus;
}
#endif
} // namespace

int availableCpuCount()
{
#ifdef __linux__
    const auto cpus = allowedCpus();
    if (!cpus.empty())
    {
        return static_cast<int>(cpus.size());
    }
#endif
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : static_cast<int>(n);
}

TuningResult tuneCurrentThread(int rtPriority, int pinCpu, bool mlockAll)
{
    TuningResult result;
#ifdef __linux__
    if (pinCpu >= 0)
    {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(pinCpu, &set);
        const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
        result.pinApplied = rc == 0;
        if (rc != 0)
        {
            result.notes += std::string("pin failed: ") + std::strerror(rc) + "; ";
        }
    }
    if (rtPriority > 0)
    {
        sched_param param{};
        param.sched_priority = rtPriority;
        const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        result.realtimeApplied = rc == 0;
        if (rc != 0)
        {
            result.notes +=
                std::string("SCHED_FIFO failed: ") + std::strerror(rc) + "; ";
        }
    }
    if (mlockAll)
    {
        const bool ok = mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
        result.mlockApplied = ok;
        if (!ok)
        {
            result.notes += std::string("mlockall failed: ") +
                            std::strerror(errno) + "; ";
        }
    }
#else
    if (rtPriority > 0 || pinCpu >= 0 || mlockAll)
    {
        result.notes = "unsupported on this platform";
    }
#endif
    return result;
}

BackgroundLoad::BackgroundLoad(int threads, int avoidCpu)
{
#ifdef __linux__
    std::vector<int> cpus = allowedCpus();
#endif
    for (int i = 0; i < threads; ++i)
    {
        workers_.emplace_back([this, avoidCpu
#ifdef __linux__
                               , cpus
#endif
                              ] {
#ifdef __linux__
            if (avoidCpu >= 0)
            {
                cpu_set_t set;
                CPU_ZERO(&set);
                bool any = false;
                for (const int cpu : cpus)
                {
                    if (cpu != avoidCpu)
                    {
                        CPU_SET(cpu, &set);
                        any = true;
                    }
                }
                if (any)
                {
                    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
                }
            }
#else
            (void)avoidCpu;
#endif
            std::uint64_t state = 0x9e3779b97f4a7c15ULL;
            while (!stop_.load(std::memory_order_relaxed))
            {
                for (int k = 0; k < 4096; ++k)
                {
                    state ^= state << 13;
                    state ^= state >> 7;
                    state ^= state << 17;
                }
            }
            loadSink.store(state, std::memory_order_relaxed);
        });
    }
}

BackgroundLoad::~BackgroundLoad()
{
    stop_.store(true);
    for (auto& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}
