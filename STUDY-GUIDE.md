# Study and presentation guide

A plain-language companion to this project: what each piece is, why it exists,
and what to say about it. Read it before a demo, a viva or a report-writing
session. The precise version of everything here is in
[IMPLEMENTATION.md](IMPLEMENTATION.md), and the commands are in
[REPRODUCE.md](REPRODUCE.md).

**Where we are today:** M1 and M2 are built and verified on the Raspberry Pi.
M3 is the next piece of work. The protocol half of M4 is already written and
tested; only the wiring is missing. This file grows with the milestones.

---

## The project in one breath

A client keeps handing a server chunks of sensor readings. The server sorts each
chunk with several threads at once and cleans up glitches with a rolling median,
then taps the client on the shoulder to say the answer is ready — and the client
comes back to collect the median and the address of the results.

| Idea | In plain words |
| --- | --- |
| 64 KB parcels | The unit of work is one parcel of data, never a whole file |
| Descriptor | Every parcel travels with a label: what is inside, and a seal that proves it arrived intact |
| Parallel merge sort | Four workers sort four piles, then merge until one sorted pile is left |
| Rolling median | Look at a small window of neighbouring readings; the middle value is the truth, a spike is not |
| Pulse | The server does not make the client wait — it taps it when the work is done |

## The words you will be asked about

| Word | What it means here |
| --- | --- |
| **Sample** (or "number") | One reading of one signal, stored as a 32-bit float: `3.9618` volts. Four bytes. |
| **Block** (or "parcel") | 16 384 samples of *one* signal = exactly 64 KB. The unit of work. |
| **Frame** | 128 samples — how much the "device" hands over in one read. |
| **Record** | In an interleaved file: four floats = one instant of four channels. |
| **Descriptor** | The label that travels with a block: which block, which sample it starts at, how many, and the seal. |
| **Seal** | A checksum: one number computed from all the bytes. Recompute it, compare, and damage is obvious. |

---

## Milestone 1 — deciding what to send

### Q: "16 384 numbers" — are those actual float values?

Yes. They are real 32-bit floating-point values, one per reading. The type says
so, and a compile-time check pins its size (`RT-MOMS/app/protocol.h`):

```c
#define PROTO_BLOCK_SAMPLES 16384u
#define PROTO_BLOCK_BYTES   (PROTO_BLOCK_SAMPLES * 4u)

typedef struct {
    float samples[PROTO_BLOCK_SAMPLES];
} proto_block_t;

PROTO_STATIC_ASSERT(sizeof(proto_block_t) == PROTO_BLOCK_BYTES, block_is_64_kib);
```

- One sample = one `float` = 4 bytes. So a block is 16 384 × 4 = **65 536 bytes
  = exactly 64 KB** — that is where the assignment's "at least 64 KB" comes from,
  with no padding.
- If the block were `short` or `char`, the arithmetic would be different. It is
  floats because the sensor produces voltages, currents and temperatures with
  decimals, and the median needs the real values.
- A recording has 60 000 such samples per channel → 3 full blocks **plus one
  small block of 10 848** (60 000 = 3 × 16 384 + 10 848). Like filling three
  crates and finishing with a half crate.

**Example anchor.** Block 0 of the voltage channel holds 16 384 numbers starting
`4.19853, 4.19854, …` and ending `3.5662 … 4.274`. Those are volts, and there are
16 384 of them.

### The label, and why a parcel needs one

Sorting the wrong numbers — or damaged numbers — gives a confident wrong answer.
So every block travels with a small struct (`proto_block_desc_t`):

| Field | Meaning |
| --- | --- |
| `sequence` | 0, 1, 2, … — which block of the stream this is |
| `first_sample` | where it starts in the recording (0, 16 384, 32 768, …) |
| `samples` | how many of the 16 384 slots are real (the last block has 10 848) |
| `rate_hz`, `channel` | the sample rate, and which signal these numbers belong to |
| `min_value`, `max_value` | the smallest and largest value in this block |
| `checksum` | the seal |

### Q: What exactly is the "seal"?

It is a **checksum**: a single 32-bit number computed by mixing every byte of the
block, in one pass. The algorithm is FNV-1a (`proto_checksum` in
`app/protocol.c`). Making it and checking it:

```c
/* the loader seals a block as it produces it */
desc.checksum = proto_checksum(block->samples, desc.samples * sizeof(float));

/* the server recomputes the seal over the block it received */
int proto_submit_verify(const proto_submit_t *msg, const proto_block_t *block);
```

- Cheap: one pass over 64 KB. Change **one bit** anywhere and the seal changes —
  so a truncated, reordered or corrupted block is caught instead of being sorted.
- It is not encryption and not a signature: it catches accidents, not sabotage.
- It covers **only the samples the descriptor claims**. A short final block has an
  unused tail; if the seal covered that tail, the value would depend on whatever
  happened to be in the buffer.
- The real values: the four blocks of `r1_voltage.bin` seal to `0xe90d24d2`,
  `0xc98cd3b2`, `0x14c728bb`, `0x362ab592` — and the *interleaved* file produces
  the same four, because after the columns are separated, the bytes are identical.

**How it is tested** (`app/../tests/test_protocol.c`):

```c
T_CHECK_SIZE(proto_checksum("", 0), 0x811c9dc5u);      /* the definition */
T_CHECK(proto_checksum(a, 5) == proto_checksum(a, 5)); /* deterministic */
b[0] = 'H';
T_CHECK(proto_checksum(b, 5) != proto_checksum(a, 5)); /* one byte differs */

T_CHECK_INT(proto_submit_verify(&msg, block), 1);
block->samples[777] = block->samples[777] + 0.001f;    /* nudge one sample */
T_CHECK_INT(proto_submit_verify(&msg, block), 0);      /* the seal notices */
```

---

## The virtual sensor, step by step (`RT-MOMS/app/sensor.c`)

**What it pretends to be:** a device that replays a recording as if it were live
— values arrive on the sensor's own clock, a few at a time, never as one bulk
array. It exists so the client can be written against a stream, and a real sensor
can replace it later without touching anything else.

**1. The settings.** Everything the caller can ask for lives in one struct
(`app/sensor.h`):

```c
typedef struct {
    int      layout;         /* PLANAR (one channel per file) or INTERLEAVED */
    int      offset;         /* interleaved only: float index inside a record */
    int      stride;         /* interleaved only: floats per record           */
    size_t   frame_samples;  /* samples per frame; 0 means the default (128)  */
    uint32_t rate_hz;        /* timestamps and pacing; 0 means 100            */
    int      loop;           /* 1 = start over at the end, like a live sensor */
} sensor_config_t;
```

**2. One formula picks a sample out of the raw records.** This is the whole trick
for handling both file layouts (`sensor.c:393`):

```c
offset = (s->next - s->window_first) * (size_t)s->cfg.stride + s->cfg.offset;
dst[n] = s->window[offset];
```

- Planar file (`r1_voltage.bin`): `{stride 1, offset 0}` → sample *i* is float
  *i* of the file.
- Interleaved file (`Chen2020_c1_T25_discharge.bin`), channel voltage:
  `{stride 4, offset 1}` → sample *i* is float `4i + 1`.
  Record 7's voltage is float 29, i.e. **byte 116** of the file.
- The three other floats of each record are read from disk and thrown away.

**3. A window of records is read ahead** (`sensor.c:330-357`), so the module
behaves like a device with a prefetch buffer instead of slurping the file:

```c
want = s->window_samples * (size_t)s->cfg.stride;   /* whole records */
got  = fread(s->window, sizeof(float), want, s->fp);
s->window_first = index;
s->window_count = got / (size_t)s->cfg.stride;      /* counted in records */
```

- Default window: 4096 records; widened when the caller's frame is bigger. The
  block loader asks for 16 384-sample frames, so the window becomes 16 384
  records — 64 KB for a planar file, 256 KB for an interleaved one, one `fread`
  per block.
- When the reader walks past the end of the window, it seeks and refills. A
  caller reading one sample at a time therefore sees *exactly* the same values as
  a caller reading whole blocks — which is one of the tests.

**4. A read, line by line** (`sensor.c:359-403`, trimmed):

```c
cap = s->cfg.frame_samples;                 /* a device read is one frame */
if (max_samples < cap) cap = max_samples;

while (n < cap) {
    if (s->next >= s->total) {
        if (!s->cfg.loop) break;            /* end of recording */
        s->next = 0;                        /* a live sensor never stops */
    }
    if (s->window_count == 0 || s->next >= s->window_first + s->window_count)
        sensor_fill(s, s->next);            /* refill the window */
    dst[n++] = s->window[(s->next - s->window_first) * s->cfg.stride + s->cfg.offset];
    s->next++;
    s->delivered++;
}
```

`sensor_read()` hands back at most one frame. `sensor_read_block()` calls it in a
loop until the caller's block is full — that is how a device read becomes a
64 KB parcel (`sensor_read_block(s, dst, 16384)`), and it is the client's job,
not the device's.

**5. Its own clock and pacing** (`sensor.c:295-300`):

```c
return s->delivered * 1000000000ull / (uint64_t)s->cfg.rate_hz;
```

- The timestamp is computed from a counter, so it does not depend on how fast the
  machine runs: after 600 samples at 100 Hz the timestamp is
  `600 × 1e9 / 100` = 6 000 000 000 ns = exactly 6 s.
- `sensor_read_paced()` adds a sleep so samples come out at the configured rate.
  This is the only platform-specific code in the whole project: on the board it is
  `clock_gettime(CLOCK_MONOTONIC)` and `nanosleep`.

**6. What it refuses.** A missing path, an unreadable or empty file, a file whose
size is not a whole number of float32 values or of records, an offset/stride/
frame/rate out of range — each returns `NULL` with a sentence on stderr. Reads
return a count, `0` for end of stream, `-1` for an error.

### How the sensor is tested (`tests/test_sensor.c`, 15 tests / 642 checks)

| Test idea | How, and what it rules out |
| --- | --- |
| Two routes to the same numbers | Read one sample at a time vs read whole blocks → must be identical; a window/refill off-by-one would show up here |
| Two file layouts | The planar file and the interleaved record → identical values, bit for bit; a stride or offset mistake would show up here |
| Exact bits | Values including `+0.0`, `-0.0`, `FLT_MIN` and a subnormal must survive untouched — compared by their bits, not by `==` |
| Frame cap | Asking for 1000 samples still returns at most one frame (128) |
| Boundaries | Reading exactly up to and past the window edge forces a refill at an awkward spot |
| Looping | With `loop`, the counter keeps counting across the wrap instead of reporting end of stream |
| Bad input on purpose | NULL path, a 3-byte file, a 6-float "record" → each rejected with a message, and the test prints `ok` afterwards |
| Real payload | The 60 000-sample recording splits as 3 + 10 848, and each channel's min/max matches its file |

The tests are plain C programs: no framework, just `T_CHECK(...)` macros that
count failures, so they build and run on the host and on the board unchanged.

---

## Milestone 2 — sorting with several workers

### Q: What is the input, and what is the output?

- **Input:** one array of 16 384 floats — one channel's samples, in the order they
  arrived (time order), plus how many of them are real.
- **Output:** *the same array*, sorted ascending in place, and a small record of
  what happened (`ms_stats`: partitions, merge phases, tasks).
- The median the project talks about is then just the middle element:
  `sorted[count / 2]`. Sorting gives it for free.

```c
int ms_sort(mergesort_pool_t *pool, float *data, size_t n);
```

### Q: How is it split, and how many threads are there?

The threads are created **once** (`ms_pool_create`), not per block: as many as you
ask for, or one per CPU by default — 4 on the Pi. They wait on a condition
variable when there is nothing to do. Each sort splits the one array it was given
(`mergesort.c:366-372`):

```c
k = (unsigned)(n / MS_PARTITION_MIN);   /* MS_PARTITION_MIN = 1024 */
if (k > p->nthreads) k = p->nthreads;   /* never more parts than workers */
part = (n + k - 1) / k;                 /* ceil(n / k) */
```

For a block on 4 workers: `k = 16384 / 1024 = 16`, capped at 4 → **4 partitions**
of 4096 samples each. Then one task per partition is posted:

```c
p->tasks[i].lo = (size_t)i * part;                 /* 0, 4096, 8192, 12288 */
p->tasks[i].hi = p->tasks[i].lo + part;            /* 4096, 8192, 12288, 16384 */
post_phase(p, k);
```

Each worker sorts its own quarter with an ordinary single-threaded merge sort,
writing into its own slice of one scratch buffer. So after phase 0 there are four
sorted runs of 4096 — not one sorted block yet.

### Q: How does it merge back?

Two sorted runs become one, by looking at the front of each and taking the smaller
— the way you would merge two sorted piles of playing cards. This is the whole
function (`mergesort.c:76-91`):

```c
while (i < na && j < nb)
    out[k++] = (a[i] <= b[j]) ? a[i++] : b[j++];
while (i < na) out[k++] = a[i++];      /* one pile ran out: copy the rest */
while (j < nb) out[k++] = b[j++];
```

With piles `[1, 4, 9]` and `[2, 3, 7]`: take 1, then 2, 3, 4, then 7, 9 →
`[1, 2, 3, 4, 7, 9]`. Six comparisons, not 6 × 6.

The phases then halve the number of runs (`mergesort.c:392-421`): 4 runs → merge
pairs → 2 runs → 1 run. Each merge reads the buffer the previous phase wrote and
fills the *other* buffer, so the two buffers take turns:

```c
while (runs > 1) {
    size_t   run_size = part << phases;   /* 4096, then 8192 */
    unsigned nt = 0;
    for (r = 0; r + 1 < runs; r += 2) {   /* pair 0+1, 2+3, then 0+1 */
        p->tasks[nt].lo = (size_t)r * run_size;
        p->tasks[nt].mid = p->tasks[nt].lo + run_size;
        p->tasks[nt].hi  = p->tasks[nt].mid + run_size;
        nt++;
    }
    post_phase(p, nt);
    swap read_buf and write_buf;
    runs = (runs + 1) / 2;
}
```

For a block on 4 workers that is: 4 partition tasks, then 2 merges, then 1 merge
= **7 tasks in 3 rounds**, and 2 merge phases in the stats.

### Q: Is the merging done by other threads too?

Yes. Every task — partition *or* merge — is claimed from the same shared queue by
whichever worker is free. The thread that called `ms_sort` never sorts or merges
anything itself; it posts the round and waits (`mergesort.c:255-293`, trimmed):

```c
for (;;) {
    pthread_mutex_lock(&p->lock);
    while (!p->quit && p->next_task >= p->ntasks)
        pthread_cond_wait(&p->work_ready, &p->lock);   /* idle: sleep here */
    task = p->tasks[p->next_task++];                   /* claim one task */
    pthread_mutex_unlock(&p->lock);

    if (task.kind == MS_TASK_PARTITION)
        sort_serial_rec(p->src + task.lo, p->dst + task.lo, task.hi - task.lo);
    else
        merge_runs(p->src + task.lo, task.mid - task.lo,
                   p->src + task.mid, task.hi - task.mid, p->dst + task.lo);

    pthread_mutex_lock(&p->lock);
    p->tasks_done++;
    if (p->tasks_done == p->ntasks)
        pthread_cond_signal(&p->phase_done);           /* last one rings */
    pthread_mutex_unlock(&p->lock);
}
```

And this is the rule that makes it correct — the barrier between rounds
(`mergesort.c:137-147`). A merge may only start once *both* runs it reads are
finished, so the caller waits for a bell instead of guessing with a sleep:

```c
p->ntasks = ntasks; p->next_task = 0; p->tasks_done = 0;
pthread_cond_broadcast(&p->work_ready);          /* "work is available" */
while (p->tasks_done < p->ntasks)
    pthread_cond_wait(&p->phase_done, &p->lock); /* wait for the last tick */
```

So: mutex = the scoreboard nobody writes to at the same time; condition variables
= the two bells (one for "work ready", one for "round finished").

### Why the answer is always the same

The merge takes from the left pile when two values are equal (`a[i] <= b[j]`), so
equal numbers keep their original order. That single character makes the result
**stable**, and stability makes it independent of the thread count — the tests
check that 1, 2, 3, 5 and 8 workers produce byte-identical output.

### The small print that matters

- **Small inputs skip the pool**: below 4096 values, sorting with threads costs
  more than it saves, so one thread does it.
- **One sort at a time per pool**: a second concurrent `ms_sort` is refused with
  `-1` rather than corrupting the shared round state.
- **The buffer flip-flop**: one scratch array is reused for every merge; if the
  last merge wrote into the scratch, one `memcpy` puts the answer back in the
  caller's array.
- **Nothing QNX-specific**: the only system header is `<pthread.h>`, so the same
  source runs on the host and on the Pi.

### The plan line, and what the timings say

| Workers | Plan the program reports | Per 64 KiB block |
| --- | --- | --- |
| 1 | 1 partition, 0 merge phases, 1 task | 2.278 ms |
| 2 | 2 partitions, 1 merge phase, 3 tasks | 1.253 ms |
| 4 | 4 partitions, 2 merge phases, 7 tasks | 0.757 ms |

1 → 4 workers is **3.01×** on the board. One block is 163.84 s of the 100 Hz
signal, and it is sorted in 0.757 ms — about **200 000 times faster than the data
arrives**. The millisecond figures wobble between runs; the *plan line* is the
deterministic proof that the parallel path ran, because 1 partition / 0 phases
cannot come out of a four-worker merge.

### How the sort is tested (`tests/test_mergesort.c`, 13 tests / 578 checks)

The witness is the standard library's `qsort`: sort the same numbers both ways and
they must agree (`tests/test_mergesort.c`, the helper every test uses):

```c
rc = ms_sort(pool, work, n);
T_CHECK_INT(rc, 0);
T_CHECK(is_sorted(work, n));
T_CHECK(order_is_consistent(work, n));
T_CHECK(same_values(work, reference, n));   /* reference == qsort(input) */
```

| Test | What it is for |
| --- | --- |
| Sizes 4095 / 4096 / 4097, 5000, 8192, 16 384, 65 536 | The threshold (4096) and the partition boundaries; an off-by-one in the split or the last run shows up here |
| 1, 2, 3, 5, 8 workers on one input | Byte-identical output — proves the merge is stable and the barrier is right (8 threads on 4 cores also proves it still works when oversubscribed) |
| All-equal input | The result must be the input, unchanged |
| Two values only | Every merge is a tie, and the boundary must land exactly at n/2 |
| Presorted, reverse-sorted, sawtooth | Ordered input, worst-case input, and short ordered runs |
| `±0.0`, denormals, `FLT_MIN`/`FLT_MAX` | The extremes must not fool a comparison |
| 20 sorts on one pool | Reuse: nothing leaks or sticks between sorts, including a jump from the parallel path to the serial one |
| The plan assertion | For 16 384 samples on 4 workers: exactly 4 partitions, 2 phases, 7 tasks |
| Real payload | No NaN anywhere in the recording; every block matches `qsort` *and* the min/max the loader recorded, and the block itself is left untouched |

### If they ask

- *Why not sort the whole recording at once?* The service is a streaming one: the
  client offloads continuously, so the natural unit is a parcel.
- *Why not one thread per channel (voltage, current, temperature, time)?* A block
  carries one channel, and one channel's 16 384 numbers already keep four cores
  busy (3.01×, 0.757 ms). Splitting by channel would add coordination for nothing;
  the protocol can do it later (per-job ids, four result slots) if the units ever
  get small.
- *Where do the numbers live while they are being sorted?* In a scratch buffer of
  the same size, allocated once per sort and flipped back and forth between
  rounds. No allocation per merge.
- *How do you know the sort is right?* Every test compares against `qsort` on the
  same numbers, and the demo re-checks each parcel came out in order.

---

## Milestone 3 — the two programs talking *(next)*

```mermaid
flowchart LR
    A["client knocks by name<br/>sortmed, up to 10 s"] --> B["one delivery:<br/>label + parcel"]
    B --> C["server checks the seal"]
    C --> D["receipt: accepted"]
    D --> E["server sorts and filters"]
    E --> F["tap: your job is done"]
    F --> G["client asks:<br/>what was the answer?"]
    G --> H["median + address of the results"]
```

- **Knocking by name.** The client asks for the service by name (`sortmed`), the
  only connection method the assignment allows. If the server is not up yet it
  keeps trying for ten seconds, then exits with a clear message instead of hanging.
- **One delivery, two items.** The label and the parcel travel together as a
  two-part list, so neither side has to glue them into a single 64 KB buffer.
- **The receipt comes before the tap**, deliberately: the client cannot hold its
  line open waiting for a late tap, so the server says "accepted" straight away
  and the client then listens on its own channel.
- **The tap is tiny.** It carries one number — the job id — like a buzzer rather
  than a letter. That is why the client asks for the details afterwards.
- **The results are left in a shared area of memory.** A sorted block plus a
  filtered block is 128 KB, and pushing that back through messages would copy it
  twice for nothing. So the server writes both into one named shared region — the
  same memory, seen by both programs — and the client reads them there. The reply
  only says *where*: which region, the two byte offsets, how many values, and a
  seal per array. That "where" is the memory-offset descriptor.
- **The rolling median** is the cleanup step: for each reading, look at a small
  window of neighbours and take the middle value. Readings `5, 5, 5, 50, 5`
  average to 14 — the spike drags the average — but their median is 5, which is
  what the sensor really saw. The number of spikes removed is reported too.
- **Why a copy is filtered.** Sorting scrambles the time order, and the filter is
  entirely about time order, so the two run on different buffers.

**If they ask**

- *Why is the server separate from the client?* That is the assignment's
  architecture, and how a QNX system is meant to be built: separate processes
  talking by messages.
- *What if the client is not listening when the tap arrives?* It does not matter.
  A pulse lands on a channel and waits there; the client is not holding anything
  open for it.
- *Median or average?* Median, because a single spike moves an average and cannot
  move a median.

---

## Milestone 4 — the tap, and proving it *(protocol ready, wiring to do)*

- The tap (`PROTO_PULSE_DONE`) is the asynchronous event the assignment asks for:
  the server *tells* the client, and the client does not sit polling.
- The answer carries exactly what the project statement promises: the **median**,
  plus a **memory-offset descriptor** — which shared region, at which byte
  offsets, how many values, and a seal over each array — along with the window
  width, the number of outliers removed, and how long the server took.
- That last figure, `elapsed_ns`, is what the timing half of the report is built
  from: same work, more workers, less time.

---

## Testing, in one place — what we test, how, and how to read it

### The machinery

There is no test framework. Each file in `RT-MOMS/tests/` is a normal C program
that includes `test_util.h` and calls check macros; `make check` builds all four
and runs them one after another, printing a summary per binary.

```c
/* test_util.h — one check, counted; a failure prints file and line */
#define T_CHECK(cond)                                            \
    do { t_checks++;                                             \
         if (!(cond)) { t_failed++;                              \
             printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define T_RUN(fn)  /* prints the test name, then "ok" or "FAILED" */
static int t_summary(void) {
    printf("\n  %d checks, %d failed, %d skipped\n", ...);
    return (t_failed == 0) ? 0 : 1;      /* the exit status is the verdict */
}
```

- **`T_CHECK` / `T_CHECK_INT` / `T_CHECK_SIZE` / `T_CHECK_SAME_FLOAT`** are the
  vocabulary: `T_CHECK_SAME_FLOAT` compares the *bits*, so `+0.0` and `-0.0` are
  not "equal", `T_CHECK_INT` prints both numbers on failure, and so on.
- **`T_RUN(test_x)`** runs one test and prints its name: a line reading `ok`
  means every check inside it passed.
- **The last line is the verdict**: `1473 checks, 0 failed, 0 skipped`. The
  process exit status is 0 only if nothing failed, so `make check` stops on a
  failure.
- **`skipped`** is not a failure: the payload tests need the generated dataset.
  Run without it (or on the target without the directory argument) and the
  summary says `1 skipped` while still exiting 0. **`0 skipped` therefore means
  the test really ran against the real recording** — that is the number to quote.
- **The same sources run on both machines**: the host compiles them with
  `gcc`/`clang`, the board with `qcc -Vgcc_ntoaarch64le`. Identical source,
  identical checks, identical summaries — which is what makes the board run a
  *confirmation* rather than a different experiment.

### The four kinds of evidence we use

| Kind | Example | Why it is convincing |
| --- | --- | --- |
| **Two independent routes to the same numbers** | planar file vs interleaved record; one sample at a time vs whole blocks | Both routes are computed by different code paths, so a wrong stride, offset or window refill makes them *disagree* |
| **An oracle** | `qsort()` from the standard library sorts the same numbers | The library is not ours and not written by us; agreement is strong evidence |
| **Invariants that must hold** | output is sorted; the multiset of values is unchanged; checksums match; the output is byte-identical for 1, 2, 3, 5 and 8 threads | These catch classes of bugs rather than one example: a lost value, a duplicated value, a half-merged phase |
| **Deliberate bad input** | a 3-byte file, a 6-float "record", a NULL path, one nudge to one sample, a fetch for an unknown job | Proves the system *refuses* correctly instead of quietly doing something wrong |

### What each binary is asking

| Binary | The one-sentence question it answers | Checks |
| --- | --- | --- |
| `test_protocol` | "Is the contract exactly what both sides think it is — 64 KiB blocks, safe message numbers, a checksum that notices damage, a shared region whose arithmetic adds up?" | 137 |
| `test_sensor` | "Does replaying a file behave like a device: same numbers however you read it, both file layouts agreeing, frames capped, clock exact, junk refused?" | 642 |
| `test_block_loader` | "Does a stream become blocks and a label that survives the trip — split right, padded right, sealed over the right bytes?" | 116 |
| `test_mergesort` | "Is a sorted block really sorted, really the same numbers, and really independent of how many threads did it?" | 578 |

The per-module detail — each test and the bug it rules out — is in the M1 and M2
sections above; this is the machinery and the vocabulary.

### How to explain testing in three sentences

> Every module has a testbench that runs as a plain C program on the host, and the
> same four programs run on the board after cross-compiling. They do not just call
> the code and check it does not crash: they compare it against an independent
> answer — the standard library's `qsort`, or the same recording read through a
> different layout — and they feed it broken input on purpose to prove it refuses.
> The last line of each run is the headline: 1473 checks on the host, 0 failed, 0
> skipped, and on the board the same counts against real telemetry.

---

## The deck, slide by slide — what to say

The deck says *what*; these notes are the *why* and the *how*, in the deck's own
order. Each slide gets: what is on it, the sentence(s) to say, and an anchor — an
example, a number or a line of code. Slides with code are explained line by line;
the code is quoted exactly as it is in the repository.

### Introduction

**Title / Outline** — Say: "A client offloads 64 KB blocks of sensor data; the
server sorts them on four threads and filters spikes; a pulse tells the client
when the answer is ready." The outline is the seven sections; no need to read it
out, just name them.

**The Problem Statement** — The assignment's demands are on the left (parallel
merge sort, rolling median filter, QNX pulse) and our reading of them on the right
(real-time constraints, the IOV rule, the architectural rules). Say: "The data
arrives continuously, so the unit of work cannot be a file — it has to be a
fixed-size parcel that fits through one message."

**Why a Median Filter Over an Average?** — This slide has the best one-liner in
the deck: readings `5, 5, 5, 5, 50` average to 14 V but their median is 5 V, and
5 V is the truth. Say: "One bad sample moves the mean by 9 volts and the median by
nothing — that is the entire reason for the filter." Then the two design points:
an **odd** window width (so there is always a middle sample), and the filter runs
on the **raw copy** because it needs chronological order while the sort destroys it.

**Core Design Principles** — Five claims, each backed by something concrete:
contract first (`protocol.h` written before the logic), modular pipeline (sensor →
loader → messages → sort, each testable on the PC), zero padding (16 384 float32 =
65 536 B exactly), deterministic execution (stable merge ⇒ identical output for
any thread count), non-blocking server (the ack releases the client immediately),
and empirical rigor (every number measured on the Pi).

### System Architecture (four diagram slides)

**System overview** — Two boxes, one band, three arrows. Say: "Both programs
compile the same header. The submit and the ack are messages; the pulse is
asynchronous. The block crosses as the *second part of an IOV*: the server reads
48 bytes of header, validates it, and only then pulls the 64 KB payload — so it
never needs a 64 KB receive buffer just to learn how big the message is."

**Client: connect, offload, collect** — Walk it as a loop: `name_open("sortmed")`,
retry until the **10-second deadline** (then report and exit, never hang), take
the next block, `MsgSendv {header, block}`, read the ack, and if it was refused go
back for the next block; then receive on its own channel for the pulse and fetch
the result.

**Server: accept, validate, answer** — Say: "One receive loop, and it never sorts
in that loop." Type switch (submit → validate → **ack, then queue** → sort →
filter → pulse; fetch → reply the result; anything else → refuse). The reason the
ack comes before the pulse: an `rcvid` is only valid until it is replied to, so
the client cannot still be waiting for an answer when a later pulse arrives — it
supplies its own connection for that.

**Data flow: what changes at each step** — The key sentence on the whole deck:
**two buffers, not one.** The sort needs the values, the filter needs the *order*
of those values, so the arriving block is never sorted in place. The sorted copy
yields the block median as a by-product; the time-ordered copy feeds the rolling
median.

### Implementation Details

**Virtual Sensor: Architecture & Mechanics** — What is on the slide: the stride/
offset extraction, the 4096-record window, 128-sample frames, the timestamp
arithmetic, strict validation and pacing. Say: "The sensor makes a file behave
like a device: values arrive in frames, on their own clock, and the caller never
loads the file." Anchor: record 7 of the voltage channel lives at float `4×7+1 =
29`, **byte 116** of the interleaved file. One nuance to have ready: the window is
4096 records *unless the caller's frame is larger* — the block loader asks for
16 384-sample frames, so in the real pipeline the window is 16 384 records, i.e.
256 KiB of interleaved file per refill.

**Virtual Sensor: Code Walkthrough** — The listing is the read loop; walk it line
by line:

```c
while (n < cap) {                       /* never more than one frame */
    if (s->window_count == 0 ||         /* nothing read yet */
        s->next < s->window_first ||    /* walked backwards (reset) */
        s->next >= s->window_first + s->window_count) {   /* past the window */
        if (sensor_fill(s, s->next) != 0)
            return (n > 0) ? (int)n : -1;   /* error after some output */
    }
    size_t off = (s->next - s->window_first) * s->cfg.stride + s->cfg.offset;
    dst[n++] = s->window[off];          /* the one channel we asked for */
    s->next++;
    s->delivered++;
}
```

Say: "Three conditions trigger a refill, and the third one is the interesting
one: the reader has walked past the end of the window. `off` is the whole
layout story — stride picks the column, offset picks the channel, and the
difference of indices is how far into the window we are. A partial read returns
what it got rather than throwing it away."

**Block Loader: Aggregation & Serialization** — What it does: read frames until
16 384 samples are collected (65 536 B), fill the descriptor, zero-pad a short
final block. Say: "60 000 readings become 3 full blocks and one block of 10 848;
the tail of that last block is zeroed so the bytes are reproducible and no stale
memory crosses a process boundary." Anchor: the four seals `0xe90d24d2`,
`0xc98cd3b2`, `0x14c728bb`, `0x362ab592`, identical from either file layout.

**Block Loader: Code Walkthrough** — Walk it:

```c
got = sensor_read_block(l->sensor, block->samples, PROTO_BLOCK_SAMPLES);
if (got <= 0) return got;                       /* 0 = end, -1 = error */

if ((uint32_t)got < PROTO_BLOCK_SAMPLES)        /* short final block */
    memset(&block->samples[got], 0,
           (PROTO_BLOCK_SAMPLES - (uint32_t)got) * sizeof(float));

desc->samples  = (uint32_t)got;
desc->checksum = proto_checksum(block->samples, desc->samples * sizeof(float));
```

Say: "One call fills the block; a short block gets its tail zeroed; the seal is
computed over **exactly the declared samples**, so the padding cannot change it —
which is what makes the same recording always produce the same seal." *Note for
whoever edits the deck:* the slide's listing shows a helper named
`proto_checksum_block(block->samples, desc->samples)`; that function does not
exist — the real call is the `proto_checksum(...)` above, with the byte count.

**Protocol Definition: Common IPC Header** — The table on the slide is the whole
wire format: submit 48 B + block 64 KiB (the two IOV parts), ack 8 B, pulse (1 B
code + a word), fetch 8 B, result 48 B. Say: "The types start at 0x1000 because
QNX's own IO message range is 0x100–0x1FF; a static assertion re-checks that
wherever the QNX headers are visible, so a future change breaks the build instead
of the server eating system messages." Anchor: `PROTO_STATIC_ASSERT(sizeof(proto_block_t)
== PROTO_BLOCK_BYTES, block_is_64_kib)` — the 64 KB claim is checked by the
compiler, not by a comment.

**Protocol Definition: Shared Results Region** — Why it exists: a sorted block
plus a filtered block is 128 KB, and copying that back through messages would
move it twice for nothing. Say: "The block still crosses by message — that is
where the IOV requirement lives — but the *results* are left in shared memory:
one region, a 24-byte header, then four slots of 16-byte slot header + 64 KiB
sorted + 64 KiB filtered. The arithmetic: 24 + 4 × (16 + 131 072) = 524 376
bytes." Then the two mechanisms: a **state word** (`WR2D`) instead of a lock
shared across processes, and per-array **checksums** so the client can verify what
it read; `offset` 0 means "not produced" because no real array starts at byte 0.

**Parallel Merge Sort: Phase Architecture** — The slide's four bullets, each with
a number: threshold 4096 below which the pool costs more than it saves; phase 0
splits a block into `k = min(n/1024, threads)` slices (4 × 4096 for a block on 4
workers); phases 1…log₂k merge adjacent pairs, swapping buffers; one `memcpy` puts
the answer back in the caller's array if the last merge landed in the scratch.
Then the two claims: a mutex + two condition variables and **no sleeps**; and
stability (`a[i] <= b[j]`) making the output independent of thread count.

**Parallel Merge Sort: Barrier Code Walkthrough** — Walk `post_phase()`:

```c
pthread_mutex_lock(&p->lock);
p->ntasks = ntasks; p->next_task = 0; p->tasks_done = 0;
pthread_cond_broadcast(&p->work_ready);            /* "there is work" */
while (p->tasks_done < p->ntasks)
    pthread_cond_wait(&p->phase_done, &p->lock);   /* wait for the last tick */
pthread_mutex_unlock(&p->lock);
```

Say: "The caller is a manager: it publishes the round, wakes the workers, then
blocks on `phase_done`. Each worker claims the next task index under the mutex,
runs it, and increments the count; the one that reaches `ntasks` rings the bell.
The mutex is the scoreboard nobody writes to at the same time, and there is no
`sleep`, so the round cannot start before the runs it consumes exist — on a slow
machine it just takes longer, it does not go wrong." Be ready for: *what if the
barrier were missing?* A merge could read a run that is still being sorted, which
is a wrong answer that appears and disappears between runs.

### Dataset & File Format

**Synthetic Battery Telemetry (PyBaMM)** — Say: "We need data with a known
answer, so the telemetry is simulated: one LG M50 cell discharged at 0.5C, 1C and
2C, four channels, 60 000 samples each at 100 Hz — ten minutes per run — with
Gaussian noise plus 1.5 % impulsive corruption, and the corruption mask kept
offline as the answer key." If asked about `44 blocks` on the slide: it is
720 000 values ÷ 16 384 rounded up; per channel it is 12 series × 4 blocks = 48
transport blocks, because a block never mixes channels.

**Format Clarification: Planar vs Interleaved** — The two formulas side by side:
planar `sample_i = file[i]` (stride 1, offset 0), interleaved
`sample_i = file[4i + k]` (stride 4, offset *k*). Say: "On an interleaved file,
sorting without choosing a channel sorts seconds against volts against amperes —
it would run happily and mean nothing. That is why the channel-to-offset mapping
lives in exactly one function." Then the deck's own conclusion: interleaved is the
production format; planar is the cross-check that catches a stride mistake.

**Production Payload: Interleaved Record Layout** — The table is the whole file
format: 16 bytes per record, `[time, voltage, current, temperature]`. Say: "Record
*n* starts at byte `16n`, and the voltage of record *n* is at `16n + 4`. Extracting
that column gives the four seals on the slide — the same four the planar file
produces, which is how we know the extraction is right."

### Dedicated Module Testbenches

Each of these four slides has the same shape — *what it does / how it is done /
verifiable output* — so use the same three sentences on every one.

**Testbench: Virtual Sensor (`test_sensor`)** — Say: "It asks one question:
does replaying a file behave like a device? It writes synthetic files with known
values, reads them one sample at a time and in whole blocks, reads the same
recording through both layouts, and checks the two agree bit for bit. Then it
feeds the sensor junk — a missing file, a 3-byte file, a 6-float record — and
requires a named rejection rather than a crash." 15 tests, 642 checks.

```c
/* the two-route idea, in one line */
planar  = sensor_open(td_voltage_path(), NULL);          /* stride 1, offset 0 */
strided = sensor_open(td_interleaved_path(), &cfg);      /* stride 4, offset 1 */
/* ... every sample of both must be bit-identical */
```

**Testbench: Protocol Definition (`test_protocol`)** — Say: "This one is about the
contract, and most of it is checked by the compiler before the tests even run.
Then the tests pin what a compiler cannot: the message numbers stay clear of
QNX's range, the checksum notices a single changed byte, and a short block's
unused tail cannot affect its seal." 12 tests, 137 checks.

```c
T_CHECK_SIZE(sizeof(proto_block_t), 65536);          /* the 64 KB claim */
T_CHECK(PROTO_MSG_SUBMIT > 0x1FF);                   /* not a kernel type */
T_CHECK_SIZE(proto_checksum("", 0), 0x811c9dc5u);    /* the definition */
block->samples[777] += 0.001f;                       /* one nudge ... */
T_CHECK_INT(proto_submit_verify(&msg, block), 0);    /* ... and it is refused */
```

**Testbench: Block Loader (`test_block_loader`)** — Say: "It builds a ramp whose
values are known exactly (0, 0.5, 1.0, …), so every expected number can be
written down, checks a 20 000-sample stream splits as 16 384 + 3616 and reassembles
bit-for-bit, then runs the real recording through both file layouts and requires
**byte-identical blocks and descriptors**." 8 tests, 116 checks.

**Testbench: Parallel Merge Sort (`test_mergesort`)** — Say: "The witness here is
`qsort` from the standard library: sort the same numbers both ways and they must
agree. Then the properties a single comparison cannot prove: the output is ordered,
it is the same multiset of values, and it is byte-identical whether one, two,
three, five or eight threads did the work." Then the shapes: all-equal input,
two-value ties, presorted, reverse-sorted, sawtooth, `±0.0`, denormals, the
threshold sizes 4095/4096/4097, and twenty sorts on one pool. 13 tests, 578 checks.

### Two-Fold Verification & Results

**Two-Fold Verification Strategy** — Say why both folds exist, because this is the
slide that has to convince: "Fold one is the host testbenches — fast, easy to
instrument, they can compare against `qsort` and feed broken input. Fold two is the
same programs cross-compiled and running on the board: a different compiler, a
different C library and a different CPU. When the board prints the same counts and
the same medians, the only thing left that could differ is the kernel — and that is
what the shared-memory region and the pulse will exercise in M3/M4." Numbers to
quote: **1473 checks on the host, 0 failed, 0 skipped**; on the board
`test_protocol` 137, `test_block_loader` 116, `test_mergesort` 578 — all 0 failed,
0 skipped.

**Target Testbench Setup** — Hardware, not a simulator: Raspberry Pi 4 Model B,
four Cortex-A72 cores, QNX 8.0.0 booting straight into RAM from the image on the
SD card, driven over an FTDI serial adapter on `/dev/ser1` at 115200 8N1. Say:
"Nothing is copied to the board and nothing is mounted — the programs *and* the
telemetry are inside the image, so a run cannot pick up a stale file."

**The nine console slides** — one sentence each, and point at the last line of the
screenshot:

| Slide | Say |
| --- | --- |
| Board boot | "All four cores come up (`procnto-smp-instr`), then the shell prompt — that prompt is the proof the image booted." |
| Protocol layer | "The same suite as the host. Note the screenshot is from the image *before* the shared-results-region tests — it shows 82 checks; the current suite prints 137, so re-capture after the next flash." |
| Virtual sensor | "642 checks, and the `sensor: …` lines are the test failing on purpose — every one is followed by `ok`." |
| Block loader | "116 checks: the interleaved file produces the same blocks and the same seals as the host." |
| On-board payload | "`cksum` of the files inside the image matches the host's, then `loader_demo` accepts all four blocks — the data itself is bit-identical, not just the code." |
| Live sensor streaming | "`sensor_demo` at 10 Hz, paced by the monotonic clock: the sensor can replay in real time, not as fast as the CPU allows." |
| Parallel sorting | "578 checks on the board. The sort is not faster-but-different: the medians and the plan line are the host's." |
| Multi-core execution | "Four workers, and the plan line reads `4 partitions, 2 merge phases, 7 tasks` — that line is the proof the parallel path ran." |
| Core scaling sweep | "1 → 2 → 4 workers: 2.278 → 1.253 → 0.757 ms per block, with the medians unchanged." |

**Benchmark Analysis and Throughput** — The table (1 / 1.82× / 3.01×) and the
headline: a 64 KiB block is **163.84 seconds** of 100 Hz telemetry and it sorts in
**0.757 ms**, about **216 000× real time**. Then volunteer the caveats before
anyone asks: "These are one run per point, so treat ±10–20 % as noise; the
deterministic evidence is the plan line, and the *medians* being identical across
thread counts is what shows the parallelism changed the speed, not the answer."

### Applications & Conclusion

**Practical Industrial Applications** — One sentence per bullet: battery packs
(pack transients need filtering while cell distributions need sorting), edge
gateways (keeping acquisition responsive while 64 KB blocks are processed),
automotive telemetry (deterministic, reproducible sorting across bus channels),
flight data recorders (verifiable, checksummed statistical descriptors).

**Summary and Project Status** — Say where we are: M1 and M2 complete and verified
on hardware; 1473 host checks plus the on-board suites; 0.757 ms per block and
3.01× on four cores. Then the next steps, honestly: M3 is the client/server IPC and
the rolling median filter, M4 the pulse wiring and the end-to-end timing — the
protocol half of both already exists, and the shared region the results will use is
already defined and tested.

*Two lines in the deck are worth correcting before the presentation:*
`proto_checksum_block(...)` on the block-loader code slide is not a function we
have, and the merge-sort testbench slide says the invariance test covers
"1, 2, 4 and 8" threads, while the test actually uses **1, 2, 3, 5 and 8** (the
*benchmark* sweep is the 1/2/4 one).

---

## Numbers to have ready

| Fact | Value |
| --- | --- |
| One sample | one float32 reading = 4 bytes |
| One parcel | 16 384 numbers = exactly 64 KB |
| One recording | 60 000 readings = 3 full parcels + 10 848 |
| Sort plan with 4 workers | 4 partitions, 2 merge phases, 7 tasks |
| Time per parcel on the board | 0.757 ms (one worker: 2.278 ms, so 3.01×) |
| Speed versus the data | 163.84 s of signal sorted in 0.757 ms ≈ 200 000× real time |
| Tests | 48 tests, 1473 checks on the host; the sort's 578 also pass on the board |
| Boot image | 37 742 192 B — seven programs and the data inside one file |

---

## A five-line version, if you get sixty seconds

> The client replays sensor data and offloads it in 64 KB parcels, each with a
> label and a checksum so the server can refuse anything damaged. The server sorts
> a parcel with four threads — split it into four quarters, sort each, then merge
> pairs until one sorted pile remains — and filters it with a rolling median to
> drop sensor spikes. When it is finished it taps the client with a pulse, and the
> client fetches the median and the address of the results. On the Raspberry Pi
> one parcel takes 0.757 ms with four workers against 2.278 ms with one.
