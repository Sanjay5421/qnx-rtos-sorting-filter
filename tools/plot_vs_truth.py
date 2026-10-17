#!/usr/bin/env python3
"""Compare a sortmed run against the dataset's ground truth.

Usage:
    uv run python tools/plot_vs_truth.py RUN_DIR [--out DIR] [--npz PATH]

RUN_DIR holds the files the board's client wrote (raw.bin, filtered.bin). The
script finds which run and channel of `battery_sensor_dataset.npz` the run came
from by matching raw.bin bit for bit, and uses that run's `clean` series as the
answer key.

Channel-order trap: the .npz tensor stores [time, current, voltage, temperature],
while the interleaved `.bin` exports are [time, voltage, current, temperature].
Matching by value sidesteps it.

Figures written into OUT (default: RUN_DIR):
    truth_full.png     the whole recording: clean, received, filtered + metrics
    truth_zooms.png    the three busiest stretches, zoomed
    truth_error.png    error before/after, and the same error over time
    truth_columns.png  the block medians of all four columns of the recording
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
DPI = 200
CLEAN = "#111111"
NOISY = "#8d949c"
FILTER = "#1558d6"
FLAG = "#d93025"
MEDIAN = "#188038"

UNITS = ("s", "A", "V", "K")
NPZ_CHANNELS = ("time", "current", "voltage", "temperature")
BLOCKS = (16384, 16384, 16384, 10848)

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
    raise SystemExit("plot_vs_truth: cannot find battery_sensor_dataset.npz; pass --npz PATH")


def match_run(npz, raw: np.ndarray) -> tuple[int, int]:
    noisy = npz["noisy"]
    for run in range(noisy.shape[0]):
        for channel in range(noisy.shape[2]):
            if np.array_equal(noisy[run, :, channel].view("u4"), raw.view("u4")):
                return run, channel
    raise SystemExit("plot_vs_truth: raw.bin does not match any run/channel in the "
                     "dataset; is this the recording the run used?")


def style(ax, ylabel: str | None = None, xlabel: str | None = None) -> None:
    ax.grid(True, color="#d8d8d8", linewidth=0.7)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    if ylabel:
        ax.set_ylabel(ylabel)
    if xlabel:
        ax.set_xlabel(xlabel)


def envelope(x: np.ndarray, bins: int = 1500):
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


def block_medians(x: np.ndarray) -> list[float]:
    """The service's definition (median_of_sorted): the middle element for an
    odd count, the mean of the two middle elements for an even one. Every block
    here has an even count, so it is the mean - np.median does the same."""
    out, at = [], 0
    for size in BLOCKS:
        chunk = np.sort(x[at:at + size])
        out.append(float(np.median(chunk)))
        at += size
    return out


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
    run_id = str(npz["run_ids"][run])
    truth_col = npz["clean"][run, :, channel].astype(np.float64)
    n = raw.size
    t = np.arange(n) / RATE_HZ

    replaced = raw.view("u4") != filtered.view("u4")
    err_raw = np.abs(raw.astype(np.float64) - truth_col)
    err_flt = np.abs(filtered.astype(np.float64) - truth_col)
    rmse = lambda e: float(np.sqrt((e * e).mean()))
    r_raw, r_flt = rmse(err_raw), rmse(err_flt)
    mae_raw, mae_flt = float(err_raw.mean()), float(err_flt.mean())
    p99_raw, p99_flt = float(np.percentile(err_raw, 99)), float(np.percentile(err_flt, 99))
    gain = r_raw / r_flt
    noise = float(np.median(err_raw))

    times = npz["clean"][run, :, 0].astype(np.float64)
    span = float(times[-1] - times[0])
    print(f"directory : {run_dir}")
    print(f"matched   : run '{run_id}', channel {channel} ({NPZ_CHANNELS[channel]})")
    print(f"samples   : {n} over {span:.1f} s of recording (mean {n / span:.2f} Hz; "
          f"{RATE_HZ:.0f} Hz is only the client's nominal flag)")
    print(f"step      : {span / (n - 1) * 1e3:.2f} ms per sample (simulator output points)")
    print(f"noise     : median |received - clean| = {noise:.6f} V, "
          f"largest excursion {err_raw.max():.3f} V")
    print(f"received  : RMSE {r_raw:.6f}  MAE {mae_raw:.6f}  p99 {p99_raw:.6f}")
    print(f"filtered  : RMSE {r_flt:.6f}  MAE {mae_flt:.6f}  p99 {p99_flt:.6f}")
    print(f"gain      : RMSE {gain:.2f}x better, {int((err_flt < err_raw).sum())} of {n} samples closer")

    zoom_starts = busiest(replaced, int(2.0 * RATE_HZ), 3)

    # --- 1. the whole recording ------------------------------------------------
    centres, lo, hi = envelope(raw)
    fig, ax = plt.subplots(figsize=(15, 5.8), dpi=DPI)
    ax.plot(t, truth_col, color=CLEAN, linewidth=1.1, label="clean (ground truth)")
    ax.fill_between(centres / RATE_HZ, lo, hi, color=NOISY, alpha=0.45, linewidth=0,
                    label="received (min–max per 0.4 s)")
    ax.plot(t, filtered, color=FILTER, linewidth=1.4, label="filtered (service output)")
    for start in zoom_starts:
        ax.axvspan(start / RATE_HZ, (start + int(2.0 * RATE_HZ)) / RATE_HZ,
                   color=FLAG, alpha=0.10, linewidth=0)
    ax.set_xlim(0, t[-1])
    style(ax, "voltage (V)", "time (s)")
    ax.set_title("Against ground truth — the whole recording\n"
                 f"{run_id}, {n} samples, {n / RATE_HZ:.0f} s at {RATE_HZ:.0f} Hz", fontsize=13)
    box = (f"RMSE   received {r_raw * 1000:6.2f} mV   ->   filtered {r_flt * 1000:6.2f} mV"
           f"   ({gain:.1f}x better)\n"
           f"MAE    received {mae_raw * 1000:6.2f} mV   ->   filtered {mae_flt * 1000:6.2f} mV\n"
           f"p99    received {p99_raw * 1000:6.2f} mV   ->   filtered {p99_flt * 1000:6.2f} mV")
    ax.text(0.012, 0.035, box, transform=ax.transAxes, fontsize=10.5, va="bottom",
            family="monospace",
            bbox=dict(boxstyle="round,pad=0.45", facecolor="white",
                      edgecolor="#bbbbbb", alpha=0.95))
    ax.legend(loc="upper right", ncol=3, framealpha=0.95)
    fig.tight_layout()
    fig.savefig(out_dir / "truth_full.png", dpi=DPI)
    plt.close(fig)

    # --- 2. the three busiest stretches --------------------------------------
    fig, axes = plt.subplots(len(zoom_starts), 1, figsize=(14, 3.0 * len(zoom_starts) + 0.9),
                             dpi=DPI, squeeze=False)
    for ax, start in zip(axes[:, 0], zoom_starts):
        stop = min(start + int(2.0 * RATE_HZ), n)
        sl = slice(start, stop)
        sel = replaced[sl]
        ax.plot(t[sl], truth_col[sl], color=CLEAN, linewidth=2.2, label="clean (truth)")
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
            ax.legend(loc="upper right", ncol=4, framealpha=0.95)
    axes[-1, 0].set_xlabel("time (s)")
    fig.suptitle("The busiest stretches — received, filtered and the truth they track",
                 fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.965))
    fig.savefig(out_dir / "truth_zooms.png", dpi=DPI)
    plt.close(fig)

    # --- 3. error before and after -------------------------------------------
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 5.2), dpi=DPI,
                                   gridspec_kw={"width_ratios": [1.15, 1]})
    positive = err_raw[err_raw > 0]
    lo_e = max(float(positive.min()) if positive.size else 1e-5, 1e-5)
    bins = np.logspace(np.log10(lo_e), np.log10(float(err_raw.max())), 70)
    ax1.hist(np.clip(err_raw, 1e-5, None), bins=bins, color=NOISY, alpha=0.8,
             label=f"received — RMSE {r_raw * 1000:.2f} mV")
    ax1.hist(np.clip(err_flt, 1e-5, None), bins=bins, color=FILTER, alpha=0.8,
             label=f"filtered — RMSE {r_flt * 1000:.2f} mV")
    ax1.set_xscale("log")
    ax1.set_yscale("log")
    style(ax1, "samples (log)", "|error| against the clean signal (V, log)")
    ax1.set_title("How far each sample sits from the truth", fontsize=13)
    ax1.legend(framealpha=0.95)

    labels = ["median", "p90", "p99", "p99.9", "largest"]
    qs = [50, 90, 99, 99.9]
    raw_bars = [float(np.percentile(err_raw, q)) for q in qs] + [float(err_raw.max())]
    flt_bars = [float(np.percentile(err_flt, q)) for q in qs] + [float(err_flt.max())]
    x = np.arange(len(labels))
    ax2.bar(x - 0.19, raw_bars, 0.36, color=NOISY, label="received")
    ax2.bar(x + 0.19, flt_bars, 0.36, color=FILTER, label="filtered")
    ax2.set_yscale("log")
    ax2.set_xticks(x, labels)
    for xi, r, f in zip(x, raw_bars, flt_bars):
        ax2.text(xi - 0.19, r, f"{r * 1000:.1f}", ha="center", va="bottom", fontsize=9)
        ax2.text(xi + 0.19, f, f"{f * 1000:.1f}", ha="center", va="bottom", fontsize=9)
    ax2.set_ylim(min(flt_bars) * 0.35, max(raw_bars) * 3.2)
    style(ax2, "error (V, log)", "error percentile — labels in mV")
    ax2.set_title(f"Error profile, {gain:.1f}x better on RMSE", fontsize=13)
    ax2.legend(framealpha=0.95)
    fig.suptitle("What the filter bought", fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.94))
    fig.savefig(out_dir / "truth_error.png", dpi=DPI)
    plt.close(fig)

    # --- 4. every column of the recording ------------------------------------
    fig, axes = plt.subplots(2, 2, figsize=(14, 8.4), dpi=DPI)
    print("\nblock medians per column (the service's median_of_sorted: the mean of the")
    print("two middle values, every block here being even-sized)")
    print(f"{'column':12s} {'unit':5s} " + " ".join(f"{'block ' + str(i):>14s}" for i in range(4))
          + f" {'global':>14s}")
    for ax, column in zip(axes.ravel(), range(npz["noisy"].shape[2])):
        series = npz["noisy"][run, :, column].astype(np.float64)
        meds = block_medians(series)
        glob = float(np.median(series[:sum(BLOCKS)]))
        bars = ax.bar([f"block {i}\n{size} samples" for i, size in enumerate(BLOCKS)],
                      meds, color=FILTER, width=0.6, alpha=0.85)
        for bar, value in zip(bars, meds):
            ax.text(bar.get_x() + bar.get_width() / 2, value, f"{value:.5g}",
                    ha="center", va="bottom", fontsize=10)
        ax.axhline(glob, color=MEDIAN, linestyle="--", linewidth=1.4,
                   label=f"whole-recording median {glob:.5g}")
        span = max(meds) - min(meds)
        ax.set_ylim(min(meds) - 0.25 * span - 0.02, max(meds) + 0.45 * span + 0.02)
        style(ax, f"{NPZ_CHANNELS[column]} ({UNITS[column]})")
        ax.set_title(f"{NPZ_CHANNELS[column]} — median per block", fontsize=12.5)
        ax.legend(fontsize=9.5, loc="upper right", framealpha=0.95)
        pre = "     "
        print(f"{pre}{NPZ_CHANNELS[column]:12s} {UNITS[column]:5s} "
              + " ".join(f"{v:14.7g}" for v in meds) + f" {glob:14.7g}")
    fig.suptitle(f"All four columns of {run_id}\n"
                 "computed from the same recording — the board run processed voltage "
                 "(one channel per run)", fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    fig.savefig(out_dir / "truth_columns.png", dpi=DPI)
    plt.close(fig)

    for name in ("truth_full.png", "truth_zooms.png", "truth_error.png", "truth_columns.png"):
        print(f"wrote     : {out_dir / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
