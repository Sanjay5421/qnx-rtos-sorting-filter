# M3 integration bundle — apply to the updated `d8ed6ee` tree

## Files added/updated

- `RT-MOMS/app/server.c`: QNX named-channel server, 64 KiB `MsgReceivev` submit, parallel block sort, block Hampel filter, shared-memory result publication, completion pulse and fetch.
- `RT-MOMS/app/client.c`: QNX client using `block_loader`, `MsgSendv`, completion pulse, fetch, shared-memory bounds/checksum validation, and end-of-recording global median/filter.
- `RT-MOMS/app/shm_region.h/.c`: create/map/validate/close the protocol-defined `/sortmed-results` object.
- `RT-MOMS/app/Makefile`: adds `sortmed-server` and `sortmed-client` targets.
- `RT-MOMS/tests/Makefile`: includes `test_shared_memory` in the host test suite.

## Host verification performed

The existing host suite passed:
- sensor: 148 checks, 0 failed, 1 skipped (dataset payload absent)
- protocol: 137 checks, 0 failed
- block loader: 47 checks, 0 failed, 1 skipped (dataset payload absent)
- merge sort: 535 checks, 0 failed, 1 skipped (dataset payload absent)
- median: 7,551 checks, 0 failed
- shared-memory bounds: 13 checks, 0 failed

`shm_region.c`, `server.c`, and `client.c` passed a C99 `-Wall -Wextra -Werror` syntax check using temporary QNX API declarations. This is a syntax check, **not a QNX SDK/link test**. Build both programs in Momentics with the QNX SDP before running them on the Pi.

## Important interface/functional limits

1. The current shared protocol has no “end of recording” message. To avoid breaking the teammate's unchanged M4 protocol, the server returns a median for each block. The client combines all blocks and computes the **global recording median** once the loader reaches EOF.
2. The server filters each block independently because the current submit message carries no neighbouring samples. The client then runs the final filter over the full time-ordered recording, so the final client-side output has correct block-seam context. The per-block filtered arrays in shared memory are diagnostic/intermediate results and do not themselves include cross-block context.
3. The protocol-defined shared region holds sorted and filtered output arrays. The incoming block arrives in the `MsgSendv`/`MsgReceivev` message, consistent with the current `protocol.h` comments; it is not received directly into shared memory.
4. This implementation processes one submitted job at a time in the server loop, although the merge-sort pool parallelizes the sorting work. It is a compatible baseline, not the earlier proposed queued multi-worker `server_core` architecture.

## Build and run on QNX

Build through the RT-MOMS Momentics project or from an environment where `qnxsdp-env.bat` has set the SDP variables:

```sh
make -C app sortmed-server sortmed-client
```

Copy/install both executables to the Pi. Start the server first:

```sh
./sortmed-server
```

Then run the client:

```sh
./sortmed-client /path/to/r1_voltage.bin planar voltage 100
```

For an interleaved recording, use `interleaved` and choose the channel to extract, for example:

```sh
./sortmed-client /path/to/recording.bin interleaved voltage 100
```

The server must be running before the client starts. Only one client at a time is supported.
