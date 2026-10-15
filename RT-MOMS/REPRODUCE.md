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
```

---

## 2. Host: build without running **[verified]**

```sh
cd RT-MOMS/tests
mingw32-make all
```

Produces `test_sensor.exe test_protocol.exe test_block_loader.exe
sensor_demo.exe loader_demo.exe`.

---

## 3. Host: one suite at a time **[verified]**

```sh
cd RT-MOMS/tests
./test_sensor.exe        ../../data/artifacts/dataset
./test_protocol.exe      # needs no payload
./test_block_loader.exe  ../../data/artifacts/dataset
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
```

Expected from `make demo`: four blocks, sequences 0–3, samples
16384/16384/16384/10848, `accepted : 4 of 4`.

---

## 6. Without make **[verified]**

```sh
cd RT-MOMS/tests
gcc -std=c99 -Wall -Wextra -O2 -g -o test_protocol test_protocol.c \
    ../app/sensor.c ../app/protocol.c ../app/block_loader.c
./test_protocol
```

---

## 7. Cross-compile for the Raspberry Pi, `aarch64le` **[verified]**

Git Bash / MSYS2:

```sh
export QNX_HOST="P:/qnx/qnx800/host/win64/x86_64"
export QNX_TARGET="P:/qnx/qnx800/target/qnx"
QCC="$QNX_HOST/usr/bin/qcc.exe"

cd RT-MOMS/tests
for f in test_sensor test_protocol test_block_loader loader_demo sensor_demo; do
    "$QCC" -Vgcc_ntoaarch64le -std=c99 -D_QNX_SOURCE -Wall -Wextra -O2 -o "$f.arm" \
        $f.c ../app/sensor.c ../app/protocol.c ../app/block_loader.c
done
```

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

## 8. Run on the target **[planned]**

The stock image boots to a shell on the serial console at 115200 8N1
(mini-UART). To get a cross-compiled binary onto the board without rebuilding
the image, the image runs `dhcpcd` and `sshd`, so copy it over the network:

```sh
scp test_block_loader.arm root@<pi-ip>:/dev/shmem/
```

then on the Pi console:

```sh
cd /dev/shmem && ./test_block_loader.arm /path/to/payload
```

Watch out: `/tmp` and `/dev/shmem` are RAM, so it disappears on reboot.

---

## 9. Build and burn the boot image **[planned — our code is not wired in yet]**

`RT-MOMS/images/ifs-rpi4.bin` is the stock prebuilt image; it contains no
project code. Building an image that does contain it needs two changes first:

1. a QNX-convention `Makefile` under `RT-MOMS/app/` (or a `src/app/` directory)
   so `make install` copies the binaries into `install/aarch64le/...`;
2. entries in `RT-MOMS/images/rpi4.build` for those binaries, and a launch line
   in its `[+script] startup-script` block if they should start at boot.

Then:

```bat
cd RT-MOMS
P:\qnx\qnx800\qnxsdp-env.bat
make
```

which produces `images/ifs-rpi4.bin`, to be copied to the SD card's FAT
partition together with the firmware files and a `config.txt` containing
`kernel=ifs-rpi4.bin`.

Known gap: `images/Makefile` ends with
`-include $(CURDIR)/../../Templates_build_sdp800/template.mk`, and that directory
does not exist in this checkout, so `$(MKIFSFLAGS)` is undefined. Momentics'
System Builder normally creates it beside the BSP project; it will need to exist
before `make` can build an image here.

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
