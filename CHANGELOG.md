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

## [Unreleased] — 2026-10-17

### Changed

- **Results now travel through a shared region instead of a reply.** The client
  still sends `{header, 64 KiB block}` as a two-part IOV — the block crosses by
  message, so the IOV and the 64 KB payload properties of the design stand — but
  the server unpacks each block into a named `/dev/shmem` region and leaves the
  sorted and filtered arrays there, instead of copying 128 KiB back through the
  kernel. The result message now carries byte offsets into that region.
- **`protocol.h` defines the region**: `PROTO_SHM_NAME`
  (`/dev/shmem/sortmed-results`), a header carrying magic, version, slot count,
  slot size and a generation counter, then four slots of a slot header plus a
  sorted and a filtered array of `PROTO_BLOCK_SAMPLES` each, with the offsets of
  everything in it. The leading `/` is what puts the object in `/dev/shmem`
  rather than under the server's working directory, and `shm_open` needs nothing
  beyond libc in either Makefile.
- **`proto_result_t` describes that region**: `offset` became `sorted_offset`
  plus `filtered_offset`, both byte offsets from the region start where 0 means
  "not produced", and each array carries its own checksum so the client verifies
  what it reads instead of trusting memory. `PROTO_ERR_BUSY` acquired a job: a
  submit that finds every slot taken is refused rather than overwriting a result
  nobody has collected yet.
- **`test_protocol.c` grew three tests** — 9 → 12 tests, 82 → 137 checks — for the
  region's name rules, the layout arithmetic, the offset alignment and a
  full-length result fitting in the last slot. The host suite is 1473 checks, all
  passing, and the header still cross-compiles for `aarch64le` with no warnings.

### Fixed

- **The server resolved the client's connection once and then reused it.** Each
  run of `sortmed-client` registers the same name afresh, so from the second run
  on the completion pulse went to a dead connection and the client waited for an
  event that could not arrive. The server now opens the client's name for every
  job and closes it after the pulse — which is what makes four consecutive runs,
  one channel each, possible without restarting the service. The tidier shape is
  for the client to pass its own coid in the submit message; that header is
  frozen at milestone 1, so the server resolves the name per job instead.
- **The boot image carries that fix.** `RT-MOMS/images/ifs-rpi4.bin` is
  37 742 192 B, SHA-256
  `57637557e9a8565974714802f412f074767bc1d3d5fab1c2779437d045588c49` — the same
  size as the previous one, so the hash is what tells them apart. The
  `sortmed-server` and `sortmed-client` extracted from it match the freshly built
  `install/aarch64le/bin` binaries byte for byte up to mkifs's page rounding,
  while the one in the previous image does not.
- **The comparison tooling uses the service's own median definition**
  (`median_of_sorted`: the mean of the two middle values for an even-sized
  block) and reports the recording's real time base — 60 000 samples over
  4489.8 s, 13.36 Hz — instead of the client's nominal rate flag. Re-deriving the
  board's `medians.csv` from the dataset now reproduces it exactly:
  3.961808 / 3.641635 / 3.384104 / 2.973536 V.

## [Unreleased] — 2026-10-15

The QNX side of the project (`RT-MOMS/`) landed in this window without an entry
here, so it is recorded in full below, followed by what the last piece of it —
the parallel merge sort, milestone 2 — added.

### Added — the service sources (`RT-MOMS/app/`)

- **`protocol.h`/`protocol.c` — the common message definition**, the interface
  both sides compile against, and the "message definition in a common header
  file" the prerequisites ask for: a 16384-sample float32 block (exactly 64 KiB,
  so "payload at least 64 KB" needs no padding), a block descriptor (sequence,
  first sample, sample count, rate, channel, FNV-1a checksum, min/max), the
  two-part IOV submit `{header, block}`, the ack, the completion pulse (code 16)
  behind which the client fetches its result, and the result itself carrying the
  median and the memory-offset descriptor. Message types are asserted at compile
  time to stay clear of QNX's system message range, and the pulse code clear of
  the kernel's reserved codes.
- **`sensor.h`/`sensor.c` — the virtual sensor**: replays a recording like a
  device, a sample or a frame at a time, from either on-disk layout through one
  code path (`offset`/`stride`, so planar is `{0, 1}` and interleaved `{k, 4}`),
  with bounded read-ahead, exact per-sample timestamps and optional pacing.
- **`block_loader.h`/`block_loader.c` — recording → 64 KiB blocks plus
  descriptors**, and the one place that knows the interleaved record's field
  order (`time_s, voltage_V, current_A, temperature_K`) is *not* the protocol's
  channel order. A short final block is zero-padded, so its bytes — and its
  checksum — are reproducible.

### Added — parallel merge sort (milestone 2)

- **`mergesort.h`/`mergesort.c`** — the sort the server runs on each block, on a
  pool of POSIX worker threads: phase 0 sorts one partition per worker, then each
  merge phase halves the number of runs until one covers the array. The phase
  boundary is a real barrier — one mutex plus a work-ready and a phase-done
  condition variable — so no phase begins before the runs it reads exist, and
  nothing is ordered by a sleep. One `n`-float scratch buffer is ping-ponged with
  the caller's array; the merge is stable, so equal values keep their order and
  the output does not depend on the thread count; the sort is in place; and
  `ms_stats()` reports the partitions, phases and worker tasks that actually ran.
  Inputs below 4096 values take a single-threaded path, and a second concurrent
  sort on the same pool is refused rather than allowed to corrupt the shared
  phase state.
- **`tests/test_mergesort.c`** — 13 tests, 578 checks: bit-identical output for
  1–8 threads; every size from 0 to 64; the sizes around the 4096 threshold and
  the partition boundaries; all-equal, two-value, presorted, reverse-sorted and
  sawtooth input; `±0.0`, denormals, `FLT_MIN`/`FLT_MAX`; and, over the real
  payload, no NaN anywhere plus every block matching `qsort()`'s answer and the
  descriptor's own min/max while leaving the block itself untouched.
- **`tests/pipeline_demo.c`** — the compute half of the service without the IPC:
  a block stream in, the median, the range, the sort time and the throughput out.

### Added — the host suite and the boot image

- **`RT-MOMS/tests/`** — the whole host-side suite: `test_sensor.c` (15 tests,
  642 checks), `test_protocol.c` (9, 82), `test_block_loader.c` (8, 116),
  `test_mergesort.c` (13, 578), with `sensor_demo.c`, `loader_demo.c` and
  `pipeline_demo.c` alongside. `mingw32-make check` builds and runs all four
  suites — 1418 checks — and `demo-pipeline` sorts the payload in its 64 KiB
  blocks, four workers deep.
- **BSP wiring**: `app/Makefile` cross-builds the programs for `aarch64le` into
  `install/aarch64le/bin`, the top-level `make` runs it, and `images/rpi4.build`
  places them in `/bin` with the payload in `/data`. The image is 37 610 936 B
  and carries seven programs; `dumpifs` lists them back out of the built image.
- **`IMPLEMENTATION.md`** and **`REPRODUCE.md`** (both at the repository root) —
  what each stage implements and why, in pipeline order with diagrams, and every
  command with its expected output, host and target, marked verified or planned.
- **Verified on the board**: `test_protocol` (82 checks, 0 failed, 0 skipped),
  `test_block_loader /data` (116 / 0 / 0) and `test_mergesort /data` (578 / 0 / 0,
  the payload tests included), both `cksum` values of the payload inside the
  image, `sensor_demo`, `loader_demo` in both layouts, and `pipeline_demo` at 1, 2
  and 4 workers on both layouts — medians, ranges, plan lines, checksums and order
  verdicts all identical to the host. One 64 KiB block takes 0.757 ms on the board
  with four workers against 2.278 ms with one (3.01×), which is 163.84 s of 100 Hz
  signal sorted per 0.757 ms. The only line still to capture there is
  `test_sensor /data`, whose earlier run the serial session cut short.

### Changed

- **`tests/test_data.h`** now includes `test_util.h` and marks both payload path
  helpers `T_MAYBE_UNUSED`, so a suite that needs only one of the two layouts
  still builds warning-free.
- **`tests/Makefile`** grew a `THREADCC` probe: the binaries that link
  `mergesort.c` need `<pthread.h>`, which MinGW.org's gcc on this host does not
  ship at all, so the compiler is chosen by compiling and linking a probe instead
  of being assumed. On QNX and Linux the probe answers `$(CC)` itself, and
  `THREADCC=…` overrides the choice.
- **`.gitignore`** covers the new host and target binaries and the probe's
  scratch file.
- **`README.md`** — the repository layout now names `RT-MOMS/` and
  `qnx_project/`.

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
- **Raw binary export in two layouts** — for every recording, an *interleaved*
  pair (`<run_id>.bin` sensor record, `<run_id>.truth.bin` clean ground truth;
  headerless little-endian float32 in `time_s, voltage_V, current_A,
  temperature_K` order, 16 bytes per sample) and a *per-channel* set
  (`bin/channels/r<index>_<channel>.bin`, one flat float32 run per channel plus
  `manifest.txt`). The per-channel layout is the one `qnx_project/loader.c`
  reads: min/max over such a file is meaningful, whereas over an interleaved
  record it mixes all four channels. Both layouts are declared in
  `metadata.json`.
- **Five diagnostic figures** (`01_signal_overview`, `02_noise_injection`,
  `03_median_filter_recovery`, `04_noise_statistics`, `05_run_gallery`).
- **`metadata.json` manifest** recording the build configuration, the cell
  identity behind each parameter set, shapes, per-run duration and per-channel
  outlier counts.
- **`CHANGELOG.md`** (this file).
- Reference rolling-median implementation used only for the recovery figures.
- **Pi export script** (`export_for_pi.py`) — writes the noisy `time`,
  `current`, `voltage` and `temperature` channels of each recording as raw
  little-endian float32 `.bin` files (`pi_data/`, 12 files of 240 000 B each)
  plus a `manifest.txt`, so the QNX client can read them without NumPy. Only
  the corrupted readings are exported; the clean signal and the outlier mask
  stay on the host as the answer key.
- **Stage-1 virtual sensor loader** (`qnx_project/loader.c`) — loads one
  raw float32 channel file, then prints the sample count, the number of
  64 KiB blocks, the min / max and the first samples. Every libc return value
  is checked. First step towards the client of the sorting / median-filter
  service.

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
- **`.gitignore`** now also ignores the generated `pi_data/` export.

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

- `export_for_pi.py` — its per-channel export now runs inside the generator
  (`binio.py`), emitting the same `r<index>_<channel>.bin` files plus a manifest.
- `data/README.md` and `data/.gitignore` (consolidated at the repository root).
- `OKane2022` from the default sweep (duplicate of the `Chen2020` cell).
- Dead `LogNorm` import and an unused accumulator in the plotting module.
