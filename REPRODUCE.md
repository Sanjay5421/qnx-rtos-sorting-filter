# Reproduce

Every command needed to build this project's QNX-side code and run its tests.
Commands only — the explanation is in [IMPLEMENTATION.md](IMPLEMENTATION.md).

Markers: **[verified]** = run on this machine and produced the stated result.
**[planned]** = not run yet.

---

## 0. Where things are

| What | Path |
| --- | --- |
| QNX SDP 8.0 | `P:\qnx\qnx800` |
| host tools (`qcc`, `mkifs`, `mkqnximage`) | `P:\qnx\qnx800\host\win64\x86_64\usr\bin` and `P:\qnx\qnx800\host\common\bin` |
| target libraries | `P:\qnx\qnx800\target\qnx` |
| Momentics IDE | `P:\qnx\MyQNX\QNX\qnxmomenticside` |
| RPi4 BSP archive | `P:\qnx\qnx800\bsp\BSP_raspberrypi-bcm2711-rpi4_be-800_SVN1024006_JBN484.zip` |
| this project | `RT-MOMS/` |

Installed packages the image build needs, all present:
`com.qnx.qnx800.target.microkernel.core`, `.net.openSSH`, `.pci.hw.bcm2711`,
`.usb.hcd.xhci.bcm2711`, plus the `raspberrypi_bcm2711_rpi4` and `x86_64` BSPs.

---

## 1. Host: build and run every test **[verified]**

Needs only a C99 compiler and `mingw32-make` (or `make`). No QNX required.

```sh
cd RT-MOMS/tests
mingw32-make check
```

Expected tail of each block, and exit status 0 only if nothing failed:

```
=== test_sensor            642 checks, 0 failed, 0 skipped
=== test_protocol           82 checks, 0 failed, 0 skipped
=== test_block_loader      116 checks, 0 failed, 0 skipped
=== test_mergesort         578 checks, 0 failed, 0 skipped
```

`test_mergesort` and `pipeline_demo` link `../app/mergesort.c`, which needs
`<pthread.h>`. MinGW.org's gcc on this host has no such header, so the Makefile
probes for a thread-capable compiler and picks the MSYS2 one
(`C:/msys64/mingw64/bin/gcc.exe`); on QNX and Linux the probe answers `$(CC)`
itself and nothing needs setting. `mingw32-make check THREADCC=…` overrides the
choice.

---

## 2. Host: build without running **[verified]**

```sh
cd RT-MOMS/tests
mingw32-make all
```

Produces `test_sensor.exe test_protocol.exe test_block_loader.exe
test_mergesort.exe sensor_demo.exe loader_demo.exe pipeline_demo.exe`.

---

## 3. Host: one suite at a time **[verified]**

```sh
cd RT-MOMS/tests
./test_sensor.exe        ../../data/artifacts/dataset
./test_protocol.exe      # needs no payload
./test_block_loader.exe  ../../data/artifacts/dataset
./test_mergesort.exe     ../../data/artifacts/dataset
```

The payload argument is optional. Without it, the tests look in
`../../data/artifacts/dataset`, `../data/artifacts/dataset` then
`data/artifacts/dataset`, then skip the payload-dependent tests instead of
failing. `BATTERY_DATASET_DIR` works too.

---

## 4. The payload **[verified]**

Git-ignored, regenerated on demand (about 20 s, needs `uv`):

```sh
cd data
uv run python -m battery_dataset
```

Without it, section 1 reports `0 skipped` only because the payload is present —
with it missing you should see the payload tests skipped and still exit 0.

---

## 5. Demos **[verified]**

```sh
cd RT-MOMS/tests
mingw32-make demo          # the 64 KiB block stream + server-side validation
mingw32-make demo-sensor   # live sample-by-sample trace at 10 Hz
mingw32-make demo-pipeline # sort the recording in its 64 KiB blocks
```

Expected from `make demo`: four blocks, sequences 0–3, samples
16384/16384/16384/10848, `accepted : 4 of 4`.

`make demo-pipeline` sorts each block with four workers and prints per block the
min, the median, the max, the sort time and the throughput. On this host, over
`r1_voltage.bin` (60 000 samples):

```
  blocks        : 4 (3 full 64 KiB, 1 short)
  samples sorted: 60000
  sort time     : 1.753 ms total, 0.438 ms per block
  throughput    : 130.55 MiB/s, 34223135 samples/s
  last block    : 4 partitions, 2 merge phases, 7 tasks, 4 workers
  order checked : 4 of 4 blocks sorted
```

The medians fall block by block (3.96, 3.64, 3.38, 2.97 V) because the recording
is a discharge and the sort makes that monotonic; `order checked` and the
`ok` column are the demo re-verifying the sort on real payload. The times are
this machine's, not the Pi's.

---

## 6. Without make **[verified]**

```sh
cd RT-MOMS/tests
gcc -std=c99 -Wall -Wextra -O2 -g -o test_protocol test_protocol.c \
    ../app/sensor.c ../app/protocol.c ../app/block_loader.c
./test_protocol
```

The two that sort add `../app/mergesort.c` to that list and need a compiler with
`<pthread.h>` — on this host MinGW.org's `gcc` is not one, so:

```sh
C:/msys64/mingw64/bin/gcc.exe -std=c99 -Wall -Wextra -O2 -g \
    -o test_mergesort test_mergesort.c ../app/sensor.c ../app/protocol.c \
    ../app/block_loader.c ../app/mergesort.c
./test_mergesort
```

---

## 7. Cross-compile for the Raspberry Pi, `aarch64le` **[verified]**

Git Bash / MSYS2:

```sh
export QNX_HOST="P:/qnx/qnx800/host/win64/x86_64"
export QNX_TARGET="P:/qnx/qnx800/target/qnx"
QCC="$QNX_HOST/usr/bin/qcc.exe"
SRC="../app/sensor.c ../app/protocol.c ../app/block_loader.c ../app/mergesort.c"

cd RT-MOMS/tests
for f in test_sensor test_protocol test_block_loader test_mergesort \
         loader_demo sensor_demo pipeline_demo; do
    "$QCC" -Vgcc_ntoaarch64le -std=c99 -D_QNX_SOURCE -Wall -Wextra -O2 \
        -o "$f.arm" "$f.c" $SRC
done
```

All seven compile with no warnings. `../app/mergesort.c` needs POSIX threads,
which QNX's C library provides — the three binaries that call it link against
`libc.so.6` alone, the same as the others (`-d` shows no extra `NEEDED` entry).

`cmd.exe`:

```bat
P:\qnx\qnx800\qnxsdp-env.bat
cd RT-MOMS\tests
qcc -Vgcc_ntoaarch64le -std=c99 -D_QNX_SOURCE -Wall -Wextra -O2 -o test_protocol.arm ^
    test_protocol.c ..\app\sensor.c ..\app\protocol.c ..\app\block_loader.c
```

**`-D_QNX_SOURCE` is required with `-std=c99`.** QNX hides POSIX declarations
under a strict ISO C standard; without it the build stops at
`error: 'CLOCK_MONOTONIC' undeclared` and `implicit declaration of function
'nanosleep'`. `-std=gnu99` also works, because it defines `_QNX_SOURCE` for you.

Confirm the result is an AArch64 binary:

```sh
"$QNX_HOST/usr/bin/ntoaarch64-readelf.exe" -h test_protocol.arm | head -6
#   Class:   ELF64        Machine:  AArch64
```

---

## 8. Run on the target, over the UART console **[verified on hardware]**

The board boots the image from section 9 and is driven from the serial console
at 115200 8N1. Nothing is copied to the board and nothing is mounted: the
programs and their payload are inside the image.

### On the SD card's FAT partition

| File | Notes |
| --- | --- |
| `ifs-rpi4.bin` | **37 610 936 B**, SHA-256 `b71677d514c186e2fa055def8c8e227280d45b7e3aa994f6f1c5239c6f4b54d2`. The stock image is 34 489 984 B and contains none of the programs below. |
| `config.txt` | the BSP guide's `[rpi4]` block; `kernel=ifs-rpi4.bin` is what boots this image |
| firmware | `start4.elf`, `fixup4.dat`, `bcm2711-rpi-4-b.dtb`, `overlays/` |

### A good boot looks like this

```

Loading IFS...done
...
Welcome to QNX 8.0.0 on RaspberryPi4B !

Starting wdtkick ...
Starting I2C driver ...
Starting PCI Server ...
Starting serial driver (/dev/ser1)
Starting SPI master driver ...
Starting SDMMC driver (/dev/sd0)
Starting USB xHCI controller in the host mode (/dev/usb/*)...
Starting devf-ram filesystem ...
Starting networking ...
Starting DHCP client ...
Starting SSH daemon ...
Starting devc-pty manager ...
Starting qconn daemon ...
Starting shell ...
#
```

The `cpu0` … `cpu3` blocks above the banner are `procnto-smp-instr` bringing up
all four Cortex-A72 cores. The shell prompt is the thing that says the boot
succeeded.

### The sequence, once you have the prompt

```sh
uname -a                    # QNX 8.0.0 ... RaspberryPi4B aarch64le
cd /tmp                     # the test suites write their temporary files here

test_protocol               # the protocol layer
test_sensor /data           # the virtual sensor
test_block_loader /data     # the block loader
test_mergesort /data        # the parallel merge sort

cksum /data/bin/channels/r1_voltage.bin
cksum /data/bin/Chen2020_c1_T25_discharge.bin
loader_demo /data/bin/channels/r1_voltage.bin planar voltage 100
loader_demo /data/bin/Chen2020_c1_T25_discharge.bin interleaved voltage 100
sensor_demo /data/bin/channels/r1_voltage.bin 10 0 1 20
pipeline_demo /data/bin/channels/r1_voltage.bin planar voltage 100 4
```

`/data` is the dataset directory. The two payload files are at
`/data/bin/channels/r1_voltage.bin` and
`/data/bin/Chen2020_c1_T25_discharge.bin`.

Pass it explicitly on the target: the relative fallbacks the payload tests use
on the host do not resolve from `/tmp`, so `test_mergesort` without `/data` runs
its eleven unit tests and skips the two payload ones — visible as `535 checks, 0
failed, 1 skipped` instead of `578 checks, 0 failed, 0 skipped`.

### What each must print, against the host

| Command | Target | Host |
| --- | --- | --- |
| `test_protocol` | `82 checks, 0 failed, 0 skipped` | same |
| `test_sensor /data` | `642 checks, 0 failed, 0 skipped` | same |
| `test_block_loader /data` | `116 checks, 0 failed, 0 skipped` | same |
| `test_mergesort /data` | `578 checks, 0 failed, 0 skipped` | same |
| `cksum` of `r1_voltage.bin` | `2261229843 240000` | same |
| `cksum` of the interleaved record | `530587124 960000` | same |
| `loader_demo`, both layouts | `0xe90d24d2`, `0xc98cd3b2`, `0x14c728bb`, `0x362ab592`; `3 full + 10848`; `accepted : 4 of 4` | same |
| `sensor_demo` | values `4.19853` … `4.20414`; `min / max / mean 2.59783 / 4.27399 / 3.53497` | same |
| `pipeline_demo`, 4 workers | medians `3.9618 3.6416 3.3841 2.9735`; `4 partitions, 2 merge phases, 7 tasks`; `4 of 4 blocks sorted`; **0.757 ms per block, 75.55 MiB/s** | same medians, ranges, plan and order; 0.25–0.44 ms per block, 130–230 MiB/s |

The sort columns to compare across machines are the *values*, not the times:
`is_sorted()` runs on the aarch64 output here, `test_mergesort` compares against
`qsort()` on both, and the plan is fixed by `n` and the thread count, so
`4 partitions, 2 merge phases, 7 tasks` must read the same on the Pi.

### The thread sweep, measured on the board

Same file, the worker count as the last argument. The plan line changes shape
with it, which is the proof that the parallel path — not just a faster serial
one — is doing the work:

| Workers | Plan the run reports | Per 64 KiB block | Throughput | Task plan |
| --- | --- | --- | --- | --- |
| 1 | `1 partitions, 0 merge phases, 1 tasks` | 2.278 ms | 25.12 MiB/s | one partition, nothing to merge |
| 2 | `2 partitions, 1 merge phases, 3 tasks` | 1.253 ms | 45.66 MiB/s | 2 + 1 |
| 4 | `4 partitions, 2 merge phases, 7 tasks` | 0.757 ms | 75.55 MiB/s | 4 + 2 + 1 |

1 → 4 workers is **3.01×** on the board (9.110 ms → 3.029 ms for the whole
60 000-sample recording), and one 64 KiB block is 163.84 s of the 100 Hz signal,
so the sort finishes 163.84 s of samples in 0.757 ms — about 216 000 times real
time. The host's own numbers move between runs (0.74–1.06 ms per block at one
worker, 0.25–0.44 ms at four); the board's were taken in one session, one run
each.

A second board session put the interleaved record through the same command and
got 0.744 ms per block (76.91 MiB/s) with the same medians and ranges — the planar
figure again, to within run-to-run noise, which is the cross-check that the two
on-disk layouts reach the same signal.

The checksums are FNV-1a over 16384 float32 values each, computed from the file
bytes, so matching the host means the whole path — file I/O, the channel offset
and stride, float handling and the checksum — is bit-identical on aarch64. The
`cksum` values are POSIX CRCs of the payload files and owe nothing to our code:
they show the data inside the image is the original file.

Confirmed on hardware: `test_protocol` (82 / 0 failed / 0 skipped),
`test_block_loader /data` (116 / 0 / 0), `test_mergesort /data` (578 / 0 / 0, the
payload tests included), both `cksum` values, `sensor_demo`, `loader_demo` in both
layouts, and `pipeline_demo` at 1, 2 and 4 workers on both layouts — medians,
ranges, plan lines and order verdicts identical to the host, and the planar and
interleaved routes producing the same four checksums.

Still to capture on the board: `test_sensor /data` past `test_real_block_partition`
— the two tests after it and the `642 checks, 0 failed, 0 skipped` line. An earlier
run of it was cut short when the serial session closed mid-stream, so that is
missing output rather than a failure: everything up to the cut matched the host.

### Three lines that need reading correctly

- **`sensor: …` during `test_sensor`** is expected output. That test feeds bad
  input on purpose — NULL path, missing file, a 3-byte file, a 6-float "record" —
  and every diagnostic is immediately followed by `ok`.
- **`/usr/libexec/sshd-auth does not exist or is not executable`** at boot comes
  from the BSP's own `ssh-server.sh`, so ssh is unusable in this image. That
  costs nothing here because the console is the interface, and it is not caused
  by anything under `app/`.
- **`Path=0 - bcm2711 … SDMMC`** is the SD card enumerating; expected.

### If a command is not found

Then the image on the card is not the one from section 9 — check the
37 610 936-byte size first, before looking at anything else.

---

## 9. Build the boot image **[verified]**

The BSP builds end to end with this SDP. Measured in a scratch copy, so the
repository stayed clean:

| Step | How | Result |
| --- | --- | --- |
| Compile the BSP sources | `make` | exit 0, 83 s |
| Build the image | `make images` | exit 0, 7 s, `ifs-rpi4.bin` = 36 197 292 B |

```bat
cd RT-MOMS
P:\qnx\qnx800\qnxsdp-env.bat
make
```

`make` runs `install` (copy `prebuilt/`, then build `src/` with `qcc`), `links`,
then `images` (`mkifs`). If the `images` step says "nothing to be done", the
copied image is newer than `rpi4.build` — delete `images/ifs-rpi4.bin` or touch
the build file to force a rebuild.

Two things to know before running it inside the repository:

- It writes hundreds of files into `RT-MOMS/install/` and object files
  throughout `RT-MOMS/src/`, and it **overwrites the committed**
  `RT-MOMS/images/ifs-rpi4.bin`. Restore that with
  `git checkout -- RT-MOMS/images/ifs-rpi4.bin`, and consider ignoring
  `RT-MOMS/install/`.
- The image it produces is *not* byte-identical to the shipped one
  (36 197 292 B against 34 489 984 B): it resolves its components from this SDP
  installation rather than from the environment QNX built the shipped image in.
  Both boot the same way.

`Templates_build_sdp800/template.mk` turns out not to be needed — the `-include`
is optional, so `$(MKIFSFLAGS)` simply expands to nothing.

**The project programs are wired in** (`7002f6f`): `app/Makefile` builds them
for aarch64le and installs them into `install/aarch64le/bin`, the top-level
`make install` runs it, and `images/rpi4.build` places them in `/bin` with the
payload in `/data`. One `make` therefore produces an image that contains them:

| In the image | Size |
| --- | --- |
| `/bin/test_protocol`, `/bin/test_sensor`, `/bin/test_block_loader` | 33 KB, 46 KB, 38 KB |
| `/bin/test_mergesort` | 42 KB |
| `/bin/sensor_demo`, `/bin/loader_demo`, `/bin/pipeline_demo` | 29 KB each |
| `/data/bin/channels/r1_voltage.bin` | 240 000 B |
| `/data/bin/Chen2020_c1_T25_discharge.bin` | 960 000 B |

The image grew from 36 197 292 B (the BSP alone) to 37 610 936 B; the sort and
its two binaries account for 86 148 B of that. `dumpifs images/ifs-rpi4.bin`
lists them back out of the built image, which is the check that they really are
inside it. Copy it to the SD card's FAT partition with the firmware files and a
`config.txt` containing `kernel=ifs-rpi4.bin`.

Then, on the target console and with nothing to mount:

```sh
test_protocol                 # expected:  82 checks, 0 failed, 0 skipped
test_sensor /data             # expected: 642 checks, 0 failed, 0 skipped
test_block_loader /data       # expected: 116 checks, 0 failed, 0 skipped
test_mergesort /data          # expected: 578 checks, 0 failed, 0 skipped
```

`0 failed` **and** `0 skipped` together mean the programs ran *and* found their
payload. A non-zero `skipped` means the image carries no data in `/data` — a
packaging problem, not a code failure. The payload entries in `rpi4.build` are
optional for exactly that reason, so a checkout without the generated data still
builds.

---

## 10. A QNX virtual machine, if wanted **[verified available, not used]**

Everything needed is installed: `mkqnximage` (`host/common/bin/mkqnximage.bat`),
the x86_64 BSP package, and the VMware Workstation installer already downloaded
at `P:\qnx\downloads\VMware-Workstation-Full-26H1u1-25688693.exe`.

```bat
mkdir D:\qnx\vms\dev1
cd D:\qnx\vms\dev1
P:\qnx\qnx800\qnxsdp-env.bat
mkqnximage --type=vmware --hostname=qnx-dev --build
mkqnximage --run
```

Remember: a VMware guest on Windows is x86-64 only, so that is a second target
for logic testing — never the aarch64 or the real-time acceptance target.
