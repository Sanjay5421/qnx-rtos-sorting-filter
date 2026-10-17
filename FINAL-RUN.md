# Final run — the integrated service on the Raspberry Pi

The end-to-end run of the finished M3/M4 path: the client offloads the production
recording in 64 KiB blocks, the server sorts and filters each one, taps the client
with a pulse, and the client collects the median and the arrays out of the shared
region — then writes the completed run back to disk.

**Status — executed on hardware, 2026-10-17.** The service ran end to end on the
board: four blocks accepted, four completions, four results read out of the shared
region and checksum-verified, and the write-back line printed. The four block
medians were `3.961808`, `3.641635`, `3.384104`, `2.973536` — exactly the numbers
`pipeline_demo` produces — with `431 / 382 / 406 / 283` outliers per block, a global
median of `3.56619358` and `1502` outliers over the recording. The server measured
`7.51 / 7.32 / 7.30 / 5.01 ms` per block for sort + filter + checksums (the sort
itself is 0.757 ms of that; the rolling filter dominates).

One thing to know before relying on the outputs: they are written into `/tmp`,
which on this image is `/dev/shmem` — **RAM** — so they do not survive a power-off.
Section 3 shows how to keep them on the SD card. Section 6 lists what to look at
if the run misbehaves.

| Step | Where | What happens |
| --- | --- | --- |
| 1 | host | verify the image, copy it to the card |
| 2 | board | start `sortmed-server` |
| 3 | board | run `sortmed-client` over the recording |
| 4 | board | check the four files it wrote |
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
| Size | 37 742 192 B |
| SHA-256 | `57637557e9a8565974714802f412f074767bc1d3d5fab1c2779437d045588c49` |

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
sortmed-server: service 'sortmed' ready; shm=/sortmed-results (524376 bytes)
```

Wait for the region to exist — this is the clean "the server is up" signal,
because the server creates the region *before* it registers the service name:

```sh
waitfor /dev/shmem/sortmed-results
```

## 3. Board: the run

```sh
sortmed-client /data/bin/Chen2020_c1_T25_discharge.bin interleaved voltage 100
```

Arguments: the recording, its layout (`planar` or `interleaved`), the channel to
work on (`time`, `current`, `voltage`, `temperature`), and the sample rate.

Expected output — one line per block, then a summary, then the write-back:

```
job=1 samples=16384 block_median=3.9618 outliers=… elapsed=… ns
job=2 samples=16384 block_median=3.6416 outliers=… elapsed=… ns
job=3 samples=16384 block_median=3.3841 outliers=… elapsed=… ns
job=4 samples=10848 block_median=2.9735 outliers=… elapsed=… ns
recording complete: samples=60000 global_median=… rolling_filter_width=9 global_outliers=…
output written: raw.bin (240000 bytes), sorted.bin (240000 bytes), filtered.bin (240000 bytes), medians.csv (4 blocks)
```

The four block medians are the same numbers `pipeline_demo` prints (`3.9618`,
`3.6416`, `3.3841`, `2.9735`) — the server's `median_of_sorted()` and the demo read
the same element of the same sorted block. `outliers=` is what the rolling Hampel
filter (width 9, threshold 4 σ) rejected in that block; `elapsed=` is the server's
own processing time for it.

### Getting the files onto your PC: `/tmp` is RAM

On this image `/tmp` is `/dev/shmem` — memory. The four files therefore live only
until the board loses power, and the SD card's FAT partition is the only road to
your PC (the console is the interface, and ssh is not usable in this image).

**If the board is still running**, the files are still there. Mount the card and
copy them:

```sh
mkdir -p /var/mnt                  # /var is a real filesystem; /tmp cannot hold directories
ls /dev/sd*                        # the card: /dev/sd0, FAT partition /dev/sd0t12
mount -t dos /dev/sd0t12 /var/mnt  # use the t... the listing actually showed
cp raw.bin filtered.bin sorted.bin medians.csv /var/mnt/
sync
umount /var/mnt
```

Then `shutdown`, pull the card, and plug it into the PC: the four files are in the
card's FAT partition (the drive letter Windows gives it), next to
`ifs-rpi4.bin` and `config.txt`.

**If the board was already switched off**, the files are gone and the run has to be
repeated. Run the client *from the mounted card* so it writes straight there:

```sh
cd /tmp
sortmed-server &
waitfor /dev/shmem/sortmed-results
mkdir -p /var/mnt
mount -t dos /dev/sd0t12 /var/mnt
cd /var/mnt
sortmed-client /data/bin/Chen2020_c1_T25_discharge.bin interleaved voltage 100
sync ; cd / ; umount /var/mnt
```

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
ls -l raw.bin sorted.bin filtered.bin medians.csv
cksum raw.bin
cksum /data/bin/channels/r1_voltage.bin
cat medians.csv
```

| Check | Expected |
| --- | --- |
| `raw.bin`, `sorted.bin`, `filtered.bin` | 240 000 B each (60 000 float32 samples) |
| `cksum raw.bin` vs `cksum /data/bin/channels/r1_voltage.bin` | both print `2261229843 240000` — `raw.bin` is the voltage column of the interleaved record and the planar file is the same recording, so the bytes must match (the test `test_real_layouts_agree` asserts exactly that) |
| `medians.csv` | a header plus 4 rows; the `median` column must equal the four `block_median=` values above |
| `sorted.bin` | non-decreasing — the sort's own order is also re-checked by `test_mergesort` |

The client verifies the shared arrays itself before using them: it checks the
slot's `job_id` and state word against the result it fetched and recomputes both
FNV-1a checksums, so a mismatch would have been reported instead of written out.

## 4b. Figures from the run

On the PC, with the four files in one directory (the card's drive letter, or a copy
of it):

```sh
cd data
uv run python ../tools/plot_run.py D:\
```

Writes three PNGs (150 dpi) into that directory: `run_cleaned_vs_noisy.png` — the
noisy and cleaned series over the whole 600 s with every removed sample marked;
`run_zoom.png` — a few seconds where a spike is visibly cut; `run_summary.png` —
the sorted curve with the median, the per-block medians, and the removals over
time. A missing `sorted.bin` or `medians.csv` just drops its panel. It prints the
sample count, the duration, how many samples differ between raw and filtered, and
the medians it read.

## 5. Board: the module suites (optional)

Run from `/tmp` as well; the payload argument is the `/data` directory. The first
two need no payload at all.

```sh
test_shared_memory        # the region's bounds and layout
test_median               # the rolling filter
test_mergesort /data      # the parallel sort
test_sensor /data         # the virtual sensor
test_block_loader /data   # the loader
test_protocol             # the message definition
```

| Binary | Host result | Seen on the board so far |
| --- | --- | --- |
| `test_sensor /data` | 642 checks, 0 failed, 0 skipped | reached `test_real_block_partition`, tail not captured |
| `test_protocol` | 137 checks, 0 failed, 0 skipped | 82 with the earlier image (before the shared-region tests) |
| `test_block_loader /data` | 116 checks, 0 failed, 0 skipped | 116, 0 failed, 0 skipped |
| `test_mergesort /data` | 578 checks, 0 failed, 0 skipped | 578, 0 failed, 0 skipped |
| `test_median` | 7551 checks, 0 failed, 0 skipped | not yet |
| `test_shared_memory` | 13 checks, 0 failures | not yet |

`0 failed` **and** `0 skipped` together mean the suite ran *and* found its payload.

## 6. If something goes wrong

| Symptom | Cause, and what to do |
| --- | --- |
| The output files are missing after a power cycle | They were written into `/tmp`, which is memory on this image. Section 3 mounts the SD card and copies them (or runs the client there) |
| `mount: Can't mount /var/mnt (type dos)` | The device name is wrong. Use the `t…` partition that `ls /dev/sd*` shows (`/dev/sd0t12` on the current card); if it still refuses, start the module directly: `/lib/dll/fs-dos.so /dev/sd0t12 /var/mnt` |
| `mkdir: '/tmp/mnt': Function not implemented` | `/tmp` is `/dev/shmem`, and shmem has no directories. Create the mount point under `/var` instead — section 3 uses `/var/mnt` |
| `client: name_open server (start server first)` | The server was not up yet. Start it (section 2) and re-run the client — it makes a single connection attempt, it does not retry |
| `client: submit … rejected: …` | The server refused the block: `checksum` = it arrived damaged, `version` = the two sides disagree, `busy` = all four result slots are taken, `argument` = malformed header |
| `client: shared-memory checksum mismatch` | The arrays changed between the server writing them and the client reading them — treat the run as invalid and run it again |
| `perror` on `raw.bin` / `sorted.bin` / … | The current directory is not writable. `cd /tmp` and re-run |
| Either program faults immediately | The 64 KiB `proto_block_t` on the stack. Heap-allocate that block in the program, or link with a larger stack |
| Nothing under `/dev/shmem` | The server never created the region — check its ready line; it also unlinks and recreates a stale region at startup, so an old object cannot be read by mistake |
| `client: recording exceeds 60000 samples` | The client's buffers are sized for 60 000 samples; a longer recording is refused rather than truncated |

## 7. What this run proves

- **The synchronous path**: `name_open` → `MsgSendv` of `{header, 64 KiB block}`
  (the two-part IOV) → validation → ack.
- **The asynchronous path**: the server pulsing code 16 with the job id, and the
  client fetching the result afterwards.
- **Shared memory**: the sorted and filtered arrays read back out of
  `/dev/shmem/sortmed-results` and checksum-verified, with per-block descriptors
  (offsets, length, checksums) instead of bulk data in messages.
- **The rolling filter**: outliers replaced and counted, per block and then over
  the whole recording so the block seams have context.
- **Write-back**: the completed run on disk — raw, sorted, filtered and the
  per-block medians.

---

## 8. Stop the service

```sh
slay sortmed-server
```

On a clean exit the server unlinks the shared region; if it is killed hard the
region stays behind, and the next server unlinks and recreates it at startup — no
manual cleanup is needed either way.
