/*
 * ============================================================================
 *   Real-Time Multi-Core Sorting and Median Filter Service
 *
 *   The client offloads 64 KiB blocks of unsorted float32 time-series metrics to
 *   the server. The server sorts each block with a parallel merge sort over four
 *   worker threads -- mutexes and condition variables guard the merge phases --
 *   applies a rolling Hampel median filter to remove sensor outliers, and signals
 *   completion with a QNX pulse carrying the median, with the sorted and filtered
 *   arrays left in shared memory for the client to read.
 *
 *   Anmol Govindarajapuram Krishnan   -  CB.EN.U4EEE23103
 *   Mauli Rajguru                     -  CB.EN.U4EEE23120
 *   Sanjay TS                         -  CB.EN.U4EEE23136
 *
 *   26ELC331 Real-Time Operating Systems
 *   Date of evaluation: 17-10-2026
 * ============================================================================
 */

/*
 * test_kpi.c
 *
 * The speed half of the report in one short program, next to the invariants
 * that must survive whatever the speed turns out to be:
 *
 *   1. a 64 KiB block sorted with 1, 2 and 4 workers -- ms per block, MiB/s and
 *      Msamples/s, and the same median every time (the merge is stable, so the
 *      thread count cannot change the answer);
 *   2. the rolling median filter over that block -- what it costs, and that it
 *      leaves clean data alone while finding injected spikes;
 *   3. the global merge the server performs, four sorted blocks into one -- what
 *      it costs, and that its result equals qsort() of the concatenation;
 *   4. the number that ties the two routes together: the global median from the
 *      merged array is bit-identical to the one from re-sorting the whole
 *      recording, which is what the client cross-checks against the server.
 *
 * The timings are printed, not asserted: this file has to pass on a loaded
 * host, in a VM and on the board, and those differ by an order of magnitude.
 * What is asserted is the part that is a matter of correctness.
 */

#include "../app/median.h"
#include "../app/mergesort.h"
#include "../app/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define BLOCK_SAMPLES PROTO_BLOCK_SAMPLES
#define BLOCKS        4u
#define RECORDING     ((size_t)BLOCKS * BLOCK_SAMPLES)
#define REPEATS       20u

static int checks = 0;
static int failures = 0;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        printf("FAIL line %d: %s\n", __LINE__, #expr); \
    } \
} while (0)

static float  block[BLOCK_SAMPLES];
static float  work[BLOCK_SAMPLES];
static float  chunks[BLOCKS][BLOCK_SAMPLES];
static float  recording[RECORDING];
static float  reference[RECORDING];
static float  merged[RECORDING];
static float  scratch[RECORDING];

static double now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0.0;
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

/* Deterministic values: the same block on every host and every run. */
static uint32_t seed = 20251017u;

static float next_value(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)((seed >> 8) & 0xFFFFu) / 256.0f - 128.0f;
}

/* The merge from milestone 2, again: two ascending runs into one. */
static void merge_two(const float *a, size_t na, const float *b, size_t nb,
                      float *out)
{
    size_t i = 0, j = 0, k = 0;

    while (i < na && j < nb) out[k++] = (a[i] <= b[j]) ? a[i++] : b[j++];
    while (i < na) out[k++] = a[i++];
    while (j < nb) out[k++] = b[j++];
}

static int cmp_float(const void *pa, const void *pb)
{
    float a = *(const float *)pa, b = *(const float *)pb;
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

/* Best of REPEATS sorts of the same block on a pool of `threads` workers. */
static double sort_bench(unsigned threads, float *median_out)
{
    mergesort_options_t opt;
    mergesort_pool_t   *pool;
    double              best = 0.0;
    unsigned            r;

    mergesort_options_default(&opt);
    opt.threads = threads;
    pool = ms_pool_create(&opt);
    if (pool == NULL) return -1.0;

    memcpy(work, block, sizeof(work));
    CHECK(ms_sort(pool, work, BLOCK_SAMPLES) == 0);        /* warm up */

    for (r = 0; r < REPEATS; r++) {
        double t0, t1;

        memcpy(work, block, sizeof(work));
        t0 = now_ms();
        CHECK(ms_sort(pool, work, BLOCK_SAMPLES) == 0);
        t1 = now_ms();
        if (best == 0.0 || t1 - t0 < best) best = t1 - t0;
    }
    if (median_out != NULL)
        *median_out = median_of_sorted(work, BLOCK_SAMPLES);
    ms_pool_destroy(pool);
    return best;
}

static void report(const char *what, unsigned workers, double ms)
{
    double mib_per_s = ms > 0.0
        ? ((double)PROTO_BLOCK_BYTES / (1024.0 * 1024.0)) / (ms / 1000.0)
        : 0.0;
    double msamples_per_s = ms > 0.0
        ? ((double)BLOCK_SAMPLES / 1e6) / (ms / 1000.0)
        : 0.0;

    printf("  %-9s %4u  %8.3f ms  %8.1f MiB/s  %8.1f Msamples/s\n",
           what, workers, ms, mib_per_s, msamples_per_s);
}

int main(void)
{
    float           medians[3] = { 0.0f, 0.0f, 0.0f };
    median_config_t cfg;
    unsigned        i, b;
    uint32_t        outliers = 0;
    float          *filtered;
    double          ms1, ms2, ms4, ms_serial, ms_filter, ms_merge;
    double          t0, t1;
    float           global_merged, global_resorted;

    filtered = (float *)malloc(BLOCK_SAMPLES * sizeof(float));
    CHECK(filtered != NULL);
    if (filtered == NULL)
        return 1;

    for (i = 0; i < BLOCK_SAMPLES; i++)
        block[i] = next_value();

    printf("KPI: one 64 KiB block = %u samples = %u B; recording = %u samples\n\n",
           (unsigned)BLOCK_SAMPLES, (unsigned)PROTO_BLOCK_BYTES,
           (unsigned)RECORDING);

    /* --- 1. the sort at 1, 2 and 4 workers -------------------------------- */
    printf("parallel merge sort, best of %u runs:\n", REPEATS);
    printf("  %-9s %4s  %11s  %12s  %14s\n", "stage", "wrk", "per block",
           "throughput", "sample rate");
    ms1 = sort_bench(1, &medians[0]);
    ms2 = sort_bench(2, &medians[1]);
    ms4 = sort_bench(4, &medians[2]);
    CHECK(ms1 > 0.0 && ms2 > 0.0 && ms4 > 0.0);
    report("sort", 1, ms1);
    report("sort", 2, ms2);
    report("sort", 4, ms4);
    printf("  1 -> 4 workers: %.2fx\n", ms4 > 0.0 ? ms1 / ms4 : 0.0);

    /* The answer cannot depend on how many threads produced it. */
    CHECK(medians[0] == medians[1] && medians[1] == medians[2]);

    memcpy(work, block, sizeof(work));
    t0 = now_ms();
    CHECK(ms_sort_serial(work, BLOCK_SAMPLES) == 0);
    t1 = now_ms();
    ms_serial = t1 - t0;
    report("serial", 1, ms_serial);
    CHECK(median_of_sorted(work, BLOCK_SAMPLES) == medians[0]);
    printf("\n");

    /* --- 2. the rolling median filter ------------------------------------- */
    /*
     * "Clean" for this filter means "no impulses", so the clean case here is a
     * smooth signal: a slow ramp with a small ripple, the shape of the
     * recording's own time column. Uniform random data is *not* clean to a
     * Hampel filter -- its local MAD is small, so a couple of per cent of the
     * samples legitimately look unusual to a 4-sigma test. That is why what
     * gets asserted below is that injected spikes are caught and a smooth
     * signal is left alone, not that noise has a particular rate.
     */
    for (i = 0; i < BLOCK_SAMPLES; i++)
        block[i] = 100.0f + 0.001f * (float)i + 0.5f * (float)(i % 7);

    median_config_default(&cfg);
    t0 = now_ms();
    CHECK(median_filter_signal(&cfg, block, BLOCK_SAMPLES, filtered,
                               &outliers) == 0);
    t1 = now_ms();
    ms_filter = t1 - t0;
    printf("rolling median filter (width %u, threshold %.1f sigma):\n",
           cfg.width, (double)cfg.threshold);
    printf("  clean block: %8.3f ms, %u replacements\n", ms_filter, outliers);
    CHECK(outliers == 0);                    /* a smooth signal is left alone */

    for (i = 100; i < BLOCK_SAMPLES; i += 2048)
        block[i] += 1000.0f;                 /* 8 spikes, 1000x the spread */
    t0 = now_ms();
    CHECK(median_filter_signal(&cfg, block, BLOCK_SAMPLES, filtered,
                               &outliers) == 0);
    t1 = now_ms();
    ms_filter = t1 - t0;
    printf("  spiked block: %7.3f ms, %u replacements for 8 injected spikes\n",
           ms_filter, outliers);
    CHECK(outliers >= 8);                    /* every spike, and its neighbours */
    for (i = 100; i < BLOCK_SAMPLES; i += 2048)
        block[i] -= 1000.0f;
    printf("\n");

    /* --- 3. the global merge, and 4. the two routes to the global median --- */
    for (b = 0; b < BLOCKS; b++) {
        size_t k;

        for (k = 0; k < BLOCK_SAMPLES; k++)
            chunks[b][k] = next_value();
        memcpy(recording + (size_t)b * BLOCK_SAMPLES, chunks[b], sizeof(chunks[b]));
        CHECK(ms_sort_serial(chunks[b], BLOCK_SAMPLES) == 0);
    }

    t0 = now_ms();
    merge_two(chunks[0], BLOCK_SAMPLES, chunks[1], BLOCK_SAMPLES, merged);
    merge_two(merged, 2 * BLOCK_SAMPLES, chunks[2], BLOCK_SAMPLES, scratch);
    merge_two(scratch, 3 * BLOCK_SAMPLES, chunks[3], BLOCK_SAMPLES, merged);
    t1 = now_ms();
    ms_merge = t1 - t0;

    printf("global merge over the recording's four sorted blocks:\n");
    printf("  %.3f ms for %u samples (%.1f MiB/s)\n", ms_merge,
           (unsigned)RECORDING,
           ms_merge > 0.0
               ? ((double)RECORDING * 4.0 / (1024.0 * 1024.0)) / (ms_merge / 1000.0)
               : 0.0);

    /* The oracle: qsort of the concatenation, then the same comparisons. */
    memcpy(reference, recording, sizeof(reference));
    qsort(reference, RECORDING, sizeof(float), cmp_float);
    CHECK(memcmp(merged, reference, sizeof(merged)) == 0);

    global_merged = median_of_sorted(merged, RECORDING);
    memcpy(scratch, recording, sizeof(scratch));
    CHECK(ms_sort_serial(scratch, RECORDING) == 0);
    global_resorted = median_of_sorted(scratch, RECORDING);
    CHECK(proto_float_to_pulse(global_merged) == proto_float_to_pulse(global_resorted));
    printf("  global median, merged %.9g == re-sorted %.9g (bit for bit)\n",
           (double)global_merged, (double)global_resorted);
    printf("\n");

    printf("%d checks, %d failures\n", checks, failures);
    free(filtered);
    return failures == 0 ? 0 : 1;
}
