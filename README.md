# Real-Time Data Processing and Scheduling in C++ (clean version)

A stripped-down version of the PoC on `master`, kept for reading the code. It
has the same pipeline and the same two approaches, but no metrics, benchmarks
or system tuning. For jitter, latency, data age and the benchmark results, see
the `master` branch.

## Pipeline

```
Producer (500 Hz) -> raw buffer -> Processor -> prepared buffer -> Scheduler (250 Hz) -> publish
```

| Component | Files | Role |
|---|---|---|
| `MeasurementProducer` | `MeasurementProducer.*` | Thread that pushes a sample every 2 ms; can deliver some samples late. |
| `MeasurementBuffer` | `MeasurementBuffer.hpp` | Mutex-protected bounded queue. Hands out the newest sample and drops older ones. |
| `MeasurementProcessor` | `MeasurementProcessor.*` | Thread that "processes" samples (sleep or busy CPU), optionally slower over time. |
| `PeriodicScheduler` | `PeriodicScheduler.*` | Calls a callback every 4 ms; skips ticks that are already in the past. |
| `main.cpp` | `main.cpp` | Wires everything together and prints each publication. |

All parts share one time origin. The publisher never waits: if no new sample
is ready it repeats the last value (`REPEAT` in the output).

## Two approaches

- **A, in the tick:** the scheduler takes the newest raw sample and processes
  it inside its own callback. Slow processing delays the tick, and the
  scheduler skips the ticks it missed (gaps in the tick numbers).
- **B, separate thread (default):** the processor thread prepares samples;
  the tick only picks up finished data.

## Build and run

Requires a C++17 compiler and pthreads (Linux or WSL).

```bash
./run.sh                                          # approach B, default scenario
./run.sh --approach A --scenario load-ramp        # watch the ticks get skipped
./run.sh --approach B --scenario load-ramp        # same load, values repeat instead
./run.sh cpu --scenario delayed --duration-ms 400
```

| Option | Meaning |
|---|---|
| `sleep` / `cpu` | Processing simulated by sleeping or by busy CPU work. |
| `--approach A\|B` | Where processing runs. |
| `--scenario NAME` | `default` (12 ms processing), `baseline` (3 ms), `delayed` (5% of samples 20 ms late), `load-ramp` (2 ms -> 20 ms). |
| `--duration-ms N` | Run length, default 200 ms. |
