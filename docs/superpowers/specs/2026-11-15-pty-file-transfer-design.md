# PTY file transfer: sender + receiver

## Why
Practice prototype for the demo data playback service (DDPS) in `synergy-base`:
read data from a binary file and pass it to the "other side" over the same kind of
link `synergy-base` already uses for ECG — a virtual serial port pair made by `socat`.
Not meant to be useful, only to work end to end.

## Goals / non-goals
- Goal: two separate programs, `sender` and `receiver`, talking over a pty pair.
- Goal: no third-party libraries (POSIX + C++17 only).
- Non-goal: tests, benchmarks, `.info` files, gain/offset decoding, acknowledgements, retries.
- Anti-goal: anything as layered as DDPS.

## Approaches considered
1. **pty via `socat` — picked.** Same mechanism as `msmt-service-ecg/start_ecg_with_simulator.sh`,
   no dependencies, transferable to DDPS. Cost: our own framing (~100 lines).
2. HTTP/REST — less code, but needs `cpp-httplib` and is farther from the real setup.
3. gRPC — heavy build for a toy.

## Design
**Link.** `run.sh` starts `socat pty,raw,echo=0,link=/tmp/ttyHC0 pty,raw,echo=0,link=/tmp/ttyHC1`,
then `receiver /tmp/ttyHC1`, then `sender /tmp/ttyHC0 <file>`; it stops everything on exit.
Both programs open the port and set raw mode with `termios` (`cfmakeraw`).

**Packet** (the "imaginary object"): `{ uint32 seq; int64 timestampMs; vector<uint8_t> bytes; }`.

**Frame on the wire** (little-endian):
`[0xAA 0x55][uint16 payloadLen][uint32 seq][int64 timestampMs][payload][uint16 checksum]`
— checksum is the 16-bit sum of all bytes from `payloadLen` to the end of the payload.

**Sender.** Reads the whole file, splits it into chunks of `--chunk` bytes (default 64,
last one may be shorter), and sends one packet per tick through the existing
`PeriodicScheduler` (`--period` ms, default 100). Sends the file once, then a final
packet with an empty payload as "end of stream", then exits.

**Receiver.** Reads the byte stream, hunts for `0xAA55`, reads the header and payload,
verifies the checksum. Prints one line per packet: seq, size, latency
(now − timestampMs), OK/BAD. On end-of-stream prints totals (packets, bytes,
bad checksums, seq gaps) and exits. Bad frames are skipped by resyncing on the next marker.

**Code layout.** Keep `PeriodicScheduler`; remove `MeasurementBuffer/Producer/Processor`
and old `main.cpp`. New: `Packet.hpp`, `Frame.{hpp,cpp}` (encode/decode),
`SerialPort.{hpp,cpp}` (open + raw mode + read/write all), `sender.cpp`, `receiver.cpp`.
Sample data: copy `SpO2_Numerics.bin` from DDPS into `data/`. Build with g++ in `run.sh`
(no CMake), remove the committed `main` binary.

## Left to the builder
Exact CLI parsing, log formatting, timestamp clock (system_clock ms so both processes agree).
