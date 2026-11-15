# Can per-tick file reads keep up with ECG at 2 ms?

Question from the DDPS team: ECG_Wave is sent every 2 ms with 8 channels; is reading
the samples from files on every tick too slow?

## Setup
- `sender --channels 8 --period 2 --duration 10`: every tick reads one 2-byte sample from
  each of 8 channels and sends a 24 B payload (+18 B framing) over a socat pty pair.
- `--read per-tick`: one `std::ifstream` per channel, `read()` of 2 bytes per tick and
  `seekg(0)` on wrap — the same access pattern as DDPS `ChildMetricReader`.
- `--read preload`: the file is in memory; baseline without file I/O.
- WSL2 (Ubuntu) on a Windows laptop, default scheduler, no real-time tuning. 3 runs per mode, 5000 ticks each.
- Data: `SpO2_Numerics.bin` (3.6 KB) for all 8 channels — timing does not depend on content.

## Results (µs, budget per tick = 2000 µs)

| | read 8 ch avg | read 8 ch p99 | read 8 ch max | tick start lateness avg / p99 / max | whole tick p99 | skipped ticks |
|---|---|---|---|---|---|---|
| per-tick | 5.5–6.3 | 16–22 | 111–374 | ~215 / ~460 / 940–1805 | 155–227 | 0, 0, 2 |
| preload  | 0.9–1.1 | 2–4 | 42–136 | ~195 / ~440 / 1090–1360 | 132–165 | 0, 2, 0 |

Receiver: arrival interval avg 2000 µs, p99 ~2500–2600 µs in both modes; worst single gap 3.4–10 ms in both modes.

## Conclusion
- Reading 8 channels per tick costs ~6 µs on average, ~20 µs at p99 — about 1% of the 2 ms budget.
  `ifstream` buffers the file, so most `read()` calls are a memory copy; the syscall happens only on refill/wrap.
- The timing risk is the OS, not the file: the thread wakes ~200 µs late on average and up to ~1.8 ms
  late in the worst case, and occasionally a whole tick is skipped — in **both** modes.
- So the team's worry about file reading is not justified for page-cached files. What deserves
  attention is wake-up jitter: real-time priority (`SCHED_FIFO`), CPU pinning, and an RT kernel on target.

## Not covered
- Large files not in the page cache (cold disk): an `ifstream` refill could then take milliseconds.
  If that matters, preload the files or read ahead on a separate thread.
- The target hardware/kernel: WSL2 numbers show the order of magnitude only.

Reproduce: `./run.sh data/SpO2_Numerics.bin --period 2 --channels 8 --duration 10 [--read preload]`
