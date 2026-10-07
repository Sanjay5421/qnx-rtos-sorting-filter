"""CSV rendering of the dataset: one self-contained file per run.

The ``.npz`` stays the primary artefact (compact, exactly reproducible), while
the CSV rendering makes the same data inspectable with no Python involved --
one row per sample, ground truth, corrupted reading and corruption flags side
by side.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np

from .config import CHANNELS, SENSOR_CHANNELS, DatasetConfig
from .dataset import Dataset

#: ``sample_index`` then, for every channel, clean / noisy / corruption flag.
RUN_CSV_HEADER: tuple[str, ...] = (
    "sample_index",
    *CHANNELS,
    *(f"{name}_noisy" for name in SENSOR_CHANNELS),
    *(f"outlier_{name.split('_')[0]}" for name in SENSOR_CHANNELS),
)

_RUN_CSV_FMT = ["%d", "%.6f", "%.6f", "%.6f", "%.6f", "%.6f", "%.6f", "%.6f", "%d", "%d", "%d"]

INDEX_HEADER = (
    "run_id,file,n_samples,duration_s,outliers_current,outliers_voltage,outliers_temperature"
)


def write_run_csv(path: Path, clean: np.ndarray, noisy: np.ndarray, mask: np.ndarray) -> Path:
    """Write one ``(n_samples, n_channels)`` recording as a CSV block."""
    n_samples = clean.shape[0]
    table = np.empty((n_samples, len(RUN_CSV_HEADER)), dtype=np.float64)
    table[:, 0] = np.arange(n_samples)
    table[:, 1:5] = clean
    table[:, 5:8] = noisy[:, 1:]
    table[:, 8:11] = mask[:, 1:]

    path.parent.mkdir(parents=True, exist_ok=True)
    np.savetxt(
        path,
        table,
        fmt=_RUN_CSV_FMT,
        delimiter=",",
        header=",".join(RUN_CSV_HEADER),
        comments="",
    )
    return path


def write_dataset_csvs(
    dataset: Dataset,
    config: DatasetConfig,
    verbose: bool = True,
) -> tuple[list[Path], Path]:
    """Render every run to ``csv/<run_id>.csv`` plus a ``csv_index.csv`` manifest."""
    csv_dir = config.csv_dir()
    csv_dir.mkdir(parents=True, exist_ok=True)

    paths: list[Path] = []
    index_rows = [INDEX_HEADER]
    for i, run_id in enumerate(dataset.run_ids):
        paths.append(write_run_csv(csv_dir / f"{run_id}.csv",
                                   dataset.clean[i], dataset.noisy[i], dataset.outlier_mask[i]))
        counts = dataset.outlier_mask[i].sum(axis=0)
        index_rows.append(
            f"{run_id},csv/{run_id}.csv,{dataset.shape[1]},{float(dataset.duration_s[i]):.3f},"
            f"{int(counts[1])},{int(counts[2])},{int(counts[3])}"
        )
        if verbose and (i + 1) % 10 == 0:
            print(f"  CSV {i + 1}/{len(dataset.run_ids)} runs", flush=True)

    index_path = config.dataset_dir() / "csv_index.csv"
    index_path.write_text("\n".join(index_rows) + "\n", encoding="utf-8")
    return paths, index_path


def total_bytes(paths: list[Path]) -> int:
    return int(sum(p.stat().st_size for p in paths))
