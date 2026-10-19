# Testing — every test, and how to run it

Three places things run, and none of them substitutes for the others:

| Where | What runs there | What it proves |
| --- | --- | --- |
| **Host** (Windows + MSYS2/MinGW, or Linux/macOS) | the unit suites and the KPI harness | the algorithms: sorting, the filter, the loader, the message layout, the validation |
| **Board** (QNX 8 on the Pi 4) | the same suites, the service itself, the sweep scripts, the soak | that the real service on real hardware does what the host tests say — and how long it takes |
| **PC afterwards** | `tools/filter_sweep.py`, `plot_run.py`, `plot_vs_truth.py` | what a run's own output files mean against ground truth |

Nothing about *timing* is measured on the host or in a VM: every number in the
report comes from the board, produced by the board's own build.
`FINAL-RUN.md` records the integrated run; this file is how to repeat it and
how to add the measurements to it.

---

## 1. The host suite

```sh
cd RT-MOMS/tests
mingw32-make check
```

That builds and runs every test binary in one go. The payload argument they
share is `../../data/artifacts/dataset`, so the tests that need the dataset find
it by themselves; without it they report themselves as skipped rather than
failing. Current result on the host: **9 150 checks, 0 failed, 0 skipped**.

| Binary | What it checks | Checks |
| --- | --- | --- |
| `test_sensor` | the virtual sensor: layout conversion, ranges, real recordings | 642 |
| `test_protocol` | the message layout, the width rule, validation, the pulse float round trip, the shared-region maths | 186 |
| `test_block_loader` | recording → 64 KiB blocks, descriptors, the two file layouts agreeing | 116 |
| `test_mergesort` | the parallel sort, its stability, thread-count invariance | 578 |
| `test_median` | the Hampel filter against a brute-force reference at several widths, bursts, seam context | 7 615 |
| `test_shared_memory` | the region's bounds and layout | 13 |
| `test_kpi` | the performance harness: sort scaling, filter cost, global merge | 79 |

`test_kpi` prints timings but asserts only correctness: it has to pass on a
loaded host, in a VM and on the board, and those differ by an order of
magnitude. The numbers worth quoting come from the board.

There are also three demos, useful when something needs *seeing* rather than
asserting:

```sh
mingw32-make demo           # the 64 KiB block stream the client will offload
mingw32-make demo-sensor    # a recording, sample by sample
mingw32-make demo-pipeline  # the recording sorted in its blocks, with timing
```

---

## 2. Building the image (only when the sources change)

```bat
P:\qnx\qnx800\qnxsdp-env.bat
cd RT-MOMS
make
```

`make` builds the BSP, the programs and the board scripts, installs them, and
then builds the image:

| | |
| --- | --- |
| Path | `RT-MOMS/images/ifs-rpi4.bin` — and a copy at the repository root (`ifs-rpi4.bin`) for convenience |
| Size | 37 797 716 B |
| SHA-256 | `2ac01f08157ded8fcb0ae132fbd732bb3f5b5756d40757f490b4e20a31fc9d8b` |

The image carries the service (`/bin/sortmed-server`, `/bin/sortmed-client`),
the suites, the three board scripts, and the payload under `/data/bin`. If the
image is newer than `rpi4.build`, delete it first — `mkifs` overwrites it.

Copy it to the SD card's FAT partition beside `config.txt` (`kernel=ifs-rpi4.bin`)
and the firmware files; `REPRODUCE.md` §7–8 covers the card and the boot
details. Rebuild the image whenever `RT-MOMS/app/` changes — the board runs
exactly the build in this file, and `FINAL-RUN.md` records the hash that was
actually used.

---

## 3. On the board

Serial console, 115200 8N1, `#` prompt. What is on the image:

| Path | |
| --- | --- |
| `/bin/sortmed-server`, `/bin/sortmed-client` | the service |
| `/bin/test_*`, `*_demo` | the same suites the host ran |
| `/bin/perf-sweep.sh` | the width/thread sweep (§3.3, §3.4) |
| `/bin/client-wait-test.sh` | prerequisite 7, both cases (§3.5) |
| `/bin/soak.sh` | repeated requests (§3.6) |
| `/data/bin/...` | the payload: the interleaved recording and one per-channel file |

`/tmp` is `/dev/shmem` — RAM, and it holds **flat files only**: `mkdir` there
answers "Function not implemented". Use `/tmp` for one run's four files, and
`/var` or the mounted card for anything with directories.

### 3.1 The module suites

```sh
cd /tmp
test_shared_memory
test_protocol
test_median
test_mergesort /data
test_sensor /data
test_block_loader /data
test_kpi
```

Each prints `N checks, M failed, K skipped`. The payload argument is the
directory the image carries the recordings in; without it those tests skip.

### 3.2 One service run

```sh
cd /tmp
sortmed-server &
sortmed-client /data/bin/Chen2020_c1_T25_discharge.bin interleaved voltage 100
```

The server can be started *after* the client — the client retries for ten
seconds and says how long it waited — and one server serves any number of runs.
Expected output is the four `pulse:` lines, the four `job:` lines, the summary,
the cross-check and the write-back, then two `timing:` lines; the shape of it
is in `FINAL-RUN.md` §3.

### 3.3 The width sweep — the performance study

```sh
ls /dev/sd*                              # find the card; the FAT partition has a t suffix
mount -t dos /dev/sd0t12 /var/mnt
sh /bin/perf-sweep.sh width
```

Roughly two minutes and 80 MiB for the default sweep (9 widths × 4 channels ×
3 repeats). Narrow it from the environment:

```sh
WIDTHS="5 9 13 21" CHANNELS="voltage" REPS=2 sh /bin/perf-sweep.sh width /var/mnt/perf-short
```

What it does: starts **one** server (the filter width travels in each submit,
so no restart is needed), then runs the client once per case at that width.
Each case gets its own directory holding `raw.bin`, `sorted.bin`,
`filtered.bin`, `medians.csv` and `client.log`; every case appends its
parameters to `sweep.manifest`; the server's `perf:` lines go to `server.log`
in the sweep directory. Nothing is printed during a case except the script's
own one-line progress — the client's output and the server's are redirected to
files, so the serial console's 115200 bps cannot distort the timings.

The card must be mounted, and the space matters: writing 80 MiB into `/var` (a
16 MiB RAM filesystem) would fill it up part way through. The script refuses to
start if the output directory's parent does not look like the card; set
`ALLOW_ANY=1` to override that, or point the second argument elsewhere.

### 3.4 The thread sweep

```sh
sh /bin/perf-sweep.sh threads
```

The merge-pool size is a server setting, so here the server is started once per
worker count (1, 2, 3, 4) and every case runs at `FILTER_WIDTH` (default 9).
Everything else — layout, manifest, logs — is as in §3.3. This is the sweep
that answers "is there any speedup": the sort is parallel and the filter is
serial, so the interesting number is how little the *total* moves.

### 3.5 The client-wait test (prerequisite 7)

```sh
sh /bin/client-wait-test.sh
```

Two cases, about 15 s in total: with no server at all the client must reach its
ten-second deadline, say so and exit non-zero; with the server started three
seconds *after* the client, the client must connect by itself and finish a run.
The evidence is the client's own output, which reports the wait and the number
of attempts — this is the script to run before demonstrating prerequisite 7.

### 3.6 The soak — repeated requests

```sh
sh /bin/soak.sh 20
```

Runs the client 20 times against one server and checks the block medians are
identical every time (they are a property of the recording, so any difference
means something moved). It also prints each run's own `timing:` line, which is
what shows the service staying put run after run.

---

## 4. Score the sweep on the PC

Copy the sweep directory back from the card as it is, then:

```sh
cd data                                  # the Python environment lives here
uv run python ../tools/filter_sweep.py "D:/perf" --out ../run-figures/perf
```

`filter_sweep.py` reads the board's own output; it never filters anything
itself, so what it reports is exactly what the service produced. The answer key
is the dataset's `clean` series and `outlier_mask`.

| Output | What to read from it |
| --- | --- |
| `quality.csv` | per case: how many injected outliers were replaced (recall), how many *clean* samples were replaced, RMSE/MAE/p99 before and after, the worst surviving error at an injected sample |
| `cost.csv` | per full block: the server's stage times in ms — copy, sort, filter, sums, merge, publish, total |
| `client_timing.csv` | the client's own view: whole run, submit, service, fetch, and its post-processing |
| `width_quality.png` | recall, false replacements and RMSE gain against width |
| `width_cost.png` | sort, filter and whole-block time against width, log-log |
| `width_tradeoff.png` | filter cost against the RMSE gain it buys, one labelled point per width |
| `width_bursts.png` | recall as a heat map over filter width and injected burst length |
| `threads_scaling.png` | only from the thread sweep: cost per stage, and speedup against worker count |
| `threads_stages.png` | only from the thread sweep: where the block time goes, stage by stage |

It also checks the invariants a sweep must not break, and exits non-zero if any
fails: every case of the same recording and channel must have an identical
`raw.bin` (same input), identical block medians (they are of the raw data, so
they cannot depend on the width), and repeated cases must give identical
quality numbers.

How to choose a width from the result: the smallest width whose recall stops
improving and whose replacements on clean samples stay negligible — with the
cost as the tie-breaker. What makes that concrete for this dataset is the
injected burst length: a window can only swallow a burst shorter than half its
width. Most injected bursts are 1 to 6 samples, but the realised mask also
carries a few longer runs — 8 runs of 7 samples, 2 of 8, and one each of 9 and
10, out of 4196 runs — so recall keeps improving until the half-window reaches
10, which is width 21, and that is exactly where the curve flattens. The width
heat map shows the same thing cell by cell.

---

## 5. One run's own figures

```sh
cd data
uv run python ../tools/plot_run.py "../tools/voltage" --channel voltage --out ../run-figures/voltage
uv run python ../tools/plot_vs_truth.py "../tools/voltage" --out ../run-figures/voltage
```

`plot_run.py` needs `--channel` because the run's files carry no channel name;
`plot_vs_truth.py` works the channel out by matching `raw.bin` against the
dataset. Both take a directory holding a run's four files, so point them at a
copied-back case directory from the sweep if that is what you want to look at.

---

## 6. When something is wrong

| Symptom | What it is |
| --- | --- |
| `no server named 'sortmed' after 10.0 s` | the server is not running — that message is prerequisite 7 doing its job |
| a stale mapping or region complaint | `slay sortmed-server` and start it again: the server unlinks and recreates the region at startup |
| `mount -t dos` refuses | `ls /dev/sd*` first: the FAT partition is the one with the `t` suffix; `t1` does not exist on this card |
| `mkdir` under `/tmp` fails | `/tmp` is `/dev/shmem`, flat files only — use `/var` or the card |
| the sweep refuses: "does not look like the mounted card" | mount the card first, or pass another output directory, or `ALLOW_ANY=1` |
| payload tests report themselves skipped | the image was built without `/data` — rebuild with the dataset present |
| a timing looks absurd | check the run was redirected to files (§3.3) and that the board is not being driven by two consoles at once |

---

## 7. What the numbers mean

- The server's `elapsed_ns` per block is its own work for that block: copy,
  sort, filter and the checksums. It excludes the IPC.
- The client's `timing:` line: `block_avg_ns` is the whole per-block round trip,
  and `service_avg_ns − server_avg_ns` is what the pulse delivery and the
  scheduling around it cost.
- `perf: total=` adds the run merge (and, on the last block, the publish).
- At width 9 the filter is the dominant term — around 6.5 ms of the ~7.5 ms per
  block the board measured, with the sort at 0.757 ms — which is why the sweep
  measures the filter's cost curve rather than the sort's.
