#!/usr/bin/env bash
# Run all benchmark scenarios for both approaches, several repeats each.
# Usage: ./bench.sh [runs=5] [sleep|cpu=sleep] [duration_ms=10000]
# Output: results/<mode>/summary.csv (one row per run) and
#         results/<mode>/<scenario>_<approach>.csv (per-publication, run 1 only).
# Runs are strictly sequential so they do not disturb each other's timing.
set -euo pipefail
cd "$(dirname "$0")"

runs="${1:-5}"
mode="${2:-sleep}"
duration_ms="${3:-10000}"
out="results/${mode}"
scenarios=(baseline delayed load-ramp)
approaches=(A B)

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o main *.cpp

mkdir -p "$out"
rm -f "$out"/summary.csv "$out"/*_[AB].csv

for scenario in "${scenarios[@]}"; do
    for approach in "${approaches[@]}"; do
        for ((run = 1; run <= runs; ++run)); do
            echo "scenario=$scenario approach=$approach mode=$mode run=$run/$runs"
            extra=()
            if [ "$run" -eq 1 ]; then
                extra=(--csv "$out/${scenario}_${approach}.csv")
            fi
            ./main "$mode" --scenario "$scenario" --approach "$approach" \
                --duration-ms "$duration_ms" --seed "$run" \
                --summary "$out/summary.csv" "${extra[@]}" >/dev/null
        done
    done
done

echo "Done: $out/summary.csv ($(($(wc -l < "$out/summary.csv") - 1)) runs)"
