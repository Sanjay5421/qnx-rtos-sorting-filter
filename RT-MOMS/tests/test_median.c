/*
 * test_median.c
 *
 * Host-side tests for app/median.c: the rolling median (Hampel) filter and the
 * median of a sorted array. Needs no payload and no QNX.
 *
 *   gcc -std=c99 -Wall -Wextra -O2 -g -o test_median test_median.c \
 *       ../app/median.c -lm
 *   ./test_median
 *
 * The key test is test_matches_reference: an independent brute-force
 * implementation, written from the definition in median.h with qsort, is
 * compared bit for bit with the module on noisy data at several widths.
 */

#include "../app/median.h"
#include "test_util.h"

#include <math.h>
#include <stdlib.h>

#define SIGNAL_LEN 5000

/* ------------------------------------------------------------------------- */
/* Deterministic data                                                         */
/* ------------------------------------------------------------------------- */

static uint32_t rng_state = 2463534242u;

static uint32_t rng_next(void)              /* xorshift32 */
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float rng_uniform(void)              /* [0, 1) */
{
    return (float)(rng_next() >> 8) / 16777216.0f;
}

static float rng_gaussian(void)             /* mean 0, sigma 1 (sum of 12) */
{
    float sum = 0.0f;
    int i;

    for (i = 0; i < 12; i++) {
        sum += rng_uniform();
    }
    return sum - 6.0f;
}

/* A slowly falling curve, standing in for a discharge voltage. */
static float curve(size_t i)
{
    return 4.2f - 1.4f * (float)i / (float)SIGNAL_LEN
           + 0.05f * sinf((float)i * 0.01f);
}

/* ------------------------------------------------------------------------- */
/* Independent reference, straight from the definition                        */
/* ------------------------------------------------------------------------- */

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a;
    float y = *(const float *)b;

    return (x > y) - (x < y);
}

static float ref_median(float *v, size_t count)
{
    qsort(v, count, sizeof(float), cmp_float);
    if (count % 2 == 1) {
        return v[count / 2];
    }
    return (v[count / 2 - 1] + v[count / 2]) * 0.5f;
}

static uint32_t ref_filter(const float *in, size_t n, unsigned width,
                           float threshold, float *out)
{
    long half = (long)(width - 1) / 2;
    uint32_t replaced = 0;
    long i;

    for (i = 0; i < (long)n; i++) {
        float w[MEDIAN_MAX_WIDTH];
        float d[MEDIAN_MAX_WIDTH];
        long lo = (i - half < 0) ? 0 : i - half;
        long hi = (i + half > (long)n - 1) ? (long)n - 1 : i + half;
        size_t count = (size_t)(hi - lo + 1);
        size_t k;
        float med;
        float mad;

        for (k = 0; k < count; k++) {
            w[k] = in[lo + (long)k];
        }
        med = ref_median(w, count);
        for (k = 0; k < count; k++) {
            d[k] = fabsf(in[lo + (long)k] - med);
        }
        mad = ref_median(d, count);
        if (fabsf(in[i] - med) > threshold * 1.4826f * mad) {
            out[i] = med;
            replaced++;
        } else {
            out[i] = in[i];
        }
    }
    return replaced;
}

/* A noisy, spiky signal. truth[] receives the clean curve. */
static void make_signal(float *noisy, float *truth, size_t n,
                        float sigma, size_t spike_every)
{
    size_t i;

    for (i = 0; i < n; i++) {
        truth[i] = curve(i);
        noisy[i] = truth[i] + sigma * rng_gaussian();
    }
    if (spike_every > 0) {
        for (i = spike_every; i < n; i += spike_every) {
            float size = (10.0f + 30.0f * rng_uniform()) * sigma;

            noisy[i] += (i % 2 == 0) ? size : -size;
        }
    }
}

static double rms_error(const float *a, const float *b, size_t n)
{
    double sum = 0.0;
    size_t i;

    for (i = 0; i < n; i++) {
        double d = (double)a[i] - (double)b[i];

        sum += d * d;
    }
    return sqrt(sum / (double)n);
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                      */
/* ------------------------------------------------------------------------- */

static void test_config(void)
{
    median_config_t cfg;

    median_config_default(&cfg);
    T_CHECK_INT(cfg.width, 9);
    T_CHECK_NEAR(cfg.threshold, 4.0, 1e-9);
    T_CHECK(median_config_valid(&cfg));

    T_CHECK(!median_config_valid(NULL));
    cfg.width = 8;                      /* even: no centre sample */
    T_CHECK(!median_config_valid(&cfg));
    cfg.width = 1;
    T_CHECK(!median_config_valid(&cfg));
    cfg.width = 33;
    T_CHECK(!median_config_valid(&cfg));
    cfg.width = MEDIAN_MIN_WIDTH;
    T_CHECK(median_config_valid(&cfg));
    cfg.width = MEDIAN_MAX_WIDTH;
    T_CHECK(median_config_valid(&cfg));
    cfg.threshold = 0.0f;
    T_CHECK(!median_config_valid(&cfg));
    cfg.threshold = -1.0f;
    T_CHECK(!median_config_valid(&cfg));
    cfg.threshold = NAN;
    T_CHECK(!median_config_valid(&cfg));
}

static void test_rejects_bad_arguments(void)
{
    median_config_t cfg;
    float in[16] = {0};
    float out[16];
    uint32_t count = 77;

    median_config_default(&cfg);
    T_CHECK_INT(median_filter_signal(NULL, in, 16, out, &count), -1);
    T_CHECK_INT(median_filter_signal(&cfg, NULL, 16, out, &count), -1);
    T_CHECK_INT(median_filter_signal(&cfg, in, 16, NULL, &count), -1);
    /* A context length without a context pointer. */
    T_CHECK_INT(median_filter_block(&cfg, NULL, 4, in, 16, NULL, 0, out,
                                    &count), -1);
    T_CHECK_INT(median_filter_block(&cfg, NULL, 0, in, 16, NULL, 4, out,
                                    &count), -1);
    /* In-place would read values the loop has already overwritten. */
    T_CHECK_INT(median_filter_signal(&cfg, in, 16, in, &count), -1);
    T_CHECK_INT(count, 77);             /* untouched on failure */
}

static void test_empty_and_tiny(void)
{
    median_config_t cfg;
    float in[3] = {1.0f, 100.0f, 2.0f};
    float out[3] = {0};
    float ref[3];
    uint32_t count = 99;
    uint32_t ref_count;

    median_config_default(&cfg);

    T_CHECK_INT(median_filter_signal(&cfg, in, 0, out, &count), 0);
    T_CHECK_INT(count, 0);

    /* Shorter than the window: the window is whatever exists. */
    T_CHECK_INT(median_filter_signal(&cfg, in, 3, out, &count), 0);
    ref_count = ref_filter(in, 3, cfg.width, cfg.threshold, ref);
    T_CHECK_INT(count, ref_count);
    T_CHECK_SAME_FLOAT(out[0], ref[0]);
    T_CHECK_SAME_FLOAT(out[1], ref[1]);
    T_CHECK_SAME_FLOAT(out[2], ref[2]);

    /* A single sample is its own window and cannot be an outlier. */
    in[0] = 5.0f;
    T_CHECK_INT(median_filter_signal(&cfg, in, 1, out, &count), 0);
    T_CHECK_SAME_FLOAT(out[0], 5.0f);
    T_CHECK_INT(count, 0);
}

static void test_clean_signal_untouched(void)
{
    median_config_t cfg;
    float in[SIGNAL_LEN];
    float out[SIGNAL_LEN];
    uint32_t count = 1;
    size_t i;

    median_config_default(&cfg);

    /* Constant: every window has MAD 0 and the centre equals the median. */
    for (i = 0; i < SIGNAL_LEN; i++) {
        in[i] = 3.7f;
    }
    T_CHECK_INT(median_filter_signal(&cfg, in, SIGNAL_LEN, out, &count), 0);
    T_CHECK_INT(count, 0);
    T_CHECK(memcmp(in, out, sizeof(in)) == 0);

    /* A straight ramp: a monotone window's median is its centre. */
    for (i = 0; i < SIGNAL_LEN; i++) {
        in[i] = 0.001f * (float)i;
    }
    T_CHECK_INT(median_filter_signal(&cfg, in, SIGNAL_LEN, out, &count), 0);
    T_CHECK_INT(count, 0);
    T_CHECK(memcmp(in, out, sizeof(in)) == 0);
}

static void test_single_spike_removed(void)
{
    median_config_t cfg;
    float in[64];
    float out[64];
    uint32_t count = 0;
    size_t i;

    median_config_default(&cfg);
    for (i = 0; i < 64; i++) {
        in[i] = 4.0f + 0.001f * (float)(i % 5);   /* a little texture */
    }
    in[30] = 9.0f;                                 /* the spike */

    T_CHECK_INT(median_filter_signal(&cfg, in, 64, out, &count), 0);
    T_CHECK_INT(count, 1);
    T_CHECK(out[30] < 4.01f && out[30] > 3.99f);   /* back on the trend */
    for (i = 0; i < 64; i++) {                     /* everything else kept */
        if (i != 30) {
            T_CHECK_SAME_FLOAT(out[i], in[i]);
        }
    }
    T_CHECK_SAME_FLOAT(in[30], 9.0f);              /* input not modified */
}

static void test_burst_and_dropout(void)
{
    median_config_t cfg;
    float in[100];
    float out[100];
    uint32_t count = 0;
    size_t i;

    median_config_default(&cfg);            /* width 9: bursts up to 4 */
    for (i = 0; i < 100; i++) {
        in[i] = 2.0f + 0.002f * (float)(i % 7);
    }
    in[20] = in[21] = in[22] = 8.0f;        /* a 3-sample burst  */
    in[60] = in[61] = in[62] = 0.0f;        /* a 3-sample dropout */

    T_CHECK_INT(median_filter_signal(&cfg, in, 100, out, &count), 0);
    T_CHECK_INT(count, 6);
    for (i = 20; i <= 22; i++) {
        T_CHECK(out[i] > 1.99f && out[i] < 2.02f);
    }
    for (i = 60; i <= 62; i++) {
        T_CHECK(out[i] > 1.99f && out[i] < 2.02f);
    }

    /* A burst longer than the half-window owns its window: documented limit. */
    for (i = 0; i < 100; i++) {
        in[i] = 2.0f + 0.002f * (float)(i % 7);
    }
    for (i = 40; i < 49; i++) {
        in[i] = 8.0f;                       /* 9 samples = the whole window */
    }
    T_CHECK_INT(median_filter_signal(&cfg, in, 100, out, &count), 0);
    T_CHECK_SAME_FLOAT(out[44], 8.0f);      /* survives, by design */
}

static void test_matches_reference(void)
{
    static const unsigned widths[] = {3, 5, 9, 15, 31};
    static float noisy[SIGNAL_LEN];
    static float truth[SIGNAL_LEN];
    static float got[SIGNAL_LEN];
    static float want[SIGNAL_LEN];
    size_t w;

    make_signal(noisy, truth, SIGNAL_LEN, 0.002f, 67);

    for (w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
        median_config_t cfg;
        uint32_t count = 0;
        uint32_t want_count;

        cfg.width = widths[w];
        cfg.threshold = 3.0f;
        T_CHECK_INT(median_filter_signal(&cfg, noisy, SIGNAL_LEN, got,
                                         &count), 0);
        want_count = ref_filter(noisy, SIGNAL_LEN, cfg.width, cfg.threshold,
                                want);
        T_CHECK_INT(count, want_count);
        T_CHECK(memcmp(got, want, sizeof(got)) == 0);   /* bit for bit */
    }
}

/* Filtering block by block, with context, equals filtering the whole signal. */
static void filter_in_chunks(const median_config_t *cfg, const float *signal,
                             size_t n, size_t chunk, float *out,
                             uint32_t *total)
{
    size_t half = MEDIAN_HALF_WIDTH(cfg->width);
    size_t start;

    *total = 0;
    for (start = 0; start < n; start += chunk) {
        size_t len = (n - start < chunk) ? n - start : chunk;
        size_t nleft = (start < half) ? start : half;
        size_t after = n - (start + len);
        size_t nright = (after < half) ? after : half;
        uint32_t count = 0;

        T_CHECK_INT(median_filter_block(cfg, signal + start - nleft, nleft,
                                        signal + start, len,
                                        signal + start + len, nright,
                                        out + start, &count), 0);
        *total += count;
    }
}

static void test_blocks_equal_whole_signal(void)
{
    static const size_t chunks[] = {1, 3, 7, 1000, 4999, 5000};
    static float noisy[SIGNAL_LEN];
    static float truth[SIGNAL_LEN];
    static float whole[SIGNAL_LEN];
    static float pieces[SIGNAL_LEN];
    median_config_t cfg;
    uint32_t whole_count = 0;
    size_t c;

    make_signal(noisy, truth, SIGNAL_LEN, 0.002f, 53);
    median_config_default(&cfg);
    T_CHECK_INT(median_filter_signal(&cfg, noisy, SIGNAL_LEN, whole,
                                     &whole_count), 0);
    T_CHECK(whole_count > 0);

    for (c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
        uint32_t total = 0;

        memset(pieces, 0, sizeof(pieces));
        filter_in_chunks(&cfg, noisy, SIGNAL_LEN, chunks[c], pieces, &total);
        T_CHECK(memcmp(whole, pieces, sizeof(whole)) == 0);
        T_CHECK_INT(total, whole_count);
    }
}

/* A spike sitting exactly on a block seam is still removed. */
static void test_spike_on_block_seam(void)
{
    median_config_t cfg;
    float in[40];
    float out[40];
    uint32_t count = 0;
    size_t i;

    median_config_default(&cfg);
    for (i = 0; i < 40; i++) {
        in[i] = 1.0f + 0.001f * (float)(i % 4);
    }
    in[20] = 50.0f;     /* first sample of the second block */

    /* Without context the second block cannot see the first block's samples,
     * but with it the spike is judged on a full window. */
    T_CHECK_INT(median_filter_block(&cfg, in, 20, in + 20, 20, NULL, 0,
                                    out, &count), 0);
    T_CHECK_INT(count, 1);
    T_CHECK(out[0] < 1.01f);
}

static void test_spikes_removed_in_noise(void)
{
    static float noisy[SIGNAL_LEN];
    static float truth[SIGNAL_LEN];
    static float clean[SIGNAL_LEN];
    const float sigma = 0.002f;
    median_config_t cfg;
    uint32_t count = 0;
    size_t spikes = 0;
    size_t missed = 0;
    size_t i;
    double before;
    double after;

    rng_state = 12345u;     /* own data: independent of the other tests */
    make_signal(noisy, truth, SIGNAL_LEN, sigma, 67);
    for (i = 67; i < SIGNAL_LEN; i += 67) {
        spikes++;
    }

    median_config_default(&cfg);
    T_CHECK_INT(median_filter_signal(&cfg, noisy, SIGNAL_LEN, clean,
                                     &count), 0);

    /* Injected spikes (10-40 sigma) are removed. Recall is statistical: over
     * 100 seeds the default misses 0.28 % of spikes (a 10 sigma spike whose
     * window MAD came out large), so allow one miss here rather than tune the
     * expectation to one random sequence. */
    for (i = 67; i < SIGNAL_LEN; i += 67) {
        if (fabsf(clean[i] - truth[i]) >= 6.0f * sigma) {
            missed++;
        }
    }
    T_CHECK(missed <= 1);
    /* All spikes were counted, and the false alarms stay rare. */
    T_CHECK((size_t)count + missed >= spikes);
    T_CHECK((size_t)count <= spikes + SIGNAL_LEN / 50);   /* < 2 % false alarms */

    /* The error against the clean curve falls to the noise floor. Samples the
     * filter keeps still carry their Gaussian noise (about 1 sigma), so that
     * is the best it can do; the spikes alone lift the raw error to ~3 sigma. */
    before = rms_error(noisy, truth, SIGNAL_LEN);
    after = rms_error(clean, truth, SIGNAL_LEN);
    T_CHECK(before > 2.5 * (double)sigma);
    T_CHECK(after < before * 0.4);
    T_CHECK(after < 1.2 * (double)sigma);
}

static void test_median_of_sorted(void)
{
    static float big[16384];
    float odd[5] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    float even[4] = {1.0f, 2.0f, 4.0f, 8.0f};
    float one[1] = {42.0f};
    size_t i;

    T_CHECK_SAME_FLOAT(median_of_sorted(odd, 5), 3.0f);
    T_CHECK_SAME_FLOAT(median_of_sorted(even, 4), 3.0f);
    T_CHECK_SAME_FLOAT(median_of_sorted(one, 1), 42.0f);
    T_CHECK_SAME_FLOAT(median_of_sorted(odd, 0), 0.0f);
    T_CHECK_SAME_FLOAT(median_of_sorted(NULL, 5), 0.0f);

    for (i = 0; i < 16384; i++) {           /* one 64 KiB block */
        big[i] = (float)i;
    }
    T_CHECK_SAME_FLOAT(median_of_sorted(big, 16384), 8191.5f);
}

/* The global median comes from the RAW data: spikes must not move it, and
 * filtering first would be a different (and unwanted) statistic. */
static void test_global_median_is_of_raw_data(void)
{
    static float raw[SIGNAL_LEN];
    static float truth[SIGNAL_LEN];
    static float filtered[SIGNAL_LEN];
    static float sorted_raw[SIGNAL_LEN];
    static float sorted_truth[SIGNAL_LEN];
    median_config_t cfg;
    float global;

    make_signal(raw, truth, SIGNAL_LEN, 0.002f, 67);
    median_config_default(&cfg);
    T_CHECK_INT(median_filter_signal(&cfg, raw, SIGNAL_LEN, filtered, NULL),
                0);

    memcpy(sorted_raw, raw, sizeof(raw));
    qsort(sorted_raw, SIGNAL_LEN, sizeof(float), cmp_float);
    global = median_of_sorted(sorted_raw, SIGNAL_LEN);

    /* The raw median sits on the curve's own median to within the noise: the
     * 1.5 % spikes are symmetric in sign and cannot drag it away. */
    memcpy(sorted_truth, truth, sizeof(truth));
    qsort(sorted_truth, SIGNAL_LEN, sizeof(float), cmp_float);
    T_CHECK_NEAR(global, median_of_sorted(sorted_truth, SIGNAL_LEN), 0.006);
    /* Filtering changed the signal but not the raw copy it was given. */
    T_CHECK(memcmp(raw, filtered, sizeof(raw)) != 0);
    T_CHECK(memcmp(raw, sorted_raw, sizeof(raw)) != 0);   /* sorting reordered only its own copy */
}

int main(void)
{
    printf("test_median\n");

    T_RUN(test_config);
    T_RUN(test_rejects_bad_arguments);
    T_RUN(test_empty_and_tiny);
    T_RUN(test_clean_signal_untouched);
    T_RUN(test_single_spike_removed);
    T_RUN(test_burst_and_dropout);
    T_RUN(test_matches_reference);
    T_RUN(test_blocks_equal_whole_signal);
    T_RUN(test_spike_on_block_seam);
    T_RUN(test_spikes_removed_in_noise);
    T_RUN(test_median_of_sorted);
    T_RUN(test_global_median_is_of_raw_data);

    return t_summary();
}
