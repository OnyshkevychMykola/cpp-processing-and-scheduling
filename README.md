# Real-Time Data Processing and Scheduling in C++ (PoC)

A proof of concept that studies how **scheduling, buffering and concurrent
processing** affect the timing accuracy and reliability of periodic data
publication. It simulates a data source, a processing stage and a periodic
publisher, injects delays and load, and measures jitter, deadline misses,
buffer underruns and latency.

## Architecture

```
Producer (500 Hz) -> raw buffer -> Processor -> prepared buffer -> Scheduler (250 Hz) -> publish
```

| Component | Files | Role |
|---|---|---|
| `MeasurementProducer` | `MeasurementProducer.*` | Thread that emits a sample every `--sample-ms`; can delay delivery (`--delay-prob`, `--delay-ms`). |
| `MeasurementBuffer` | `MeasurementBuffer.hpp` | Mutex-protected bounded queue. Counts overflow drops and stale drops. |
| `MeasurementProcessor` | `MeasurementProcessor.*` | Simulates processing (sleep or busy CPU), optionally ramping the load up over time. |
| `PeriodicScheduler` | `PeriodicScheduler.*` | Fires a callback every tick, measures start lateness, interval jitter (avg/p95/p99/max), skipped ticks and deadline misses. Optional hybrid wait (sleep, then spin). |
| `SystemTuning` | `SystemTuning.*` | Linux-only helpers: pin to a CPU, `SCHED_FIFO`, `mlockall`, and busy background load threads. |
| `main.cpp` | `main.cpp` | CLI, scenarios, publication policy, statistics and CSV export. |

All components share one time origin. The publisher never blocks on the
processor: if no new prepared sample exists it repeats the last value.

### Two approaches to processing

- **A, in the tick:** the scheduler takes the newest raw sample and processes
  it inside the tick callback. Slow processing delays the tick.
- **B, separate thread (default):** a processor thread prepares samples; the
  tick only picks up finished data and never waits.

Select with `--approach A|B`.

## Build and run

Requires a C++17 compiler (tested with g++ 15 on Ubuntu/WSL2) and pthreads.

```bash
./run.sh                                  # build + run defaults (approach B, 2 s)
./run.sh cpu --scenario load-ramp --approach A
```

`run.sh` builds with `g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread`
and passes all arguments to `./main`. Any invalid argument (for example
`./main --help`) prints the full option list and exits with an error.

## Scenarios

| Scenario | What it does |
|---|---|
| `default` | 12 ms processing (slow processor, original setup). |
| `baseline` | 3 ms processing, no delays: scheduling accuracy. |
| `delayed` | baseline + 5% of samples delivered 20 ms late (late samples block the queue, so data arrives in bursts). |
| `load-ramp` | Processing time grows from 2 ms to 20 ms over the test. |
| `stress` | baseline + busy background threads (2 per CPU) competing for the cores. |

Every scenario value can be overridden by an option.

## Options

| Option | Meaning |
|---|---|
| `sleep` / `cpu`, `--mode` | Processing simulated by sleeping or by busy CPU work. |
| `--scenario NAME` | Preset (see above). |
| `--approach A\|B` | Where processing runs. |
| `--sample-ms`, `--tick-ms`, `--processing-ms`, `--duration-ms` | Timing parameters. |
| `--raw-cap`, `--prepared-cap` | Buffer capacities. |
| `--delay-prob P`, `--delay-ms N`, `--seed N` | Delayed delivery. |
| `--ramp-to-ms N` | Ramp processing time up to N ms. |
| `--timing sleep\|hybrid`, `--spin-us N` | Hybrid wait: sleep, then spin the last N us (default 300). |
| `--pin-cpu N` | Pin the scheduler thread to CPU N; background load avoids that CPU. |
| `--rt-priority N` | `SCHED_FIFO` priority (needs `CAP_SYS_NICE`; failure is reported and ignored). |
| `--mlock 0\|1` | `mlockall` (may fail on `RLIMIT_MEMLOCK`; reported and ignored). |
| `--stress N\|auto` | Background busy threads (`auto` = 2 x CPUs). |
| `--max-age-ms N` | Do not publish data older than N ms (0 = always publish). |
| `--csv FILE` | Per-publication rows. |
| `--summary FILE` | Append one summary row per run (header added if empty). |
| `--label TEXT` | Tag stored in the summary row. |

## Metrics

| Metric | Definition |
|---|---|
| Interval jitter | `abs(actual interval - scheduled interval)` between consecutive executed ticks: avg, p95, p99, max. |
| Tick start lateness | Actual tick start minus its scheduled time. |
| Deadline miss | The callback finished after `scheduled time + tick interval`. |
| Skipped ticks | Ticks whose time had already passed when the scheduler was free again. |
| Buffer underrun (publisher) | A tick found no new prepared sample (the last value is repeated, or nothing is published). Reported as a count and as % of ticks. |
| Buffer underrun (processor) | The processor polled an empty raw buffer. |
| Processing latency | Time to process one sample. |
| Publication lateness | Publication time minus scheduled tick time. |
| Data age | Publication time minus the sample's own timestamp. |

Note: underruns of A and B are not directly comparable, because A skips ticks
under overload instead of repeating data. Read underruns together with
skipped ticks.

## Benchmarks

Scripts run sequentially so runs do not disturb each other's timing.

```bash
./bench.sh 5 sleep 10000      # A vs B, 3 scenarios -> results/sleep/
./bench.sh 5 cpu   10000      # same in cpu mode    -> results/cpu/
./bench_extra.sh 5 10000 all  # timing techniques and max-age -> results/timing/, results/age/
```

Arguments: runs per configuration, mode (`bench.sh` only), duration in ms.
`bench_extra.sh` skips the `SCHED_FIFO` configurations when they are not
permitted.

Plots need Python with `pandas` and `matplotlib`:

```bash
python plot.py results/sleep
python plot.py results/cpu
python plot_extra.py results
```

Each results folder contains `summary.csv` (one row per run), per-publication
CSVs for run 1, `summary_table.md` and `fig_*.png`.

## Results summary

Median of 5 runs of 10 s each, WSL2, sleep mode. Details and error bars are in
`results/`.

**A (processing in the tick) vs B (separate thread), `load-ramp`:**

| | A | B |
|---|---|---|
| Publication lateness (avg) | 9.0 ms | 0.27 ms |
| Skipped ticks | ~1570 of 2500 | 0 |
| Deadline misses | 724 | 0 |
| Data age (avg) | 10.3 ms | 18.2 ms |
| Publisher underruns | 0% | 54.5% |

- B keeps publication on time; A pays with skipped ticks, B pays with repeated
  (older) data.
- Interval jitter p99 is similar for A and B (differences are inside the
  run-to-run spread).
- Delayed delivery hurts both approaches equally (data age peaks near 100 ms).

**Timing consistency under CPU contention (approach B):**

| Configuration | Start lateness avg | Jitter p95 | Jitter p99 | Jitter max |
|---|---|---|---|---|
| idle | 0.22 ms | 0.20 ms | 0.30 ms | 0.64 ms |
| stress | 0.10 | 0.26 | 3.38 | 3.85 |
| stress + hybrid | 0.14 | 1.71 | 3.03 | 3.97 |
| stress + pin | 0.12 | 0.12 | 1.57 | 3.84 |
| stress + pin + hybrid | 0.03 | 0.04 | 1.90 | 3.83 |

Pinning to an isolated core helps; spinning without isolation hurts; rare
3-4 ms outliers remain in every configuration (cause not established).

**Max-age policy under delayed delivery:** a 20 ms limit cuts the maximum
published data age from 96 ms to 18.5 ms at the cost of 30% of ticks with
nothing published.

## Limitations

- Runs are short (10 s) with only 5 repeats; treat results as orders of
  magnitude, not precise figures.
- Measured on WSL2 (a hypervisor sits under the Linux kernel), not on a
  real-time OS.
- `SCHED_FIFO` and `mlockall` were not tested (insufficient privileges in the
  test environment); the code reports and ignores such failures.
- Load is simulated (sleep or busy loop), not real signal processing.
- Mutex-protected buffers; no lock-free implementation.
