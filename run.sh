#!/usr/bin/env bash
# Usage: ./run.sh [sleep|cpu] [--scenario default|baseline|delayed|load-ramp] [options]
# Run ./main with a bad argument to print the full option list.
set -euo pipefail
cd "$(dirname "$0")"

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o main *.cpp

echo "=== Args: ${*:-(none)} ==="
./main "$@"
