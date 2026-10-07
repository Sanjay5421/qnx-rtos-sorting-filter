"""Sensor noise / impulsive-outlier injection and reference median filtering.

``inject_noise`` corrupts a clean ``(n_samples, n_channels)`` matrix; the
returned boolean mask marks every corrupted *sample/channel* cell, which gives
the downstream service ground truth to score its filter against.

``median_filter`` is a NumPy reference implementation used only for quality
control plots -- the production filter lives in the service itself.
"""

from __future__ import annotations

import numpy as np

from .config import CHANNELS, SENSOR_CHANNELS, NoiseConfig

_SENSOR_INDEX: dict[str, int] = {name: CHANNELS.index(name) for name in SENSOR_CHANNELS}


def sigma_vector(noise: NoiseConfig) -> np.ndarray:
    """Per-channel Gaussian sigma aligned with :data:`CHANNELS` (time -> 0)."""
    return np.array(
        [0.0] + [noise.sigma_by_channel[name] for name in SENSOR_CHANNELS],
        dtype=np.float64,
    )


def inject_noise(
    clean: np.ndarray,
    noise: NoiseConfig,
    rng: np.random.Generator,
) -> tuple[np.ndarray, np.ndarray]:
    """Corrupt ``clean`` and return ``(noisy, outlier_mask)``.

    ``clean`` is ``(n_samples, n_channels)`` in :data:`CHANNELS` order. The time
    channel is left untouched. The mask is ``True`` exactly where an impulsive
    outlier was written, so Gaussian-only cells stay ``False``.
    """
    clean = np.asarray(clean, dtype=np.float64)
    if clean.ndim != 2 or clean.shape[1] != len(CHANNELS):
        raise ValueError(f"expected (n_samples, {len(CHANNELS)}), got {clean.shape}")

    n_samples = clean.shape[0]
    sigma = sigma_vector(noise)
    noisy = clean.copy()
    mask = np.zeros(clean.shape, dtype=bool)

    # --- Band-limited sensor electronics: additive white Gaussian noise. ----
    sensor_sigma = sigma[1:]
    noisy[:, 1:] += rng.normal(0.0, 1.0, size=(n_samples, sensor_sigma.size)) * sensor_sigma

    spike_lo, spike_hi = noise.spike_sigma_range

    # --- Impulsive corruption, per sensor channel. -------------------------
    # Each channel consumes a fixed sample budget so the realised corruption
    # rate matches ``outlier_fraction`` exactly, regardless of event lengths.
    for name in SENSOR_CHANNELS:
        ch = _SENSOR_INDEX[name]
        s = sigma[ch]
        budget = int(round(noise.outlier_fraction * n_samples))
        if budget <= 0:
            continue

        # Stuck/dropout plateaus latch onto a constant level for the channel.
        stuck_level = float(np.median(clean[:, ch]))
        written = 0

        while written < budget:
            dropout = bool(rng.random() < noise.dropout_probability)
            if dropout:
                length = int(rng.integers(2, noise.dropout_max_len + 1))
            elif rng.random() < noise.burst_probability:
                length = int(rng.integers(2, noise.burst_max_len + 1))
            else:
                length = 1
            length = min(length, budget - written, n_samples)

            start = int(rng.integers(0, n_samples - length + 1))
            idx = np.arange(start, start + length)
            if dropout:
                noisy[idx, ch] = stuck_level
            else:
                magnitude = rng.uniform(spike_lo, spike_hi) * s
                sign = -1.0 if rng.random() < 0.5 else 1.0
                noisy[idx, ch] += sign * magnitude
            mask[idx, ch] = True
            written += length

    return noisy.astype(np.float32), mask


def median_filter(values: np.ndarray, kernel: int) -> np.ndarray:
    """Zero-phase rolling median over the last axis; edges use shrinking windows."""
    if kernel < 1 or kernel % 2 == 0:
        raise ValueError("kernel must be a positive odd integer")
    values = np.asarray(values, dtype=np.float64)
    half = kernel // 2
    padded = np.pad(values, [(half, half)] + [(0, 0)] * (values.ndim - 1), mode="edge")
    windows = np.lib.stride_tricks.sliding_window_view(padded, kernel, axis=0)
    return np.median(windows, axis=-1)
