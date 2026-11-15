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

# Both programs write their reports at about the same time; buffer each one and
# print them one after the other so the lines do not interleave.
SENDER_LOG=$(mktemp)
RECEIVER_LOG=$(mktemp)

PIDS=()
cleanup() {
    kill "${PIDS[@]}" 2>/dev/null || true
    rm -f "$SENDER_LOG" "$RECEIVER_LOG"
}
trap cleanup EXIT

# Stale links from an earlier run could point at someone else's terminal.
rm -f "$TTY_SENDER" "$TTY_RECEIVER"
# stderr silenced: socat warns "exiting on signal 15" when we stop it; a failed start is caught below.
socat pty,raw,echo=0,link="$TTY_SENDER" pty,raw,echo=0,link="$TTY_RECEIVER" 2>/dev/null &
SOCAT_PID=$!
PIDS+=($SOCAT_PID)
for _ in $(seq 50); do
    [[ -e "$TTY_SENDER" && -e "$TTY_RECEIVER" ]] && break
    kill -0 "$SOCAT_PID" 2>/dev/null || break
    sleep 0.1
done
[[ -e "$TTY_SENDER" && -e "$TTY_RECEIVER" ]] || { echo "socat failed to create the pty pair" >&2; exit 1; }

./receiver "$TTY_RECEIVER" ${VERBOSE:+-v} > "$RECEIVER_LOG" 2>&1 &
RECEIVER_PID=$!
PIDS+=($RECEIVER_PID)
sleep 0.2 # let the receiver open its end before data flows

echo "running: sender $TTY_SENDER -> receiver $TTY_RECEIVER ..."
SENDER_STATUS=0
./sender "$TTY_SENDER" "$FILE" "$@" > "$SENDER_LOG" 2>&1 || SENDER_STATUS=$?
# If the sender failed, the receiver never sees end of stream: stop it.
(( SENDER_STATUS == 0 )) || kill "$RECEIVER_PID" 2>/dev/null || true
RECEIVER_STATUS=0
wait "$RECEIVER_PID" || RECEIVER_STATUS=$?

echo; echo "################ SENDER ################"
cat "$SENDER_LOG"
echo; echo "############### RECEIVER ###############"
cat "$RECEIVER_LOG"

(( SENDER_STATUS == 0 && RECEIVER_STATUS == 0 ))
