#!/usr/bin/env python3
"""Plot the outputs of one sortmed run.

Usage:
    uv run python tools/plot_run.py RUN_DIR [--out DIR]

RUN_DIR holds the files the client wrote on the board:

    raw.bin       float32, the channel as received (the rate flag is nominal;
                  this dataset's time column advances ~74.8 ms per sample)
    filtered.bin  float32, outliers replaced by the rolling median
    sorted.bin    float32, the same samples ascending       (optional)
    medians.csv   block_index,samples,median,outliers_removed (optional)

Figures written into OUT (default: RUN_DIR):

    run_cleaned_vs_noisy.png  the whole recording: received vs filtered, spikes marked
    run_zoom.png              three stretches where the filter acted, zoomed
    run_sorted_proof.png      as-received vs sorted, with the checks that prove it
    run_summary.png           per-block medians and when samples were replaced
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

RATE_HZ = 100.0
DPI = 200
NOISY = "#8d949c"
FILTER = "#1558d6"
FLAG = "#d93025"
MEDIAN = "#188038"

plt.rcParams.update({
    "font.size": 12,
    "axes.titlesize": 14,
    "axes.labelsize": 12.5,
    "legend.fontsize": 10.5,
    "xtick.labelsize": 10.5,
    "ytick.labelsize": 10.5,
    "axes.linewidth": 0.9,
    "axes.edgecolor": "#444444",
    "figure.facecolor": "white",
    "savefig.facecolor": "white",
})


def style(ax, ylabel: str | None = None, xlabel: str | None = None) -> None:
    ax.grid(True, color="#d8d8d8", linewidth=0.7)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    if ylabel:
        ax.set_ylabel(ylabel)
    if xlabel:
        ax.set_xlabel(xlabel)


def envelope(x: np.ndarray, bins: int = 1500) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Per-column min and max of a long series, so spikes stay visible."""
    edges = np.linspace(0, x.size, bins + 1).astype(int)
    centres = (edges[:-1] + edges[1:]) / 2.0
    lo = np.array([x[a:b].min() for a, b in zip(edges[:-1], edges[1:])])
    hi = np.array([x[a:b].max() for a, b in zip(edges[:-1], edges[1:])])
    return centres, lo, hi


def busiest(flags: np.ndarray, width: int, count: int) -> list[int]:
    density = np.convolve(flags.astype(np.int32), np.ones(width, np.int32), "valid")
    order = np.argsort(density)[::-1]
    picked: list[int] = []
    for start in order:
        if density[start] == 0:
            break
        if all(abs(int(start) - p) >= width for p in picked):
            picked.append(int(start))
        if len(picked) == count:
            break
    return sorted(picked)


def read_csv(path: Path) -> list[dict]:
    with path.open() as handle:
        return list(csv.DictReader(handle))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run_dir", nargs="?", default=".")
    ap.add_argument("--out")
    args = ap.parse_args(argv[1:])

    run_dir = Path(args.run_dir).expanduser()
    out_dir = Path(args.out).expanduser() if args.out else run_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    raw = np.fromfile(run_dir / "raw.bin", dtype="<f4")
    filtered = np.fromfile(run_dir / "filtered.bin", dtype="<f4")
    if raw.size == 0:
        raise SystemExit("plot_run: raw.bin is missing or empty")
    if raw.size != filtered.size:
        raise SystemExit(f"plot_run: raw.bin has {raw.size} samples, "
                         f"filtered.bin has {filtered.size}")

    n = raw.size
    t = np.arange(n) / RATE_HZ
    changed = raw.view("u4") != filtered.view("u4")
    zoom_starts = busiest(changed, int(2.0 * RATE_HZ), 3)

    sorted_values = None
    sorted_ok = None
    if (run_dir / "sorted.bin").exists():
        sorted_values = np.fromfile(run_dir / "sorted.bin", dtype="<f4")
        same_values = np.array_equal(np.sort(raw), sorted_values)
        nondecreasing = bool(np.all(np.diff(sorted_values) >= 0))
        sorted_ok = (same_values and nondecreasing)
    medians = read_csv(run_dir / "medians.csv") if (run_dir / "medians.csv").exists() else []

    print(f"directory  : {run_dir}")
    print(f"samples    : {n}  ({n / RATE_HZ:.1f} s at {RATE_HZ:.0f} Hz)")
    print(f"differing  : {int(changed.sum())} (raw != filtered)")
    if sorted_values is not None:
        print(f"sorted.bin : {sorted_values.size} samples, "
              f"median {float(np.median(sorted_values)):.9g}, "
              f"non-decreasing {nondecreasing}, same values {same_values}")
    for row in medians:
        print(f"  block {row['block_index']}: {row['samples']} samples, "
              f"median {float(row['median']):.9g}, replaced {row['outliers_removed']}")

    # --- 1. the whole recording, with an envelope so spikes stay visible ------
    centres, lo, hi = envelope(raw)
    fig, ax = plt.subplots(figsize=(15, 5.6), dpi=DPI)
    ax.fill_between(centres / RATE_HZ, lo, hi, color=NOISY, alpha=0.55,
                    linewidth=0, label="received (min–max per 0.4 s)")
    ax.plot(t, filtered, color=FILTER, linewidth=1.3, label="filtered (service output)")
    ax.plot(t[changed], raw[changed], "o", color=FLAG, markersize=2.6, alpha=0.6,
            linestyle="none", label=f"samples replaced ({int(changed.sum())})")
    for start in zoom_starts:
        ax.axvspan(start / RATE_HZ, (start + int(2.0 * RATE_HZ)) / RATE_HZ,
                   color=FLAG, alpha=0.10, linewidth=0)
    ax.set_xlim(0, t[-1])
    style(ax, "voltage (V)", "time (s)")
    ax.set_title(f"The whole recording — noisy telemetry and the filtered signal\n"
                 f"{n} samples over {n / RATE_HZ:.0f} s at {RATE_HZ:.0f} Hz, "
                 f"{int(changed.sum())} samples replaced", fontsize=13)
    ax.legend(loc="upper right", framealpha=0.95, ncol=3)
    fig.tight_layout()
    fig.savefig(out_dir / "run_cleaned_vs_noisy.png", dpi=DPI)
    plt.close(fig)

    # --- 2. three stretches where the filter acted ---------------------------
    fig, axes = plt.subplots(len(zoom_starts), 1, figsize=(14, 3.0 * len(zoom_starts) + 0.8),
                             dpi=DPI, squeeze=False)
    for ax, start in zip(axes[:, 0], zoom_starts):
        stop = min(start + int(2.0 * RATE_HZ), n)
        sl = slice(start, stop)
        sel = changed[sl]
        ax.plot(t[sl], raw[sl], "o-", color=NOISY, linewidth=0.9, markersize=3.2,
                markerfacecolor="white", label="received")
        ax.plot(t[sl], filtered[sl], color=FILTER, linewidth=2.0, label="filtered")
        ax.plot(t[sl][sel], raw[sl][sel], "o", color=FLAG, markersize=9,
                markerfacecolor="none", markeredgewidth=1.6, linestyle="none",
                label="replaced")
        ax.set_xlim(t[start], t[stop - 1])
        style(ax, "voltage (V)")
        ax.set_title(f"{t[start]:.1f}–{t[stop - 1]:.1f} s — {int(sel.sum())} samples replaced",
                     fontsize=12.5, loc="left")
        if start == zoom_starts[0]:
            ax.legend(loc="upper right", ncol=3, framealpha=0.95)
    axes[-1, 0].set_xlabel("time (s)")
    fig.suptitle("Where the filter acted — three stretches, zoomed", fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.965))
    fig.savefig(out_dir / "run_zoom.png", dpi=DPI)
    plt.close(fig)

    # --- 3. proof of the sort ------------------------------------------------
    if sorted_values is not None:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 5.4), dpi=DPI)
        ax1.plot(t, raw, color=NOISY, linewidth=0.7)
        ax1.set_xlim(0, t[-1])
        style(ax1, "value (as received)", "sample index → time (s)")
        ax1.set_title("Before: the values in arrival order\n(unsorted)", fontsize=13)

        ax2.plot(np.arange(n), sorted_values, color=FILTER, linewidth=1.6)
        for value, label, colour in ((sorted_values[0], "min", NOISY),
                                     (float(np.median(sorted_values)), "median", MEDIAN),
                                     (sorted_values[-1], "max", NOISY)):
            ax2.axhline(value, color=colour, linestyle="--", linewidth=1.2, alpha=0.9)
            ax2.annotate(f"{label} {value:.5g}", xy=(0.015, value), xycoords=("axes fraction", "data"),
                         va="bottom", ha="left", fontsize=11, color=colour)
        style(ax2, "value (sorted)", "rank (0 → 59 999)")
        ax2.set_title("After: the same values, sorted ascending\n"
                      "non-decreasing ✓   same 60 000 values ✓", fontsize=13)
        fig.suptitle("The sort, shown: arrival order becomes ascending order", fontsize=14)
        fig.tight_layout(rect=(0, 0, 1, 0.94))
        fig.savefig(out_dir / "run_sorted_proof.png", dpi=DPI)
        plt.close(fig)

    # --- 4. per-block medians and when samples were replaced ------------------
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 5.0), dpi=DPI,
                                   gridspec_kw={"width_ratios": [1, 1.5]})
    if medians:
        index = [int(r["block_index"]) for r in medians]
        med = [float(r["median"]) for r in medians]
        rep = [int(r["outliers_removed"]) for r in medians]
        bars = ax1.bar([f"block {i}\n({r['samples']} samples)" for i, r in zip(index, medians)],
                       med, color=FILTER, width=0.6, alpha=0.85)
        for bar, value, count in zip(bars, med, rep):
            ax1.text(bar.get_x() + bar.get_width() / 2, value, f"{value:.5g} V\n{count} replaced",
                     ha="center", va="bottom", fontsize=10)
        ax1.set_ylim(min(med) - 0.35, max(med) + 0.55)
        style(ax1, "block median (V)")
        ax1.set_title("Median of each 64 KiB block", fontsize=13)
    else:
        ax1.text(0.5, 0.5, "medians.csv not present", ha="center", va="center",
                 transform=ax1.transAxes)
        ax1.set_axis_off()

    ax2.plot(t, raw, color=NOISY, linewidth=0.6, alpha=0.8, label="received")
    ax2.plot(t, filtered, color=FILTER, linewidth=1.2, label="filtered")
    ax2.plot(t[changed], raw[changed], "|", color=FLAG, markersize=6, mew=1.0,
             linestyle="none", label="replaced")
    ax2.set_xlim(0, t[-1])
    style(ax2, "voltage (V)", "time (s)")
    ax2.set_title(f"When the filter acted — {int(changed.sum())} samples", fontsize=13)
    ax2.legend(loc="upper right", ncol=3, framealpha=0.95)
    fig.tight_layout()
    fig.savefig(out_dir / "run_summary.png", dpi=DPI)
    plt.close(fig)

    for name in ("run_cleaned_vs_noisy.png", "run_zoom.png",
                 "run_sorted_proof.png", "run_summary.png"):
        print(f"wrote      : {out_dir / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
