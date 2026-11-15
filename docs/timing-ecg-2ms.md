# Can per-tick file reads keep up with ECG at 2 ms?

Question from the DDPS team: ECG_Wave is sent every 2 ms with 8 channels; is reading
the samples from files on every tick too slow?

**Short answer:** on average reading is cheap (~5 µs per tick, 0.3% of the budget), but the
tail is not: the few ticks where `std::ifstream` has to go to the kernel (first read, buffer
refill, wrap-around) occasionally take **2–14 ms**, which misses deadlines and skips ticks.
Preloading the channel files removes those spikes, and so does async double buffering; warming
up the buffer before the first tick does not. OS wake-up jitter remains in every mode.

## Setup
- `sender --channels 8 --period 2`: every tick reads one 2-byte sample from each of 8 channels
  and sends a 24 B payload (+18 B framing) over a socat pty pair.
- `--read per-tick`: one `std::ifstream` per channel, `read()` of 2 bytes per tick and
  `seekg(0)` on wrap, the same access pattern as DDPS `ChildMetricReader`.
- `--read preload`: files loaded into memory up front; baseline without file I/O.
- WSL2 (Ubuntu) on a Windows laptop, default scheduler, no real-time tuning.
- Run 1: `SpO2_Numerics.bin` (3.6 KB) for all 8 channels, 10 s, 3 runs per mode.
- Run 2: real ECG test signals `ecg-1..8.bin` (500 Hz, 2 B/sample, 57,856 B = 57.9 s each, one file
  per channel) from `ecg-client-simulator/testsignals/InputData`, 60 s = 30,000 ticks.
  Files copied to the WSL ext4 disk (already in page cache); one extra run read them from `/mnt/c`.

## Results (µs, budget per tick = 2000 µs)

### Run 1: small file (3.6 KB, fits one ifstream buffer)

| | read 8 ch avg / p99 / max | tick lateness avg / p99 / max | skipped ticks |
|---|---|---|---|
| per-tick | ~6 / ~20 / ≤374 | ~215 / ~460 / ≤1805 | 0, 0, 2 |
| preload  | ~1 / ~3 / ≤136 | ~195 / ~440 / ≤1360 | 0, 2, 0 |

### Run 2: real ECG files, 60 s each

| | read avg / p99 / max | reads > 500 µs | deadline misses | skipped ticks | longest arrival gap |
|---|---|---|---|---|---|
| per-tick (ext4) | 5.5 / 15–28 / **11,370–13,738** | 3 per run | 2, 4, 3 | 33, 17, 9 | 13–16.5 ms |
| per-tick (`/mnt/c`) | 6.8 / 23 / **12,621** | – | 9 | 37 | 14 ms |
| preload | 1.6–1.8 / 6 / 128–184 | 0 | 0, 0 | 0, 13 | 3–8 ms |

Where the slow reads happen (per-tick, ext4): tick 0 (first fill, 4–5 ms), the wrap-around at
sample 28,928 (`seekg(0)` drops the buffer, 2–6 ms), and one buffer refill per run (libstdc++
buffer = 8191 B ≈ every 4096 samples ≈ 8 s, 12–14 ms). All 8 channels have the same file size,
so they refill on **the same tick**.

### Run 3: four read modes, real ECG files, 3 × 60 s each (modes interleaved)

- `per-tick`: `ifstream::read` on every tick (as above).
- `warmup`: the same, but each `ifstream` fills its buffer (`peek()`) before the first tick.
- `preload`: all 8 files in memory before the first tick (~460 KB).
- `async`: per channel two chunks of 4096 samples; the tick reads one from memory while a
  background thread refills the other from the file (double buffering).

| mode | read avg | longest read (worst run) | reads > 500 µs (3 runs) | deadline misses (sum) | skipped ticks (sum) |
|---|---|---|---|---|---|
| per-tick | 5.3 µs | 11.7 ms | 6 | 8 | 23 |
| warmup | 5.3 µs | 10.5 ms | 7 | 6 | 26 |
| preload | 1.6 µs | 0.24 ms | 0 | **0** | 2 |
| async | 2.1 µs | 0.68 ms | 2 | **0** | 6 |

- `warmup` removes only the first-read spike; buffer refills (ticks ~16383, 20477, 24578, 28672) and
  the wrap-around (tick ~28931) still take 3–11 ms.
- `async` never had to wait for a chunk (0 underruns in all runs). Its longest reads (0.6–0.7 ms)
  are at the chunk swap, where the tick thread wakes the loader thread.
- Skipped ticks in `preload`/`async` come from late wake-ups (lateness max 1.2–1.9 ms in every mode),
  not from reading. All 12 runs delivered every sent packet: `bad=0 gaps=0`.

## Conclusion
- The average cost of reading per tick is negligible; the team's worry is about the **tail**, and
  there it is justified: a refill or wrap that lands on a slow syscall blocks the tick thread for
  several periods. With 2 ms periods any stall above ~2 ms is a missed deadline.
- Preloading removes those spikes completely (max read ≤ 184 µs, no deadline misses). The data is
  small: 8 × 58 KB ≈ 460 KB for ECG, 4 × 600 KB for `NSR120`. Preloading at start is the simplest fix
  for DDPS. If files ever get too large for memory, async double buffering (measured: 0 deadline
  misses, 0 underruns) works too, at the cost of a thread and synchronization — the prototype's first
  version refilled chunks out of order when both were handed back at once.
- Warming up the buffer alone is not enough: it fixes only the start.
- OS wake-up jitter (~200 µs avg, up to ~1.8 ms, occasional skipped ticks) is present in **both**
  modes and is the next limit: real-time priority (`SCHED_FIFO`), CPU pinning and an RT kernel on target.

## Not covered
- The target hardware/kernel: WSL2 numbers show the order of magnitude only.
- Cold page cache (dropping caches needs root): would make refills slower, which only strengthens the case for preloading.

Reproduce (8 files, one per channel):
`./run.sh "$(ls /path/to/ecg-{1..8}.bin | paste -sd,)" --period 2 --channels 8 --duration 60 [--read per-tick|warmup|preload|async]`
