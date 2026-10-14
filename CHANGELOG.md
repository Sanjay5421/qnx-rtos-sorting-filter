# Changelog

All notable changes to this repository are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## Generated dataset — statistics

What the default full build currently produces (`uv run python -m battery_dataset`,
seed `20251007`), measured from `data/artifacts/`.

### Runs by type

3 recordings, one per C-rate, on a single cell at 25 °C, discharged from full:

| Recording | C-rate | Current | Duration | Temperature rise |
| --- | --- | --- | --- | --- |
| `Chen2020_c0.5_T25_discharge` | 0.5C | 2.5 A | 2.27 h | 3.5 K |
| `Chen2020_c1_T25_discharge` | 1C | 5.0 A | 1.25 h | 8.9 K |
| `Chen2020_c2_T25_discharge` | 2C | 10.0 A | 0.74 h | 19.9 K |

Holding the cell, the ambient temperature and the experiment fixed leaves the
load as the only variable, so the three recordings are directly comparable and
visibly different.

### Size

| Quantity | Value |
| --- | --- |
| Recordings | 3 |
| Samples per recording | 60 000 |
| Channels per sample | 4 — `time_s`, `current_A`, `voltage_V`, `temperature_K` |
| Sample rows in total | 180 000 |
| Scalar values in the tensor | 720 000 |
| npz payload (array bytes) | 6 480 000 B = 6.2 MiB = 44 × 64 KiB blocks |
| npz file on disk | 6 482 424 B |
| CSV files | 4 (3 recording files + 1 index) |
| CSV total on disk | 14.2 MiB |
| Figures | 5 PNGs |

### Signal and corruption statistics

| Quantity | Value |
| --- | --- |
| Recording duration | 0.74 h – 2.27 h (median 1.25 h) |
| Temperature rise | 3.5 K – 19.9 K |
| Corrupted samples — current | 2 679 (1.4883 %) |
| Corrupted samples — voltage | 2 681 (1.4894 %) |
| Corrupted samples — temperature | 2 680 (1.4889 %) |
| Corrupted samples — total | 8 040 of 540 000 sensor cells (1.489 %) |

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

- **Default sweep reduced from 54 recordings to 3.** One cell at three C-rates
  replaces the 3 cells × 3 C-rates × 3 ambient temperatures × 2 experiments
  grid, which was redundant for the median-filter workload. Artefacts shrank
  from 371 MiB to 22 MiB and the build from ≈ 50 s to ≈ 20 s.
- **Both authors listed** in `data/pyproject.toml`, sourced from the git history
  and the GitHub API: [@Anmol-G-K](https://github.com/Anmol-G-K) and
  [@Sanjay5421](https://github.com/Sanjay5421), each with their GitHub no-reply
  address.
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
- **Figure 05(b)/(c)** — the ambient-temperature colourbar drew a meaningless
  23–27 °C gradient when a single ambient temperature was simulated, and the
  duration panel legended a `cccv` profile that was not in the dataset. Both now
  show only what the data actually contains.

### Removed

- `data/README.md` and `data/.gitignore` (consolidated at the repository root).
- `OKane2022` from the default sweep (duplicate of the `Chen2020` cell).
- Dead `LogNorm` import and an unused accumulator in the plotting module.
