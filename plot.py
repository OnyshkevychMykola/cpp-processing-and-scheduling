#!/usr/bin/env python3
"""Plot benchmark results written by bench.sh.

Usage: python plot.py [results_dir=results]
Writes into results_dir: fig_summary.png, fig_timeline.png, summary_table.md
Needs: pandas, matplotlib.
"""
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

# Fixed categorical order (validated palette): slot 1 = A, slot 2 = B.
COLORS = {"A": "#2a78d6", "B": "#eb6834"}
LABELS = {"A": "A: processing inside tick", "B": "B: separate processing thread"}
SCENARIOS = ["baseline", "delayed", "load-ramp"]
SCENARIO_TITLES = {
    "baseline": "Baseline (3 ms work)",
    "delayed": "Delayed delivery (5% x 20 ms)",
    "load-ramp": "Load ramp (2 -> 20 ms)",
}
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"

# (column, title, unit)
METRICS = [
    ("jitter_p99_ms", "Interval jitter p99", "ms"),
    ("deadline_misses", "Deadline misses", "count"),
    ("underrun_pct", "Buffer underruns (publisher)", "% of ticks"),
    ("proc_avg_ms", "Processing latency (avg)", "ms"),
    ("pub_late_avg_ms", "Publication lateness (avg)", "ms"),
    ("data_age_avg_ms", "Data age (avg)", "ms"),
]


def style(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=MUTED, labelsize=8)
    ax.grid(axis="y", color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)


def summary_figure(df, out):
    grouped = df.groupby(["scenario", "approach"])
    med = grouped.median(numeric_only=True)
    lo = grouped.min(numeric_only=True)
    hi = grouped.max(numeric_only=True)
    fig, axes = plt.subplots(2, 3, figsize=(12, 6.5), facecolor=SURFACE)
    width = 0.36
    for ax, (col, title, unit) in zip(axes.flat, METRICS):
        style(ax)
        for i, ap in enumerate("AB"):
            xs = [j + (i - 0.5) * (width + 0.04) for j in range(len(SCENARIOS))]
            vals = [med.loc[(s, ap), col] for s in SCENARIOS]
            lows = [med.loc[(s, ap), col] - lo.loc[(s, ap), col] for s in SCENARIOS]
            highs = [hi.loc[(s, ap), col] - med.loc[(s, ap), col] for s in SCENARIOS]
            ax.bar(xs, vals, width, color=COLORS[ap], label=LABELS[ap],
                   yerr=[lows, highs], ecolor=INK,
                   error_kw={"elinewidth": 0.9, "capsize": 2.5})
            for x, v, h in zip(xs, vals, highs):
                ax.text(x, v + h, f"{v:.3g}", ha="center", va="bottom",
                        fontsize=7, color=INK)
        ax.set_xticks(range(len(SCENARIOS)))
        ax.set_xticklabels(["baseline", "delayed", "load-ramp"])
        ax.set_title(f"{title} [{unit}]", fontsize=10, color=INK, loc="left")
        ax.margins(y=0.15)
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=2, frameon=False,
               fontsize=9, labelcolor=INK)
    n = int(df.groupby(["scenario", "approach"]).size().min())
    fig.suptitle(f"Scheduling approaches A vs B - median of {n} runs, "
                 "whiskers = min..max",
                 color=INK, fontsize=12, x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0.05, 1, 0.96))
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def timeline_figure(res, out):
    fig, axes = plt.subplots(3, 2, figsize=(12, 8), facecolor=SURFACE,
                             sharex="col")
    drawn = False
    for r, sc in enumerate(SCENARIOS):
        for c, (col, title) in enumerate(
                [("lateness_ms", "Publication lateness [ms]"),
                 ("data_age_ms", "Data age [ms]")]):
            ax = axes[r][c]
            style(ax)
            for ap in "AB":
                f = res / f"{sc}_{ap}.csv"
                if not f.exists():
                    continue
                d = pd.read_csv(f)
                ax.plot(d["scheduled_ms"] / 1000.0, d[col], color=COLORS[ap],
                        linewidth=1.0, label=LABELS[ap])
                drawn = True
            if r == 0:
                ax.set_title(title, fontsize=10, color=INK, loc="left")
            if c == 0:
                ax.set_ylabel(SCENARIO_TITLES[sc], fontsize=9, color=INK)
            if r == len(SCENARIOS) - 1:
                ax.set_xlabel("scenario time [s]", fontsize=9, color=MUTED)
    if drawn:
        handles, labels = axes[0][0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="lower center", ncol=2, frameon=False,
                   fontsize=9, labelcolor=INK)
    fig.suptitle("Per-publication timeline (run 1)", color=INK, fontsize=12,
                 x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0.04, 1, 0.96))
    fig.savefig(out, dpi=150, facecolor=SURFACE)
    plt.close(fig)


def table(df, out):
    med = df.groupby(["scenario", "approach"]).median(numeric_only=True)
    cols = [m[0] for m in METRICS] + ["skipped_ticks", "jitter_max_ms",
                                      "data_age_max_ms"]
    lines = ["| scenario | approach | " + " | ".join(cols) + " |",
             "|---|---|" + "---|" * len(cols)]
    for sc in SCENARIOS:
        for ap in "AB":
            row = med.loc[(sc, ap)]
            lines.append(f"| {sc} | {ap} | " +
                         " | ".join(f"{row[c]:.3g}" for c in cols) + " |")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    res = Path(sys.argv[1] if len(sys.argv) > 1 else "results")
    df = pd.read_csv(res / "summary.csv")
    summary_figure(df, res / "fig_summary.png")
    timeline_figure(res, res / "fig_timeline.png")
    table(df, res / "summary_table.md")
    print(f"Wrote figures and table to {res}")


if __name__ == "__main__":
    main()
