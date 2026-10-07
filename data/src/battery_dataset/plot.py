"""Publication-style diagnostics for the synthetic battery dataset.

Every figure is a multi-panel, colour-blind-safe SVG-quality chart built with a
constrained layout so titles, labels, colourbars and legends never collide.
Panels are lettered ``(a)``, ``(b)`` ... and every series carries its physical
unit.
"""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import Normalize
from matplotlib.lines import Line2D
from matplotlib.ticker import MaxNLocator

from .config import CHANNELS, DatasetConfig, RunSpec
from .dataset import Dataset
from .noise import median_filter

# --------------------------------------------------------------------------- #
# Style
# --------------------------------------------------------------------------- #

#: Okabe-Ito colour-blind-safe qualitative palette.
PALETTE = {
    "blue": "#0072B2",
    "orange": "#E69F00",
    "green": "#009E73",
    "vermillion": "#D55E00",
    "sky": "#56B4E9",
    "purple": "#CC79A7",
    "yellow": "#F0E442",
    "grey": "#8C8C8C",
}

CHANNEL_COLOR = {
    "current_A": PALETTE["orange"],
    "voltage_V": PALETTE["blue"],
    "temperature_K": PALETTE["green"],
}
CHANNEL_LABEL = {
    "current_A": "Current  [A]",
    "voltage_V": "Terminal voltage  [V]",
    "temperature_K": "Cell temperature  [K]",
}
SENSOR_CHANNELS = tuple(CHANNEL_COLOR)

FIGURE_DPI = 220


def apply_style() -> None:
    """Install the shared rcParams used by every figure."""
    plt.rcParams.update(
        {
            "figure.dpi": 120,
            "savefig.dpi": FIGURE_DPI,
            "savefig.facecolor": "white",
            "font.family": "DejaVu Sans",
            "font.size": 10,
            "axes.titlesize": 10.5,
            "axes.titleweight": "bold",
            "axes.titlelocation": "left",
            "axes.labelsize": 9.5,
            "axes.labelcolor": "#1A1A1A",
            "axes.edgecolor": "#4D4D4D",
            "axes.linewidth": 0.8,
            "axes.grid": True,
            "axes.axisbelow": True,
            "grid.color": "#BFBFBF",
            "grid.alpha": 0.45,
            "grid.linewidth": 0.6,
            "grid.linestyle": "-",
            "axes.spines.top": False,
            "axes.spines.right": False,
            "xtick.labelsize": 9,
            "ytick.labelsize": 9,
            "xtick.color": "#4D4D4D",
            "ytick.color": "#4D4D4D",
            "legend.frameon": True,
            "legend.framealpha": 0.92,
            "legend.edgecolor": "#CCCCCC",
            "legend.fontsize": 8.5,
            "legend.title_fontsize": 9,
            "lines.linewidth": 1.4,
            "lines.solid_capstyle": "round",
        }
    )


def _panel(ax, letter: str, title: str) -> None:
    ax.set_title(f"({letter})  {title}", loc="left", pad=8)


def _save(fig, path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=FIGURE_DPI, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    return path


# --------------------------------------------------------------------------- #
# Selection helpers
# --------------------------------------------------------------------------- #


def _representatives(runs: list[RunSpec]) -> list[int]:
    """Indices of three comparable runs spanning the C-rate axis."""
    base = runs[0].parameter_set
    profile = runs[0].profile
    ambient = runs[len(runs) // 4].ambient_c
    picks = [
        i
        for i, r in enumerate(runs)
        if r.parameter_set == base and r.profile == profile and r.ambient_c == ambient
    ]
    return picks[:3] if picks else list(range(min(3, len(runs))))


def _densest_window(mask_1d: np.ndarray, half: int) -> int:
    """Centre index of the densest outlier cluster (for zoom panels)."""
    kernel = np.ones(2 * half + 1)
    density = np.convolve(mask_1d.astype(float), kernel, mode="same")
    return int(np.argmax(density)) if density.size else mask_1d.size // 2


def _zoom_slice(center: int, half: int, n_samples: int) -> slice:
    lo = max(0, min(center - half, n_samples - 2 * half - 1))
    return slice(lo, lo + 2 * half + 1)


def _mask_run_lengths(mask_2d: np.ndarray) -> np.ndarray:
    """Lengths of contiguous ``True`` runs per row of a ``(n_runs, n_samples)`` mask."""
    lengths: list[int] = []
    for row in mask_2d:
        edges = np.diff(np.concatenate(([False], row, [False])).astype(np.int8))
        starts = np.flatnonzero(edges == 1)
        stops = np.flatnonzero(edges == -1)
        lengths.extend((stops - starts).tolist())
    return np.asarray(lengths, dtype=np.int64)


def _fmt_bytes(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024.0
    return f"{n:.1f} GiB"


def _header(ds: Dataset, config: DatasetConfig) -> str:
    n_runs, n_samples, n_channels = ds.shape
    return (
        f"Synthetic battery sensor dataset  \u2014  {n_runs} runs \u00d7 {n_samples:,} samples "
        f"\u00d7 {n_channels} channels  \u2022  payload {_fmt_bytes(ds.n_bytes)} "
        f"\u2022  {ds.block_count:,} \u00d7 64 KiB blocks  \u2022  noise seed {config.noise.seed}"
    )


# --------------------------------------------------------------------------- #
# Figure 1 - signal overview
# --------------------------------------------------------------------------- #


def fig_signal_overview(ds: Dataset, runs: list[RunSpec], config: DatasetConfig, path: Path) -> Path:
    picks = _representatives(runs)
    c_rates = [runs[i].c_rate for i in picks]
    cmap = plt.get_cmap("viridis")
    norm = Normalize(vmin=min(c_rates), vmax=max(c_rates))

    fig, axes = plt.subplots(2, 2, figsize=(12.4, 7.4), layout="constrained")
    panels = [
        ("current_A", 1, "Applied current \u2014 adaptive DAE solution"),
        ("voltage_V", 2, "Terminal voltage \u2014 adaptive DAE solution"),
        ("temperature_K", 3, "Cell temperature \u2014 lumped thermal model"),
    ]
    for row, (ax, (channel, column, title)) in enumerate(zip(axes.ravel()[:3], panels)):
        for idx in picks:
            t = ds.clean[idx, :, 0] / 3600.0
            ax.plot(t, ds.clean[idx, :, column], color=cmap(norm(runs[idx].c_rate)), lw=1.3)
        ax.set_xlabel("Time  [h]")
        ax.set_ylabel(CHANNEL_LABEL[channel])
        _panel(ax, "abc"[row], title)
        ax.margins(x=0.01)

    sm = plt.cm.ScalarMappable(cmap=cmap, norm=norm)
    bar = fig.colorbar(sm, ax=axes.ravel()[:3].tolist(), location="right", shrink=0.85, pad=0.015)
    bar.set_label("C-rate  [1/h]")

    ax = axes[1, 1]
    profiles = sorted({r.profile for r in runs})
    c_grid = sorted({r.c_rate for r in runs})
    widths = 0.8 / max(len(profiles), 1)
    x = np.arange(len(c_grid))
    colors = [PALETTE["blue"], PALETTE["vermillion"]]
    for k, profile in enumerate(profiles):
        counts = [sum(1 for r in runs if r.profile == profile and r.c_rate == c) for c in c_grid]
        bars = ax.bar(x + k * widths - 0.4 + widths / 2, counts, widths * 0.92,
                      label=profile.replace("_", " "), color=colors[k % len(colors)], edgecolor="white", lw=0.6)
        ax.bar_label(bars, padding=2, fontsize=8, color="#4D4D4D")
    ax.set_xticks(x, [f"{c:g}C" for c in c_grid])
    ax.set_xlabel("C-rate")
    ax.set_ylabel("Simulated runs  [count]")
    ax.set_ylim(0, max(1.2, max(sum(1 for r in runs if r.profile == p and r.c_rate == c)
                                 for p in profiles for c in c_grid) * 1.55))
    ax.legend(title="Experiment profile", loc="upper left")
    _panel(ax, "d", "Sweep coverage of the generated run set")

    fig.suptitle(_header(ds, config), fontsize=11, fontweight="bold")
    return _save(fig, path)


# --------------------------------------------------------------------------- #
# Figure 2 - noise and outlier injection
# --------------------------------------------------------------------------- #


def fig_noise_injection(ds: Dataset, runs: list[RunSpec], config: DatasetConfig, path: Path) -> Path:
    idx = _representatives(runs)[0]
    mask = ds.outlier_mask[idx]
    center = int(np.argmax(mask.sum(axis=1)))
    half = 400
    zoom = _zoom_slice(center, half, ds.shape[1])

    fig, axes = plt.subplots(3, 2, figsize=(13.0, 8.0), layout="constrained")
    for row, channel in enumerate(SENSOR_CHANNELS):
        column = CHANNELS.index(channel)
        t = ds.clean[idx, :, 0]
        out = mask[:, column]

        for col, sl in enumerate((slice(None), zoom)):
            ax = axes[row, col]
            ax.plot(t[sl], ds.clean[idx, sl, column], color="#1A1A1A", lw=1.3, label="ground truth", zorder=3)
            ax.plot(t[sl], ds.noisy[idx, sl, column], color=PALETTE["grey"], lw=0.7, alpha=0.95,
                    label="noisy sensor", zorder=2)
            m_idx = np.flatnonzero(out[sl])
            if m_idx.size:
                ax.scatter(t[sl][m_idx], ds.noisy[idx, sl, column][m_idx], s=16, marker="D",
                           color=PALETTE["vermillion"], edgecolor="white", linewidth=0.4, zorder=4,
                           label="injected outlier")
                ax.set_facecolor("#FFF7F0" if col == 1 else "#FBFBFB")
            ax.set_ylabel(CHANNEL_LABEL[channel])
            ax.margins(x=0.01)
            if row == 2:
                ax.set_xlabel("Time  [s]")
            if col == 0:
                _panel(ax, "abc"[row], f"Full run \u2014 {ds.shape[1]:,} samples")
            else:
                _panel(ax, "def"[row], f"Zoom \u2014 {int(out[sl].sum())} outliers in window")

    handles = [
        Line2D([], [], color="#1A1A1A", lw=1.4, label="Ground truth (noise-free)"),
        Line2D([], [], color=PALETTE["grey"], lw=1.0, label="Sensor reading (AWGN + impulses)"),
        Line2D([], [], color=PALETTE["vermillion"], marker="D", ls="none", markersize=5,
               markeredgecolor="white", label="Injected outlier (ground-truth mask)"),
    ]
    fig.legend(handles=handles, loc="outside lower center", ncols=3, frameon=False, fontsize=9.5)
    fig.suptitle(
        f"{runs[idx].run_id}  \u2014  {config.noise.outlier_fraction:.2%} outlier rate per channel",
        fontsize=11, fontweight="bold",
    )
    return _save(fig, path)


# --------------------------------------------------------------------------- #
# Figure 3 - median filter recovery
# --------------------------------------------------------------------------- #


def fig_median_filter(ds: Dataset, runs: list[RunSpec], config: DatasetConfig, path: Path,
                      kernel: int = 9) -> Path:
    idx = _representatives(runs)[0]
    mask = ds.outlier_mask[idx]
    center = int(np.argmax(mask.sum(axis=1)))
    zoom = _zoom_slice(center, 350, ds.shape[1])

    fig, axes = plt.subplots(2, 2, figsize=(12.8, 7.6), layout="constrained")

    for row, channel in enumerate(SENSOR_CHANNELS):
        column = CHANNELS.index(channel)
        t = ds.clean[idx, zoom, 0]
        clean = ds.clean[idx, :, column].astype(np.float64)
        noisy = ds.noisy[idx, :, column].astype(np.float64)
        filtered = median_filter(noisy, kernel)
        ax = axes.ravel()[row]
        ax.plot(t, noisy[zoom], color=PALETTE["grey"], lw=0.8, alpha=0.9, label="noisy")
        ax.plot(t, clean[zoom], color="#1A1A1A", lw=1.3, label="ground truth")
        ax.plot(t, filtered[zoom], color=PALETTE["vermillion"], lw=1.6, label=f"median (k={kernel})")
        ax.set_xlabel("Time  [s]")
        ax.set_ylabel(CHANNEL_LABEL[channel])
        _panel(ax, "abc"[row], "Outlier rejection \u2014 zoom")
        ax.margins(x=0.01)

    # (d) recovery error against kernel width
    ax = axes[1, 1]
    kernels = [1, 3, 5, 7, 9, 13, 17, 25, 33, 49]
    for channel in SENSOR_CHANNELS:
        column = CHANNELS.index(channel)
        clean = ds.clean[idx, :, column].astype(np.float64)
        noisy = ds.noisy[idx, :, column].astype(np.float64)
        rmse = [
            float(np.sqrt(np.mean((median_filter(noisy, k) - clean) ** 2))) for k in kernels
        ]
        ax.plot(kernels, rmse, marker="o", ms=4, color=CHANNEL_COLOR[channel], label=channel.split("_")[0])
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Median kernel width  [samples]")
    ax.set_ylabel("RMSE vs ground truth")
    ax.set_xticks(kernels, [str(k) for k in kernels])
    ax.tick_params(axis="x", labelsize=8)
    ax.legend(title="Channel", loc="lower right")
    _panel(ax, "d", "Reconstruction error vs kernel width")

    axes.ravel()[0].legend(loc="best", fontsize=8)
    fig.suptitle(
        "Rolling-median reconstruction of impulsive sensor noise  \u2014  "
        f"{runs[idx].run_id}",
        fontsize=11, fontweight="bold",
    )
    return _save(fig, path)


# --------------------------------------------------------------------------- #
# Figure 4 - noise statistics
# --------------------------------------------------------------------------- #


def fig_noise_statistics(ds: Dataset, runs: list[RunSpec], config: DatasetConfig, path: Path) -> Path:
    sigmas = config.noise.sigma_by_channel
    fig, axes = plt.subplots(2, 2, figsize=(12.6, 7.4), layout="constrained")

    # (a) residual distribution vs fitted Gaussian (Gaussian-only cells, so the
    #     impulsive component does not fatten the tails)
    ax = axes[0, 0]
    for channel in SENSOR_CHANNELS:
        column = CHANNELS.index(channel)
        delta = ds.noisy[:, :, column].astype(np.float64) - ds.clean[:, :, column].astype(np.float64)
        residual = (delta[~ds.outlier_mask[:, :, column]] / sigmas[channel]).ravel()
        ax.hist(residual, bins=np.linspace(-5, 5, 120), density=True, histtype="step",
                color=CHANNEL_COLOR[channel], lw=1.5, label=channel.split("_")[0])
    grid = np.linspace(-5, 5, 400)
    ax.plot(grid, np.exp(-0.5 * grid**2) / np.sqrt(2 * np.pi), color="#1A1A1A", ls="--", lw=1.4,
            label="unit Gaussian")
    ax.set_yscale("log")
    ax.set_xlabel("Residual / \u03c3  [dimensionless]")
    ax.set_ylabel("Probability density")
    ax.legend(loc="upper right", title="Normalised residual")
    _panel(ax, "a", "Noise-only residuals collapse onto N(0, \u03c3\u00b2)")

    # (b) injected outlier magnitude
    ax = axes[0, 1]
    magnitudes = {}
    for channel in SENSOR_CHANNELS:
        column = CHANNELS.index(channel)
        delta = np.abs(ds.noisy[:, :, column].astype(np.float64) - ds.clean[:, :, column].astype(np.float64))
        sel = ds.outlier_mask[:, :, column]
        magnitudes[channel] = delta[sel] / sigmas[channel]
    pooled = np.concatenate([m for m in magnitudes.values() if m.size]) if any(
        m.size for m in magnitudes.values()) else np.array([1.0])
    bins = np.logspace(np.log10(max(pooled.min(), 1e-1)), np.log10(max(pooled.max(), 1.0)), 70)
    for channel in SENSOR_CHANNELS:
        if magnitudes[channel].size:
            ax.hist(magnitudes[channel], bins=bins, histtype="step", lw=1.5,
                    color=CHANNEL_COLOR[channel], label=channel.split("_")[0])
    ax.set_xscale("log")
    ax.set_xlabel("Outlier magnitude / \u03c3  [dimensionless, log scale]")
    ax.set_ylabel("Corrupted samples  [count]")
    ax.legend(loc="upper right")
    _panel(ax, "b", "Impulsive outliers sit well outside the noise band")

    # (c) configured vs realised outlier rate per channel
    ax = axes[1, 0]
    x = np.arange(len(SENSOR_CHANNELS))
    realised = [ds.outlier_mask[:, :, CHANNELS.index(c)].mean() for c in SENSOR_CHANNELS]
    bars = ax.bar(x, realised, width=0.52, color=[CHANNEL_COLOR[c] for c in SENSOR_CHANNELS],
                  edgecolor="white", lw=0.8)
    ax.bar_label(bars, labels=[f"{v:.3%}" for v in realised], padding=3, fontsize=8.5)
    ax.axhline(config.noise.outlier_fraction, color="#1A1A1A", ls="--", lw=1.2,
               label=f"target {config.noise.outlier_fraction:.2%}")
    ax.set_xticks(x, [c.split("_")[0] for c in SENSOR_CHANNELS])
    ax.set_xlabel("Sensor channel")
    ax.set_ylabel("Outlier rate  [fraction of samples]")
    ax.set_ylim(0, max(realised) * 1.35 if max(realised) else 1.0)
    ax.legend(loc="upper right")
    _panel(ax, "c", "Realised corruption rate matches configuration")

    # (d) power spectral density of the voltage channel
    ax = axes[1, 1]
    idx = _representatives(runs)[0]
    column = CHANNELS.index("voltage_V")
    fs = 1.0 / float(np.median(np.diff(ds.clean[idx, :, 0])))  # nominal sample rate [Hz]
    for name, series, color, lw in (
        ("clean", ds.clean[idx, :, column], "#1A1A1A", 1.3),
        ("noisy", ds.noisy[idx, :, column], PALETTE["vermillion"], 1.0),
    ):
        values = series.astype(np.float64)
        values = values - values.mean()
        window = np.hanning(values.size)
        spectrum = np.abs(np.fft.rfft(values * window)) ** 2
        freqs = np.fft.rfftfreq(values.size, d=1.0 / fs)
        psd = spectrum / (fs * np.sum(window**2))
        ax.loglog(freqs[1:], psd[1:], color=color, lw=lw, label=f"{name} voltage")
    ax.set_xlabel("Frequency  [Hz]")
    ax.set_ylabel("PSD  [V$^2$/Hz]")
    ax.legend(loc="lower left")
    _panel(ax, "d", "AWGN raises the broadband noise floor")

    fig.suptitle(
        "Sensor-noise characterisation  \u2014  "
        f"\u03c3 = ({config.noise.voltage_sigma_v * 1e3:g} mV, {config.noise.current_sigma_a * 1e3:g} mA, "
        f"{config.noise.temperature_sigma_k:g} K)",
        fontsize=11, fontweight="bold",
    )
    return _save(fig, path)


# --------------------------------------------------------------------------- #
# Figure 5 - run gallery
# --------------------------------------------------------------------------- #


def fig_run_gallery(ds: Dataset, runs: list[RunSpec], config: DatasetConfig, path: Path) -> Path:
    fig, axes = plt.subplots(2, 2, figsize=(12.8, 7.6), layout="constrained")
    n_runs, n_samples, _ = ds.shape

    # (a) voltage family across every generated run
    ax = axes[0, 0]
    c_rates = np.array([r.c_rate for r in runs])
    cmap = plt.get_cmap("viridis")
    norm = Normalize(vmin=c_rates.min(), vmax=c_rates.max())
    for i in range(n_runs):
        ax.plot(ds.clean[i, :, 0] / 3600.0, ds.clean[i, :, 2], color=cmap(norm(c_rates[i])), lw=0.8, alpha=0.75)
    ax.set_xlabel("Time  [h]")
    ax.set_ylabel(CHANNEL_LABEL["voltage_V"])
    ax.margins(x=0.01)
    bar = fig.colorbar(plt.cm.ScalarMappable(cmap=cmap, norm=norm), ax=ax, pad=0.015)
    bar.set_label("C-rate  [1/h]")
    _panel(ax, "a", f"Voltage traces \u2014 all {n_runs} runs")

    # (b) temperature rise against C-rate
    ax = axes[0, 1]
    marker_pool = ("o", "s", "^", "D", "v", "P")
    markers = {
        ps: marker_pool[i % len(marker_pool)]
        for i, ps in enumerate(sorted({r.parameter_set for r in runs}))
    }
    ambients = sorted({r.ambient_c for r in runs})
    amb_cmap = plt.get_cmap("coolwarm")
    amb_norm = Normalize(vmin=min(ambients), vmax=max(ambients))
    for i, r in enumerate(runs):
        rise = float(ds.clean[i, :, 3].max() - ds.clean[i, :, 3].min())
        ax.scatter(r.c_rate, rise, s=58, marker=markers.get(r.parameter_set, "o"), alpha=0.82,
                   color=amb_cmap(amb_norm(r.ambient_c)), edgecolor="#333333", linewidth=0.6, zorder=3)
    ax.set_xticks(sorted({r.c_rate for r in runs}), [f"{c:g}" for c in sorted({r.c_rate for r in runs})])
    ax.set_xlabel("C-rate  [1/h]")
    ax.set_ylabel("Temperature rise  \u0394T  [K]")
    bar = fig.colorbar(plt.cm.ScalarMappable(cmap=amb_cmap, norm=amb_norm), ax=ax, pad=0.015)
    bar.set_label("Ambient  [\u00b0C]")
    handles = [Line2D([], [], marker=m, ls="none", color="#666666", markersize=6, label=k)
               for k, m in markers.items()]
    ax.legend(handles=handles, title="Parameter set", loc="upper left")
    _panel(ax, "b", "Thermal response vs electrical load")

    # (c) run duration, ordered
    ax = axes[1, 0]
    order = np.argsort(ds.duration_s / 3600.0)
    hours = ds.duration_s[order] / 3600.0
    colors = [PALETTE["blue"] if runs[i].profile == "discharge" else PALETTE["vermillion"] for i in order]
    ax.bar(np.arange(hours.size), hours, color=colors, width=0.85, edgecolor="white", lw=0.4)
    ax.set_xlabel("Run index  [sorted by duration]")
    ax.set_ylabel("Recording duration  [h]")
    ax.xaxis.set_major_locator(MaxNLocator(integer=True, nbins=10))
    handles = [
        Line2D([], [], color=PALETTE["blue"], lw=6, label="discharge"),
        Line2D([], [], color=PALETTE["vermillion"], lw=6, label="cccv"),
    ]
    ax.legend(handles=handles, title="Profile", loc="upper left")
    _panel(ax, "c", f"Duration spread (median {np.median(hours):.1f} h)")

    # (d) corruption event length distribution, recovered from the mask
    ax = axes[1, 1]
    lengths = {c: _mask_run_lengths(ds.outlier_mask[:, :, CHANNELS.index(c)]) for c in SENSOR_CHANNELS}
    longest = int(min(max(int(v.max()) for v in lengths.values()), 12))
    x = np.arange(1, longest + 1)
    width = 0.26
    for k, channel in enumerate(SENSOR_CHANNELS):
        counts = [int((lengths[channel] == n).sum()) for n in x]
        ax.bar(x + (k - 1) * width, counts, width * 0.9, color=CHANNEL_COLOR[channel],
               edgecolor="white", lw=0.5, label=channel.split("_")[0])
    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xlabel("Contiguous corruption length  [samples]")
    ax.set_ylabel("Events  [count, log scale]")
    mean_len = float(np.concatenate(list(lengths.values())).mean())
    ax.legend(title=f"Channel (mean {mean_len:.2f} samples)", loc="upper right")
    _panel(ax, "d", "Spikes dominate; bursts and dropouts form the tail")

    fig.suptitle(_header(ds, config), fontsize=11, fontweight="bold")
    return _save(fig, path)


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #


def render_all(ds: Dataset, runs: list[RunSpec], config: DatasetConfig) -> list[Path]:
    """Render every diagnostic figure and return the written paths."""
    apply_style()
    out = config.figure_dir()
    return [
        _save_path
        for _save_path in (
            fig_signal_overview(ds, runs, config, out / "01_signal_overview.png"),
            fig_noise_injection(ds, runs, config, out / "02_noise_injection.png"),
            fig_median_filter(ds, runs, config, out / "03_median_filter_recovery.png"),
            fig_noise_statistics(ds, runs, config, out / "04_noise_statistics.png"),
            fig_run_gallery(ds, runs, config, out / "05_run_gallery.png"),
        )
    ]
