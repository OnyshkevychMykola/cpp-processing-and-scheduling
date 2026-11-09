#!/usr/bin/env bash
# Extra experiments (approach B, sleep mode):
#   timing: timing-consistency techniques under CPU contention
#   age:    max-age publication policy under delayed delivery
# Usage: ./bench_extra.sh [runs=5] [duration_ms=10000] [timing|age|all=all]
# Output: results/timing/summary.csv, results/age/summary.csv
# Sequential on purpose: runs must not disturb each other.
set -euo pipefail
cd "$(dirname "$0")"

runs="${1:-5}"
duration_ms="${2:-10000}"
which="${3:-all}"
pin_cpu=3

g++ -std=c++17 -O2 -Wall -Wextra -pedantic -pthread -o main *.cpp

# SCHED_FIFO needs CAP_SYS_NICE (or RLIMIT_RTPRIO); include it only if allowed.
rt_ok=1
# Capture first: piping into `grep -q` plus pipefail would misreport (SIGPIPE).
rt_probe="$(./main --scenario baseline --rt-priority 80 --duration-ms 300 2>&1 || true)"
if grep -q "SCHED_FIFO failed" <<<"$rt_probe"; then
    rt_ok=0
    echo "NOTE: SCHED_FIFO not permitted here, RT configurations are skipped"
fi

run_cfg() {  # run_cfg <outdir> <label> <args...>
    local out="$1" label="$2"; shift 2
    mkdir -p "$out"
    for ((run = 1; run <= runs; ++run)); do
        echo "[$out] $label run=$run/$runs"
        ./main sleep "$@" --approach B --duration-ms "$duration_ms" \
            --seed "$run" --label "$label" --summary "$out/summary.csv" \
            >/dev/null 2>&1
    done
}

if [ "$which" = "timing" ] || [ "$which" = "all" ]; then
    rm -f results/timing/summary.csv
    run_cfg results/timing idle          --scenario baseline
    run_cfg results/timing stress        --scenario stress
    run_cfg results/timing stress+hybrid --scenario stress --timing hybrid
    run_cfg results/timing stress+pin    --scenario stress --pin-cpu "$pin_cpu"
    run_cfg results/timing stress+pin+hybrid \
        --scenario stress --pin-cpu "$pin_cpu" --timing hybrid
    if [ "$rt_ok" -eq 1 ]; then
        run_cfg results/timing stress+rt \
            --scenario stress --rt-priority 80
        run_cfg results/timing stress+rt+pin+hybrid \
            --scenario stress --rt-priority 80 --pin-cpu "$pin_cpu" --timing hybrid
    fi
fi

if [ "$which" = "age" ] || [ "$which" = "all" ]; then
    rm -f results/age/summary.csv
    run_cfg results/age age-off --scenario delayed
    run_cfg results/age age-40  --scenario delayed --max-age-ms 40
    run_cfg results/age age-20  --scenario delayed --max-age-ms 20
    run_cfg results/age age-10  --scenario delayed --max-age-ms 10
fi

echo "Done."
