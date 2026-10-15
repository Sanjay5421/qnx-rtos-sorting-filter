"""Raw binary rendering of a recording, for direct reads on the target device.

Two layouts are written, both headerless little-endian float32:

*Interleaved* -- one file per recording per variant, fixed 16-byte stride, so a
reader seeks straight to sample ``n`` at ``n * 16``::

    record = [time_s, voltage_V, current_A, temperature_K]

``<run_id>.bin`` is the sensor record (AWGN plus impulsive outliers), what the
service actually consumes; ``<run_id>.truth.bin`` is the clean ground truth.

*Per channel* -- one file per recording per channel, a flat run of samples, so a
reader loads a single signal and its min/max means something::

    r0_time.bin  r0_current.bin  r0_voltage.bin  r0_temperature.bin

``bin/channels/manifest.txt`` maps each ``r<index>`` prefix to its recording id.
The CSV is the human-readable rendering of the same data.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np

from .config import BIN_FIELDS, BIN_ITEM_BYTES, CHANNEL_NAMES, DatasetConfig
from .dataset import Dataset

#: Explicit little-endian, so the files are byte-identical on any host.
BIN_DTYPE = np.dtype(f"<f{BIN_ITEM_BYTES}")


def _record(clean_run: np.ndarray, noisy_run: np.ndarray, noisy: bool) -> np.ndarray:
    """Assemble one recording in :data:`BIN_FIELDS` order.

    The dataset stores time/current/voltage/temperature; the binary record is
    time/voltage/current/temperature, hence the reordering.
    """
    source = noisy_run if noisy else clean_run
    return np.column_stack([clean_run[:, 0], source[:, 2], source[:, 1], source[:, 3]])


def write_run_bin(path: Path, record: np.ndarray) -> Path:
    """Write one ``(n_samples, 4)`` record as a headerless float32 blob."""
    path.parent.mkdir(parents=True, exist_ok=True)
    np.ascontiguousarray(record, dtype=BIN_DTYPE).tofile(path)
    return path


def write_channel_bins(
    dataset: Dataset,
    config: DatasetConfig,
    verbose: bool = True,
) -> tuple[list[Path], Path]:
    """Write one flat file per channel per recording, plus a manifest.

    Each file is a plain sequence of float32 samples for a single channel, which
    is what a C reader can load and summarise directly (min/max over the file is
    meaningful). The manifest maps the short ``r<index>`` file prefixes back to
    the recording ids.
    """
    channel_dir = config.channel_bin_dir()
    channel_dir.mkdir(parents=True, exist_ok=True)

    paths: list[Path] = []
    manifest_rows: list[str] = []
    for i, run_id in enumerate(dataset.run_ids):
        manifest_rows.append(f"r{i} {run_id}")
        for channel_index, name in enumerate(CHANNEL_NAMES):
            path = channel_dir / f"r{i}_{name}.bin"
            paths.append(write_run_bin(path, dataset.noisy[i, :, channel_index]))
        if verbose and (i + 1) % 10 == 0:
            print(f"  BIN channels {i + 1}/{len(dataset.run_ids)}", flush=True)

    manifest_path = channel_dir / "manifest.txt"
    manifest_path.write_text("\n".join(manifest_rows) + "\n", encoding="utf-8")
    return paths, manifest_path


def write_dataset_bins(
    dataset: Dataset,
    config: DatasetConfig,
    verbose: bool = True,
) -> list[Path]:
    """Write the interleaved sensor record and ground truth for every recording."""
    bin_dir = config.bin_dir()
    bin_dir.mkdir(parents=True, exist_ok=True)

    paths: list[Path] = []
    for i, run_id in enumerate(dataset.run_ids):
        clean_run = dataset.clean[i]
        noisy_run = dataset.noisy[i]
        paths.append(write_run_bin(bin_dir / f"{run_id}.bin", _record(clean_run, noisy_run, True)))
        paths.append(
            write_run_bin(bin_dir / f"{run_id}.truth.bin", _record(clean_run, noisy_run, False))
        )
        if verbose and (i + 1) % 10 == 0:
            print(f"  BIN {i + 1}/{len(dataset.run_ids)} recordings", flush=True)

    return paths
