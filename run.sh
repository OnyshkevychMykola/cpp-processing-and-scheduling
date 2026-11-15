#!/usr/bin/env bash
# Builds sender + receiver, links them with a socat pty pair and sends a file.
# Usage: ./run.sh [file] [sender options]      (VERBOSE=1 prints one line per received packet)
#   ./run.sh                                                   send the file once, 64 B every 100 ms
#   ./run.sh data/SpO2_Numerics.bin --period 2 --channels 8 --duration 10              ECG-like, ifstream per tick
#   ./run.sh data/SpO2_Numerics.bin --period 2 --channels 8 --duration 10 --read preload   baseline
set -euo pipefail
cd "$(dirname "$0")"

FILE="${1:-data/SpO2_Numerics.bin}"
shift || true

TTY_SENDER=/tmp/ttyHC0
TTY_RECEIVER=/tmp/ttyHC1

command -v socat >/dev/null || { echo "socat not found: sudo apt install socat" >&2; exit 1; }

FLAGS=(-std=c++17 -O2 -Wall -Wextra -pedantic -pthread)
g++ "${FLAGS[@]}" -o sender sender.cpp Frame.cpp SerialPort.cpp PeriodicScheduler.cpp
g++ "${FLAGS[@]}" -o receiver receiver.cpp Frame.cpp SerialPort.cpp

PIDS=()
cleanup() { kill "${PIDS[@]}" 2>/dev/null || true; }
trap cleanup EXIT

# Stale links from an earlier run could point at someone else's terminal.
rm -f "$TTY_SENDER" "$TTY_RECEIVER"
socat pty,raw,echo=0,link="$TTY_SENDER" pty,raw,echo=0,link="$TTY_RECEIVER" &
SOCAT_PID=$!
PIDS+=($SOCAT_PID)
for _ in $(seq 50); do
    [[ -e "$TTY_SENDER" && -e "$TTY_RECEIVER" ]] && break
    kill -0 "$SOCAT_PID" 2>/dev/null || break
    sleep 0.1
done
[[ -e "$TTY_SENDER" && -e "$TTY_RECEIVER" ]] || { echo "socat failed to create the pty pair" >&2; exit 1; }

./receiver "$TTY_RECEIVER" ${VERBOSE:+-v} &
RECEIVER_PID=$!
PIDS+=($RECEIVER_PID)
sleep 0.2 # let the receiver open its end before data flows

./sender "$TTY_SENDER" "$FILE" "$@"
wait "$RECEIVER_PID"
