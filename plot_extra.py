#!/usr/bin/env python3
"""Plot the extra experiments written by bench_extra.sh.

Usage: python plot_extra.py [results_dir=results]
Writes: <dir>/timing/fig_timing.png, <dir>/timing/summary_table.md,
        <dir>/age/fig_age.png,       <dir>/age/summary_table.md
Needs: pandas, matplotlib.
"""
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
BLUE, GRAY = "#2a78d6", "#9a9892"  # validated blue; neutral for the reference

TIMING_ORDER = ["idle", "stress", "stress+hybrid", "stress+pin",
                "stress+pin+hybrid", "stress+rt", "stress+rt+pin+hybrid"]
AGE_ORDER = ["age-off", "age-40", "age-20", "age-10"]
AGE_NAMES = {"age-off": "no limit", "age-40": "40 ms", "age-20": "20 ms",
             "age-10": "10 ms"}


def style(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=MUTED, labelsize=8)
    ax.grid(axis="y", color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)


def bars(ax, df, order, col, title, unit, names=None, gray_first=False):
    g = df.groupby("label")[col]
    med, lo, hi = g.median(), g.min(), g.max()
    labels = [l for l in order if l in med.index]
    xs = range(len(labels))
    vals = [med[l] for l in labels]
    colors = [GRAY if (gray_first and i == 0) else BLUE
              for i in range(len(labels))]
    ax.bar(xs, vals, 0.62, color=colors,
           yerr=[[med[l] - lo[l] for l in labels], [hi[l] - med[l] for l in labels]],
           ecolor=INK, error_kw={"elinewidth": 0.9, "capsize": 2.5})
    for x, l in zip(xs, labels):
        ax.text(x, hi[l], f"{med[l]:.3g}", ha="center", va="bottom",
                fontsize=7, color=INK)
    ax.set_xticks(list(xs))
    ax.set_xticklabels([(names or {}).get(l, l).replace("+", "+\n") for l in labels],
                       fontsize=7)
    ax.set_title(f"{title} [{unit}]", fontsize=10, color=INK, loc="left")
    ax.margins(y=0.15)
    style(ax)


def timing(res):
    f = res / "timing" / "summary.csv"
    if not f.exists():
        return
    df = pd.read_csv(f)
    # Drop runs that asked for real-time scheduling but did not get it: they
    # would be mislabelled (they behave like the non-RT configuration).
    df = df[~((df["rt_requested"] == 1) & (df["rt_applied"] == 0))]
    fig, axes = plt.subplots(2, 2, figsize=(12, 7), facecolor=SURFACE)
    for ax, (col, title) in zip(axes.flat, [
            ("start_late_avg_ms", "Tick start lateness (avg)"),
            ("jitter_p95_ms", "Interval jitter p95"),
            ("jitter_p99_ms", "Interval jitter p99"),
            ("jitter_max_ms", "Interval jitter max")]):
        bars(ax, df, TIMING_ORDER, col, title, "ms", gray_first=True)
    n = int(df.groupby("label").size().min())
    fig.suptitle(f"Timing consistency under CPU contention - median of {n} runs, "
                 "whiskers = min..max (grey = idle reference)",
                 color=INK, fontsize=12, x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    fig.savefig(res / "timing" / "fig_timing.png", dpi=150, facecolor=SURFACE)
    plt.close(fig)

    med = df.groupby("label").median(numeric_only=True)
    cols = ["start_late_avg_ms", "jitter_p95_ms", "jitter_p99_ms",
            "jitter_max_ms", "deadline_misses", "rt_applied", "pin_applied"]
    lines = ["| config | " + " | ".join(cols) + " |", "|---|" + "---|" * len(cols)]
    for l in [x for x in TIMING_ORDER if x in med.index]:
        lines.append(f"| {l} | " + " | ".join(f"{med.loc[l, c]:.3g}" for c in cols) + " |")
    (res / "timing" / "summary_table.md").write_text("\n".join(lines) + "\n",
                                                     encoding="utf-8")


def age(res):
    f = res / "age" / "summary.csv"
    if not f.exists():
        return
    df = pd.read_csv(f)
    df["withheld_pct"] = 100.0 * df["withheld"] / df["executed_ticks"]
    fig, axes = plt.subplots(1, 3, figsize=(12, 4.2), facecolor=SURFACE)
    for ax, (col, title, unit) in zip(axes, [
            ("data_age_avg_ms", "Data age (avg, published)", "ms"),
            ("data_age_max_ms", "Data age (max, published)", "ms"),
            ("withheld_pct", "Ticks with nothing published", "% of ticks")]):
        bars(ax, df, AGE_ORDER, col, title, unit, names=AGE_NAMES)
        ax.set_xlabel("max allowed data age", fontsize=8, color=MUTED)
    n = int(df.groupby("label").size().min())
    fig.suptitle(f"Max-age policy, delayed delivery - median of {n} runs, "
                 "whiskers = min..max", color=INK, fontsize=12, x=0.01, ha="left")
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    fig.savefig(res / "age" / "fig_age.png", dpi=150, facecolor=SURFACE)
    plt.close(fig)

    med = df.groupby("label").median(numeric_only=True)
    cols = ["data_age_avg_ms", "data_age_max_ms", "withheld_pct", "underrun_pct",
            "pub_late_avg_ms"]
    lines = ["| config | " + " | ".join(cols) + " |", "|---|" + "---|" * len(cols)]
    for l in [x for x in AGE_ORDER if x in med.index]:
        lines.append(f"| {l} | " + " | ".join(f"{med.loc[l, c]:.3g}" for c in cols) + " |")
    (res / "age" / "summary_table.md").write_text("\n".join(lines) + "\n",
                                                  encoding="utf-8")


def main():
    res = Path(sys.argv[1] if len(sys.argv) > 1 else "results")
    timing(res)
    age(res)
    print(f"Wrote extra figures and tables to {res}")


if __name__ == "__main__":
    main()
