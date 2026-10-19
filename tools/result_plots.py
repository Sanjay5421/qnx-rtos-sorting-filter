#!/usr/bin/env python3
# ============================================================================
#   Real-Time Multi-Core Sorting and Median Filter Service
#
#   The client offloads 64 KiB blocks of unsorted float32 time-series metrics to
#   the server. The server sorts each block with a parallel merge sort over four
#   worker threads -- mutexes and condition variables guard the merge phases --
#   applies a rolling Hampel median filter to remove sensor outliers, and signals
#   completion with a QNX pulse carrying the median, with the sorted and filtered
#   arrays left in shared memory for the client to read.
#
#   Anmol Govindarajapuram Krishnan   -  CB.EN.U4EEE23103
#   Mauli Rajguru                     -  CB.EN.U4EEE23120
#   Sanjay TS                         -  CB.EN.U4EEE23136
#
#   26ELC331 Real-Time Operating Systems
#   Date of evaluation: 17-10-2026
# ============================================================================

"""The result figures, one idea each.

Usage:
    uv run python tools/result_plots.py RUN_ROOT [--out DIR] [--npz PATH]
                                                [--only NAME]

RUN_ROOT holds one directory per channel the service ran -- voltage, current,
temperature, time -- each with the files the board's client wrote: raw.bin,
sorted.bin, filtered.bin and medians.csv. --only NAME draws a single channel.

Figures, into OUT (default: RUN_ROOT/../run-figures):

    sort_curve_<ch>.png      one 64 KiB block: as received, and sorted
    sort_multiset_<ch>.png   the values before and after the sort: they are identical
    filter_overlay_<ch>.png  what arrived against what the service returned
    filter_error_<ch>.png    error against ground truth, received vs filtered
    filter_zoom_<ch>.png     three windows, zoomed onto where the filter acted most
    error_map.png            error over time and channel, in units of each noise sigma

Each figure carries one message and no more: no metrics boxes, no annotation of
events, no spike markers. The numbers that belong in a caption -- RMSE, MAE, p99,
outlier counts -- are printed by this script instead.

The error figures need the dataset for their answer key: the `clean` series of
battery_sensor_dataset.npz. Which run and channel a directory holds is found by
matching raw.bin to `noisy` bit for bit, which also sidesteps the channel-order
trap -- the .npz is [time, current, voltage, temperature], while the interleaved
.bin exports are [time, voltage, current, temperature].

error_map.png normalises each channel's error by that channel's own noise sigma
(from the generator's metadata.json, beside the .npz), because a volt, an amp and
a kelvin are not comparable. The per-channel figures stay in the channel's own
units, where a reader can check them against the datasheet.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.colors import LogNorm  # noqa: E402

RATE_HZ = 100.0                    # nominal; the dataset's own column is the truth
DPI = 200
UNITS = {"time": "s", "current": "A", "voltage": "V", "temperature": "K"}
SENSORS = ("voltage", "current", "temperature")     # the channels that carry noise
NPZ_CHANNELS = ("time", "current", "voltage", "temperature")
SIGMA_KEYS = {"voltage": "voltage_sigma_v",
              "current": "current_sigma_a",
              "temperature": "temperature_sigma_k"}

# Okabe-Ito, the colour-blind-safe set the sweep figures use.
RECEIVED = "#999999"               # what the sensor reported, outliers and all
SERVICE = "#0072B2"                # what the service returned
TRUTH = "#111111"                  # the dataset's clean series
FLAG = "#D55E00"                   # the values the filter replaced

plt.rcParams.update({
    "font.family": "serif",
    "font.size": 13,
    "axes.titlesize": 15,
    "axes.labelsize": 13,
    "legend.fontsize": 11.5,
    "xtick.labelsize": 11,
    "ytick.labelsize": 11,
    "axes.linewidth": 0.9,
    "axes.edgecolor": "#444444",
    "figure.facecolor": "white",
    "savefig.facecolor": "white",
    "savefig.bbox": "tight",
})

NPZ_CANDIDATES = (
    "data/artifacts/dataset/battery_sensor_dataset.npz",
    "artifacts/dataset/battery_sensor_dataset.npz",
    "../data/artifacts/dataset/battery_sensor_dataset.npz",
)


def find_npz(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit).expanduser()
        if not path.exists():
            raise SystemExit(f"result_plots: no such file: {path}")
        return path
    for candidate in NPZ_CANDIDATES:
        path = Path(candidate)
        if path.exists():
            return path
    raise SystemExit("result_plots: cannot find battery_sensor_dataset.npz; "
                     "pass --npz PATH")


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
    """Per-column min and max of a long series, so single-sample spikes stay
    visible instead of collapsing into the line's thickness."""
    edges = np.linspace(0, x.size, bins + 1).astype(int)
    centres = (edges[:-1] + edges[1:]) / 2.0
    lo = np.array([x[a:b].min() for a, b in zip(edges[:-1], edges[1:])])
    hi = np.array([x[a:b].max() for a, b in zip(edges[:-1], edges[1:])])
    return centres, lo, hi


def busiest(flags: np.ndarray, width: int, count: int) -> list[int]:
    """The starts of `count` non-overlapping windows of `width` samples holding
    the most flagged samples -- where there is something to see."""
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


def match_run(npz, raw: np.ndarray) -> tuple[int, int]:
    """Find which run and channel of the dataset this raw.bin came from."""
    noisy = npz["noisy"]
    for run in range(noisy.shape[0]):
        for channel in range(noisy.shape[2]):
            if np.array_equal(noisy[run, :, channel].view("u4"), raw.view("u4")):
                return run, channel
    raise SystemExit("result_plots: raw.bin does not match any run/channel in the "
                     "dataset; is this the recording the run used?")


def noise_sigmas(npz_path: Path) -> dict[str, float]:
    """The generator's per-channel noise, from the metadata beside the .npz."""
    meta = npz_path.with_name("metadata.json")
    if not meta.exists():
        raise SystemExit(f"result_plots: no metadata beside {npz_path}; "
                         "error_map.png needs the noise sigmas")
    config = json.loads(meta.read_text())["config"]["noise"]
    return {"voltage": float(config["voltage_sigma_v"]),
            "current": float(config["current_sigma_a"]),
            "temperature": float(config["temperature_sigma_k"])}


def read_run(run_dir: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray | None]:
    raw = np.fromfile(run_dir / "raw.bin", dtype="<f4")
    filtered = np.fromfile(run_dir / "filtered.bin", dtype="<f4")
    if raw.size == 0:
        raise SystemExit(f"result_plots: {run_dir / 'raw.bin'} is missing or empty")
    if raw.size != filtered.size:
        raise SystemExit(f"result_plots: {run_dir}: raw.bin has {raw.size} samples, "
                         f"filtered.bin has {filtered.size}")
    sorted_path = run_dir / "sorted.bin"
    ordered = np.fromfile(sorted_path, dtype="<f4") if sorted_path.exists() else None
    if ordered is not None and ordered.size != raw.size:
        raise SystemExit(f"result_plots: {run_dir}: sorted.bin has {ordered.size} "
                         f"samples, raw.bin has {raw.size}")
    return raw, filtered, ordered


def verify(raw: np.ndarray, ordered: np.ndarray | None,
           medians_path: Path) -> list[str]:
    """What the sort figures claim, checked rather than asserted.

    sorted.bin is the client's own ascending order of the whole recording -- the
    by-product of its cross-check, not the server's per-block arrays -- so it is
    compared against the global sort. The server's own sorting is evidenced by
    the block medians: the completion pulse carries each one, the CSV holds what
    the client got back, and each must be the median of that block's raw samples
    (which is what the service's median_of_sorted computes)."""
    out: list[str] = []
    if ordered is not None:
        same = np.array_equal(np.sort(raw), ordered)
        ascending = bool(np.all(np.diff(ordered) >= 0))
        out.append("sorted.bin: ascending, bit-exact permutation of raw.bin"
                   if (same and ascending) else
                   f"FAILED: identical={same} ascending={ascending}")
    if medians_path.exists():
        at, bad = 0, []
        for row in csv.DictReader(medians_path.open()):
            size = int(row["samples"])
            mine = float(np.median(np.sort(raw[at:at + size])))
            if not np.isclose(mine, float(row["median"]), rtol=1e-6, atol=0.0):
                bad.append((row["block_index"], mine, float(row["median"])))
            at += size
        out.append(f"medians.csv: {at} samples over "
                   f"{len(list(csv.DictReader(medians_path.open())))} blocks match"
                   if not bad else f"FAILED medians: {bad[:2]}")
    return out


# ---------------------------------------------------------------------------
# 1. sorting: what came in, and what left
# ---------------------------------------------------------------------------

def fig_sort_curve(raw, ordered, out, channel, unit):
    """Every sample of the recording against the position it occupies. The
    sorted series is the same 60 000 values in ascending order, so the two
    curves share a y-range and differ only in the order of their points."""
    index = np.arange(raw.size)
    fig, ax = plt.subplots(figsize=(13.5, 5.4), dpi=DPI)
    ax.plot(index, raw, color=RECEIVED, linewidth=0.8, alpha=0.9,
            label="as received")
    ax.plot(index, ordered, color=SERVICE, linewidth=1.3, label="sorted")
    style(ax, f"{channel} ({unit})", "sample index")
    ax.set_title(f"{raw.size} samples: the same values, placed in order")
    ax.legend(loc="best", frameon=False)
    fig.savefig(out / f"sort_curve_{channel}.png", dpi=DPI)
    plt.close(fig)


def fig_sort_multiset(raw, ordered, out, channel, unit):
    """The values, not their order. The two histograms must sit on top of one
    another: the sort permutes, it cannot create or lose a sample."""
    lo = float(min(raw.min(), ordered.min()))
    hi = float(max(raw.max(), ordered.max()))
    bins = np.linspace(lo, hi, 120)
    fig, ax = plt.subplots(figsize=(11, 5.6), dpi=DPI)
    ax.hist(ordered, bins=bins, color=SERVICE, alpha=0.45,
            label="after the sort", zorder=2)
    ax.hist(raw, bins=bins, histtype="step", color=RECEIVED, linewidth=1.6,
            label="before the sort", zorder=3)
    ax.set_yscale("log")
    style(ax, "samples per bin (log)", f"{channel} ({unit})")
    ax.set_title("The same values before and after: the sort only reorders")
    ax.legend(loc="best", frameon=False)
    fig.savefig(out / f"sort_multiset_{channel}.png", dpi=DPI)
    plt.close(fig)


# ---------------------------------------------------------------------------
# 2. the filter: what it did to the recording, and what it left behind
# ---------------------------------------------------------------------------

def fig_filter_overlay(raw, filtered, out, channel, unit):
    """Two series and nothing else: what arrived, what was returned."""
    t = np.arange(raw.size) / RATE_HZ
    centres, lo, hi = envelope(raw)
    fig, ax = plt.subplots(figsize=(13.5, 5.4), dpi=DPI)
    ax.fill_between(centres / RATE_HZ, lo, hi, color=RECEIVED, alpha=0.75,
                    linewidth=0, label="received (min-max per 0.4 s)")
    ax.plot(t, filtered, color=SERVICE, linewidth=1.5,
            label="filtered (service output)")
    style(ax, f"{channel} ({unit})", "time (s)")
    ax.set_title(f"The recording and the service's answer ({raw.size} samples)")
    ax.legend(loc="best", frameon=False)
    fig.savefig(out / f"filter_overlay_{channel}.png", dpi=DPI)
    plt.close(fig)


def fig_filter_error(err_raw, err_filtered, out, channel, unit):
    """The distribution of the error against ground truth, before and after."""
    top = max(float(err_raw.max()), float(err_filtered.max()))
    bins = np.logspace(np.log10(top) - 4.6, np.log10(top), 90)
    fig, ax = plt.subplots(figsize=(11, 5.6), dpi=DPI)
    ax.hist(err_raw, bins=bins, histtype="step", color=RECEIVED, linewidth=1.7,
            label="received")
    ax.hist(err_filtered, bins=bins, histtype="step", color=SERVICE, linewidth=1.7,
            label="filtered")
    ax.set_xscale("log")
    ax.set_yscale("log")
    style(ax, "samples per bin (log)", f"error ({unit}, log)")
    ax.set_title("Error against ground truth, before and after the filter")
    ax.legend(loc="best", frameon=False)
    fig.savefig(out / f"filter_error_{channel}.png", dpi=DPI)
    plt.close(fig)


def fig_filter_zoom(raw, filtered, truth, changed, out, channel, unit, windows):
    """Three windows, zoomed onto where the filter acted most: the received
    samples, the answer, and the truth they are both trying to be."""
    rows = len(windows)
    fig, axes = plt.subplots(rows, 1, figsize=(12.5, 3.1 * rows), dpi=DPI,
                             sharey=True)
    for ax, start in zip(np.atleast_1d(axes), windows):
        stop = min(start + int(2.0 * RATE_HZ), raw.size)
        t = np.arange(start, stop) / RATE_HZ
        ax.plot(t, raw[start:stop], color=RECEIVED, linewidth=1.2,
                marker="o", markersize=2.4, label="received")
        ax.plot(t, filtered[start:stop], color=SERVICE, linewidth=1.6,
                label="filtered")
        ax.plot(t, truth[start:stop], color=TRUTH, linewidth=1.0,
                linestyle="--", label="ground truth")
        n_flag = int(changed[start:stop].sum())
        ax.set_title(f"{t[0]:.1f} s  ({n_flag} samples replaced)", fontsize=13)
        style(ax, f"{channel} ({unit})")
        if ax is np.atleast_1d(axes)[-1]:
            ax.set_xlabel("time (s)")
    axes0 = np.atleast_1d(axes)[0]
    axes0.legend(loc="best", frameon=False, ncols=3)
    fig.suptitle("Where the filter acted, magnified", y=1.0, fontsize=16)
    fig.tight_layout()
    fig.savefig(out / f"filter_zoom_{channel}.png", dpi=DPI)
    plt.close(fig)


# ---------------------------------------------------------------------------
# 3. the one that compares everything at once
# ---------------------------------------------------------------------------

def fig_error_map(rows, out):
    """error_map.png -- each sensor channel's error over time, before and after,
    in units of that channel's own noise sigma so the channels are comparable.
    Per time bin the *worst* sample is shown: a 0.4 s bin is 40 samples, and
    averaging would hide exactly the events the service exists to remove."""
    channels = [name for name, _, _ in rows]
    err_received = np.stack([err for _, err, _ in rows])
    err_filtered = np.stack([err for _, _, err in rows])
    bins = 150
    edges = np.linspace(0, err_received.shape[1], bins + 1).astype(int)
    worst = lambda x: np.array([x[:, a:b].max(axis=1) for a, b in zip(edges[:-1], edges[1:])]).T
    before, after = worst(err_received), worst(err_filtered)
    x_edges = edges / RATE_HZ          # pcolormesh wants edges, not centres

    vmax = 10.0 ** np.ceil(np.log10(float(before.max())))
    norm = LogNorm(vmin=0.5, vmax=vmax)
    fig, axes = plt.subplots(2, 1, figsize=(13, 6.6), dpi=DPI, sharex=True)
    for ax, grid, title in ((axes[0], before, "as received"),
                            (axes[1], after, "after the filter")):
        mesh = ax.pcolormesh(x_edges, np.arange(len(channels) + 1), grid,
                             cmap="magma", norm=norm, shading="flat")
        ax.set_yticks(np.arange(len(channels)) + 0.5)
        ax.set_yticklabels(channels)
        ax.set_title(title, fontsize=14)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
    axes[1].set_xlabel("time (s)")
    bar = fig.colorbar(mesh, ax=axes, pad=0.02)
    bar.set_label("worst error in the 0.4 s bin (noise sigma)")
    fig.suptitle("What the corruption did, and what the filter did about it",
                 fontsize=16)
    fig.savefig(out / "error_map.png", dpi=DPI)
    plt.close(fig)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run_root", nargs="?", default=".",
                    help="directory holding one subdirectory per channel")
    ap.add_argument("--out", help="where to write the figures (default: "
                                  "RUN_ROOT/../run-figures)")
    ap.add_argument("--npz", help="the dataset, for the ground truth")
    ap.add_argument("--only", action="append", choices=sorted(UNITS),
                    help="draw one channel only (repeatable)")
    args = ap.parse_args(argv[1:])

    run_root = Path(args.run_root).expanduser()
    if not run_root.is_dir():
        raise SystemExit(f"result_plots: no such directory: {run_root}")
    out = Path(args.out).expanduser() if args.out else run_root.parent / "run-figures"
    out.mkdir(parents=True, exist_ok=True)

    npz_path = find_npz(args.npz)
    npz = np.load(npz_path)
    sigmas = noise_sigmas(npz_path)
    wanted = args.only or [c for c in UNITS if (run_root / c).is_dir()]

    map_rows = []
    for channel in wanted:
        run_dir = run_root / channel
        if not run_dir.is_dir():
            print(f"skip       : {channel} (no {run_dir})")
            continue
        raw, filtered, ordered = read_run(run_dir)
        run, index = match_run(npz, raw)
        truth = npz["clean"][run, :, index].astype(np.float64)
        changed = raw.view("u4") != filtered.view("u4")
        err_raw = np.abs(raw.astype(np.float64) - truth)
        err_filtered = np.abs(filtered.astype(np.float64) - truth)

        if ordered is not None:
            fig_sort_curve(raw, ordered, out, channel, UNITS[channel])
            fig_sort_multiset(raw, ordered, out, channel, UNITS[channel])
        if changed.any():
            fig_filter_overlay(raw, filtered, out, channel, UNITS[channel])
            fig_filter_error(err_raw, err_filtered, out, channel, UNITS[channel])
            fig_filter_zoom(raw, filtered, truth, changed, out, channel,
                            UNITS[channel], busiest(changed, int(2.0 * RATE_HZ), 3))
        else:
            # A channel the filter never touched: the time ramp carries no
            # injected corruption, so its three filter figures would be three
            # identical lines and a log axis of zeros.
            print(f"{channel:12s} skipped  : no filter figures, "
                  f"nothing was replaced")

        if channel in sigmas:
            map_rows.append((channel, err_raw / sigmas[channel],
                             err_filtered / sigmas[channel]))

        # The numbers that belong in a caption, not in the figure.
        rmse = lambda x: float(np.sqrt((x ** 2).mean()))
        for line in verify(raw, ordered, run_dir / "medians.csv"):
            print(f"{'':12s} {line}")
        print(f"{channel:12s} run      : {npz['run_ids'][run]} column {index}")
        print(f"{'':12s} replaced : {int(changed.sum())} samples "
              f"of {raw.size}")
        print(f"{'':12s} RMSE     : {rmse(err_raw):.6g} -> "
              f"{rmse(err_filtered):.6g} {UNITS[channel]}")
        print(f"{'':12s} MAE      : {err_raw.mean():.6g} -> "
              f"{err_filtered.mean():.6g} {UNITS[channel]}")
        print(f"{'':12s} p99      : {np.percentile(err_raw, 99):.6g} -> "
              f"{np.percentile(err_filtered, 99):.6g} {UNITS[channel]}")

    if len(map_rows) >= 2:
        fig_error_map(map_rows, out)
        print(f"wrote      : {out / 'error_map.png'}")
    else:
        print("skip       : error_map.png (needs at least two sensor channels)")

    for name in sorted(p.name for p in out.glob("*.png")):
        print(f"wrote      : {out / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
