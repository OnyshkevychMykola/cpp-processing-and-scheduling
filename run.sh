#!/usr/bin/env bash
# Usage: ./run.sh [sleep|cpu] [--approach A|B] [--scenario NAME] [--duration-ms N]
set -euo pipefail
cd "$(dirname "$0")"

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o main *.cpp

echo "=== Args: ${*:-(none)} ==="
./main "$@"
