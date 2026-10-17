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
 * test_mergesort.c
 *
 * Tests for the parallel merge sort (app/mergesort.c).
 *
 * Strategy: every sort is checked against qsort()'s answer for the same values,
 * at sizes that straddle the serial/parallel threshold and the partition
 * boundaries, plus the input shapes that break naive sorts - all-equal values,
 * presorted and reverse-sorted runs, and the float extremes.
 *
 * Two comparisons are made differently on purpose:
 *
 *   - Where the expectation is bit-exact (thread-count invariance, all-equal
 *     input), stability is what makes it exact: a merge takes from the left run
 *     on ties, so equal values keep the order they arrived in whatever the
 *     partitioning, and two thread counts must agree bit for bit.
 *   - Against qsort() the comparison is by value (==), because qsort is not
 *     stable and +0.0 and -0.0 are distinct bit patterns that compare equal.
 *
 * The last two tests need the generated payload; they skip cleanly without it.
 */

#include "../app/block_loader.h"
#include "../app/mergesort.h"
#include "../app/protocol.h"

#include "test_data.h"
#include "test_util.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static mergesort_pool_t *make_pool(unsigned threads)
{
    mergesort_options_t opt;
    mergesort_pool_t   *pool;

    mergesort_options_default(&opt);
    opt.threads = threads;
    pool = ms_pool_create(&opt);
    if (pool == NULL)
        printf("    (no pool with %u threads)\n", threads);
    return pool;
}

/*
 * Deterministic pseudo-random values in [-10, 10] with three decimals, so the
 * streams contain a good number of ties as well as a spread of magnitudes.
 */
static void fill_random(float *a, size_t n, unsigned seed)
{
    uint32_t s = seed | 1u;
    size_t   i;

    for (i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        a[i] = (float)((s >> 8) % 20001u) / 1000.0f - 10.0f;
    }
}

static int is_sorted(const float *a, size_t n)
{
    size_t i;

    for (i = 1; i < n; i++)
        if (!(a[i - 1] <= a[i]))
            return 0;
    return 1;
}

/* Element-wise "same value". == treats +0.0 and -0.0 as the same value, which
 * is the right equivalence for asking whether a sort moved what it was given. */
static int same_values(const float *a, const float *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (!(a[i] == b[i]))
            return 0;
    return 1;
}

static int cmp_float(const void *pa, const void *pb)
{
    float a = *(const float *)pa;
    float b = *(const float *)pb;

    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

static int is_nan(float f)
{
    return !(f == f);
}

/* Self-consistency: the sorted order itself must be a total order. */
static int order_is_consistent(const float *a, size_t n)
{
    size_t i;

    for (i = 1; i < n; i++) {
        if (a[i - 1] > a[i])
            return 0;
        if (cmp_float(&a[i - 1], &a[i]) > 0)
            return 0;
    }
    return 1;
}

/*
 * The workhorse: sort a copy of `input` with the pool and check that the result
 * is ordered and holds exactly the values that went in, against qsort()'s
 * answer for the same input. Only reads `input`.
 */
static void check_matches_qsort(mergesort_pool_t *pool, const float *input,
                                size_t n)
{
    float *work      = malloc(n * sizeof(float));
    float *reference = malloc(n * sizeof(float));
    size_t i;
    int    rc;

    if (work == NULL || reference == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL out of memory for %lu samples\n", (unsigned long)n);
        free(reference);
        free(work);
        return;
    }

    for (i = 0; i < n; i++) {
        work[i]      = input[i];
        reference[i] = input[i];
    }
    qsort(reference, n, sizeof(float), cmp_float);

    rc = ms_sort(pool, work, n);
    T_CHECK_INT(rc, 0);
    T_CHECK(is_sorted(work, n));
    T_CHECK(order_is_consistent(work, n));
    T_CHECK(same_values(work, reference, n));

    free(reference);
    free(work);
}

/* ------------------------------------------------------------------------- */
/* Pool lifecycle and arguments                                               */
/* ------------------------------------------------------------------------- */

static void test_pool_lifecycle(void)
{
    mergesort_options_t opt;
    mergesort_pool_t   *pool;

    ms_pool_destroy(NULL);                 /* a no-op, not a crash */
    T_CHECK_INT(ms_pool_threads(NULL), 0);

    mergesort_options_default(&opt);
    T_CHECK_INT(opt.threads, 0);
    mergesort_options_default(NULL);       /* likewise a no-op */

    pool = make_pool(1);
    T_CHECK(pool != NULL);
    if (pool != NULL) {
        T_CHECK_INT(ms_pool_threads(pool), 1);
        ms_pool_destroy(pool);
    }

    /* A request above the cap is clamped rather than refused. */
    pool = make_pool(MS_MAX_THREADS + 9);
    T_CHECK(pool != NULL);
    if (pool != NULL) {
        unsigned threads = ms_pool_threads(pool);
        T_CHECK(threads >= 1 && threads <= MS_MAX_THREADS);
        ms_pool_destroy(pool);
    }

    /* NULL options mean one worker per online CPU. */
    pool = ms_pool_create(NULL);
    T_CHECK(pool != NULL);
    if (pool != NULL) {
        T_CHECK(ms_pool_threads(pool) >= 1);
        ms_pool_destroy(pool);
    }
}

static void test_argument_checks(void)
{
    mergesort_pool_t *pool = make_pool(2);
    ms_stats_t        stats;
    float             values[8];
    size_t            i;

    fill_random(values, 8, 99u);

    if (pool == NULL)
        return;

    T_CHECK_INT(ms_sort(NULL, values, 8), -1);
    T_CHECK_INT(ms_sort(pool, NULL, 8), -1);
    T_CHECK_INT(ms_sort_serial(NULL, 8), -1);

    /* Fewer than two values is nothing to order, and the buffer is untouched. */
    values[0] = 3.5f;
    T_CHECK_INT(ms_sort(pool, values, 0), 0);
    T_CHECK_INT(ms_sort(pool, values, 1), 0);
    T_CHECK_INT(ms_sort_serial(values, 0), 0);
    T_CHECK_INT(ms_sort_serial(values, 1), 0);
    T_CHECK_SAME_FLOAT(values[0], 3.5f);

    /* An eight-value sort runs on the serial path inside ms_sort, so the
     * counters describe that: one partition, no merge phases, no worker tasks.
     */
    for (i = 0; i < 8; i++)
        values[i] = (float)(8 - i);
    T_CHECK_INT(ms_sort(pool, values, 8), 0);
    T_CHECK(is_sorted(values, 8));
    ms_stats(pool, &stats);
    T_CHECK_INT(stats.threads, 2);
    T_CHECK_INT(stats.partitions, 1);
    T_CHECK_INT(stats.phases, 0);
    T_CHECK_U64(stats.tasks, 0);

    /* ms_stats() writes nothing when either argument is NULL. */
    memset(&stats, 0xA5, sizeof(stats));
    ms_stats(NULL, &stats);
    ms_stats(pool, NULL);
    T_CHECK_INT(stats.threads, 0xA5A5A5A5u);

    ms_pool_destroy(pool);
}

/* ------------------------------------------------------------------------- */
/* Ordering                                                                   */
/* ------------------------------------------------------------------------- */

static void test_small_inputs(void)
{
    mergesort_pool_t *pool = make_pool(4);
    float             values[64];
    size_t            n;

    if (pool == NULL)
        return;

    /* Every size from 0 to 64 takes the serial path inside ms_sort. */
    for (n = 0; n <= 64; n++) {
        fill_random(values, n, (unsigned)(100 + n));
        if (n < 2) {
            T_CHECK_INT(ms_sort(pool, values, n), 0);
        } else {
            check_matches_qsort(pool, values, n);
        }
    }

    /* The serial entry point on its own, including the odd sizes. */
    for (n = 1; n <= 64; n += 7) {
        float copy[64];

        fill_random(copy, n, (unsigned)(7 * n + 1));
        T_CHECK_INT(ms_sort_serial(copy, n), 0);
        T_CHECK(is_sorted(copy, n));
    }
}

static void test_parallel_sizes(void)
{
    static const size_t sizes[] = {
        4095, 4096, 4097, 5000, 8192, 10000, 16384, 16385, 32768, 65536
    };
    mergesort_pool_t *pool = make_pool(4);
    size_t            i;

    if (pool == NULL)
        return;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        size_t n      = sizes[i];
        float *values = malloc(n * sizeof(float));

        if (values == NULL) {
            t_checks++;
            t_failed++;
            printf("    FAIL out of memory for %lu samples\n", (unsigned long)n);
            continue;
        }
        fill_random(values, n, (unsigned)(1000 + i));
        check_matches_qsort(pool, values, n);
        free(values);
    }

    ms_pool_destroy(pool);
}

static void test_sizes_just_above_the_threshold(void)
{
    mergesort_pool_t *pool = make_pool(2);
    size_t            n;

    if (pool == NULL)
        return;

    /* The first parallel sizes, where the partition count and the run sizes
     * disagree (k partitions of ceil(n/k), so the last one is short). */
    for (n = 4096; n <= 4300; n += 13) {
        float *values = malloc(n * sizeof(float));

        if (values == NULL) {
            t_checks++;
            t_failed++;
            printf("    FAIL out of memory for %lu samples\n", (unsigned long)n);
            continue;
        }
        fill_random(values, n, (unsigned)(2000 + n));
        check_matches_qsort(pool, values, n);
        free(values);
    }

    ms_pool_destroy(pool);
}

static void test_thread_count_invariance(void)
{
    static const unsigned counts[] = { 1, 2, 3, 5, 8 };
    const size_t n      = 20000;
    float       *input  = malloc(n * sizeof(float));
    float       *golden = NULL;
    unsigned     c;

    if (input == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL out of memory for the invariance input\n");
        return;
    }
    fill_random(input, n, 4242u);

    for (c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
        mergesort_pool_t *pool = make_pool(counts[c]);
        float            *work = malloc(n * sizeof(float));

        if (pool == NULL || work == NULL) {
            t_checks++;
            t_failed++;
            printf("    FAIL no pool or buffer for %u threads\n", counts[c]);
            free(work);
            ms_pool_destroy(pool);
            continue;
        }

        memcpy(work, input, n * sizeof(float));
        T_CHECK_INT(ms_sort(pool, work, n), 0);
        T_CHECK(is_sorted(work, n));

        if (golden == NULL) {
            golden = malloc(n * sizeof(float));
            if (golden != NULL)
                memcpy(golden, work, n * sizeof(float));
        } else {
            /* Stability is what makes this exact: the same input produces the
             * same order of equal values however it was partitioned. */
            T_CHECK(memcmp(golden, work, n * sizeof(float)) == 0);
        }

        free(work);
        ms_pool_destroy(pool);
    }

    free(golden);
    free(input);
}

static void test_duplicate_values(void)
{
    const size_t      n      = 16384;
    const size_t      bytes  = n * sizeof(float);
    mergesort_pool_t *pool   = make_pool(4);
    float            *values = malloc(bytes);
    float            *before = malloc(bytes);
    size_t            i;

    if (pool == NULL || values == NULL || before == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL no pool or buffers for the duplicate tests\n");
        free(before);
        free(values);
        ms_pool_destroy(pool);
        return;
    }

    /* All the same value: the result must be the input, bit for bit. */
    for (i = 0; i < n; i++)
        values[i] = 3.5f;
    memcpy(before, values, bytes);
    T_CHECK_INT(ms_sort(pool, values, n), 0);
    T_CHECK(memcmp(before, values, bytes) == 0);

    /* Two values, so every merge is decided by ties. */
    for (i = 0; i < n; i++)
        values[i] = (i % 2) ? 1.0f : 2.0f;
    check_matches_qsort(pool, values, n);

    /* The same input sorted in place this time, to pin the boundary between
     * the two halves: everything below n/2 is the smaller value and the rest
     * is the larger one. */
    T_CHECK_INT(ms_sort(pool, values, n), 0);
    T_CHECK_SAME_FLOAT(values[n / 2 - 1], 1.0f);
    T_CHECK_SAME_FLOAT(values[n / 2], 2.0f);
    T_CHECK_SAME_FLOAT(values[n - 1], 2.0f);

    /* A large family of ties, and a sawtooth of short ordered runs. */
    for (i = 0; i < n; i++)
        values[i] = (float)(i % 13) - 6.0f;
    check_matches_qsort(pool, values, n);

    free(before);
    free(values);
    ms_pool_destroy(pool);
}

static void test_presorted_and_reversed(void)
{
    const size_t      n      = 8192;
    mergesort_pool_t *pool   = make_pool(4);
    float            *values = malloc(n * sizeof(float));
    size_t            i;

    if (pool == NULL || values == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL no pool or buffer for the ordered-input tests\n");
        free(values);
        ms_pool_destroy(pool);
        return;
    }

    for (i = 0; i < n; i++)
        values[i] = (float)i;
    check_matches_qsort(pool, values, n);

    for (i = 0; i < n; i++)
        values[i] = (float)(n - i);
    check_matches_qsort(pool, values, n);

    for (i = 0; i < n; i++)
        values[i] = (float)(i % 97);
    check_matches_qsort(pool, values, n);

    free(values);
    ms_pool_destroy(pool);
}

static void test_float_extremes(void)
{
    mergesort_pool_t *pool = make_pool(2);
    float             values[] = {
        FLT_MAX,  -FLT_MAX,
        FLT_MIN,  -FLT_MIN,
        5.877472e-39f, -5.877472e-39f,        /* denormals: FLT_MIN / 2 */
        0.0f,     -0.0f,
        1.0f,     -1.0f,
        FLT_EPSILON, -FLT_EPSILON,
        123.456f, -123.456f,
        1e30f,    -1e30f
    };
    const size_t n = sizeof(values) / sizeof(values[0]);
    float       *big;
    size_t       m = 4096;

    if (pool == NULL)
        return;

    /* The extremes through the serial path, then through the parallel one:
     * the comparisons must agree wherever the values land. */
    check_matches_qsort(pool, values, n);

    big = malloc(m * sizeof(float));
    if (big == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL out of memory for the extreme-value block\n");
        ms_pool_destroy(pool);
        return;
    }
    fill_random(big, m, 777u);
    big[0]     = FLT_MAX;
    big[1]     = -FLT_MAX;
    big[2]     = 0.0f;
    big[3]     = -0.0f;
    big[4]     = 5.877472e-39f;
    big[5]     = -5.877472e-39f;
    big[m - 1] = FLT_MIN;
    check_matches_qsort(pool, big, m);

    free(big);
    ms_pool_destroy(pool);
}

static void test_repeated_sorts(void)
{
    mergesort_pool_t *pool = make_pool(4);
    unsigned          round;

    if (pool == NULL)
        return;

    /* One pool, many sorts: the phase bookkeeping has to reset cleanly between
     * them, including across the jump from the parallel path to the serial one
     * and back. */
    for (round = 0; round < 20; round++) {
        size_t n      = (round % 2) ? 2000u + round : 12000u + round * 37u;
        float *values = malloc(n * sizeof(float));

        if (values == NULL) {
            t_checks++;
            t_failed++;
            printf("    FAIL out of memory on round %u\n", round);
            continue;
        }
        fill_random(values, n, 9000u + round);
        check_matches_qsort(pool, values, n);
        free(values);
    }

    ms_pool_destroy(pool);
}

static void test_stats_describe_the_plan(void)
{
    const size_t      n      = 16384;
    mergesort_pool_t *pool   = make_pool(4);
    float            *values = malloc(n * sizeof(float));
    ms_stats_t        stats;

    if (pool == NULL || values == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL no pool or buffer for the statistics test\n");
        free(values);
        ms_pool_destroy(pool);
        return;
    }

    fill_random(values, n, 55u);
    T_CHECK_INT(ms_sort(pool, values, n), 0);
    T_CHECK(is_sorted(values, n));
    ms_stats(pool, &stats);

    /*
     * 16384 values over 4 workers: n / 1024 wants 16 partitions but there are
     * only 4 workers, so 4 partitions of 4096. Merging 4 runs into 1 takes two
     * phases, of 2 and 1 merges, so 4 + 2 + 1 = 7 tasks, every one of them run
     * by a worker.
     */
    T_CHECK_INT(stats.threads, 4);
    T_CHECK_INT(stats.partitions, 4);
    T_CHECK_INT(stats.phases, 2);
    T_CHECK_U64(stats.tasks, 7);

    free(values);
    ms_pool_destroy(pool);
}

/* ------------------------------------------------------------------------- */
/* The payload the service actually sorts                                     */
/* ------------------------------------------------------------------------- */

static void test_payload_has_no_nan(void)
{
    block_loader_t    *loader;
    proto_block_t     *block = malloc(sizeof(*block));
    proto_block_desc_t desc;
    size_t             scanned = 0;
    size_t             bad     = 0;
    int                got;

    if (block == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL out of memory for a block\n");
        return;
    }

    loader = block_loader_open(td_interleaved_path(), SENSOR_LAYOUT_INTERLEAVED,
                               PROTO_CHANNEL_VOLTAGE, 100u);
    T_CHECK(loader != NULL);
    if (loader == NULL) {
        free(block);
        return;
    }

    while ((got = block_loader_next(loader, block, &desc)) > 0) {
        int i;

        for (i = 0; i < got; i++)
            if (is_nan(block->samples[i]))
                bad++;
        scanned += (size_t)got;
    }
    T_CHECK_INT(got, 0);                   /* the recording ended cleanly */

    /*
     * The sort orders values with <=, which has no useful answer for NaN, so
     * the tests above are only meaningful because the payload has none.
     */
    T_CHECK_SIZE(scanned, 60000);
    T_CHECK_SIZE(bad, 0);

    block_loader_close(loader);
    free(block);
}

static void test_payload_blocks_sort(void)
{
    block_loader_t    *loader;
    mergesort_pool_t  *pool   = make_pool(4);
    proto_block_t     *block  = malloc(sizeof(*block));
    float             *before = malloc(sizeof(block->samples));
    float             *work   = malloc(sizeof(block->samples));
    proto_block_desc_t desc;
    int                blocks = 0;
    int                got;

    if (pool == NULL || block == NULL || before == NULL || work == NULL) {
        t_checks++;
        t_failed++;
        printf("    FAIL no pool or buffers for the payload sort test\n");
        free(work);
        free(before);
        free(block);
        ms_pool_destroy(pool);
        return;
    }

    loader = block_loader_open(td_interleaved_path(), SENSOR_LAYOUT_INTERLEAVED,
                               PROTO_CHANNEL_VOLTAGE, 100u);
    T_CHECK(loader != NULL);
    if (loader == NULL) {
        free(work);
        free(before);
        free(block);
        ms_pool_destroy(pool);
        return;
    }

    while ((got = block_loader_next(loader, block, &desc)) > 0) {
        size_t bytes = (size_t)got * sizeof(float);

        T_CHECK_INT(desc.samples, got);

        /* Sort the declared samples of a copy - the block that arrived stays
         * as it was, because the descriptor's checksum describes it. */
        memcpy(before, block->samples, bytes);
        check_matches_qsort(pool, block->samples, (size_t)got);
        T_CHECK(memcmp(block->samples, before, bytes) == 0);

        /* The range the loader recorded must be the range the sorted block
         * actually has. */
        memcpy(work, block->samples, bytes);
        T_CHECK_INT(ms_sort(pool, work, (size_t)got), 0);
        T_CHECK_SAME_FLOAT(work[0], desc.min_value);
        T_CHECK_SAME_FLOAT(work[got - 1], desc.max_value);

        blocks++;
    }
    T_CHECK_INT(got, 0);
    T_CHECK(blocks > 0);

    block_loader_close(loader);
    free(work);
    free(before);
    free(block);
    ms_pool_destroy(pool);
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int have_data = td_find_dataset(argc, argv);

    printf("parallel merge sort tests (threshold, partitions, phases)\n");
    printf("  dataset: %s\n\n",
           have_data ? td_dir : "(not found -- payload tests will be skipped)");

    T_RUN(test_pool_lifecycle);
    T_RUN(test_argument_checks);
    T_RUN(test_small_inputs);
    T_RUN(test_parallel_sizes);
    T_RUN(test_sizes_just_above_the_threshold);
    T_RUN(test_thread_count_invariance);
    T_RUN(test_duplicate_values);
    T_RUN(test_presorted_and_reversed);
    T_RUN(test_float_extremes);
    T_RUN(test_repeated_sorts);
    T_RUN(test_stats_describe_the_plan);

    if (have_data) {
        T_RUN(test_payload_has_no_nan);
        T_RUN(test_payload_blocks_sort);
    } else {
        T_SKIP("no payload directory: the real-data sort tests not run");
    }

    return t_summary();
}
