#!/usr/bin/env python3
"""Plot the outputs of one sortmed run.

Usage:
    uv run python tools/plot_run.py [DIRECTORY]

DIRECTORY (default: the current directory) is expected to hold the files the
sortmed client writes:

    raw.bin       float32, the voltage channel at 100 Hz
    filtered.bin  float32, outliers replaced by a rolling Hampel median
    sorted.bin    float32, the same samples ascending          (optional)
    medians.csv   one row per block: index,samples,median,...  (optional)

Every file that is present is plotted; a missing `sorted.bin` or `medians.csv`
is reported on stdout and simply drops its panel.

Three figures are written into DIRECTORY:

    run_cleaned_vs_noisy.png  the whole recording, noisy vs cleaned
    run_zoom.png              a few seconds around the worst removed spike
    run_summary.png           sorted curve, per-block medians, removals in time
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

SAMPLE_RATE_HZ = 100.0
DPI = 150
ZOOM_SECONDS = 4.0

_FLOAT32 = np.dtype("<f4")

# One colour per meaning, reused across the figures.
_RAW_COLOR = "#9e9e9e"
_CLEAN_COLOR = "#1f77b4"
_REMOVED_COLOR = "#d62728"
_SORTED_COLOR = "#2ca02c"


def load_floats(path: Path) -> np.ndarray:
    """Read a headerless little-endian float32 file into a 1-D array."""
    return np.fromfile(path, dtype=_FLOAT32)


def load_medians(path: Path) -> list[dict[str, float]]:
    """Read medians.csv as a list of rows keyed by column name."""
    rows: list[dict[str, float]] = []
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            rows.append(
                {
                    "block_index": int(row["block_index"]),
                    "samples": int(row["samples"]),
                    "median": float(row["median"]),
                    "outliers_removed": int(row["outliers_removed"]),
                }
            )
    return rows


def plot_overview(directory: Path, t: np.ndarray, raw: np.ndarray,
                  filtered: np.ndarray, removed: np.ndarray) -> Path:
    """Full recording: noisy and cleaned overlaid, removed samples marked."""
    path = directory / "run_cleaned_vs_noisy.png"
    duration = float(t[-1]) if t.size else 0.0
    replaced = int(removed.sum())

    fig, ax = plt.subplots(figsize=(15, 5), dpi=DPI)
    ax.plot(t, raw, lw=0.5, color=_RAW_COLOR, label="raw (noisy)")
    ax.plot(t, filtered, lw=0.5, color=_CLEAN_COLOR, label="filtered (cleaned)")
    ax.plot(t[removed], raw[removed], linestyle="none", marker="o", ms=3.0,
            mfc="none", mec=_REMOVED_COLOR, mew=0.8,
            label=f"removed by filter ({replaced})")

    ax.set_xlabel("time [s]")
    ax.set_ylabel("voltage [V]")
    ax.set_xlim(0.0, duration)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper right", framealpha=0.9)
    ax.set_title(f"{directory.name}: raw vs filtered, {raw.size} samples "
                 f"({duration:.1f} s at {SAMPLE_RATE_HZ:g} Hz)")
    fig.tight_layout()
    fig.savefig(path, dpi=DPI)
    plt.close(fig)
    return path


def plot_zoom(directory: Path, t: np.ndarray, raw: np.ndarray,
              filtered: np.ndarray, removed: np.ndarray) -> Path:
    """A few seconds around the largest cut so a spike is visibly clipped."""
    path = directory / "run_zoom.png"

    deviation = np.abs(raw - filtered)
    score = np.where(removed, deviation, -1.0)
    centre = int(np.argmax(score)) if removed.any() else int(np.argmax(deviation))
    half = max(1, int(round(ZOOM_SECONDS * SAMPLE_RATE_HZ / 2.0)))
    lo = max(0, centre - half)
    hi = min(raw.size, centre + half)

    window = slice(lo, hi)
    view = removed[window]
    cut = int(view.sum())

    fig, ax = plt.subplots(figsize=(12, 5), dpi=DPI)
    ax.plot(t[window], raw[window], lw=1.1, color=_RAW_COLOR, label="raw (noisy)")
    ax.plot(t[window], filtered[window], lw=1.1, color=_CLEAN_COLOR,
            label="filtered (cleaned)")
    ax.plot(t[window][view], raw[window][view], linestyle="none", marker="o",
            ms=6.0, mfc="none", mec=_REMOVED_COLOR, mew=1.4,
            label=f"removed by filter ({cut})")

    ax.set_xlabel("time [s]")
    ax.set_ylabel("voltage [V]")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper right", framealpha=0.9)
    ax.set_title(f"{directory.name}: zoom {t[lo]:.2f}-{t[hi - 1]:.2f} s, "
                 f"{cut} spike(s) cut")
    fig.tight_layout()
    fig.savefig(path, dpi=DPI)
    plt.close(fig)
    return path


def _panel_sorted(ax, sorted_values: np.ndarray) -> None:
    median = float(np.median(sorted_values))
    ax.plot(np.arange(sorted_values.size), sorted_values, lw=0.8,
            color=_SORTED_COLOR, label="sorted samples")
    ax.axhline(median, color=_REMOVED_COLOR, ls="--", lw=1.0,
               label=f"median = {median:.6g} V")
    ax.set_xlabel("rank")
    ax.set_ylabel("voltage [V]")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper left", framealpha=0.9)
    ax.set_title("sorted curve and its median")


def _panel_blocks(ax, rows: list[dict[str, float]]) -> None:
    index = [r["block_index"] for r in rows]
    medians = [r["median"] for r in rows]
    outliers = [r["outliers_removed"] for r in rows]

    ax.plot(index, medians, marker="o", color=_CLEAN_COLOR, lw=1.2,
            label="block median")
    ax.set_xlabel("block index")
    ax.set_ylabel("block median [V]")
    ax.grid(True, alpha=0.3)

    twin = ax.twinx()
    twin.bar(index, outliers, width=0.55, color=_REMOVED_COLOR, alpha=0.18,
             label="outliers removed")
    twin.set_ylabel("outliers removed")
    twin.set_ylim(0, max(max(outliers) * 1.35, 1.0))

    handles = ax.get_lines() + twin.containers
    labels = [h.get_label() for h in handles]
    ax.legend(handles, labels, loc="upper right", framealpha=0.9)
    ax.set_title("per-block medians (bars: outliers removed)")


def _panel_removals(ax, t: np.ndarray, removed: np.ndarray) -> None:
    duration = float(t[-1]) if t.size else 0.0
    bins = max(10, min(120, int(round(duration / 10.0)) or 10))
    counts, edges = np.histogram(t[removed], bins=bins, range=(0.0, duration))
    ax.bar(edges[:-1], counts, width=np.diff(edges), align="edge",
           color=_REMOVED_COLOR, alpha=0.75)
    ax.set_xlabel("time [s]")
    ax.set_ylabel(f"samples removed (per {duration / bins:.0f} s)")
    ax.grid(True, alpha=0.3)
    ax.set_title(f"removals over time ({int(removed.sum())} total)")


def plot_summary(directory: Path, t: np.ndarray, removed: np.ndarray,
                 sorted_values: np.ndarray | None,
                 medians: list[dict[str, float]] | None) -> Path:
    """Compact panels: sorted curve, per-block medians, removals over time."""
    path = directory / "run_summary.png"

    have_sorted = sorted_values is not None and sorted_values.size > 0
    have_csv = bool(medians)

    fig = plt.figure(figsize=(14, 9), dpi=DPI)
    if have_sorted and have_csv:
        grid = fig.add_gridspec(2, 2, height_ratios=[1.15, 1.0])
        axes = {
            "sorted": fig.add_subplot(grid[0, :]),
            "blocks": fig.add_subplot(grid[1, 0]),
            "removals": fig.add_subplot(grid[1, 1]),
        }
    elif have_sorted or have_csv:
        grid = fig.add_gridspec(2, 1, height_ratios=[1.15, 1.0])
        top = "sorted" if have_sorted else "blocks"
        axes = {
            top: fig.add_subplot(grid[0, 0]),
            "removals": fig.add_subplot(grid[1, 0]),
        }
    else:
        grid = fig.add_gridspec(1, 1)
        axes = {"removals": fig.add_subplot(grid[0, 0])}

    if have_sorted:
        _panel_sorted(axes["sorted"], sorted_values)
    if have_csv:
        _panel_blocks(axes["blocks"], medians)
    _panel_removals(axes["removals"], t, removed)

    fig.suptitle(f"{directory.name}: run summary "
                 f"({t.size} samples, {int(removed.sum())} replaced)")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(path, dpi=DPI)
    plt.close(fig)
    return path


def main(argv: list[str]) -> int:
    directory = Path(argv[1] if len(argv) > 1 else ".").expanduser()
    if not directory.is_dir():
        print(f"plot_run: {directory} is not a directory", file=sys.stderr)
        return 2

    raw_path = directory / "raw.bin"
    filtered_path = directory / "filtered.bin"
    sorted_path = directory / "sorted.bin"
    medians_path = directory / "medians.csv"

    for required in (raw_path, filtered_path):
        if not required.is_file():
            print(f"plot_run: {required} is missing (required)", file=sys.stderr)
            return 2

    raw = load_floats(raw_path)
    filtered = load_floats(filtered_path)
    if raw.size != filtered.size:
        print(f"plot_run: raw.bin has {raw.size} samples but filtered.bin has "
              f"{filtered.size}; using the shorter overlap", file=sys.stderr)
        shared = min(raw.size, filtered.size)
        raw, filtered = raw[:shared], filtered[:shared]

    n = int(raw.size)
    t = np.arange(n, dtype=np.float64) / SAMPLE_RATE_HZ
    duration = n / SAMPLE_RATE_HZ
    removed = raw != filtered
    differing = int(removed.sum())

    sorted_values = None
    skipped: list[str] = []
    if sorted_path.is_file():
        sorted_values = load_floats(sorted_path)
    else:
        skipped.append("sorted.bin")

    medians = None
    if medians_path.is_file():
        medians = load_medians(medians_path)
    else:
        skipped.append("medians.csv")

    written = [
        plot_overview(directory, t, raw, filtered, removed),
        plot_zoom(directory, t, raw, filtered, removed),
        plot_summary(directory, t, removed, sorted_values, medians),
    ]

    print(f"directory  : {directory.resolve()}")
    print(f"samples    : {n}")
    print(f"duration   : {duration:.3f} s ({SAMPLE_RATE_HZ:g} Hz)")
    print(f"differing  : {differing} (raw != filtered)")
    if sorted_values is not None:
        print(f"sorted.bin : {sorted_values.size} samples, "
              f"global median = {float(np.median(sorted_values)):.9g}")
    if medians is not None:
        print(f"medians.csv: {len(medians)} block(s)")
        for row in medians:
            print(f"  block {row['block_index']}: samples={row['samples']} "
                  f"median={row['median']:.9g} "
                  f"outliers_removed={row['outliers_removed']}")
        print(f"  sum of block outliers = "
              f"{sum(r['outliers_removed'] for r in medians)}")
    if skipped:
        print(f"skipped    : {', '.join(skipped)} (not present)")
    for path in written:
        print(f"wrote      : {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
