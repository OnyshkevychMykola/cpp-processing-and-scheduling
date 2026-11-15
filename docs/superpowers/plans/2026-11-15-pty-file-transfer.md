# PTY File Transfer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two programs, `sender` and `receiver`, that move a binary file in framed packets over a `socat` pty pair.

**Architecture:** `sender` chunks a file into `Packet`s and writes one encoded frame per `PeriodicScheduler` tick to `/tmp/ttyHC0`; `receiver` reads `/tmp/ttyHC1`, resyncs on the `0xAA55` marker, decodes, verifies checksum, prints per-packet lines and totals.

**Tech Stack:** C++17, g++, POSIX `termios`, `socat`. No third-party libraries.

**Spec:** `docs/superpowers/specs/2026-11-15-pty-file-transfer-design.md`

## Global Constraints

- No tests, benchmarks, `.info` files, gain/offset (user request).
- Build: `g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread`, no warnings.
- Frame (little-endian): `[0xAA 0x55][uint16 payloadLen][uint32 seq][int64 timestampMs][payload][uint16 checksum]`; checksum = 16-bit wrapping sum of bytes from `payloadLen` through end of payload.
- Defaults: `--chunk 64`, `--period 100` ms; ports `/tmp/ttyHC0` (sender) / `/tmp/ttyHC1` (receiver).
- Empty payload = end of stream.
- Timestamps: `std::chrono::system_clock` milliseconds.

## Review Focus

- Partial reads/writes on the pty: `read`/`write` may return fewer bytes — loop until done.
- Garbage before/inside the stream: decoder must skip to the next `0xAA55`, not crash.
- `payloadLen` larger than any real chunk (corrupt header): cap at 4096, treat as bad frame and resync.
- File size not a multiple of chunk: last packet shorter, total bytes received == file size.
- Receiver started before sender: blocks on read, no busy loop.

---

### Task 1: Clean out old pipeline, add sample data

**Files:** Delete `MeasurementBuffer.hpp`, `MeasurementProcessor.{hpp,cpp}`, `MeasurementProducer.{hpp,cpp}`, `main.cpp`, `main`. Create `data/SpO2_Numerics.bin` (copy from `~/phillips/synergy-base/apps/demo-data-playback-service/resources/`). Create `.gitignore` with `/sender`, `/receiver`.

- [ ] `git rm` old files, copy data, add `.gitignore`.

### Task 2: Packet, Frame, SerialPort

**Files:** Create `Packet.hpp`, `Frame.hpp`, `Frame.cpp`, `SerialPort.hpp`, `SerialPort.cpp`.

**Interfaces (produces):**
- `struct Packet { std::uint32_t seq; std::int64_t timestampMs; std::vector<std::uint8_t> bytes; };`
- `std::vector<std::uint8_t> encodeFrame(const Packet&);`
- `class FrameDecoder { public: void feed(const std::uint8_t* data, std::size_t n); std::optional<Packet> next(); std::size_t badFrames() const; };` — buffers bytes; `next()` returns a decoded packet or `nullopt` when more bytes are needed; on checksum mismatch or `payloadLen > 4096` increments `badFrames`, drops one byte and resyncs.
- `class SerialPort { public: explicit SerialPort(const std::string& path); ~SerialPort(); void writeAll(const std::uint8_t*, std::size_t); std::size_t readSome(std::uint8_t*, std::size_t); };` — `open(O_RDWR|O_NOCTTY)`, `cfmakeraw`, `VMIN=1 VTIME=0`; throws `std::system_error` on failure; non-copyable.

- [ ] Implement. Verify: `g++ ... -c Frame.cpp SerialPort.cpp` compiles with no warnings.

### Task 3: sender and receiver executables + run.sh

**Files:** Create `sender.cpp`, `receiver.cpp`; rewrite `run.sh`; keep `PeriodicScheduler.{hpp,cpp}` unchanged.

- `sender <port> <file> [--chunk N] [--period MS]`: read file, build packets seq 0..n-1, run `PeriodicScheduler(period, period * (n + 1))`; on tick i < n send packet i, on tick n send empty end packet. Print one summary line (packets, bytes) at end.
- `receiver <port>`: loop `readSome` → `feed` → drain `next()`. Per packet: `seq=<n> size=<b> latency=<ms>ms OK`. On end packet print `packets=<n> bytes=<b> bad=<k> gaps=<g>` and exit 0. A gap = seq != previous + 1.
- `run.sh [file] [sender args]`: build both, check `socat` exists, start socat pair in background, wait until both links exist, start receiver in background, run sender, wait receiver, kill socat via `trap`.

- [ ] Implement.
- [ ] Verify: `./run.sh` → receiver prints 57 `OK` lines (3630 bytes / 64 → 56 full + 1 of 46 bytes), totals `packets=57` `bytes=3630 bad=0 gaps=0`.
- [ ] Commit: `feat: pty sender/receiver for binary file transfer`.
