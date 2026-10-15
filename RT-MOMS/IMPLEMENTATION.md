# Implementation

What each file implements and how. Run the commands in
[REPRODUCE.md](REPRODUCE.md) first; this file explains what they exercise.

---

## 1. Scope

The project is a client/server pair on QNX: the client offloads 64 KiB blocks of
float32 time-series metrics to the server, which sorts them with a parallel
merge sort and applies a rolling median filter, then reports back
asynchronously with a pulse.

Built so far — everything that can be written, tested and proved **without**
QNX hardware or the QNX runtime:

```
recording (.bin)  ->  sensor  ->  block loader  ->  protocol  ->  [ client -> server ]   M3
                                      |                              [ merge sort ]       M2
                                      |                              [ median filter ]    M3
                                      +--- built, tested, verified    [ pulse + fetch ]    M4
```

`RT-MOMS/app/` and `RT-MOMS/tests/` are ours. Everything else under `RT-MOMS/`
is the unmodified QNX Raspberry Pi 4 board support package — do not edit the
BSP tree itself except `images/rpi4.build` when it is time to add our binaries.

---

## 2. Where data flows

| Stage | Module | Unit of work | Unit size |
| --- | --- | --- | --- |
| Replay a recording | `app/sensor.c` | one sample, one frame | 1 / 128 samples |
| Assemble the payload | `app/block_loader.c` | one block | **16384 samples = 64 KiB** |
| Sort a block | `app/mergesort.c` | one block, k partitions | 16384 samples over 4 workers |
| Describe and validate | `app/protocol.c` | one submit | 48 B header + 64 KiB |
| Transfer (M3) | client/server | IOV: `{header, block}` | 2 parts |

---

## 3. `app/sensor.h`, `app/sensor.c` — the virtual sensor

**Implements.** A device that replays a recording like a live sensor: values
arrive a sample at a time, on the sensor's own clock, optionally paced, never as
one bulk array.

**How.**

- **Two on-disk layouts, one code path.** A sample is read at
  `window[(i - window_first) * stride + offset]`, so a planar file is
  `{offset 0, stride 1}` and an interleaved record is `{offset k, stride 4}`.
  Both `.bin` flavours of the dataset are just configuration.
- **Bounded read-ahead, not a file slurp.** The file is read through a 4096-sample
  sliding window (larger if the caller's frame is), so the module behaves like a
  device with a prefetch buffer, and a caller reading one sample at a time sees
  exactly the values a caller reading whole blocks sees.
- **Device semantics versus client semantics.** `sensor_read()` delivers at most
  one *frame*; `sensor_read_block()` aggregates whole frames up to a requested
  size. That split is deliberate: a real device hands you a frame, and
  assembling a 64 KiB payload is the client's job, not the device's.
- **Its own clock.** `sensor_timestamp_ns()` is
  `index * 1e9 / rate_hz` from a sample counter, so timestamps are exact and
  independent of how fast the host happens to run. In `loop` mode the counter
  keeps advancing across the wrap, like a sensor that never stops.
- **Pacing.** `sensor_read_paced()` sleeps so the stream holds the configured
  rate. The only platform-dependent code in the whole module is the monotonic
  clock and the sleep, behind one `#if defined(_WIN32)`; the `#else` branch is
  `clock_gettime(CLOCK_MONOTONIC)` and `nanosleep`, which is what QNX uses.

**Why this shape.** The client must not care whether it is reading a file, a
replay or a real sensor. Everything above `sensor.c` is written against the
stream interface, so swapping in real hardware later touches nothing else.

---

## 4. `app/protocol.h`, `app/protocol.c` — the common message definition

**Implements.** The message definition both sides compile against — the
"common header file" the project prerequisites ask for, and the M1 interface
specification in code form. This is the file to read to understand the protocol.

**How, and the decisions it encodes.**

1. **The block is exactly 64 KiB**: `PROTO_BLOCK_SAMPLES 16384 × 4 B`. The
   "payload at least 64 KB" requirement is met by construction, with no padding,
   and a 60 000-sample recording is 3 full blocks plus 10 848 samples.
2. **The descriptor is a separate struct from the block**, which is what allows
   the server to receive a 48-byte header, validate it, and only then pull the
   payload with `MsgReadv()` — instead of needing a 64 KiB receive buffer just to
   learn how large the message is.
3. **The checksum covers exactly `desc.samples` floats**, not the whole 64 KiB. A
   short final block has an unused tail, and checksumming that tail would make
   the value depend on whatever happened to be in the buffer.
4. **Message types start at `0x1000`.** QNX's own `<sys/iomsg.h>` puts the system
   message range at `_IO_BASE 0x100 .. _IO_MAX 0x1FF`; application messages must
   stay clear of it. `PROTO_STATIC_ASSERT` re-checks the relation wherever the
   QNX headers are visible, so a future change fails the build rather than the
   server silently eating system messages.
5. **The completion pulse uses code 16**, clear of the small codes the kernel
   reserves, inside the user range `_PULSE_CODE_MINAVAIL.._PULSE_CODE_MAXAVAIL`,
   which is also asserted at compile time on the target.
6. **The ack precedes the pulse, and the header says why**: an `rcvid` is only
   usable until it has been replied to, so a server cannot deliver a late event
   through one it has already answered. The client therefore supplies its own
   connection and receives the pulse after the ack.
7. **The pulse carries a job id; the fetch carries the result.** A pulse is one
   byte of code plus one machine word — enough for an id, not for a median, an
   offset and a length. `RESULT` carries the median, the memory-offset descriptor
   (`offset`, `length`, `checksum`) and `elapsed_ns` for the timing study.
8. **Channel numbers follow the dataset's order** (`time, current, voltage,
   temperature`) and are *not* the interleaved record's order — see the next
   section, which owns that mapping.
9. **`proto_submit_prepare()` clears the struct before filling it**, so no stale
   padding goes on the wire; two otherwise identical submits are byte-identical.
10. **Status is in the reply, not in `MsgError`.** `MsgError` is reserved for
    transport failures; the reply status tells the client *why* a job was
    refused (`PROTO_ERR_CHECKSUM`, `PROTO_ERR_VERSION`, …) and
    `proto_status_str()` turns it into a sentence for the log.

**Validation the server will run** is already implemented and tested:
`proto_block_desc_valid()` for the descriptor, `proto_submit_valid()` for type,
version and descriptor, and `proto_submit_verify()` for "does the block match the
checksum the descriptor claims".

---

## 5. `app/block_loader.h`, `app/block_loader.c` — the loader

**Implements.** The recording → blocks + descriptors conversion: what the client
will actually send.

**How.**

- Wraps `sensor_open()` with the layout and channel, reads one block with
  `sensor_read_block()`, and fills a descriptor: sequence number, index of the
  first sample, sample count, rate, channel, min/max, checksum.
- **The one place that knows the file layouts.** The interleaved record is
  `[time_s, voltage_V, current_A, temperature_K]` — a *different* order from the
  `PROTO_CHANNEL_*` enumeration — so `block_loader_channel_offset()` maps
  channel → position (`voltage` is 1, `current` is 2). Getting this wrong would
  silently sort the wrong signal, so it lives in one function and is tested for
  all four channels.
- **Zeroes the unused tail of a short final block**, so a block is reproducible
  byte for byte, its checksum is stable, and no stale bytes from another buffer
  cross a process boundary.
- Keeps three counters (`total`, blocks emitted, samples emitted) for the report
  and for tests.

---

## 6. `app/mergesort.h`, `app/mergesort.c` — the parallel merge sort

**Implements.** Milestone 2: the sort the server runs on each 64 KiB block, on a
pool of POSIX worker threads, with the recursive merge phases held together by a
mutex and two condition variables.

**How.**

- **Partitions first, then merges.** A sort of `n` values over `k` partitions is
  `log2(k)` merge phases: phase 0 has the workers sort one partition each, phase
  1 merges pairs of partitions, and so on until a single run covers the array.
  `k` is `min(n / 1024, workers)`, so a 16 384-sample block on four workers is
  4 partitions and 2 merge phases.
- **The phase boundary is a barrier, not a delay.** A phase's tasks go into a
  shared array under the mutex; each worker claims the next index, runs it, and
  increments a completion count; the worker that finishes the last task signals
  `phase_done`, and the caller waits on that condition before posting the next
  phase. That dependency is real — a merge reads two runs the previous phase
  produced — and nothing is timed, so the result cannot depend on machine speed.
- **One scratch buffer, ping-ponged** between the caller's array and one
  `n`-float buffer, which doubles as the scratch for the phase-0 partitions
  (each partition owns its slice). One allocation per sort, none per merge.
- **Stable, therefore deterministic.** A merge takes from the left run on ties,
  so equal values keep the order they arrived in, and the output does not depend
  on the thread count. `test_mergesort.c` asserts that bit for bit.
- **In place, and it reports its plan.** The sorted values end up in the caller's
  buffer; `ms_stats()` returns the partitions, the merge phases and the tasks the
  workers actually ran, which is how the tests prove the parallel path was taken
  rather than the serial fallback.
- **Small inputs take the serial path** (`n < 4096`), where a pool costs more
  than it saves, and **one sort per pool at a time**: a second concurrent
  `ms_sort()` is refused instead of corrupting the shared phase state.

**Why this shape.** The server receives a block, validates it, and sorts — so
the sort has to be an ordinary function over a caller-owned buffer, not a thread
of its own, and the pool has to exist before the first request so that a request
costs no thread creation. Nothing here is QNX-specific: `<pthread.h>` is the only
system header, and QNX's C library provides POSIX threads, so the same source
runs on the Pi.

---

## 7. `tests/` — the test suite

No framework, because the point is that these build and run with nothing but a C
toolchain: on the Windows host today, and on the target later.

| File | What it pins down |
| --- | --- |
| `test_util.h` | Assertions and the runner. Includes a bit-exact float compare and an explicit `T_MAYBE_UNUSED` marker so a test binary that never compares floats still builds warning-free. |
| `test_data.h` | Finds the payload from the argument, `BATTERY_DATASET_DIR`, or the usual relative locations; the payload tests then run or skip, never fail, when it is absent. |
| `test_sensor.c` | 15 tests: exact float32 round trips including ±0, `FLT_MIN` and a subnormal; frame capping; EOF versus looping; reset; read-ahead refills at unaligned offsets; timestamp arithmetic; block reads; pacing; and over the real payload: the two layouts agreeing bit for bit, the 3 + 10 848 partition, min/max, and per-channel ranges. |
| `test_protocol.c` | 9 tests: the block is 64 KiB; message types are above the system range, distinct and 16-bit; the pulse code is in range; `type` is the first field of every message; the checksum's definition, determinism and sensitivity (including `+0.0` vs `-0.0`); `prepare` is byte-deterministic from a dirtied struct; validation rejects a wrong type, version, size, rate or channel; a partial block's tail cannot affect its checksum. |
| `test_block_loader.c` | 8 tests: the channel mapping; rejected arguments; a synthetic 20 000-sample file splitting as 16 384 + 3616 and reassembling bit-exactly; the real partition; **the planar and interleaved routes producing byte-identical blocks and descriptors**; the loader→submit→server-validation round trip; and a single changed sample being caught. |
| `test_mergesort.c` | 13 tests: the pool lifecycle and rejected arguments (including the thread-count clamp and `ms_stats()` with a NULL); every size from 0 to 64 through the serial path; the sizes that straddle the 4096 threshold and the partition boundaries; **thread counts 1–8 producing bit-identical output**; all-equal input; two-value input with the tie boundary exactly at `n/2`; presorted, reverse-sorted and sawtooth input; `±0.0`, denormals, `FLT_MIN/MAX`; 20 sorts on one pool; the statistics describing the plan (4 partitions, 2 phases, 7 tasks); and over the real payload, no NaN anywhere plus every block sorting to qsort's answer, to the descriptor's own min/max, and leaving the block itself untouched. |
| `sensor_demo.c` | Streams a recording at a chosen rate and prints a payload summary. |
| `loader_demo.c` | Prints the block stream with its descriptors and the verdict of the same validation the server applies — the M1 payload specification, running. |
| `pipeline_demo.c` | The compute half of the service without the IPC: loads a recording as 64 KiB blocks, sorts each on a pool of worker threads, and prints the median, the range, the sort time and the throughput per block — the numbers the pulse and the `RESULT` will carry. |
| `Makefile` | `check`, `demo`, `demo-sensor`, `demo-pipeline`, `clean`. Overrides make's built-in `CC` default properly (`ifeq ($(origin CC),default)`) and lets `CC=`/`CFLAGS=` on the command line win, which is how the target build reuses it. The two binaries that link `mergesort.c` are built with `THREADCC`, chosen by a compile-and-link probe, because MinGW.org's gcc on this host ships no `<pthread.h>` at all; on QNX and Linux the probe answers `$(CC)` itself, and `THREADCC=` overrides it. |

**The tests that matter most** are the two independent-route comparisons: the
planar per-channel file against the interleaved record (two files, two
`sensor.c` code paths), and the block stream against a straight sensor read. A
mistake in stride handling, channel mapping or refill logic disagrees there. The
sort has the same kind of witness: `qsort()` over the very same values, which no
ordering bug, lost value, duplicated value or half-merged phase can survive.

---

## 8. Conventions

- **C99 plus libc**, deliberately: `-std=c99 -Wall -Wextra -pedantic` is clean,
  and the same sources build for the host and for `qcc -Vgcc_ntoaarch64le`. The
  one addition is POSIX threads in `app/mergesort.c`: on the target they are part
  of the C library, on this host they need a winpthreads-capable compiler, which
  is what `THREADCC` in `tests/Makefile` picks.
- **No QNX headers in host-testable code.** `protocol.h` deliberately does not
  include `<sys/neutrino.h>`; it defines the constants it needs and asserts the
  relations against QNX's values only where those headers exist.
- **Static assertions in C99** via `PROTO_STATIC_ASSERT` (a negative-size array
  typedef), rather than C11's `_Static_assert`.
- **Errors are reported, never silent**: constructors return `NULL` and say why
  on stderr; readers return a count, `0` for end of stream and `-1` for an error.
- **Deterministic bytes.** Fixed-width fields, structs cleared before use, short
  blocks zero-padded — so anything that crosses a boundary can be compared.

---

## 9. Verified numbers

| Quantity | Value |
| --- | --- |
| Host test result | 1418 checks, 0 failed, 0 skipped (642 + 82 + 116 + 578) |
| Block size | 16384 samples = 65536 B |
| 60 000-sample recording | 3 full blocks + 10 848 samples |
| Payload bytes as blocks | 262 144 B (4 × 65 536) |
| Sample bytes | 240 000 B |
| Sort plan for one 64 KiB block, 4 workers | 4 partitions, 2 merge phases, 7 worker tasks |
| Block sort on this host, 4 workers | 0.44 ms per 64 KiB block, ≈ 130 MiB/s; 1.75 ms for the whole 60 000-sample recording |
| Cross-compile | 7/7 binaries, ELF64 AArch64, no warnings |
| Boot image carrying the programs | 37 610 936 B, SHA-256 `b71677d5…4b54d2` |

---

## 10. Not implemented yet, and where each piece plugs in

| Piece | Milestone | Plugs into |
| --- | --- | --- |
| Client: `name_open`, retry to a 10 s deadline, `MsgSendv` of `{header, block}` | M3 | `protocol.h` submit + `block_loader` |
| Server: `name_attach`, `MsgReceivev`/`MsgReadv`, validation, ack | M3 | `proto_submit_verify()` already exists |
| Rolling median filter, outlier removal | M3 | needs the time-ordered stream, i.e. a *copy*: sorting destroys the order the filter needs |
| Completion pulse, fetch, result descriptor | M4 | `PROTO_PULSE_DONE`, `PROTO_MSG_FETCH`, `proto_result_t` |
| Image wiring: `app/Makefile`, `rpi4.build` entries | M0 | see REPRODUCE.md section 9 |

The protocol was written first on purpose: it fixes the interface the sort, the
filter and the IPC must all satisfy, and it is the one part that can be proved
correct before any QNX code exists.
