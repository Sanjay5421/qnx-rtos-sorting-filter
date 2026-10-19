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

"""Score a filter-width (or worker-count) sweep against the dataset's truth.

Usage:
    uv run python tools/filter_sweep.py SWEEP_DIR [--out DIR] [--npz PATH]

SWEEP_DIR is what RT-MOMS/board/perf-sweep.sh wrote on the board, copied back
whole: one directory per case (raw.bin, filtered.bin, medians.csv, client.log),
a sweep.manifest describing every case, and the server log(s) that hold the
per-block `perf:` lines.

The quality numbers come from the board's own filtered.bin -- this script never
filters anything itself, so what it reports is what the service produced -- and
the answer key is the dataset's own `clean` series and `outlier_mask`.

Outputs, into --out (default: SWEEP_DIR):

    quality.csv                per case: masked recall, clean replacements, RMSE
    cost.csv                   per full block: the server's stage times, in ms
    width_quality.png          recall, false replacements and RMSE gain vs width
    width_cost.png             sort, filter and whole-block ms vs width, log-log
    width_tradeoff.png         what a width costs against what it buys (voltage)
    width_bursts.png           recall against the injected burst length
    threads_scaling.png        cost and speedup against the worker count

The width figures are written only when the sweep covered more than one width,
and the scaling figure only when it covered more than one worker count: a
single-point curve is worse than no figure.

It also checks the invariants a sweep must not break: every case of the same
recording and channel has an identical raw.bin (the same input), identical
block medians (the medians are of the raw data, so they cannot depend on the
width), and identical quality numbers across repeats of the same case.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

DPI = 200

# Okabe-Ito, the standard colour-blind-safe set: one colour per channel, used
# identically in every figure. The three stages get their own colours too.
CHANNEL_COLOURS = {
    "voltage": "#0072B2",
    "current": "#009E73",
    "temperature": "#D55E00",
    "time": "#999999",
}
STAGE_COLOURS = {
    "sort": "#0072B2",
    "filter": "#D55E00",
    "total": "#000000",
}
SENSOR_CHANNELS = ("voltage", "current", "temperature")

plt.rcParams.update({
    "font.family": "serif",
    "font.size": 14,
    "axes.titlesize": 17,
    "axes.labelsize": 15,
    "legend.fontsize": 13,
    "xtick.labelsize": 13,
    "ytick.labelsize": 13,
    "axes.linewidth": 1.0,
    "axes.edgecolor": "#333333",
    "axes.grid": True,
    "axes.axisbelow": True,
    "grid.color": "#DDDDDD",
    "grid.linewidth": 0.8,
    "xtick.direction": "in",
    "ytick.direction": "in",
    "xtick.top": True,
    "ytick.right": True,
    "figure.facecolor": "white",
    "savefig.facecolor": "white",
    "savefig.bbox": "tight",
})

NPZ_CANDIDATES = (
    "data/artifacts/dataset/battery_sensor_dataset.npz",
    "artifacts/dataset/battery_sensor_dataset.npz",
    "../data/artifacts/dataset/battery_sensor_dataset.npz",
)

PERF_RE = re.compile(
    r"^perf: run=(\d+) job=(\d+) channel=(\S+) width=(\d+) samples=(\d+) "
    r"seq=(\d+) copy=(\d+) sort=(\d+) filter=(\d+) sum=(\d+) merge=(\d+) "
    r"publish=(\d+) total=(\d+)$"
)
TIMING_PAIR_RE = re.compile(r"(\w+)=(\d+)")


def find_npz(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.exists():
            raise SystemExit(f"filter_sweep: no such file: {path}")
        return path
    for candidate in NPZ_CANDIDATES:
        path = Path(candidate)
        if path.exists():
            return path
    raise SystemExit("filter_sweep: cannot find battery_sensor_dataset.npz; "
                     "pass --npz PATH")


def read_manifest(root: Path, name: str | None = None) -> list[dict]:
    """A board-script manifest: blank-line separated key=value blocks.

    `name` picks one when the directory holds more than one sweep — a width
    sweep and a thread sweep copied in side by side, say, each with its own
    manifest. The default is the script's own `sweep.manifest`.
    """
    path = (root / name) if name else (root / "sweep.manifest")
    if not path.exists():
        raise SystemExit(f"filter_sweep: {path} is missing; is this a sweep "
                         f"directory from perf-sweep.sh, and is --manifest right?")
    cases: list[dict] = []
    current: dict = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            if current:
                cases.append(current)
                current = {}
            continue
        key, _, value = line.partition("=")
        current[key.strip()] = value.strip()
    if current:
        cases.append(current)
    return cases


def match_run(npz, raw: np.ndarray) -> tuple[int, int]:
    """Which (recording, channel) of the dataset this raw.bin is, by value."""
    noisy = npz["noisy"]
    for run in range(noisy.shape[0]):
        for channel in range(noisy.shape[2]):
            if np.array_equal(noisy[run, :, channel].view("u4"), raw.view("u4")):
                return run, channel
    raise SystemExit("filter_sweep: a raw.bin does not match any run/channel of "
                     "the dataset; is this sweep from this dataset?")


def style(ax, ylabel: str | None = None, xlabel: str | None = None) -> None:
    ax.set_ylabel(ylabel or "")
    if xlabel:
        ax.set_xlabel(xlabel)


def mask_lengths(mask: np.ndarray) -> tuple[np.ndarray, dict[int, int]]:
    """Length of the burst each masked sample belongs to, and bursts by length.

    Bursts are contiguous True runs in time order; the injected events have a
    known maximum length, but a longer run is bucketed as 7+ rather than
    dropped, so an unexpected one shows up instead of vanishing.
    """
    per_sample = np.zeros(mask.size, dtype=np.int32)
    counts: dict[int, int] = defaultdict(int)
    i = 0
    while i < mask.size:
        if not mask[i]:
            i += 1
            continue
        j = i
        while j < mask.size and mask[j]:
            j += 1
        length = j - i
        per_sample[i:j] = min(length, 7)
        counts[min(length, 7)] += 1
        i = j
    return per_sample, counts


def score_case(npz, case: dict, case_dir: Path) -> dict:
    raw = np.fromfile(case_dir / "raw.bin", dtype="<f4")
    filtered = np.fromfile(case_dir / "filtered.bin", dtype="<f4")
    if raw.size == 0 or raw.size != filtered.size:
        raise SystemExit(f"filter_sweep: {case_dir}: raw.bin and filtered.bin "
                         f"must exist and agree in length")

    run, channel = match_run(npz, raw)
    clean = npz["clean"][run, :, channel].astype(np.float64)
    mask = npz["outlier_mask"][run, :, channel]
    run_id = str(npz["run_ids"][run])
    n = raw.size

    replaced = raw.view("u4") != filtered.view("u4")
    err_raw = np.abs(raw.astype(np.float64) - clean)
    err_flt = np.abs(filtered.astype(np.float64) - clean)
    rmse = lambda e: float(np.sqrt((e * e).mean()))  # noqa: E731

    masked = int(mask.sum())
    unmasked = int(n - masked)
    masked_replaced = int((replaced & mask).sum())
    unmasked_replaced = int((replaced & ~mask).sum())

    per_sample_len, _ = mask_lengths(mask)
    recall_by_burst: dict[int, float | None] = {}
    for length in range(1, 8):
        sel = mask & (per_sample_len == length)
        total = int(sel.sum())
        recall_by_burst[length] = (
            float((replaced & sel).sum()) / total if total else None
        )

    return {
        "case": case["case"],
        "width": int(case.get("width", 0)),
        "threads": int(case.get("threads", 0)),
        "channel": case.get("channel", "?"),
        "rep": int(case.get("rep", 0)),
        "run_id": run_id,
        "npz_channel": channel,
        "samples": n,
        "masked": masked,
        "masked_replaced": masked_replaced,
        "recall": (masked_replaced / masked) if masked else None,
        "unmasked_replaced": unmasked_replaced,
        "clean_replaced_rate": (unmasked_replaced / unmasked) if unmasked else None,
        "rmse_raw": rmse(err_raw),
        "rmse_filtered": rmse(err_flt),
        "gain": (rmse(err_raw) / rmse(err_flt)) if rmse(err_flt) > 0 else None,
        "mae_raw": float(err_raw.mean()),
        "mae_filtered": float(err_flt.mean()),
        "p99_raw": float(np.percentile(err_raw, 99)),
        "p99_filtered": float(np.percentile(err_flt, 99)),
        "max_masked_err_raw": float(err_raw[mask].max()) if masked else 0.0,
        "max_masked_err_filtered": float(err_flt[mask].max()) if masked else 0.0,
        "closer": int((err_flt < err_raw).sum()),
        "recall_by_burst": recall_by_burst,
        "raw": raw,
        "filtered": filtered,
    }


def read_medians(case_dir: Path) -> list[dict] | None:
    path = case_dir / "medians.csv"
    if not path.exists():
        return None
    with path.open() as handle:
        return list(csv.DictReader(handle))


def parse_server_log(path: Path) -> dict[int, list[dict]]:
    """Per-block perf lines, grouped by the server's run number."""
    by_run: dict[int, list[dict]] = defaultdict(list)
    if not path.exists():
        return by_run
    for line in path.read_text(errors="replace").splitlines():
        match = PERF_RE.match(line.strip())
        if not match:
            continue
        (run, job, channel, width, samples, seq, copy, sort, filt, sum_, merge,
         publish, total) = match.groups()
        by_run[int(run)].append({
            "job": int(job), "channel": channel, "width": int(width),
            "samples": int(samples), "seq": int(seq),
            "copy_ns": int(copy), "sort_ns": int(sort), "filter_ns": int(filt),
            "sum_ns": int(sum_), "merge_ns": int(merge),
            "publish_ns": int(publish), "total_ns": int(total),
        })
    return by_run


def parse_client_log(path: Path) -> dict[str, int]:
    """The client's own timing lines, as a flat name -> ns mapping."""
    out: dict[str, int] = {}
    if not path.exists():
        return out
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("timing:"):
            continue
        for name, value in TIMING_PAIR_RE.findall(line):
            out.setdefault(name, int(value))
    return out


def write_csv(path: Path, rows: list[dict], fields: list[str]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("sweep_dir", nargs="?", default=".")
    ap.add_argument("--out", help="where the CSVs and figures go")
    ap.add_argument("--npz", help="the dataset, if it is not in the usual place")
    ap.add_argument("--manifest",
                    help="which manifest to read when the directory holds more "
                         "than one sweep (default: sweep.manifest)")
    args = ap.parse_args(argv[1:])

    root = Path(args.sweep_dir).expanduser()
    out_dir = Path(args.out).expanduser() if args.out else root
    out_dir.mkdir(parents=True, exist_ok=True)

    npz = np.load(find_npz(args.npz))
    cases = read_manifest(root, args.manifest)
    if not cases:
        raise SystemExit("filter_sweep: the manifest lists no cases")

    print(f"sweep      : {root}")
    print(f"cases      : {len(cases)}")

    scored: list[dict] = []
    medians_by_group: dict[tuple, dict[str, tuple]] = defaultdict(dict)
    raw_by_group: dict[tuple, bytes] = {}
    problems: list[str] = []

    for case in cases:
        case_dir = root / case["case"]
        if not (case_dir / "raw.bin").exists():
            problems.append(f"{case['case']}: no raw.bin")
            continue
        result = score_case(npz, case, case_dir)

        # Same input, same answer: group by the recording and channel.
        group = (case.get("payload", "?"), case["channel"])
        raw_bytes = result["raw"].tobytes()
        if group in raw_by_group:
            if raw_by_group[group] != raw_bytes:
                problems.append(f"{case['case']}: raw.bin differs from the other "
                                f"cases of {group[1]}")
        else:
            raw_by_group[group] = raw_bytes

        medians = read_medians(case_dir)
        if medians is not None:
            # These columns are of the raw data, so the sweep must leave them
            # alone. `outliers_removed` is a filter *output* and legitimately
            # depends on the width, so it is deliberately not compared.
            key = tuple((row["block_index"], row["samples"], row["median"])
                        for row in medians)
            if group in medians_by_group and medians_by_group[group] != key:
                problems.append(f"{case['case']}: block medians differ from the "
                                f"other cases of {group[1]} — the medians are of "
                                f"the raw data and must not move with the width")
            else:
                medians_by_group[group] = key

        # Cost: the server's own stage times for this run.
        server_log = root / case.get("server", "")
        run_rows = parse_server_log(server_log).get(int(case.get("run", 0)), [])
        full_samples = max((row["samples"] for row in run_rows), default=0)
        full_rows = [row for row in run_rows if row["samples"] == full_samples]

        result["blocks"] = len(run_rows)
        result["full_blocks"] = len(full_rows)
        for field, key in (("copy", "copy_ns"), ("sort", "sort_ns"),
                           ("filter", "filter_ns"), ("sum", "sum_ns"),
                           ("merge", "merge_ns"), ("publish", "publish_ns"),
                           ("total", "total_ns")):
            result[f"{field}_ms"] = (
                float(np.mean([row[key] for row in full_rows])) / 1e6
                if full_rows else None
            )
        result["client"] = parse_client_log(case_dir / "client.log")

        for row in full_rows:
            scored.append({**result, **row,
                           "run": int(case.get("run", 0)),
                           "server": case.get("server", "")})

    if not scored:
        raise SystemExit("filter_sweep: nothing could be scored; is the sweep "
                         "directory complete?")

    # --- tables --------------------------------------------------------------
    # quality.csv is one row per case: `scored` holds one entry per block, and
    # the quality numbers are a property of the case, not of the block.
    quality_rows = []
    seen_cases = set()
    for r in scored:
        if r["case"] in seen_cases:
            continue
        seen_cases.add(r["case"])
        quality_rows.append({
            "case": r["case"], "width": r["width"], "threads": r["threads"],
            "channel": r["channel"], "rep": r["rep"], "run_id": r["run_id"],
            "samples": r["samples"], "masked": r["masked"],
            "masked_replaced": r["masked_replaced"],
            "recall": r["recall"],
            "unmasked_replaced": r["unmasked_replaced"],
            "clean_replaced_rate": r["clean_replaced_rate"],
            "rmse_raw": r["rmse_raw"], "rmse_filtered": r["rmse_filtered"],
            "gain": r["gain"], "mae_raw": r["mae_raw"],
            "mae_filtered": r["mae_filtered"], "p99_raw": r["p99_raw"],
            "p99_filtered": r["p99_filtered"],
            "max_masked_err_raw": r["max_masked_err_raw"],
            "max_masked_err_filtered": r["max_masked_err_filtered"],
            "closer": r["closer"],
        })
    write_csv(out_dir / "quality.csv", quality_rows, list(quality_rows[0].keys()))

    cost_rows = [{
        "case": r["case"], "server": r["server"], "run": r["run"],
        "job": r["job"], "width": r["width"], "threads": r["threads"],
        "channel": r["channel"], "samples": r["samples"], "seq": r["seq"],
        "copy_ms": r["copy_ns"] / 1e6, "sort_ms": r["sort_ns"] / 1e6,
        "filter_ms": r["filter_ns"] / 1e6, "sum_ms": r["sum_ns"] / 1e6,
        "merge_ms": r["merge_ns"] / 1e6, "publish_ms": r["publish_ns"] / 1e6,
        "total_ms": r["total_ns"] / 1e6,
    } for r in scored]
    write_csv(out_dir / "cost.csv", cost_rows, list(cost_rows[0].keys()))

    # --- what the sweep is for ------------------------------------------------
    widths = sorted({r["width"] for r in scored})
    channels = sorted({r["channel"] for r in scored})
    threads = sorted({r["threads"] for r in scored})

    print(f"widths     : {', '.join(str(w) for w in widths)}")
    print(f"channels   : {', '.join(channels)}")
    print(f"threads    : {', '.join(str(t) for t in threads)}")
    print()
    print(f"{'width':>5} {'threads':>7} {'channel':<12} {'recall':>7} {'clean':>7} "
          f"{'gain':>6} {'filter':>8} {'total':>8}   (ms per block)")
    for width in widths:
        for workers in threads:
            for channel in channels:
                sel = [r for r in scored
                       if r["width"] == width and r["threads"] == workers
                       and r["channel"] == channel
                       and r["rep"] == min(x["rep"] for x in scored
                                           if x["width"] == width
                                           and x["threads"] == workers
                                           and x["channel"] == channel)]
                if not sel:
                    continue
                r = sel[0]
                recall = f"{100.0 * r['recall']:.2f}%" if r["recall"] is not None else "   —"
                clean = (f"{100.0 * r['clean_replaced_rate']:.2f}%"
                         if r["clean_replaced_rate"] is not None else "   —")
                gain = f"{r['gain']:.2f}x" if r["gain"] is not None else "  —"
                filt = f"{r['filter_ms']:.2f}" if r["filter_ms"] is not None else "—"
                total = f"{r['total_ms']:.2f}" if r["total_ms"] is not None else "—"
                print(f"{width:>5} {workers:>7} {channel:<12} {recall:>7} {clean:>7} "
                      f"{gain:>6} {filt:>8} {total:>8}")

    # The point of a thread sweep: the sort is parallel and the filter is not,
    # so this is the whole speedup, and it is much smaller than the sort's own.
    if len(threads) > 1:
        print()
        print("per worker count, mean over the sensor channels "
              "(voltage, current, temperature):")
        baseline = None
        for workers in threads:
            totals = [r["total_ms"] for r in scored if r["threads"] == workers
                      and r["channel"] in SENSOR_CHANNELS and r["total_ms"] is not None]
            sorts = [r["sort_ms"] for r in scored if r["threads"] == workers
                     and r["channel"] in SENSOR_CHANNELS and r["sort_ms"] is not None]
            if not totals:
                continue
            total = float(np.mean(totals))
            sort = float(np.mean(sorts)) if sorts else float("nan")
            if baseline is None:
                baseline = total
            print(f"  threads={workers}: sort {sort:7.3f} ms, whole block {total:7.3f} ms, "
                  f"speedup {baseline / total:.2f}x")

    # --- invariants -----------------------------------------------------------
    print()
    if problems:
        print(f"invariants : {len(problems)} PROBLEM(S)")
        for problem in problems[:10]:
            print(f"  - {problem}")
    else:
        print("invariants : ok (same raw.bin and same block medians for every "
              "case of a recording/channel)")

    # --- figures --------------------------------------------------------------
    # Only the figures a sweep can support are written: the width figures need
    # more than one width, the scaling figure more than one worker count. A
    # single-point "curve" is worse than no figure at all.
    def series(width: int, channel: str, field: str):
        """Mean of `field` over the repeats of (width, channel), or None."""
        values = [r[field] for r in scored
                  if r["width"] == width and r["channel"] == channel
                  and r[field] is not None]
        return float(np.mean(values)) if values else None

    def plotted(values):
        """Percentages, with the missing values kept out of the line."""
        return [100.0 * v if v is not None else float("nan") for v in values]

    def stage_by_width(field: str):
        """(widths, mean ms) of a stage over the sensor channels, per width."""
        xs, means = [], []
        for width in widths:
            values = [r[field] for r in scored
                      if r["width"] == width and r["channel"] in SENSOR_CHANNELS
                      and r[field] is not None]
            if not values:
                continue
            xs.append(width)
            means.append(float(np.mean(values)) / 1e6)
        return xs, means

    def stage_by_workers(field: str):
        """(worker counts, mean ms) of a stage over the sensor channels."""
        xs, means = [], []
        for workers in threads:
            values = [r[field] for r in scored
                      if r["threads"] == workers
                      and r["channel"] in SENSOR_CHANNELS
                      and r[field] is not None]
            if not values:
                continue
            xs.append(workers)
            means.append(float(np.mean(values)) / 1e6)
        return xs, means

    def legend_below(ax, columns: int = 3, y: float = -0.20) -> None:
        """A legend under the axes, so it can never sit on the data."""
        ax.legend(loc="upper center", bbox_to_anchor=(0.5, y), ncol=columns,
                  frameon=False)

    def width_axis(ax) -> None:
        ax.set_xscale("log")
        ax.set_xticks(widths, [str(w) for w in widths])
        ax.minorticks_off()

    # 1. width: what it does to quality
    if len(widths) > 1:
        fig, axes = plt.subplots(3, 1, figsize=(13, 13), dpi=DPI, sharex=True)
        for channel in [c for c in channels if c in SENSOR_CHANNELS]:
            colour = CHANNEL_COLOURS[channel]
            recall = [series(w, channel, "recall") for w in widths]
            clean = [series(w, channel, "clean_replaced_rate") for w in widths]
            gain = [series(w, channel, "gain") for w in widths]
            for ax, values in ((axes[0], recall), (axes[1], clean),
                               (axes[2], gain)):
                if not any(v is not None for v in values):
                    continue
                ys = plotted(values) if ax is not axes[2] else [
                    v if v is not None else float("nan") for v in values]
                ax.plot(widths, ys, "o-", color=colour, linewidth=2.2,
                        markersize=8, label=channel)
        axes[0].set_ylim(0, 102)
        axes[1].set_yscale("log")
        axes[2].set_yscale("log")
        style(axes[0], "Injected outliers replaced (%)")
        style(axes[1], "Clean samples replaced (%, log)")
        style(axes[2], "RMSE gain against truth (x, log)",
              "Filter width (samples, log)")
        for ax in axes:
            width_axis(ax)
        handles, labels = axes[0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="lower center", ncol=3, frameon=False,
                   bbox_to_anchor=(0.5, -0.01))
        fig.suptitle("Filter width and output quality", fontsize=19)
        fig.tight_layout(rect=(0, 0.03, 1, 0.97))
        fig.savefig(out_dir / "width_quality.png", dpi=DPI)
        plt.close(fig)

    # 2. width: what it costs, stage by stage
    if len(widths) > 1:
        fig, ax = plt.subplots(figsize=(12, 7.5), dpi=DPI)
        for field, label, colour, marker in (
                ("sort_ns", "sort", STAGE_COLOURS["sort"], "s"),
                ("filter_ns", "filter", STAGE_COLOURS["filter"], "^"),
                ("total_ns", "whole block", STAGE_COLOURS["total"], "o")):
            xs_s, means = stage_by_width(field)
            if xs_s:
                ax.plot(xs_s, means, marker + "-", color=colour,
                        linewidth=2.2, markersize=9, label=label)
        ax.set_xscale("log")
        ax.set_yscale("log")
        width_axis(ax)
        style(ax, "Time per 64 KiB block (ms, log)",
              "Filter width (samples, log)")
        ax.set_title("Filter width and processing cost")
        legend_below(ax)
        fig.tight_layout()
        fig.savefig(out_dir / "width_cost.png", dpi=DPI)
        plt.close(fig)

    # 3. width: the trade-off, one labelled point per width
    if len(widths) > 1:
        fig, ax = plt.subplots(figsize=(12, 7.5), dpi=DPI)
        xs_c, ys_c, seen = [], [], []
        for width in widths:
            cost = series(width, "voltage", "filter_ms")
            gain = series(width, "voltage", "gain")
            if cost is None or gain is None:
                continue
            xs_c.append(cost)
            ys_c.append(gain)
            seen.append(width)
        ax.plot(xs_c, ys_c, "o-", color="#000000", linewidth=2.2, markersize=9)
        for width, x, y in zip(seen, xs_c, ys_c):
            ax.annotate(str(width), (x, y), textcoords="offset points",
                        xytext=(9, -14), fontsize=13)
        ax.set_xscale("log")
        ax.set_yscale("log")
        style(ax, "RMSE gain against truth (x, log)",
              "Filter cost (ms per 64 KiB block, log)")
        ax.set_title("Voltage: quality against filter cost")
        fig.tight_layout()
        fig.savefig(out_dir / "width_tradeoff.png", dpi=DPI)
        plt.close(fig)

    # 4. two parameters vary here, so a heat map: one cell per (width, burst
    #    length) pair, colour = recall. Nine overlapping lines hid this.
    if len(widths) > 1:
        lengths = sorted({l for r in scored for l in r["recall_by_burst"]
                          if r["recall_by_burst"][l] is not None})
        grid = np.full((len(lengths), len(widths)), np.nan)
        for i, length in enumerate(lengths):
            for j, width in enumerate(widths):
                values = [r["recall_by_burst"][length] for r in scored
                          if r["width"] == width
                          and r["channel"] in SENSOR_CHANNELS
                          and r["recall_by_burst"].get(length) is not None]
                if values:
                    grid[i, j] = 100.0 * float(np.mean(values))
        fig, ax = plt.subplots(figsize=(13, 8), dpi=DPI)
        mesh = ax.pcolormesh(grid, cmap="viridis", vmin=0.0, vmax=100.0,
                             edgecolors="white", linewidth=2.0)
        ax.set_xticks(np.arange(len(widths)) + 0.5, [str(w) for w in widths])
        ax.set_yticks(np.arange(len(lengths)) + 0.5,
                      [str(l) if l < 7 else "7+" for l in lengths])
        ax.grid(False)
        style(ax, "Injected burst length (samples)", "Filter width (samples)")
        ax.set_title("Recall against filter width and burst length")
        bar = fig.colorbar(mesh, ax=ax, pad=0.02)
        bar.set_label("Injected samples replaced (%)")
        fig.tight_layout()
        fig.savefig(out_dir / "width_bursts.png", dpi=DPI)
        plt.close(fig)

    # 5. workers: what the extra threads buy, and what they do not
    if len(threads) > 1:
        fig, axes = plt.subplots(1, 2, figsize=(16, 7), dpi=DPI)
        for field, label, colour, marker in (
                ("sort_ns", "sort", STAGE_COLOURS["sort"], "s"),
                ("filter_ns", "filter", STAGE_COLOURS["filter"], "^"),
                ("total_ns", "whole block", STAGE_COLOURS["total"], "o")):
            xs_w, means = stage_by_workers(field)
            if xs_w:
                axes[0].plot(xs_w, means, marker + "-", color=colour,
                             linewidth=2.2, markersize=9, label=label)
        axes[0].set_xticks(threads, [str(t) for t in threads])
        style(axes[0], "Time per 64 KiB block (ms)", "Worker threads")
        axes[0].set_title("Cost")
        legend_below(axes[0], y=-0.18)

        for field, label, colour, marker in (
                ("sort_ns", "sort", STAGE_COLOURS["sort"], "s"),
                ("total_ns", "whole block", STAGE_COLOURS["total"], "o")):
            xs_w, means = stage_by_workers(field)
            if not means or not means[0]:
                continue
            axes[1].plot(xs_w, [means[0] / m for m in means], marker + "-",
                         color=colour, linewidth=2.2, markersize=9, label=label)
        axes[1].plot(threads, threads, ":", color="#999999", linewidth=1.6,
                     label="ideal")
        axes[1].set_xticks(threads, [str(t) for t in threads])
        style(axes[1], "Speedup against one worker (x)", "Worker threads")
        axes[1].set_title("Speedup")
        legend_below(axes[1], y=-0.18)
        fig.suptitle("Worker count, cost and speedup", fontsize=19)
        fig.tight_layout(rect=(0, 0.02, 1, 0.94))
        fig.savefig(out_dir / "threads_scaling.png", dpi=DPI)
        plt.close(fig)

    # 6. the client's view, when the logs are there
    client_rows = []
    for r in scored:
        client = r.get("client") or {}
        if not client:
            continue
        client_rows.append({
            "case": r["case"], "width": r["width"], "threads": r["threads"],
            "channel": r["channel"], **client,
        })
    if client_rows:
        write_csv(out_dir / "client_timing.csv", client_rows,
                  list(client_rows[0].keys()))
        print(f"client     : timing in {out_dir / 'client_timing.csv'}")

    print()
    for name in ("quality.csv", "cost.csv"):
        print(f"wrote      : {out_dir / name}")
    if len(widths) > 1:
        for name in ("width_quality.png", "width_cost.png",
                     "width_tradeoff.png", "width_bursts.png"):
            print(f"wrote      : {out_dir / name}")
    if len(threads) > 1:
        print(f"wrote      : {out_dir / 'threads_scaling.png'}")

    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
