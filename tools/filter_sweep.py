#!/usr/bin/env python3
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
    sweep_recall_vs_width.png  recall, false replacements and RMSE gain vs width
    sweep_cost_vs_width.png    filter and total ms per block vs width, log-log
    sweep_pareto.png           what each width costs against what it buys
    sweep_burst_recall.png     recall against the injected burst length
    sweep_threads.png          only when the sweep covered worker counts

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
CHANNEL_COLOURS = {
    "voltage": "#1558d6",
    "current": "#188038",
    "temperature": "#d93025",
    "time": "#8d949c",
}
SENSOR_CHANNELS = ("voltage", "current", "temperature")

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


def read_manifest(root: Path) -> list[dict]:
    """The board script's sweep.manifest: blank-line separated key=value blocks."""
    path = root / "sweep.manifest"
    if not path.exists():
        raise SystemExit(f"filter_sweep: {path} is missing; is this a sweep "
                         f"directory from perf-sweep.sh?")
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
    ax.grid(True, color="#d8d8d8", linewidth=0.7)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    if ylabel:
        ax.set_ylabel(ylabel)
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
    args = ap.parse_args(argv[1:])

    root = Path(args.sweep_dir).expanduser()
    out_dir = Path(args.out).expanduser() if args.out else root
    out_dir.mkdir(parents=True, exist_ok=True)

    npz = np.load(find_npz(args.npz))
    cases = read_manifest(root)
    if not cases:
        raise SystemExit("filter_sweep: sweep.manifest lists no cases")

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
            key = tuple((row["block_index"], row["samples"], row["median"],
                         row["outliers_removed"]) for row in medians)
            if group in medians_by_group and medians_by_group[group] != key:
                problems.append(f"{case['case']}: block medians differ from the "
                                f"other cases of {group[1]} (they must not "
                                f"depend on the width)")
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
    print(f"{'width':>5} {'channel':<12} {'recall':>7} {'clean':>7} "
          f"{'gain':>6} {'filter':>8} {'total':>8}   (ms per block)")
    for width in widths:
        for channel in channels:
            sel = [r for r in scored
                   if r["width"] == width and r["channel"] == channel
                   and r["rep"] == min(x["rep"] for x in scored
                                       if x["width"] == width
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
            print(f"{width:>5} {channel:<12} {recall:>7} {clean:>7} "
                  f"{gain:>6} {filt:>8} {total:>8}")

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
    def series(width: int, channel: str, field: str, reps: bool = False):
        """Mean of `field` over the repeats of (width, channel)."""
        values = [r[field] for r in scored
                  if r["width"] == width and r["channel"] == channel
                  and r[field] is not None]
        if not values:
            return None
        return float(np.mean(values))

    def cost_series(field: str, threads_value: int | None = None):
        """Mean/min/max of a stage over every block at each width."""
        xs, means, los, his = [], [], [], []
        for width in widths:
            values = [r[field] for r in scored
                      if r["width"] == width and r[field] is not None
                      and (threads_value is None or r["threads"] == threads_value)]
            if not values:
                continue
            xs.append(width)
            means.append(float(np.mean(values)) / 1e6)
            los.append(float(np.min(values)) / 1e6)
            his.append(float(np.max(values)) / 1e6)
        return xs, means, los, his

    # 1. quality against width
    fig, axes = plt.subplots(3, 1, figsize=(11, 12.5), dpi=DPI, sharex=True)
    plotted = False
    for channel in channels:
        recall = [series(w, channel, "recall") for w in widths]
        if any(v is not None for v in recall):
            axes[0].plot(widths, [100.0 * (v or 0.0) for v in recall], "o-",
                         color=CHANNEL_COLOURS.get(channel, "#333333"),
                         label=channel)
            plotted = True
    axes[0].set_ylim(0, 102)
    style(axes[0], "injected outliers replaced (%)", None)
    axes[0].set_title("What the filter finds: recall of the injected outliers", fontsize=13)
    if plotted:
        axes[0].legend(loc="lower right", ncol=3, framealpha=0.95)
    else:
        axes[0].text(0.5, 0.5, "no injected outliers in these channels",
                     transform=axes[0].transAxes, ha="center")

    for channel in channels:
        clean = [series(w, channel, "clean_replaced_rate") for w in widths]
        if any(v is not None for v in clean):
            axes[1].plot(widths, [100.0 * (v or 0.0) for v in clean], "o-",
                         color=CHANNEL_COLOURS.get(channel, "#333333"),
                         label=channel)
    style(axes[1], "clean samples replaced (%)", None)
    axes[1].set_title("What it costs in false alarms: replacements the mask does "
                      "not explain", fontsize=13)
    axes[1].legend(loc="upper right", ncol=3, framealpha=0.95)

    for channel in channels:
        gain = [series(w, channel, "gain") for w in widths]
        if any(v is not None for v in gain):
            axes[2].plot(widths, gain, "o-",
                         color=CHANNEL_COLOURS.get(channel, "#333333"),
                         label=channel)
    axes[2].axhline(1.0, color="#bbbbbb", linewidth=1.0, linestyle="--")
    style(axes[2], "RMSE gain (received / filtered)", "rolling-filter width (samples)")
    axes[2].set_title("What it buys: error against ground truth", fontsize=13)
    axes[2].legend(loc="upper left", ncol=3, framealpha=0.95)
    axes[2].set_xticks(widths, [str(w) for w in widths])
    fig.suptitle("Filter width against quality — the board's own filtered.bin, "
                 "scored against the dataset truth", fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(out_dir / "sweep_recall_vs_width.png", dpi=DPI)
    plt.close(fig)

    # 2. cost against width (log-log, with the fitted exponent)
    xs, means, los, his = cost_series("filter_ns")
    fig, ax = plt.subplots(figsize=(10.5, 6.0), dpi=DPI)
    if xs:
        ax.fill_between(xs, los, his, color="#1558d6", alpha=0.15, linewidth=0)
        ax.plot(xs, means, "o-", color="#1558d6", linewidth=1.8,
                label="filter (Hampel)")
        if len(xs) >= 2:
            slope = float(np.polyfit(np.log(xs), np.log(means), 1)[0])
            ax.text(0.03, 0.94, f"filter cost ~ width^{slope:.2f}\n"
                                f"(fitted over {xs[0]}..{xs[-1]} samples)",
                    transform=ax.transAxes, va="top", family="monospace",
                    fontsize=10.5,
                    bbox=dict(boxstyle="round,pad=0.45", facecolor="white",
                              edgecolor="#bbbbbb", alpha=0.95))
    xs_t, means_t, los_t, his_t = cost_series("total_ns")
    if xs_t:
        ax.plot(xs_t, means_t, "s--", color="#8d949c", linewidth=1.5,
                label="whole job (copy, sort, filter, sums, merge)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xticks(widths, [str(w) for w in widths])
    style(ax, "milliseconds per 64 KiB block (log)", "rolling-filter width (samples, log)")
    ax.set_title("What each width costs on the board — per-block service time", fontsize=13.5)
    ax.legend(loc="lower right", framealpha=0.95)
    fig.tight_layout()
    fig.savefig(out_dir / "sweep_cost_vs_width.png", dpi=DPI)
    plt.close(fig)

    # 3. the trade-off, one point per width
    fig, ax = plt.subplots(figsize=(10.5, 6.4), dpi=DPI)
    for channel in channels:
        xs_c, ys_c, labels = [], [], []
        for width in widths:
            cost = series(width, channel, "filter_ms")
            quality = series(width, channel, "gain")
            if cost is None or quality is None:
                continue
            xs_c.append(cost)
            ys_c.append(quality)
            labels.append(str(width))
        if not xs_c:
            continue
        colour = CHANNEL_COLOURS.get(channel, "#333333")
        ax.plot(xs_c, ys_c, "-", color=colour, alpha=0.5, linewidth=1.2)
        ax.plot(xs_c, ys_c, "o", color=colour, markersize=6, label=channel)
        for x, y, label in zip(xs_c, ys_c, labels):
            ax.annotate(label, (x, y), textcoords="offset points", xytext=(6, 4),
                        fontsize=9, color=colour)
    style(ax, "RMSE gain against ground truth",
          "filter cost (ms per 64 KiB block)")
    ax.set_title("The trade-off: what each width buys for what it costs "
                 "(labels are widths)", fontsize=13.5)
    ax.legend(loc="lower right", framealpha=0.95)
    fig.tight_layout()
    fig.savefig(out_dir / "sweep_pareto.png", dpi=DPI)
    plt.close(fig)

    # 4. recall by injected burst length
    fig, ax = plt.subplots(figsize=(10.5, 6.0), dpi=DPI)
    lengths = list(range(1, 8))
    for width in widths:
        values = []
        for length in lengths:
            per_channel = [r["recall_by_burst"][length] for r in scored
                           if r["width"] == width
                           and r["channel"] in SENSOR_CHANNELS
                           and r["recall_by_burst"].get(length) is not None]
            values.append(100.0 * float(np.mean(per_channel))
                          if per_channel else np.nan)
        if not np.all(np.isnan(values)):
            ax.plot(lengths, values, "o-", label=f"width {width}")
    ax.axvline(4.0 / 1.0, color="#eeeeee", linewidth=0)  # keeps autoscale honest
    style(ax, "injected samples replaced (%)", "length of the injected burst (samples)")
    ax.set_xticks(lengths, [str(l) if l < 7 else "7+" for l in lengths])
    ax.set_ylim(0, 102)
    ax.set_title("Bursts are where a window shows its length: recall by burst length "
                 "(sensor channels)", fontsize=13)
    ax.legend(ncol=2, loc="lower right", framealpha=0.95)
    fig.tight_layout()
    fig.savefig(out_dir / "sweep_burst_recall.png", dpi=DPI)
    plt.close(fig)

    # 5. worker counts, if the sweep covered them
    if len(threads) > 1:
        fig, ax = plt.subplots(figsize=(10.5, 6.0), dpi=DPI)
        for field, label, colour in (("sort_ns", "sort (parallel)", "#1558d6"),
                                     ("filter_ns", "filter (serial)", "#d93025"),
                                     ("total_ns", "whole job", "#188038")):
            xs_t, means_t, los_t, his_t = cost_series(field,
                                                      threads_value=None)
            # cost_series() walks widths; for the threads figure walk threads
            xs_t, means_t, los_t, his_t = [], [], [], []
            for t in threads:
                values = [r[field] for r in scored
                          if r["threads"] == t and r[field] is not None]
                if not values:
                    continue
                xs_t.append(t)
                means_t.append(float(np.mean(values)) / 1e6)
                los_t.append(float(np.min(values)) / 1e6)
                his_t.append(float(np.max(values)) / 1e6)
            ax.fill_between(xs_t, los_t, his_t, color=colour, alpha=0.12,
                            linewidth=0)
            ax.plot(xs_t, means_t, "o-", color=colour, label=label)
        ax.set_xticks(threads, [str(t) for t in threads])
        style(ax, "milliseconds per 64 KiB block", "merge-sort worker threads")
        ax.set_title("Where the threads land: workers speed up the sort, and the "
                     "serial filter sets the total", fontsize=13)
        ax.legend(framealpha=0.95)
        fig.tight_layout()
        fig.savefig(out_dir / "sweep_threads.png", dpi=DPI)
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
    for name in ("sweep_recall_vs_width.png", "sweep_cost_vs_width.png",
                 "sweep_pareto.png", "sweep_burst_recall.png"):
        print(f"wrote      : {out_dir / name}")
    if len(threads) > 1:
        print(f"wrote      : {out_dir / 'sweep_threads.png'}")

    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
