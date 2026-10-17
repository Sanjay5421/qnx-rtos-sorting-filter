Milestone 1 — System Design and Dataset Preparation
_Current priority_
Define the client-server architecture, select a floating-point time-series dataset, establish the data-block format (minimum 64 KB), and design the communication protocol.
Deliverable: Block diagram, flowchart, dataset and interface specification.
Status (2026-10-15): done on the code side. The interface exists as `RT-MOMS/app/protocol.h` — 16384-sample float32 block (exactly 64 KiB), block descriptor, two-part IOV submit, pulse plus fetch, result with median and memory offset — the dataset is the PyBaMM battery payload under `data/`, and `IMPLEMENTATION.md` documents each stage. The report's block diagram and flowchart are still to be drawn.

Milestone 2 — Parallel Sorting and Synchronization
Implement Merge Sort using multiple POSIX worker threads. Partition the input, coordinate worker completion using mutexes and condition variables, and merge the sorted partitions.
Deliverable: Working parallel sorting module and correctness tests.
Status (2026-10-15): **done and verified on the board.** `RT-MOMS/app/mergesort.c` partitions, merges in phases and holds each phase boundary with a mutex and two condition variables; `RT-MOMS/tests/test_mergesort.c` is 13 tests / 578 checks, clean on the host and, with the payload argument, 578 checks / 0 failed / 0 skipped on the Raspberry Pi 4. A 64 KiB block on 4 workers is 4 partitions, 2 merge phases and 7 tasks, 0.757 ms per block on the board against 2.278 ms with one worker (3.01×), and `pipeline_demo` prints host-identical medians and ranges on both on-disk layouts. Delivered in `0c578cb`, image `b71677d5…`, results recorded in `dece3f3` and `e567262`.

Milestone 3 — Client-Server Communication and Median Filtering
Implement data transfer between client and server, apply a rolling median filter to the original time-ordered sensor signal, and establish result storage and offset handling.
Deliverable: Integrated data-processing pipeline with outlier-removal tests.
Status (2026-10-15): next, nothing started. No client, no server and no median filter yet; the pieces they plug into are listed in `IMPLEMENTATION.md` section 13.

Milestone 4 — QNX Pulse Notification and System Validation
Implement pulse-based completion notification, validate the returned median and memory offset, and evaluate processing time, correctness and behavior under repeated requests.
Deliverable: Integrated QNX demonstration, performance results and final report.
Status (2026-10-15): planned. The protocol half is already in place — `PROTO_PULSE_DONE`, `PROTO_MSG_FETCH` and `proto_result_t` carrying the median, the offset/length/checksum descriptor and `elapsed_ns` for the timing study.

Some minor modifications can be made as we move along.
