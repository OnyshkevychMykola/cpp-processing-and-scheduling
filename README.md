# PTY data playback prototype (C++)

A small prototype for the demo data playback service (DDPS): a **sender** reads data
from binary files and sends it as framed packets over a virtual serial link to a
**receiver** that runs as a separate process. The link is a `socat` pty pair — the same
mechanism `synergy-base` uses for the ECG simulator.

It was built to answer one question: **can reading samples from files on every tick keep
up with ECG at a 2 ms period (8 channels)?** Short answer: on average yes (~5 µs per tick),
but rare file-system calls take 2–14 ms and miss deadlines; preloading the files (or
async double buffering) removes those spikes. Details: [docs/timing-ecg-2ms.md](docs/timing-ecg-2ms.md).

```
sender ──frames──▶ /tmp/ttyHC0 ═══ socat pty pair ═══ /tmp/ttyHC1 ──▶ receiver
  │ one packet per PeriodicScheduler tick                 │ decode, check, measure
  └ timing report (read / write / lateness)               └ arrival intervals, latency
```

## Requirements

Linux (tested on WSL2 Ubuntu), `g++` with C++17, and `socat`:

```bash
sudo apt install g++ socat
```

## Running

`run.sh` builds both programs, creates the pty pair, starts the receiver, runs the sender
and prints the sender report, then the receiver report.

```bash
# send a file once, 64 bytes every 100 ms
./run.sh

# ECG-like: 8 channels (one file per channel), one sample each every 2 ms, for 60 s
./run.sh "$(ls /path/to/ecg-{1..8}.bin | paste -sd,)" --period 2 --channels 8 --duration 60

# the same with another way of reading the files
./run.sh "$(ls /path/to/ecg-{1..8}.bin | paste -sd,)" --period 2 --channels 8 --duration 60 --read preload

# one line per received packet
VERBOSE=1 ./run.sh
```

### Modes

**File mode** (default, no `--channels`): the file is cut into `--chunk` byte packets and
sent once; then an empty end-of-stream packet.

**Channel mode** (`--channels N`): emulates a frontend with N channels. Every tick takes
one 2-byte sample from each channel and sends them as one packet (24 B for 8 channels),
for `--duration` seconds. The data wraps around at the end of each file. `<file>` is one
file for all channels or a comma-separated list with one file per channel.

How channel mode reads the files (`--read`):

| mode | how it reads | measured on 8 ECG files, 2 ms |
|---|---|---|
| `per-tick` (default) | one `std::ifstream` per channel, `read()` on every tick — like DDPS `ChildMetricReader` | spikes up to ~12 ms, missed deadlines |
| `warmup` | `per-tick`, but each buffer is filled before the first tick | only the start spike is gone |
| `preload` | all files in memory before the first tick | no missed deadlines |
| `async` | two chunks per channel; a background thread refills one while the tick reads the other | no missed deadlines |

### Sender options

| option | default | meaning |
|---|---|---|
| `--period MS` | 100 | tick period |
| `--chunk BYTES` | 64 | file mode: packet payload size (max 4096) |
| `--channels N` | 0 | channel mode with N channels (0 = file mode) |
| `--duration S` | 10 | channel mode: how long to send |
| `--read MODE` | `per-tick` | channel mode: `per-tick`, `warmup`, `preload`, `async` |
| `--async-chunk SAMPLES` | 4096 | `async`: samples per chunk |

## Reading the report

All times are microseconds. Each line shows `n` (how many), `avg`, `p50`, `p99` and `max`:

- **p50** (median): half of the values are below it — the typical tick.
- **p99**: 99% of the values are below it — the near-worst case without single outliers.
- **max**: the single worst value. With a hard period the worst tick matters, not the average.

Sender:

| line | meaning |
|---|---|
| `ticks executed / skipped` | ticks that ran / ticks the scheduler skipped because it was already late by a whole period (their packets are not sent) |
| `deadlineMisses` | ticks whose work did not finish before the next tick was due |
| `tick start lateness` | how late the thread woke up compared to the schedule (OS scheduling) |
| `read N ch` | time to get the samples of all channels for one tick |
| `encode + write` | building the frame and writing it to the port |
| `slow reads (> 500 us)` | reads slower than 0.5 ms, with their tick index (`tick:us`) |
| `async underruns` | ticks that had to wait because the next chunk was not loaded yet |

Receiver:

| line | meaning |
|---|---|
| `packets / bytes` | what arrived |
| `bad` | frames dropped because of a bad checksum or length |
| `gaps` | breaks in the sequence numbers of received packets |
| `arrival interval` | time between two received packets (ideal: exactly the period) |
| `delivery latency` | time from sending to receiving (both processes use `CLOCK_MONOTONIC`) |

## Wire format

Little-endian frame, resynchronised on the marker after garbage or a corrupt frame:

```
[0xAA 0x55][uint16 payloadLen][uint32 seq][int64 timestampUs][payload][uint16 checksum]
```

`checksum` is the 16-bit wrapping sum of all bytes from `payloadLen` to the end of the
payload. `timestampUs` is `steady_clock` in microseconds. An empty payload ends the stream.

## Files

| file | role |
|---|---|
| `sender.cpp` | file and channel modes, channel readers, timing report |
| `receiver.cpp` | decoding, arrival / latency statistics |
| `Frame.hpp/.cpp` | frame encoding and the resynchronising decoder |
| `SerialPort.hpp/.cpp` | opens a tty in raw mode, full writes, blocking reads |
| `PeriodicScheduler.hpp/.cpp` | fixed-period tick loop; skips ticks when late |
| `Packet.hpp`, `Stats.hpp` | packet type and microsecond clock; avg/p50/p99/max printing |
| `run.sh` | build, socat pair, run both, print reports |
| `data/SpO2_Numerics.bin` | small sample file (3.6 KB) |
| `docs/timing-ecg-2ms.md` | measurements and conclusions |

Numbers in this repository come from WSL2 on a laptop without real-time tuning: they show
orders of magnitude, not what the target hardware will do.
