"""Synthetic battery-sensor dataset generator.

Pipeline: sweep the PyBaMM run grid -> solve clean recording blocks -> inject
sensor noise and impulsive outliers -> persist ``.npz`` + ``metadata.json`` +
one CSV per run -> render diagnostic figures.

Run as a module::

    uv run python -m battery_dataset --quick
    uv run python -m battery_dataset                 # full sweep
"""

from __future__ import annotations

import argparse
import zlib
from dataclasses import replace
from pathlib import Path

import numpy as np

from .config import CELLS, CHANNELS, DatasetConfig, NoiseConfig, RunSpec
from .csvio import total_bytes, write_dataset_csvs
from .dataset import Dataset
from .dataset import save as save_dataset
from .noise import inject_noise
from .plot import render_all
from .simulate import generate_clean_dataset

__all__ = [
    "CELLS",
    "CHANNELS",
    "Dataset",
    "DatasetConfig",
    "NoiseConfig",
    "RunSpec",
    "build",
    "main",
]


def _run_rng(seed: int, run_id: str) -> np.random.Generator:
    """Deterministic per-run RNG, stable across processes and run order."""
    return np.random.default_rng([seed, zlib.crc32(run_id.encode("utf-8"))])


def _mib(n_bytes: int) -> str:
    return f"{n_bytes / 1024**2:.1f} MiB"


def build(
    config: DatasetConfig,
    limit: int | None = None,
    verbose: bool = True,
) -> tuple[Dataset, list[RunSpec]]:
    """Generate the full dataset described by ``config``."""
    runs = config.runs(limit)
    if verbose:
        cells = ", ".join(CELLS[ps].cell for ps in config.parameter_sets)
        print(f"Cells: {cells}")
        print(f"Generating {len(runs)} runs x {config.samples_per_run:,} samples "
              f"x {config.n_channels} channels (est. payload "
              f"{_mib(config.estimated_bytes(len(runs)))})")

    clean, duration_s, completed = generate_clean_dataset(config, runs, verbose=verbose)

    noisy = np.empty_like(clean)
    mask = np.zeros(clean.shape, dtype=bool)
    for i, spec in enumerate(completed):
        rng = _run_rng(config.noise.seed, spec.run_id)
        noisy[i], mask[i] = inject_noise(clean[i], config.noise, rng)

    dataset = Dataset(
        clean=clean,
        noisy=noisy,
        outlier_mask=mask,
        duration_s=duration_s,
        run_ids=[spec.run_id for spec in completed],
    )
    if verbose:
        print(f"Noise injected: {int(mask.sum()):,} corrupted samples "
              f"({mask.mean():.3%} of payload)")
    return dataset, completed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="battery-dataset", description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=None, help="artefact root directory")
    parser.add_argument("--runs", type=int, default=None, help="limit the number of runs")
    parser.add_argument("--samples", type=int, default=None, help="samples per run")
    parser.add_argument("--seed", type=int, default=None, help="outlier RNG seed")
    parser.add_argument("--no-plots", action="store_true", help="skip figure rendering")
    parser.add_argument("--no-csv", action="store_true", help="skip the CSV rendering")
    parser.add_argument("--quick", action="store_true", help="3 runs x 4k samples smoke build")
    args = parser.parse_args(argv)

    config = DatasetConfig()
    if args.quick:
        config = replace(config, samples_per_run=4000, c_rates=(1.0,), ambient_c=(25.0,),
                         profiles=("discharge",))
        args.runs = 3
    if args.output_dir is not None:
        config = replace(config, output_dir=args.output_dir)
    if args.samples is not None:
        config = replace(config, samples_per_run=args.samples)
    if args.seed is not None:
        config = replace(config, noise=replace(config.noise, seed=args.seed))

    dataset, runs = build(config, limit=args.runs)

    paths = save_dataset(dataset, config, runs)
    print(f"Wrote {paths['dataset']} ({_mib(dataset.n_bytes)}, "
          f"{dataset.block_count:,} x 64 KiB blocks)")
    print(f"Wrote {paths['metadata']}")

    if not args.no_csv:
        csv_paths, index_path = write_dataset_csvs(dataset, config)
        print(f"Wrote {len(csv_paths)} run CSVs + {index_path.name} "
              f"({_mib(total_bytes(csv_paths) + index_path.stat().st_size)})")

    if not args.no_plots:
        figures = render_all(dataset, runs, config)
        for figure in figures:
            print(f"Rendered {figure}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
