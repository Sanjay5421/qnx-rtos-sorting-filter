"""Assembly and persistence of the synthetic dataset.

The on-disk artefact is an uncompressed ``.npz`` holding float32 blocks --
large by construction, so the downstream service has realistic volumes to move
across its 64 KiB transport blocks.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .config import (
    BIN_FIELDS,
    BIN_ITEM_BYTES,
    BIN_RECORD_BYTES,
    BLOCK_BYTES,
    CHANNEL_NAMES,
    CHANNELS,
    DatasetConfig,
    RunSpec,
)

DATASET_FILENAME = "battery_sensor_dataset.npz"
METADATA_FILENAME = "metadata.json"


@dataclass
class Dataset:
    """Rectangular multi-run recording tensor.

    ``clean`` and ``noisy`` are ``(n_runs, n_samples, n_channels)`` float32;
    ``outlier_mask`` is the matching boolean ground truth.
    """

    clean: np.ndarray
    noisy: np.ndarray
    outlier_mask: np.ndarray
    duration_s: np.ndarray
    run_ids: list[str]

    @property
    def shape(self) -> tuple[int, int, int]:
        return self.clean.shape

    @property
    def n_bytes(self) -> int:
        return int(self.clean.nbytes + self.noisy.nbytes + self.outlier_mask.nbytes)

    @property
    def block_count(self) -> int:
        """Number of 64 KiB transport blocks the noisy payload spans."""
        return int(np.ceil(self.noisy.nbytes / BLOCK_BYTES))


def total_bytes(paths: list[Path]) -> int:
    """Total size of a set of written artefacts."""
    return int(sum(p.stat().st_size for p in paths))


def save(
    dataset: Dataset,
    config: DatasetConfig,
    runs: list[RunSpec],
    out_dir: Path | None = None,
) -> dict[str, Path]:
    """Write ``.npz`` payload plus ``metadata.json``; return the written paths."""
    out_dir = Path(out_dir or config.dataset_dir())
    out_dir.mkdir(parents=True, exist_ok=True)

    npz_path = out_dir / DATASET_FILENAME
    np.savez(
        npz_path,
        clean=dataset.clean,
        noisy=dataset.noisy,
        outlier_mask=dataset.outlier_mask,
        duration_s=dataset.duration_s,
        run_ids=np.asarray(dataset.run_ids, dtype="U96"),
    )

    per_run_outliers = dataset.outlier_mask.sum(axis=1)  # (n_runs, n_channels)
    metadata = {
        "config": config.to_dict(),
        "channels": list(CHANNELS),
        "shape": {
            "n_runs": int(dataset.shape[0]),
            "n_samples": int(dataset.shape[1]),
            "n_channels": int(dataset.shape[2]),
        },
        "dtype": str(dataset.clean.dtype),
        "payload_bytes": dataset.n_bytes,
        "transport_blocks_64kib": dataset.block_count,
        "binary_export": {
            "dtype": f"float{BIN_ITEM_BYTES * 8} little-endian",
            "interleaved": {
                "directory": "bin",
                "fields": list(BIN_FIELDS),
                "bytes_per_sample": BIN_RECORD_BYTES,
                "samples_per_file": int(dataset.shape[1]),
                "bytes_per_file": int(dataset.shape[1] * BIN_RECORD_BYTES),
                "files_per_recording": 2,
                "suffixes": {
                    ".bin": "sensor record (AWGN + impulsive outliers)",
                    ".truth.bin": "clean ground truth",
                },
            },
            "per_channel": {
                "directory": "bin/channels",
                "naming": "r<recording_index>_<channel>.bin",
                "channels": list(CHANNEL_NAMES),
                "samples_per_file": int(dataset.shape[1]),
                "bytes_per_file": int(dataset.shape[1] * BIN_ITEM_BYTES),
                "files_per_recording": len(CHANNEL_NAMES),
                "manifest": "bin/channels/manifest.txt",
                "variant": "sensor record (AWGN + impulsive outliers)",
            },
        },
        "runs": [
            {
                "run_id": spec.run_id,
                "parameter_set": spec.parameter_set,
                "cell": spec.cell.cell,
                "cell_source": spec.cell.source,
                "c_rate": spec.c_rate,
                "ambient_c": spec.ambient_c,
                "profile": spec.profile,
                "duration_s": float(dur),
                "outliers_per_channel": {
                    name: int(count) for name, count in zip(CHANNELS, row)
                },
            }
            for spec, dur, row in zip(runs, dataset.duration_s, per_run_outliers)
        ],
    }
    metadata_path = out_dir / METADATA_FILENAME
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    return {"dataset": npz_path, "metadata": metadata_path}


def load(path: Path) -> Dataset:
    """Reload a dataset written by :func:`save`."""
    with np.load(Path(path), allow_pickle=False) as payload:
        return Dataset(
            clean=payload["clean"],
            noisy=payload["noisy"],
            outlier_mask=payload["outlier_mask"],
            duration_s=payload["duration_s"],
            run_ids=[str(x) for x in payload["run_ids"]],
        )
