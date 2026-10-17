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
 * test_sensor.c
 *
 * Host-side tests for the virtual sensor (app/sensor.c).
 *
 *   ./test_sensor [dataset_dir]
 *
 * The first group of tests is self-contained: it writes synthetic float32
 * files next to the working directory and reads them back, so it runs anywhere
 * with no dataset and no QNX.
 *
 * The second group drives the sensor over the real generated payload and is
 * skipped, not failed, when that payload is not present. It lives here rather
 * than in a separate integration file because the interesting properties —
 * that the two on-disk layouts agree bit for bit, and that 60000 samples fall
 * into three full 64 KiB blocks plus 10848 samples — are properties of the
 * sensor, not of the client.
 *
 * The dataset directory is taken from the first argument, then from
 * BATTERY_DATASET_DIR, then guessed from the two directories the tests are
 * normally run in.
 */

#include "../app/sensor.h"
#include "test_data.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <time.h>
#endif

/* Synthetic fixtures, written into the working directory. */
#define TMP_PLANAR  "sensor_tmp_planar.bin"   /* 16-float ramp                 */
#define TMP_RAMP    "sensor_tmp_ramp.bin"     /* 10000-float ramp              */
#define TMP_RECORDS "sensor_tmp_records.bin"  /* 6 records of 4 floats         */
#define TMP_TINY    "sensor_tmp_tiny.bin"     /* 3 bytes: not a whole float32  */
#define TMP_EMPTY   "sensor_tmp_empty.bin"    /* 0 bytes                       */
#define TMP_PARTIAL "sensor_tmp_partial.bin"  /* 6 floats: 1.5 records         */

#define RAMP_LONG 10000
#define PAYLOAD_BLOCK_SAMPLES 16384  /* 64 KiB of float32 */
#define PAYLOAD_RECORD_SAMPLES 60000 /* one 60 000-sample recording */

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static int write_floats(const char *path, const float *values, size_t count)
{
    FILE  *fp;
    size_t written;

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror(path);
        return -1;
    }
    written = fwrite(values, sizeof(float), count, fp);
    if (written != count) {
        fprintf(stderr, "%s: short write (%lu of %lu values)\n", path,
                (unsigned long)written, (unsigned long)count);
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) {
        perror(path);
        return -1;
    }
    return 0;
}

static int write_bytes(const char *path, const void *bytes, size_t count)
{
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        perror(path);
        return -1;
    }
    if (count > 0 && fwrite(bytes, 1, count, fp) != count) {
        fprintf(stderr, "%s: short write\n", path);
        fclose(fp);
        return -1;
    }
    return (fclose(fp) == 0) ? 0 : -1;
}

/* dst[i] = i / 2: exact in float32 for every index used here. */
static void make_ramp(float *dst, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        dst[i] = (float)i / 2.0f;
}

/* Monotonic milliseconds for the pacing test. */
static uint64_t host_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount();
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000l);
#endif
}

/* ------------------------------------------------------------------------- */
/* Configuration                                                              */
/* ------------------------------------------------------------------------- */

static void test_config_defaults(void)
{
    sensor_config_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    sensor_config_default(&cfg);

    T_CHECK_INT(cfg.layout, SENSOR_LAYOUT_PLANAR);
    T_CHECK_INT(cfg.offset, 0);
    T_CHECK_INT(cfg.stride, 1);
    T_CHECK_SIZE(cfg.frame_samples, SENSOR_DEFAULT_FRAME_SAMPLES);
    T_CHECK_SIZE(cfg.rate_hz, SENSOR_DEFAULT_RATE_HZ);
    T_CHECK_INT(cfg.loop, 0);

    sensor_config_default(NULL);   /* must not crash */
}

static void test_open_rejects_bad_input(void)
{
    sensor_config_t cfg;
    sensor_t       *s;
    float           ramp[16];

    make_ramp(ramp, 16);
    T_CHECK(write_floats(TMP_PLANAR, ramp, 16) == 0);
    T_CHECK(write_floats(TMP_PARTIAL, ramp, 6) == 0);
    T_CHECK(write_bytes(TMP_TINY, "abc", 3) == 0);
    T_CHECK(write_bytes(TMP_EMPTY, "x", 0) == 0);

    T_CHECK(sensor_open(NULL, NULL) == NULL);
    T_CHECK(sensor_open("sensor_no_such_file.bin", NULL) == NULL);

    sensor_config_default(&cfg);
    cfg.layout = 99;
    T_CHECK(sensor_open(TMP_PLANAR, &cfg) == NULL);

    sensor_config_default(&cfg);
    cfg.stride = 0;
    T_CHECK(sensor_open(TMP_PLANAR, &cfg) == NULL);

    sensor_config_default(&cfg);
    cfg.offset = 5;                    /* offset must be < stride */
    T_CHECK(sensor_open(TMP_PLANAR, &cfg) == NULL);

    sensor_config_default(&cfg);
    cfg.offset = 1;                    /* a planar file takes no offset */
    T_CHECK(sensor_open(TMP_PLANAR, &cfg) == NULL);

    sensor_config_default(&cfg);
    T_CHECK(sensor_open(TMP_TINY, &cfg) == NULL);     /* 3 bytes              */
    T_CHECK(sensor_open(TMP_EMPTY, &cfg) == NULL);    /* 0 bytes              */

    sensor_config_default(&cfg);
    cfg.layout = SENSOR_LAYOUT_INTERLEAVED;
    cfg.stride = 4;
    cfg.offset = 1;
    T_CHECK(sensor_open(TMP_PARTIAL, &cfg) == NULL);  /* 6 floats: 1.5 records */

    /* frame_samples and rate_hz of 0 mean "use the default", not "invalid" */
    sensor_config_default(&cfg);
    cfg.frame_samples = 0;
    cfg.rate_hz = 0;
    s = sensor_open(TMP_PLANAR, &cfg);
    T_CHECK(s != NULL);
    sensor_close(s);
    sensor_close(NULL);                                /* must not crash        */
}

/* ------------------------------------------------------------------------- */
/* Value fidelity                                                             */
/* ------------------------------------------------------------------------- */

static void test_exact_bits_round_trip(void)
{
    /*
     * Values chosen so that any conversion anywhere along the path would show
     * up: signed zero, +-1, a value needing the full mantissa, the smallest
     * normal float, a subnormal, and 2*pi.
     */
    const float written[8] = {
        0.0f, -0.0f,
        1.0f, -1.0f,
        3.14159265f,
        1.17549435e-38f,   /* FLT_MIN */
        1.0e-40f,          /* subnormal */
        6.28318530f
    };
    sensor_config_t cfg;
    sensor_t       *s;
    float           got[8];
    int             i;

    T_CHECK(write_floats(TMP_PLANAR, written, 8) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 1;            /* one sample per read: smallest step */

    s = sensor_open(TMP_PLANAR, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    T_CHECK_SIZE(sensor_total_samples(s), 8);
    for (i = 0; i < 8; i++)
        T_CHECK_INT(sensor_read_frame(s, &got[i]), 1);

    for (i = 0; i < 8; i++)
        T_CHECK_SAME_FLOAT(got[i], written[i]);

    sensor_close(s);
}

static void test_strided_layout(void)
{
    /*
     * Six records of four floats, each "channel" recognisable on its own, so a
     * wrong offset or stride cannot pass by accident.
     */
    float           records[6 * 4];
    sensor_config_t cfg;
    sensor_t       *s;
    float           got[6];
    int             r;

    for (r = 0; r < 6; r++) {
        records[r * 4 + 0] = (float)r;            /* channel 0 */
        records[r * 4 + 1] = 100.0f + (float)r;   /* channel 1 */
        records[r * 4 + 2] = 200.0f + (float)r;   /* channel 2 */
        records[r * 4 + 3] = 300.0f + (float)r;   /* channel 3 */
    }
    T_CHECK(write_floats(TMP_RECORDS, records, 6 * 4) == 0);

    for (r = 0; r < 4; r++) {
        sensor_config_default(&cfg);
        cfg.layout = SENSOR_LAYOUT_INTERLEAVED;
        cfg.stride = 4;
        cfg.offset = r;
        cfg.frame_samples = 6;

        s = sensor_open(TMP_RECORDS, &cfg);
        T_CHECK(s != NULL);
        if (s == NULL)
            continue;

        T_CHECK_SIZE(sensor_total_samples(s), 6);
        T_CHECK_INT(sensor_read_frame(s, got), 6);
        T_CHECK_SAME_FLOAT(got[0], (float)(r * 100));
        T_CHECK_SAME_FLOAT(got[5], (float)(r * 100 + 5));
        sensor_close(s);
    }
}

/* ------------------------------------------------------------------------- */
/* Stream semantics                                                           */
/* ------------------------------------------------------------------------- */

static void test_frame_cap(void)
{
    float           ramp[100];
    sensor_config_t cfg;
    sensor_t       *s;
    float           got[128];
    int             n;
    int             frames;

    make_ramp(ramp, 100);
    T_CHECK(write_floats(TMP_RAMP, ramp, 100) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 8;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    /* Asking for more than one frame still yields exactly one frame. */
    T_CHECK_INT(sensor_read(s, got, 100), 8);
    frames = 1;

    while ((n = sensor_read_frame(s, got)) > 0)
        frames++;

    T_CHECK_INT(frames, 13);   /* 12 full frames of 8, then a frame of 4 */
    T_CHECK_INT(n, 0);         /* and then the end of the recording      */
    T_CHECK_SIZE(sensor_remaining(s), 0);

    sensor_close(s);
}

static void test_loop_wraps(void)
{
    float           ramp[10];
    float           got[4];
    sensor_config_t cfg;
    sensor_t       *s;

    make_ramp(ramp, 10);
    T_CHECK(write_floats(TMP_RAMP, ramp, 10) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 4;
    cfg.loop = 1;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    T_CHECK_INT(sensor_read(s, got, 4), 4);   /* 0..3 */
    T_CHECK_INT(sensor_read(s, got, 4), 4);   /* 4..7 */
    T_CHECK_INT(sensor_read(s, got, 4), 4);   /* 8, 9, then wraps to 0, 1 */
    T_CHECK_SAME_FLOAT(got[0], ramp[8]);
    T_CHECK_SAME_FLOAT(got[1], ramp[9]);
    T_CHECK_SAME_FLOAT(got[2], ramp[0]);
    T_CHECK_SAME_FLOAT(got[3], ramp[1]);

    /* The timeline keeps counting across the wrap: a live sensor never ends. */
    T_CHECK_U64(sensor_index(s), 12);
    T_CHECK_SIZE(sensor_remaining(s), 8);
    T_CHECK_INT(sensor_read(s, got, 1), 1);

    sensor_close(s);
}

static void test_reset_restarts(void)
{
    float           ramp[20];
    sensor_config_t cfg;
    sensor_t       *s;
    float           got[5];

    make_ramp(ramp, 20);
    T_CHECK(write_floats(TMP_RAMP, ramp, 20) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 5;
    cfg.rate_hz = 1000;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    T_CHECK_INT(sensor_read(s, got, 5), 5);
    T_CHECK_SAME_FLOAT(got[0], ramp[0]);
    T_CHECK_U64(sensor_index(s), 5);
    T_CHECK_U64(sensor_timestamp_ns(s), 5000000ull);   /* 5 samples at 1 kHz */

    T_CHECK_INT(sensor_reset(s), 0);
    T_CHECK_U64(sensor_index(s), 0);
    T_CHECK_U64(sensor_timestamp_ns(s), 0);
    T_CHECK_SIZE(sensor_remaining(s), 20);

    T_CHECK_INT(sensor_read(s, got, 5), 5);
    T_CHECK_SAME_FLOAT(got[0], ramp[0]);               /* same stream again */

    sensor_close(s);
}

static void test_window_boundaries(void)
{
    /*
     * 10000 samples is more than twice the sensor's read-ahead window, and a
     * frame of 7 divides neither, so this exercises refills at unaligned
     * offsets. Every sample is compared against the ramp it was written from.
     */
    static float ramp[RAMP_LONG];
    static float got[7];
    sensor_config_t cfg;
    sensor_t       *s;
    size_t          index = 0;
    unsigned long   bad = 0;
    int             n;
    int             k;

    make_ramp(ramp, RAMP_LONG);
    T_CHECK(write_floats(TMP_RAMP, ramp, RAMP_LONG) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 7;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    T_CHECK_SIZE(sensor_total_samples(s), RAMP_LONG);

    while ((n = sensor_read(s, got, 7)) > 0) {
        for (k = 0; k < n; k++) {
            if (t_bits(got[k]) != t_bits(ramp[index]))
                bad++;
            index++;
        }
    }

    T_CHECK_INT(bad, 0);
    T_CHECK_SIZE(index, RAMP_LONG);
    T_CHECK_SIZE(sensor_remaining(s), 0);

    sensor_close(s);
}

static void test_timestamps(void)
{
    sensor_config_t cfg;
    sensor_t       *s;
    float           got[100];

    static float ramp[1300];
    make_ramp(ramp, 1300);
    T_CHECK(write_floats(TMP_RAMP, ramp, 1300) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 100;
    cfg.rate_hz = 1000;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    while (sensor_index(s) < 1234) {
        size_t want = 1234 - (size_t)sensor_index(s);
        if (want > 100)
            want = 100;
        T_CHECK_INT(sensor_read(s, got, want), (int)want);
    }
    T_CHECK_U64(sensor_index(s), 1234);
    T_CHECK_U64(sensor_timestamp_ns(s), 1234000000ull);

    sensor_close(s);

    /* A rate that does not divide a second still yields an exact timestamp. */
    sensor_config_default(&cfg);
    cfg.rate_hz = 7;
    cfg.frame_samples = 7;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;
    T_CHECK_INT(sensor_read(s, got, 7), 7);
    T_CHECK_U64(sensor_timestamp_ns(s), 1000000000ull);

    sensor_close(s);
}

static void test_block_read(void)
{
    static float    ramp[RAMP_LONG];
    static float    block[PAYLOAD_BLOCK_SAMPLES];
    sensor_config_t cfg;
    sensor_t       *s;
    size_t          total = 0;
    int             n;
    int             blocks = 0;

    make_ramp(ramp, RAMP_LONG);
    T_CHECK(write_floats(TMP_RAMP, ramp, RAMP_LONG) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 128;

    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    /* A block larger than what is left returns the remainder, not an error. */
    T_CHECK_INT(sensor_read_block(s, block, PAYLOAD_BLOCK_SAMPLES), RAMP_LONG);
    sensor_close(s);

    /* Whole 64 KiB blocks come out full until the tail. */
    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    while ((n = sensor_read_block(s, block, PAYLOAD_BLOCK_SAMPLES)) > 0) {
        blocks++;
        total += (size_t)n;
    }
    T_CHECK_INT(blocks, 1);   /* one short block covers the whole ramp */
    T_CHECK_SIZE(total, RAMP_LONG);
    sensor_close(s);

    /* Exact division: 10000 samples in 1000-sample blocks is ten full blocks. */
    s = sensor_open(TMP_RAMP, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    total = 0;
    while ((n = sensor_read_block(s, block, 1000)) > 0)
        total += (size_t)n;
    T_CHECK_SIZE(total, RAMP_LONG);
    T_CHECK_SIZE(sensor_remaining(s), 0);
    sensor_close(s);
}

static void test_paced_reads(void)
{
    static float    ramp[16];
    float           got[10];
    sensor_config_t cfg;
    sensor_t       *s;
    uint64_t        elapsed;
    uint64_t        started;
    int             i;

    make_ramp(ramp, 16);
    T_CHECK(write_floats(TMP_PLANAR, ramp, 16) == 0);

    sensor_config_default(&cfg);
    cfg.frame_samples = 1;
    cfg.rate_hz = 50;                 /* 20 ms per sample */

    s = sensor_open(TMP_PLANAR, &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    started = host_now_ms();
    for (i = 0; i < 10; i++)
        T_CHECK_INT(sensor_read_paced(s, &got[i], 1), 1);
    elapsed = host_now_ms() - started;

    /*
     * Lower bound only. Ten samples at 50 Hz is 200 ms; anything from 150 ms
     * up means pacing is holding the rate. The upper bound is deliberately not
     * asserted, because a loaded host can always be slower than the rate.
     */
    T_CHECK(elapsed >= 150);

    /* Pacing must not disturb the values: it is the same stream. */
    for (i = 0; i < 10; i++)
        T_CHECK_SAME_FLOAT(got[i], ramp[i]);

    sensor_close(s);
}

/* ------------------------------------------------------------------------- */
/* The real payload                                                           */
/* ------------------------------------------------------------------------- */

static void test_real_layouts_agree(void)
{
    sensor_config_t cfg;
    sensor_t       *planar;
    sensor_t       *strided;
    float           a[128];
    float           b[128];
    unsigned long   compared = 0;
    unsigned long   bad = 0;
    size_t          index = 0;
    int             na;
    int             nb;
    int             k;

    planar = sensor_open(td_voltage_path(), NULL);

    sensor_config_default(&cfg);
    cfg.layout = SENSOR_LAYOUT_INTERLEAVED;
    cfg.stride = 4;
    cfg.offset = 1;    /* record = [time_s, voltage_V, current_A, temperature_K] */
    strided = sensor_open(td_interleaved_path(), &cfg);

    T_CHECK(planar != NULL);
    T_CHECK(strided != NULL);
    if (planar == NULL || strided == NULL) {
        sensor_close(planar);
        sensor_close(strided);
        return;
    }

    T_CHECK_SIZE(sensor_total_samples(planar), PAYLOAD_RECORD_SAMPLES);
    T_CHECK_SIZE(sensor_total_samples(strided), PAYLOAD_RECORD_SAMPLES);

    do {
        na = sensor_read(planar, a, 128);
        nb = sensor_read(strided, b, 128);
        T_CHECK_INT(na, nb);
        if (na != nb)
            break;
        for (k = 0; k < na; k++) {
            if (index % 97 == 0) {          /* sample the streams, not all of it */
                compared++;
                if (t_bits(a[k]) != t_bits(b[k]))
                    bad++;
            }
            index++;
        }
    } while (na == 128);

    T_CHECK_INT(bad, 0);
    T_CHECK(compared > 100);
    T_CHECK_SIZE(index, PAYLOAD_RECORD_SAMPLES);

    sensor_close(planar);
    sensor_close(strided);
}

static void test_real_block_partition(void)
{
    static float    block[PAYLOAD_BLOCK_SAMPLES];
    sensor_config_t cfg;
    sensor_t       *s;
    int             n;
    int             full_blocks = 0;
    size_t          total = 0;

    sensor_config_default(&cfg);
    cfg.frame_samples = 128;

    s = sensor_open(td_voltage_path(), &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    n = sensor_read_block(s, block, PAYLOAD_BLOCK_SAMPLES);
    while (n == PAYLOAD_BLOCK_SAMPLES) {
        full_blocks++;
        total += (size_t)n;
        n = sensor_read_block(s, block, PAYLOAD_BLOCK_SAMPLES);
    }
    total += (size_t)n;

    /* 60000 samples = 3 * 16384 + 10848, i.e. three full 64 KiB blocks. */
    T_CHECK_INT(full_blocks, 3);
    T_CHECK_INT(n, 10848);
    T_CHECK_SIZE(total, PAYLOAD_RECORD_SAMPLES);
    T_CHECK_SIZE(sensor_remaining(s), 0);

    sensor_close(s);
}

static void test_real_min_max(void)
{
    static float    buf[4096];
    sensor_config_t cfg;
    sensor_t       *s;
    float           min_value = 0.0f;
    float           max_value = 0.0f;
    size_t          seen = 0;
    int             n;
    int             k;

    sensor_config_default(&cfg);
    cfg.frame_samples = 128;

    s = sensor_open(td_voltage_path(), &cfg);
    T_CHECK(s != NULL);
    if (s == NULL)
        return;

    while ((n = sensor_read_block(s, buf, 4096)) > 0) {
        for (k = 0; k < n; k++) {
            if (seen == 0 || buf[k] < min_value)
                min_value = buf[k];
            if (seen == 0 || buf[k] > max_value)
                max_value = buf[k];
            seen++;
        }
    }

    /* Same figures the stage-1 loader printed for this file. */
    T_CHECK_SIZE(seen, PAYLOAD_RECORD_SAMPLES);
    T_CHECK_NEAR(min_value, 2.597834f, 1e-5);
    T_CHECK_NEAR(max_value, 4.273985f, 1e-5);

    sensor_close(s);
}

static void test_real_channel_offsets(void)
{
    static const int   offsets[3] = { 1, 2, 3 };   /* voltage, current, temperature */
    static const float lows[3]    = { 2.0f, -15.0f, 290.0f };
    static const float highs[3]   = { 4.5f,   15.0f, 320.0f };
    sensor_config_t    cfg;
    sensor_t          *s;
    float              got[100];
    int                i;
    int                k;

    for (i = 0; i < 3; i++) {
        sensor_config_default(&cfg);
        cfg.layout = SENSOR_LAYOUT_INTERLEAVED;
        cfg.stride = 4;
        cfg.offset = offsets[i];
        cfg.frame_samples = 100;

        s = sensor_open(td_interleaved_path(), &cfg);
        T_CHECK(s != NULL);
        if (s == NULL)
            continue;

        T_CHECK_INT(sensor_read_frame(s, got), 100);
        for (k = 0; k < 100; k++) {
            if (got[k] < lows[i] || got[k] > highs[i]) {
                printf("    FAIL %s:%d: offset %d sample %d is %.6g, expected %.1f..%.1f\n",
                       __FILE__, __LINE__, offsets[i], k, (double)got[k],
                       (double)lows[i], (double)highs[i]);
                t_failed++;
                t_checks++;
                break;
            }
        }
        t_checks++;   /* one check per channel, whether it broke or not */

        sensor_close(s);
    }
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int have_data = td_find_dataset(argc, argv);

    printf("virtual sensor tests\n");
    printf("  dataset: %s\n\n",
           have_data ? td_dir
                     : "(not found -- payload tests will be skipped)");

    T_RUN(test_config_defaults);
    T_RUN(test_open_rejects_bad_input);
    T_RUN(test_exact_bits_round_trip);
    T_RUN(test_strided_layout);
    T_RUN(test_frame_cap);
    T_RUN(test_loop_wraps);
    T_RUN(test_reset_restarts);
    T_RUN(test_window_boundaries);
    T_RUN(test_timestamps);
    T_RUN(test_block_read);
    T_RUN(test_paced_reads);

    if (have_data) {
        T_RUN(test_real_layouts_agree);
        T_RUN(test_real_block_partition);
        T_RUN(test_real_min_max);
        T_RUN(test_real_channel_offsets);
    } else {
        T_SKIP("no payload directory: real-data tests not run");
    }

    remove(TMP_PLANAR);
    remove(TMP_RAMP);
    remove(TMP_RECORDS);
    remove(TMP_TINY);
    remove(TMP_EMPTY);
    remove(TMP_PARTIAL);

    return t_summary();
}
