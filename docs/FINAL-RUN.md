# Final run — the integrated service on the Raspberry Pi

The end-to-end run of the finished M3/M4 path: the client offloads the production
recording in 64 KiB blocks, the server sorts and filters each one, taps the client
with a pulse, and the client collects the median and the arrays out of the shared
region — then writes the completed run back to disk.

**Status — executed on hardware, 2026-10-17.** The service ran end to end on the
board with the voltage channel: four blocks accepted, four completions, four
results read out of the shared region and checksum-verified, and the write-back
line printed. The four block medians were `3.961808`, `3.641635`, `3.384104`,
`2.973536` — exactly the numbers `pipeline_demo` produces — with
`431 / 382 / 406 / 283` outliers per block, a global median of `3.56619358` and
`1502` outliers over the recording. The server measured `7.51 / 7.32 / 7.30 /
5.01 ms` per block for sort + filter + checksums (the sort itself is 0.757 ms of
that; the rolling filter dominates).

**All four channels are the same command four times**, one channel each: the client
reads a single column of the interleaved layout, and each run writes four fixed
file names. Section 3 runs the four into four directories **on the mounted card**,
so the outputs are on the SD card as they are produced; it also keeps the older
route of running in `/tmp` and copying out, for a single channel at a time. `/tmp`
on this image is `/dev/shmem` — **RAM** — so anything left there is gone at
power-off. All four were run this way on 2026-10-17 and each channel reproduced the
host's numbers exactly; the run data and the figures are committed under
`tools/<channel>/` and `run-figures/<channel>/`. Section 6 lists what to look at if
a run misbehaves.

The loop needs the image whose hash is section 1. On the earlier one (`32c303e8…`)
the server resolved the client's connection once, so from the second channel on the
pulse went to the previous client's dead connection and the client waited for an
event that could never arrive: on that image, restart the server before each
channel.

| Step | Where | What happens |
| --- | --- | --- |
| 1 | host | verify the image, copy it to the card |
| 2 | board | start `sortmed-server` — once, for all four runs |
| 3 | board | run `sortmed-client` once per channel, into `/var/mnt/<channel>` |
| 4 | board | check each channel's four files, then `sync` and unmount |
| 5 | board | optional: the module suites |
| 6 | board | stop the server |

---

## 1. Host: the image you are about to flash

```sh
cd RT-MOMS/images
sha256sum ifs-rpi4.bin
```

| | |
| --- | --- |
| Path | `RT-MOMS/images/ifs-rpi4.bin` |
| Size | 37 797 716 B |
| SHA-256 | `2ac01f08157ded8fcb0ae132fbd732bb3f5b5756d40757f490b4e20a31fc9d8b` |
| Carries | the service, the suites, the payload, and three board scripts: `perf-sweep.sh` (the width and thread sweeps), `client-wait-test.sh` (prerequisite 7) and `soak.sh` (repeated runs) — see `TESTING.md` |

Copy it to the SD card's FAT partition beside `config.txt` (`kernel=ifs-rpi4.bin`)
and the firmware files (`start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`,
`overlays/`), then verify the copy:

```bat
certutil -hashfile D:\ifs-rpi4.bin SHA256
```

The image carries the programs (`/bin/sortmed-server`, `/bin/sortmed-client`, the
suite binaries) **and** the telemetry (`/data/bin/...`), so nothing else is copied
to the card and nothing is mounted on the board.

## 2. Board: start the service

At the `#` prompt on the serial console (115200 8N1):

```sh
cd /tmp                                  # the only writable place: /tmp is /dev/shmem (RAM)
sortmed-server &
```

Expected first line:

```
sortmed-server: service 'sortmed' ready; shm=/sortmed-results (764416 bytes)
```

Wait for the region to exist — this is the clean "the server is up" signal,
because the server creates the region *before* it registers the service name:

```sh
waitfor /dev/shmem/sortmed-results
```

## 3. Board: the run

The client processes **one channel per run** — it reads a single column of the
interleaved layout — so all four columns take four runs. Each run writes four
*fixed* file names into the current directory, and `/tmp` cannot hold directories
(shmem keeps flat objects only), so give each channel its own directory on the
mounted card instead:

```sh
waitfor /dev/shmem/sortmed-results   # the server from section 2 stays up

mkdir -p /var/mnt
ls /dev/sd*                        # the card: /dev/sd0, FAT partition /dev/sd0t12
mount -t dos /dev/sd0t12 /var/mnt

for ch in voltage current temperature time; do
    mkdir -p /var/mnt/$ch
    cd /var/mnt/$ch
    sortmed-client -w 15 /data/bin/Chen2020_c1_T25_discharge.bin interleaved $ch 100
    cd /
done

sync ; umount /var/mnt
```

The server stays up across all four runs — it resolves the client's connection
per job, which is the fix carried by the image whose hash is in section 1. On an
older image, restart it before each channel (`slay sortmed-server;
sortmed-server &`): the pulse from the second run on would otherwise go to the
previous client's dead connection.

Arguments: the recording, its layout (`planar` or `interleaved`), the channel to
work on (`time`, `current`, `voltage`, `temperature`), and the sample rate — a
nominal flag the client uses only for the durations it prints. The recording's own
time column is the real one: it advances ~74.8 ms per sample, not 10 ms.

### The width to demo: `-w 15`

The `-w 15` in the loop is the measured optimum, not the build's default.
`MEDIAN_DEFAULT_WIDTH` is 9; the 2026-10-17 run above used it. The width sweep
(`tools/filter_sweep.py` over what `perf-sweep.sh` collected on the board) says 15
is the smallest width that reaches the plateau, and that the default's 9 leaves a
quarter of the injected outliers unreplaced:

| width | recall | clean samples replaced | RMSE gain | block time |
| --- | --- | --- | --- | --- |
| 9 — the default | 73.8 % | 1.42 % | 6.2x | 6.7 ms |
| 13 | 93.8 % | 0.71 % | 9.9x | 10.2 ms |
| **15** | **98.6 %** | **0.52 %** | **12.1x** | **12.2 ms** |
| 21 | 98.9 % | 0.26 % | 12.3x | 19.7 ms |
| 31 | 98.9 % | 0.11 % | 12.2x | 35.6 ms |

Voltage channel, 64 KiB blocks, means over the three repeats of each case. Past 15
every quality number is flat to within a third of a point while the time doubles
and then triples, which is why the demo uses 15. The payload's own corruption is
fully inside that reach: the generator injects bursts of at most 6 samples and
dropouts of at most 4. The default is deliberately left at 9 — changing it would
mean a new image and a new hash in section 1 — and passing `-w` is exactly what the
per-job width field is for.

**The width does not move the medians.** They are computed from the raw sorted
data, so the `pulse:` lines, `global_median` and the `cross-check:` line must
reproduce the 2026-10-17 numbers above exactly. `outliers=`, `elapsed=`,
`rolling_filter_width=` and `filtered.bin` are what change.

Expected output — one line per block, then a summary, then the write-back:

```
pulse: job=1 code=17 median=3.961808
pulse: job=2 code=18 median=3.641635
pulse: job=3 code=19 median=3.384104
pulse: job=4 code=20 median=3.566194  (global)
job=1 samples=16384 block_median=3.961808 outliers=431 elapsed=… ns
job=2 samples=16384 block_median=3.641635 outliers=382 elapsed=… ns
job=3 samples=16384 block_median=3.384104 outliers=406 elapsed=… ns
job=4 samples=10848 block_median=2.973536 outliers=283 elapsed=… ns
recording complete: samples=60000 global_median=3.56619358 rolling_filter_width=9 global_outliers=1502
cross-check: server pulse/descriptor median 3.56619358 == client 3.56619358
output written: raw.bin (240000 bytes), sorted.bin (240000 bytes), filtered.bin (240000 bytes), medians.csv (4 blocks)
```

The current build prints two more lines after `output written:` — the client's
own timing (`timing: run_ns=… submit_avg_ns=… service_avg_ns=… fetch_avg_ns=…`
and `timing: post_sort_ns=… post_filter_ns=… write_ns=…`) — and the server
prints one `perf:` line per block with its stage times, the width it used and
the run it belongs to. Those lines are what the sweeps in `TESTING.md` read; the
numbers in this file are the 2026-10-17 run's.

The `pulse:` line is printed the moment the completion pulse arrives and is
purely what the pulse carried: its code (16 + the job id) and the median decoded
from its value word — the block's median for the first three blocks, the global
one for the last. The `job=` line follows once the per-block result has been
fetched out of the shared region. The `cross-check:` line is the client agreeing
with the server: it merged and sorted the same 60 000 values itself, and the two
medians must be identical to the bit.

For the voltage channel the four block medians are the numbers `pipeline_demo`
prints (`3.9618`, `3.6416`, `3.3841`, `2.9735`): the server and the demo call the
same `median_of_sorted()` on the same sorted block. The other three channels print
their own — section 4 lists them. `outliers=` is what the rolling Hampel
filter (width 9, threshold 4 σ) rejected in that block; `elapsed=` is the server's
own processing time for it.

### If a channel is run in `/tmp`, copy it out

`/tmp` is flat — no directories — so this route is one channel at a time, and the
four files are overwritten by the next run. Copy them out before starting the next
channel; section 3's loop writes straight to the card instead.

```sh
cd /tmp
sortmed-client /data/bin/Chen2020_c1_T25_discharge.bin interleaved voltage 100

mkdir -p /var/mnt
mount -t dos /dev/sd0t12 /var/mnt      # if it is not mounted already
mkdir -p /var/mnt/voltage
cp raw.bin filtered.bin sorted.bin medians.csv /var/mnt/voltage/
sync
```

Then `shutdown`, pull the card, and plug it into the PC: each channel's four files
are in the card's FAT partition (the drive letter Windows gives it), in
`voltage/`, `current/`, `temperature/` and `time/`, next to `ifs-rpi4.bin` and
`config.txt`. If the board was switched off before the copy, the `/tmp` files are
gone and that channel has to be run again.

Notes that matter:

- **The device name**: `ls /dev/sd*` first. On the current card it answers
  `/dev/sd0 /dev/sd0t12`, and the FAT boot partition is `/dev/sd0t12`; mounting
  `t1`, which does not exist, gives `Can't mount … Invalid argument`. Use the
  partition suffix the listing shows. A card with no partition table mounts as
  `/dev/sd0`.
- **If `mount -t dos` still refuses**: start the FAT module directly —
  `/lib/dll/fs-dos.so /dev/sd0t12 /var/mnt` — and unmount with `umount /var/mnt`.
- **`sync` then `umount`** before pulling the card — that flushes the FAT. Unplugging
  while it is mounted loses the writes, or worse, damages the filesystem.
- **Mounting is safe while the system runs**: the image is loaded into RAM at boot,
  so nothing on the board is using the card.
- **Why not `/tmp`**: `/tmp` is a symlink to `/dev/shmem`, and shmem holds flat
  objects only — `mkdir` there answers `Function not implemented`. `/var/mnt`
  works because the startup mounts a 16 MB RAM filesystem on `/var`. If `/var/mnt`
  cannot be created, check `df` for another writable filesystem and put the mount
  point there.
- The files are 240 000 B each (three of them) plus a 152-byte CSV — no room
  problem on the card.

## 4. Board: verify the result

```sh
cd /var/mnt                    # or wherever the run wrote
ls -l */                       # four directories, four files each
cksum voltage/raw.bin
cksum /data/bin/channels/r1_voltage.bin
cat voltage/medians.csv
cat current/medians.csv temperature/medians.csv time/medians.csv
```

| Check | Expected |
| --- | --- |
| each channel's `raw.bin`, `sorted.bin`, `filtered.bin` | 240 000 B each (60 000 float32 samples) |
| `cksum voltage/raw.bin` vs `cksum /data/bin/channels/r1_voltage.bin` | both print `2261229843 240000` — `raw.bin` is the voltage column of the interleaved record and the planar file is the same recording, so the bytes must match (the test `test_real_layouts_agree` asserts exactly that) |
| `voltage/medians.csv` | a header plus 4 rows; the `median` column must equal the four `block_median=` values above |
| `current/medians.csv` | 4.996816, 5.00002, 5.000128, 0.003621 A — the discharge holds 5 A until the last block, where the run ends |
| `temperature/medians.csv` | 301.3247, 304.8079, 305.5219, 305.1171 K — the cell warms into the first block and plateaus |
| `time/medians.csv` | 612.9747, 1838.999, 3065.023, 4083.916 s — a ramp, so its median is simply the middle instant |
| `sorted.bin` | non-decreasing — the sort's own order is also re-checked by `test_mergesort` |

The client verifies the shared arrays itself before using them: it checks the
slot's `job_id` and state word against the result it fetched and recomputes both
FNV-1a checksums, so a mismatch would have been reported instead of written out.

## 4b. Figures from the run

On the PC, once per channel directory (the card's drive letter, or a copy of it):

```sh
cd data
for ch in voltage current temperature time; do
    uv run python ../tools/plot_run.py "D:/$ch" --channel $ch --out "../run-figures/$ch"
    uv run python ../tools/plot_vs_truth.py "D:/$ch" --out "../run-figures/$ch"
done
```

Each channel gets its own directory, eight PNGs in each — `plot_run.py` needs
`--channel` because the run files carry no channel name, while `plot_vs_truth.py`
works it out by matching `raw.bin` against the dataset.

`plot_run.py` reads what the board produced and writes four PNGs:
`run_cleaned_vs_noisy.png` — the received series as a min-max envelope with the
filtered line over it and every replaced sample marked; `run_zoom.png` — three
two-second windows where spikes are visibly cut; `run_sorted_proof.png` — as
received against sorted, with the non-decreasing and same-values checks printed on
it; `run_summary.png` — the per-block medians and when samples were replaced. A
missing `sorted.bin` or `medians.csv` just drops its panel.

`plot_vs_truth.py` compares the same directory against the dataset's ground truth —
the clean signal and the injected outliers: `truth_full.png`, `truth_zooms.png`,
`truth_error.png` (error distribution and error over time, before and after the
filter) and `truth_columns.png`, the block medians of all four columns of that
recording. It identifies the run and the channel by matching `raw.bin` against the
dataset, so it takes no channel argument, and it prints the matched run, the error
metrics and the per-column median table. Its medians use the service's own
definition (`median_of_sorted`: the mean of the two middle values for an
even-sized block), which is why `truth_columns.png` reproduces `medians.csv` to
the last digit.

What those figures showed for this run:

| Channel | RMSE gain (before → after) | Samples closer to the truth | Samples changed |
| --- | --- | --- | --- |
| voltage | 6.17× (23.99 → 3.89 mV; p99 43.96 → 5.28 mV) | 1464 of 60 000 | 1502 |
| current | 10.79× | 1514 of 60 000 | 1548 |
| temperature | 2.00× | 1458 of 60 000 | 1488 |
| time | already exact — 0 error before and after | 0 | 0 |

`raw.bin` for every channel is the dataset's column bit for bit, and `sorted.bin`
is an exact permutation of it, so the only samples that move are the replaced ones.
`time` is the control: nothing was injected there and the filter left it
byte-identical.

## 5. Board: the module suites (optional)

Run from `/tmp` as well; the payload argument is the `/data` directory. The first
two need no payload at all.

```sh
test_shared_memory        # the region's bounds and layout
test_kpi                  # sort scaling, filter cost, global merge (the KPIs)
test_median               # the rolling filter
test_mergesort /data      # the parallel sort
test_sensor /data         # the virtual sensor
test_block_loader /data   # the loader
test_protocol             # the message definition
```

| Binary | Host result | Seen on the board so far |
| --- | --- | --- |
| `test_sensor /data` | 642 checks, 0 failed, 0 skipped | 642, 0 failed, 0 skipped (this image) |
| `test_protocol` | 186 checks, 0 failed, 0 skipped | 186, 0 failed, 0 skipped (this image); 82 with the earlier one |
| `test_block_loader /data` | 116 checks, 0 failed, 0 skipped | 116, 0 failed, 0 skipped (this image) |
| `test_mergesort /data` | 578 checks, 0 failed, 0 skipped | 578, 0 failed, 0 skipped (this image) |
| `test_median` | 7615 checks, 0 failed, 0 skipped | 7615, 0 failed, 0 skipped (this image) |
| `test_shared_memory` | 13 checks, 0 failures | 13, 0 failures (this image) |
| `test_kpi` | 79 checks, 0 failures; sort 0.872 / 0.515 / 0.334 ms per block at 1 / 2 / 4 workers (2.61×), filter 0.59 ms, global merge 0.53 ms | 79, 0 failures (this image); sort 3.010 / 1.631 / 1.011 ms (2.98×), filter 3.81 ms clean and 3.84 ms spiked, global merge 1.408 ms for 65536 samples |

`0 failed` **and** `0 skipped` together mean the suite ran *and* found its payload.

Every suite the host runs now has a board counterpart from the same image, and
the counts agree exactly: 9 150 checks on the host (642 sensor + 186 protocol +
116 loader + 578 mergesort + 7 615 median + 13 shared memory) against the same
9 150 on the board, plus `test_kpi`'s 79 in both places.

The board's KPI times came out about a third slower than the ones recorded in
`IMPLEMENTATION.md` §12 — 3.010 against 2.278 ms per block at one worker, with
the scaling unchanged (2.98× against 3.01×). The only source change since those
numbers was the identification comment, so that gap is the board's clock or
thermal state rather than the code; the `perf:` stage lines the service now
prints are the ones to quote for the report.

## 6. If something goes wrong

| Symptom | Cause, and what to do |
| --- | --- |
| The output files are missing after a power cycle | They were written into `/tmp`, which is memory on this image. Section 3 runs the four channels in `/var/mnt/<channel>` so they land on the card; if a channel went to `/tmp`, copy it out before the next one |
| The second channel hangs after `job=4`, nothing after it | The image predates the per-job client resolution (section 1): its pulse goes to the previous client's dead connection. Restart the server before each channel, or flash the current image |
| `mount: Can't mount /var/mnt (type dos)` | The device name is wrong. Use the `t…` partition that `ls /dev/sd*` shows (`/dev/sd0t12` on the current card); if it still refuses, start the module directly: `/lib/dll/fs-dos.so /dev/sd0t12 /var/mnt` |
| `mkdir: '/tmp/mnt': Function not implemented` | `/tmp` is `/dev/shmem`, and shmem has no directories. Create the mount point under `/var` instead — section 3 uses `/var/mnt` |
| `client: no server named 'sortmed' after 10.0 s (N attempts)` | Nothing answered `name_open` for ten seconds: the server is not running. The client waits by itself, so starting it (section 2) within those ten seconds carries the same run on; if the client has already given up, start the server and re-run it |
| `client: submit … rejected: …` | The server refused the block: `checksum` = it arrived damaged, `version` = the two sides disagree, `busy` = all four result slots are taken, `argument` = malformed header |
| `client: shared-memory checksum mismatch` | The arrays changed between the server writing them and the client reading them — treat the run as invalid and run it again |
| `perror` on `raw.bin` / `sorted.bin` / … | The current directory is not writable. `cd /tmp` and re-run |
| Either program faults immediately | The 64 KiB `proto_block_t` on the stack. Heap-allocate that block in the program, or link with a larger stack |
| Nothing under `/dev/shmem` | The server never created the region — check its ready line; it also unlinks and recreates a stale region at startup, so an old object cannot be read by mistake |
| `client: recording exceeds 60000 samples` | The client's buffers are sized for 60 000 samples; a longer recording is refused rather than truncated |

## 7. What this run proves

- **The synchronous path**: `name_open` → `MsgSendv` of `{header, 64 KiB block}`
  (the two-part IOV) → validation → ack.
- **The asynchronous path**: the pulse carries the calculated median — the job
  id in its code (`PROTO_PULSE_DONE + job_id`) and the float's 32 bits in its
  value word, the global median once the recording's last block is done — and
  the client fetches the per-block result afterwards.
- **The memory-offset descriptor in the region**: behind the slots, at
  `PROTO_RUN_BASE`, the server publishes the run descriptor — the global median,
  the byte offset of the globally sorted recording, its length and its checksum.
  The client verifies the magic, the array checksum, and that the pulse and the
  descriptor carry the same median, then compares that median with the one it
  computed itself.
- **Shared memory**: the sorted and filtered arrays read back out of
  `/dev/shmem/sortmed-results` and checksum-verified, with per-block descriptors
  (offsets, length, checksums) instead of bulk data in messages.
- **The rolling filter**: outliers replaced and counted, per block and then over
  the whole recording so the block seams have context.
- **Write-back**: the completed run on disk — raw, sorted, filtered and the
  per-block medians.
- **All four channels**, one run each, with no server restart in between; the fix
  that made that possible is that the server resolves the client's connection per
  job instead of caching the first one.
- **The filter's effect, measured**: 6.17× / 10.79× / 2.00× RMSE gains against the
  dataset's clean signal on voltage / current / temperature, and a clean channel
  left byte-identical (section 4b).
- **The thread picture**: four sort workers created once per process, one server
  dispatcher, one client thread, and a single-threaded rolling filter — about six
  threads of ours in flight on four cores.

---

## 8. Stop the service

```sh
slay sortmed-server
```

On a clean exit the server unlinks the shared region; if it is killed hard the
region stays behind, and the next server unlinks and recreates it at startup — no
manual cleanup is needed either way.
