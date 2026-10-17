#!/usr/bin/env python3
"""Compare a sortmed run against the dataset's ground truth.

Usage:
    uv run python tools/plot_vs_truth.py RUN_DIR [--out DIR] [--npz PATH]

RUN_DIR holds the files the board's client wrote (raw.bin, filtered.bin,
optional sorted.bin / medians.csv). The script finds which run and channel of
`battery_sensor_dataset.npz` the run came from by matching raw.bin bit for bit,
then uses that run's `clean` series as the answer key.

Note the channel order trap: the .npz tensor stores
[time, current, voltage, temperature], while the interleaved `.bin` exports are
[time, voltage, current, temperature]. Matching by value sidesteps it entirely.

Figures written into OUT (default: RUN_DIR):
    truth_full.png          the whole recording: clean, noisy, filtered + metrics
    truth_zooms.png         three heavy-corruption windows, zoomed
    truth_spike_closeup.png the largest spike, sample by sample
    truth_error.png         error before/after, and the filter's hit/miss split
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

RATE_HZ = 100.0
DPI = 150
CLEAN = "#111111"      # answer key
NOISY = "#9aa0a6"      # the signal as received
FILTER = "#1a73e8"     # what the service produced
FLAG = "#d93025"       # samples the filter replaced

NPZ_CANDIDATES = (
    "data/artifacts/dataset/battery_sensor_dataset.npz",
    "artifacts/dataset/battery_sensor_dataset.npz",
    "../data/artifacts/dataset/battery_sensor_dataset.npz",
)


def find_npz(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.exists():
            raise SystemExit(f"plot_vs_truth: no such file: {path}")
        return path
    for candidate in NPZ_CANDIDATES:
        path = Path(candidate)
        if path.exists():
            return path
    raise SystemExit("plot_vs_truth: cannot find battery_sensor_dataset.npz; "
                     "pass --npz PATH")


def match_run(npz, raw: np.ndarray) -> tuple[int, int]:
    """Return (run index, channel index) whose noisy column equals raw bit for bit."""
    noisy = npz["noisy"]
    for run in range(noisy.shape[0]):
        for channel in range(noisy.shape[2]):
            if np.array_equal(noisy[run, :, channel].view("u4"), raw.view("u4")):
                return run, channel
    raise SystemExit("plot_vs_truth: raw.bin does not match any run/channel in the "
                     "dataset; is this the recording the run used?")


def style(ax, ylabel: str | None = None) -> None:
    ax.grid(True, color="#d0d0d0", linewidth=0.6, alpha=0.8)
    ax.set_axisbelow(True)
    if ylabel:
        ax.set_ylabel(ylabel)


def metrics(err: np.ndarray) -> dict:
    return {
        "rmse": float(np.sqrt((err * err).mean())),
        "mae": float(err.mean()),
        "p99": float(np.percentile(err, 99)),
        "max": float(err.max()),
    }


def busiest_windows(flags: np.ndarray, width: int, count: int) -> list[int]:
    """Start indices of the `count` busiest non-overlapping windows of `flags`."""
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


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("run_dir", nargs="?", default=".")
    ap.add_argument("--out")
    ap.add_argument("--npz")
    args = ap.parse_args(argv[1:])

    run_dir = Path(args.run_dir).expanduser()
    out_dir = Path(args.out).expanduser() if args.out else run_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    raw = np.fromfile(run_dir / "raw.bin", dtype="<f4")
    filtered = np.fromfile(run_dir / "filtered.bin", dtype="<f4")
    if raw.size == 0 or raw.size != filtered.size:
        raise SystemExit("plot_vs_truth: raw.bin and filtered.bin must exist and agree in length")

    npz = np.load(find_npz(args.npz))
    run, channel = match_run(npz, raw)
    truth = npz["clean"][run, :, channel].astype(np.float64)
    mask = npz["outlier_mask"][run, :, channel]
    run_id = str(npz["run_ids"][run])
    names = ["time", "current", "voltage", "temperature"]
    n = raw.size
    t = np.arange(n) / RATE_HZ

    grey_gap = raw.view("u4") != filtered.view("u4")      # the filter changed these
    err_raw = np.abs(raw.astype(np.float64) - truth)
    err_flt = np.abs(filtered.astype(np.float64) - truth)
    m_raw, m_flt = metrics(err_raw), metrics(err_flt)
    tp = int((grey_gap & mask).sum())
    fp = int((grey_gap & ~mask).sum())
    fn = int((~grey_gap & mask).sum())
    precision = 100.0 * tp / max(tp + fp, 1)
    recall = 100.0 * tp / max(tp + fn, 1)
    f1 = 200.0 * tp / max(2 * tp + fp + fn, 1)
    noise = float(np.median(np.abs(raw.astype(np.float64) - truth)))

    print(f"directory : {run_dir}")
    print(f"matched   : run '{run_id}', channel {channel} ({names[channel]})")
    print(f"samples   : {n}  ({n / RATE_HZ:.1f} s at {RATE_HZ:.0f} Hz)")
    print(f"noise     : median |raw - clean| = {noise:.6f} V "
          f"(largest spike {err_raw.max():.3f} V = {err_raw.max() / max(noise, 1e-12):.0f}x)")
    print(f"raw       : RMSE {m_raw['rmse']:.6f}  MAE {m_raw['mae']:.6f}  "
          f"p99 {m_raw['p99']:.6f}  max {m_raw['max']:.6f}")
    print(f"filtered  : RMSE {m_flt['rmse']:.6f}  MAE {m_flt['mae']:.6f}  "
          f"p99 {m_flt['p99']:.6f}  max {m_flt['max']:.6f}")
    print(f"gain      : RMSE improved {m_raw['rmse'] / m_flt['rmse']:.1f}x, "
          f"{int((err_flt < err_raw).sum())} of {n} samples made closer")
    print(f"decisions : {int(grey_gap.sum())} replaced -> hits {tp}, false {fp}, "
          f"missed {fn}  precision {precision:.1f}%  recall {recall:.1f}%  F1 {f1:.1f}%")

    zooms = busiest_windows(grey_gap, int(2.0 * RATE_HZ), 3)
    spike = int(np.argmax(err_raw))

    # --- 1. the whole recording ------------------------------------------------
    fig, ax = plt.subplots(figsize=(15, 5.2), dpi=DPI)
    ax.plot(t, truth, color=CLEAN, linewidth=0.8, label="clean (ground truth)")
    ax.plot(t, raw, color=NOISY, linewidth=0.5, alpha=0.7, label="noisy (received)")
    ax.plot(t, filtered, color=FILTER, linewidth=0.7, label="filtered (service)")
    ax.plot(t[grey_gap], raw[grey_gap], "o", color=FLAG, markersize=1.6,
            linestyle="none", label=f"replaced ({int(grey_gap.sum())})")
    for start in zooms:
        ax.axvspan(start / RATE_HZ, (start + int(2.0 * RATE_HZ)) / RATE_HZ,
                   color=FLAG, alpha=0.10, linewidth=0)
    ax.set_xlim(0, t[-1])
    ax.set_xlabel("time (s)")
    style(ax, "voltage (V)")
    ax.set_title(f"{run_id} — {names[channel]}, whole recording "
                 f"({n} samples, {n / RATE_HZ:.0f} s)", fontsize=11)
    box = (f"RMSE  raw {m_raw['rmse'] * 1000:.2f} mV  ->  filtered {m_flt['rmse'] * 1000:.2f} mV"
           f"   ({m_raw['rmse'] / m_flt['rmse']:.1f}x better)\n"
           f"MAE   raw {m_raw['mae'] * 1000:.2f} mV  ->  filtered {m_flt['mae'] * 1000:.2f} mV\n"
           f"spikes replaced: {int(grey_gap.sum())}   precision {precision:.0f}%  recall {recall:.0f}%")
    ax.text(0.012, 0.03, box, transform=ax.transAxes, fontsize=8.5, va="bottom",
            family="monospace", bbox=dict(boxstyle="round,pad=0.4", facecolor="white",
                                          edgecolor="#bbbbbb", alpha=0.9))
    ax.legend(loc="upper right", fontsize=8, framealpha=0.9, ncol=4)
    fig.tight_layout()
    fig.savefig(out_dir / "truth_full.png", dpi=DPI)
    plt.close(fig)

    # --- 2. the three busiest windows, zoomed ---------------------------------
    fig, axes = plt.subplots(len(zooms), 1, figsize=(14, 2.6 * len(zooms) + 0.6),
                             dpi=DPI, squeeze=False)
    for ax, start in zip(axes[:, 0], zooms):
        stop = min(start + int(2.0 * RATE_HZ), n)
        sl = slice(start, stop)
        ax.plot(t[sl], truth[sl], color=CLEAN, linewidth=1.6, label="clean (ground truth)")
        ax.plot(t[sl], raw[sl], "o-", color=NOISY, linewidth=0.8, markersize=2.6,
                markerfacecolor="white", label="noisy (received)")
        ax.plot(t[sl], filtered[sl], color=FILTER, linewidth=1.5, label="filtered (service)")
        sel = grey_gap[sl]
        ax.plot(t[sl][sel], raw[sl][sel], "o", color=FLAG, markersize=7,
                markerfacecolor="none", markeredgewidth=1.2, linestyle="none",
                label="replaced")
        ax.set_xlim(t[start], t[stop - 1])
        style(ax, "voltage (V)")
        ax.set_title(f"{t[start]:.1f}–{t[stop - 1]:.1f} s — "
                     f"{int(sel.sum())} samples replaced", fontsize=10, loc="left")
        if start == zooms[0]:
            ax.legend(loc="upper right", fontsize=8, ncol=4, framealpha=0.9)
    axes[-1, 0].set_xlabel("time (s)")
    fig.suptitle(f"{run_id} — the three busiest stretches, zoomed", fontsize=12)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(out_dir / "truth_zooms.png", dpi=DPI)
    plt.close(fig)

    # --- 3. the largest spike, sample by sample -------------------------------
    half = 10
    lo, hi = max(0, spike - half), min(n, spike + half + 1)
    sl = slice(lo, hi)
    fig, ax = plt.subplots(figsize=(11, 4.8), dpi=DPI)
    ax.plot(t[sl], truth[sl], color=CLEAN, linewidth=1.8, marker="s", markersize=3.5,
            label="clean (ground truth)")
    ax.plot(t[sl], raw[sl], "o", color=NOISY, markersize=7, markerfacecolor="white",
            markeredgewidth=1.4, label="noisy sample (received)")
    ax.plot(t[sl], filtered[sl], color=FILTER, linewidth=1.6, marker="o", markersize=3.5,
            label="filtered sample (service)")
    ax.annotate(f"received {raw[spike]:.4f} V\nclean    {truth[spike]:.4f} V\n"
                f"filtered {filtered[spike]:.4f} V",
                xy=(t[spike], raw[spike]), xytext=(8, 14), textcoords="offset points",
                fontsize=9, family="monospace",
                bbox=dict(boxstyle="round,pad=0.35", facecolor="white", edgecolor=FLAG),
                arrowprops=dict(arrowstyle="->", color=FLAG, linewidth=1.1))
    ax.axvspan(t[spike] - 0.5 / RATE_HZ, t[spike] + 0.5 / RATE_HZ,
               color=FLAG, alpha=0.12, linewidth=0)
    ax.set_xlim(t[lo], t[hi - 1])
    ax.set_xlabel("time (s)")
    style(ax, "voltage (V)")
    ax.set_title(f"Largest spike, sample by sample — the window around "
                 f"{t[spike]:.2f} s (median window 9, threshold 4)", fontsize=11)
    ax.legend(loc="best", fontsize=8.5, framealpha=0.9)
    fig.tight_layout()
    fig.savefig(out_dir / "truth_spike_closeup.png", dpi=DPI)
    plt.close(fig)

    # --- 4. error before/after, and what the decisions were -------------------
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5), dpi=DPI,
                                   gridspec_kw={"width_ratios": [1.35, 1]})
    positive = err_raw[err_raw > 0]
    lo = max(float(positive.min()) if positive.size else 1e-5, 1e-5)
    bins = np.logspace(np.log10(lo), np.log10(float(err_raw.max())), 60)
    ax1.hist(np.clip(err_raw, 1e-5, None), bins=bins, color=NOISY, alpha=0.75,
             label=f"raw: RMSE {m_raw['rmse'] * 1000:.2f} mV")
    ax1.hist(np.clip(err_flt, 1e-5, None), bins=bins, color=FILTER, alpha=0.75,
             label=f"filtered: RMSE {m_flt['rmse'] * 1000:.2f} mV")
    ax1.set_xscale("log")
    ax1.set_yscale("log")
    ax1.set_xlabel("|error| against the clean signal (V, log scale)")
    style(ax1, "samples (log scale)")
    ax1.set_title("Error distribution before and after filtering", fontsize=11)
    ax1.legend(fontsize=9, framealpha=0.9)

    labels = ["hits\n(true outlier, replaced)", "false flags\n(clean, replaced)",
              "missed\n(true outlier, kept)"]
    values = [tp, fp, fn]
    colours = ["#188038", FLAG, "#f9ab00"]
    bars = ax2.bar(labels, values, color=colours, width=0.62)
    for bar, value in zip(bars, values):
        ax2.text(bar.get_x() + bar.get_width() / 2, value, str(value),
                 ha="center", va="bottom", fontsize=10, fontweight="bold")
    ax2.set_ylim(0, max(values) * 1.18)
    style(ax2, "samples")
    ax2.set_title(f"Decisions vs the injected-outlier mask\n"
                  f"precision {precision:.0f}%   recall {recall:.0f}%   F1 {f1:.0f}%",
                  fontsize=11)
    fig.suptitle(f"{run_id} — {names[channel]}: what the filter achieved", fontsize=12)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(out_dir / "truth_error.png", dpi=DPI)
    plt.close(fig)

    for name in ("truth_full.png", "truth_zooms.png",
                 "truth_spike_closeup.png", "truth_error.png"):
        print(f"wrote     : {out_dir / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
