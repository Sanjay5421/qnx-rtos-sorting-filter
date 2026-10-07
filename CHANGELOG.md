# Changelog

All notable changes to this repository are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Generated dataset — statistics

What the default full build currently produces (`uv run python -m battery_dataset`,
seed `20251007`), measured from `data/artifacts/`.

### Runs by type

54 runs in total, and every axis is evenly covered:

| Axis | Value | Runs |
| --- | --- | --- |
| Cell | LG M50 (`Chen2020`) | 18 |
| | graphite/NMC532 pouch (`Mohtat2020`) | 18 |
| | Enertech (`Ai2020`) | 18 |
| C-rate | 0.5C | 18 |
| | 1C | 18 |
| | 2C | 18 |
| Ambient temperature | 5 °C / 25 °C / 45 °C | 18 each |
| Experiment | `discharge` | 27 |
| | `cccv` | 27 |

That is **27 unique cell × C-rate × temperature combinations, each simulated
twice** — once as a discharge, once as a CC-CV charge. No combination is
missing and none is duplicated.

### Size

| Quantity | Value |
| --- | --- |
| Runs (records) | 54 |
| Samples per run | 60 000 |
| Channels per sample | 4 — `time_s`, `current_A`, `voltage_V`, `temperature_K` |
| Sample rows across all runs | 3 240 000 |
| Scalar values in the tensor | 12 960 000 |
| npz payload (array bytes) | 116 640 000 B = 111.2 MiB = 792 × 64 KiB blocks |
| npz file on disk | 116 662 212 B |
| CSV files | 55 (54 run files + 1 index) |
| CSV total on disk | 271.3 MB |
| Figures | 5 PNGs, 1.8 MiB |

### Signal and corruption statistics

| Quantity | Value |
| --- | --- |
| Run duration | 0.68 h – 3.64 h (median 1.56 h) |
| Temperature rise across the 18 runs at 2C | 1.53 K – 25.87 K |
| Corrupted samples — current | 48 244 (1.4890 %) |
| Corrupted samples — voltage | 48 259 (1.4895 %) |
| Corrupted samples — temperature | 48 260 (1.4895 %) |
| Corrupted samples — total | 144 763 of 9 720 000 sensor cells (1.489 %) |

## [Unreleased] — 2026-10-07

### Added

- **Synthetic dataset generator** for the client payload the sorting /
  median-filter service consumes, built on PyBaMM
  (`data/src/battery_dataset/`: `config`, `simulate`, `noise`, `dataset`,
  `csvio`, `plot`, `__init__`, `__main__`).
- **Noise model** — additive white Gaussian sensor noise plus impulsive
  corruption (single-sample spikes, contiguous bursts, stuck/dropout plateaus)
  applied per sensor channel, with an exact per-sample corruption budget.
- **Ground-truth corruption mask** stored alongside the corrupted readings, so a
  median filter can be scored rather than eyeballed.
- **CSV rendering** — one self-contained CSV per run plus a `csv_index.csv`
  manifest, in addition to the `.npz` tensor.
- **Five diagnostic figures** (`01_signal_overview`, `02_noise_injection`,
  `03_median_filter_recovery`, `04_noise_statistics`, `05_run_gallery`).
- **`metadata.json` manifest** recording the build configuration, the cell
  identity behind each parameter set, shapes, per-run duration and per-channel
  outlier counts.
- **`CHANGELOG.md`** (this file).
- Reference rolling-median implementation used only for the recovery figures.

### Changed

- **Project renamed** `data` → `battery-dataset`, package `data` →
  `battery_dataset`, so the package directory no longer repeats the project
  directory name (`data/src/data/`).
- **Cell selection reworked** to three physically distinct cells:
  `Chen2020` (LG M50), `Mohtat2020` (graphite/NMC532 pouch), `Ai2020`
  (Enertech). `OKane2022` was dropped because it parameterises the *same*
  LG M50 cell as `Chen2020` and therefore added no variety.
- **Artefact layout reorganised** — `artifacts/dataset/` (payload, manifest,
  CSV) and `artifacts/figures/` instead of one flat directory.
- **Single README** at the repository root; the generator documentation that
  lived in `data/README.md` was folded into it.
- **`.gitignore` moved to the repository root**, now covering the generated
  artefacts, `__pycache__` and virtual environments.
- Run duration and thermal range now vary far more across the sweep because the
  three cells have different capacities and thermal behaviour.

### Fixed

- **Package could not be built**: `data/pyproject.toml` declared
  `readme = "README.md"` but no such file existed; `uv build` failed. The
  package metadata now carries an inline description, so no separate README —
  per the one-README rule — is required.
- **`Mohtat2020` runs failed** with *"step is infeasible at initial
  conditions"*: PyBaMM parameter sets each carry their own default state of
  charge. Each profile now sets its initial state explicitly (`discharge` from
  full, `cccv` from empty) using `set_initial_state`, replacing the deprecated
  `set_initial_stoichiometries`.
- **CSV did not match the `.npz`**: the `time_s` column was written with 3
  decimals, silently quantising it by 1 ms. Raised to 6 decimals; all 11
  columns now reproduce the tensor exactly.
- **Realised corruption rate did not match configuration**: multi-sample bursts
  and dropouts were counted per event, so the achieved rate overshot the
  configured 1.5 %. Events now consume a fixed per-sample budget
  (realised 1.489–1.490 %).
- **Figure 02** — legend collided with the figure title; panel titles reported
  the outlier count of the whole channel instead of the zoom window.
- **Figure 01(d)** — legend overlapped the bar value labels.
- **Figure 05(d)** — panel was degenerate (every run has the same outlier count
  by construction); replaced with the corruption run-length distribution
  recovered from the mask.
- **Figure 05(b)** — parameter-set markers were hardcoded and would break when
  the cell set changed; now derived from the runs present.

### Removed

- `data/README.md` and `data/.gitignore` (consolidated at the repository root).
- `OKane2022` from the default sweep (duplicate of the `Chen2020` cell).
- Dead `LogNorm` import and an unused accumulator in the plotting module.
