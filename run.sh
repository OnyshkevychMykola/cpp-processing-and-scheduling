#!/usr/bin/env bash
# Usage: ./run.sh [sleep|cpu]   (default: sleep)
set -euo pipefail
cd "$(dirname "$0")"

mode="${1:-sleep}"

case "$mode" in
    sleep|cpu) ;;
    *)
        echo "Unknown mode: $mode" >&2
        echo "Usage: $0 [sleep|cpu]" >&2
        exit 1
        ;;
esac

if [ "$#" -gt 1 ]; then
    echo "Too many arguments" >&2
    echo "Usage: $0 [sleep|cpu]" >&2
    exit 1
fi

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o main *.cpp

echo "=== Mode: $mode ==="
./main "$mode"
