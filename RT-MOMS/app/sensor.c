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
 * sensor.c
 *
 * Virtual sensor implementation; the contract lives in sensor.h.
 *
 * The recording is read through a sliding window of raw records, so the module
 * behaves like a device with bounded read-ahead: it never holds the whole file
 * in memory, and a caller that reads one sample at a time sees exactly the
 * same values as one that reads whole blocks.
 *
 * Portability: plain C99 plus libc. The two platform-specific details are the
 * monotonic millisecond clock and the sleep, both of which are only needed for
 * paced reads; on the target they map onto POSIX, on the Windows host onto the
 * Win32 equivalents so the test suite can run before QNX is installed.
 */

#include "sensor.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#endif

/*: Samples held in the read-ahead window, before the stride is applied. */
#define SENSOR_WINDOW_SAMPLES 4096

/*: Longest single paced wait. Keeps a clock wrap or a nonsensical rate from
 *  looking like a hung process. */
#define SENSOR_MAX_WAIT_MS 60000

struct sensor {
    FILE           *fp;            /* the recording                                */
    sensor_config_t cfg;

    size_t   total;                /* samples in the recording                     */
    size_t   next;                 /* index of the next sample to deliver          */
    uint64_t delivered;            /* samples delivered since open or reset        */

    float   *window;               /* stride * window_samples raw floats           */
    size_t   window_samples;       /* samples the window holds (its capacity)      */
    size_t   window_first;         /* recording index of window[0]                 */
    size_t   window_count;         /* samples currently valid in window[]          */

    uint64_t pace_start_ms;        /* virtual clock origin for paced reads         */
    int      pace_started;
};

/* ------------------------------------------------------------------------- */
/* Clock and sleep                                                            */
/* ------------------------------------------------------------------------- */

/*
 * Milliseconds on a monotonic clock. Only pacing depends on this, so the
 * Windows branch uses the tick counter rather than anything behind a feature
 * test: it wraps every ~49.7 days, and a wrapped delta is clamped before it is
 * used as a wait (see sensor_pace()).
 */
static uint64_t sensor_now_ms(void)
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

static void sensor_wait_ms(uint64_t ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec req;

    req.tv_sec  = (time_t)(ms / 1000ull);
    req.tv_nsec = (long)((ms % 1000ull) * 1000000ull);
    while (nanosleep(&req, &req) != 0 && errno == EINTR)
        ;   /* restart the remaining time after a signal */
#endif
}

/*
 * Wait until the next sample is due on the virtual clock. That clock starts at
 * the first paced read, so pacing is relative to the caller, not to wall-clock
 * time since boot.
 */
static void sensor_pace(sensor_t *s)
{
    uint64_t now;
    uint64_t due;
    int64_t  delta;

    if (!s->pace_started) {
        s->pace_start_ms = sensor_now_ms();
        s->pace_started  = 1;
    }

    due   = s->pace_start_ms + (s->delivered * 1000ull) / (uint64_t)s->cfg.rate_hz;
    now   = sensor_now_ms();
    delta = (int64_t)due - (int64_t)now;

    if (delta > SENSOR_MAX_WAIT_MS)
        delta = SENSOR_MAX_WAIT_MS;
    if (delta > 0)
        sensor_wait_ms((uint64_t)delta);
}

/* ------------------------------------------------------------------------- */
/* Configuration                                                              */
/* ------------------------------------------------------------------------- */

void sensor_config_default(sensor_config_t *cfg)
{
    if (cfg == NULL)
        return;

    memset(cfg, 0, sizeof(*cfg));
    cfg->layout        = SENSOR_LAYOUT_PLANAR;
    cfg->offset        = 0;
    cfg->stride        = 1;
    cfg->frame_samples = SENSOR_DEFAULT_FRAME_SAMPLES;
    cfg->rate_hz       = SENSOR_DEFAULT_RATE_HZ;
    cfg->loop          = 0;
}

/* ------------------------------------------------------------------------- */
/* Open and close                                                             */
/* ------------------------------------------------------------------------- */

/* Validate a configuration, printing the first problem found. */
static int sensor_config_ok(const sensor_config_t *cfg, const char *path)
{
    if (cfg->layout != SENSOR_LAYOUT_PLANAR &&
        cfg->layout != SENSOR_LAYOUT_INTERLEAVED) {
        fprintf(stderr, "sensor: %s: unknown layout %d\n", path, cfg->layout);
        return 0;
    }
    if (cfg->stride < 1) {
        fprintf(stderr, "sensor: %s: stride must be at least 1 (got %d)\n",
                path, cfg->stride);
        return 0;
    }
    if (cfg->offset < 0 || cfg->offset >= cfg->stride) {
        fprintf(stderr, "sensor: %s: offset must be in [0, %d) (got %d)\n",
                path, cfg->stride, cfg->offset);
        return 0;
    }
    if (cfg->layout == SENSOR_LAYOUT_PLANAR && (cfg->stride != 1 || cfg->offset != 0)) {
        fprintf(stderr,
                "sensor: %s: a planar file needs offset 0 and stride 1 "
                "(got offset %d, stride %d); use SENSOR_LAYOUT_INTERLEAVED\n",
                path, cfg->offset, cfg->stride);
        return 0;
    }
    if (cfg->frame_samples == 0) {
        fprintf(stderr, "sensor: %s: frame_samples must be at least 1\n", path);
        return 0;
    }
    if (cfg->rate_hz == 0) {
        fprintf(stderr, "sensor: %s: rate_hz must be at least 1\n", path);
        return 0;
    }
    return 1;
}

sensor_t *sensor_open(const char *path, const sensor_config_t *cfg)
{
    sensor_config_t local;
    sensor_t       *s;
    long            bytes;
    long            floats;
    size_t          window_samples;

    if (path == NULL) {
        fprintf(stderr, "sensor: open called with a NULL path\n");
        return NULL;
    }

    sensor_config_default(&local);
    if (cfg != NULL)
        local = *cfg;
    if (local.frame_samples == 0)
        local.frame_samples = SENSOR_DEFAULT_FRAME_SAMPLES;
    if (local.rate_hz == 0)
        local.rate_hz = SENSOR_DEFAULT_RATE_HZ;

    if (!sensor_config_ok(&local, path))
        return NULL;

    s = calloc(1, sizeof(*s));
    if (s == NULL) {
        fprintf(stderr, "sensor: %s: out of memory\n", path);
        return NULL;
    }
    s->cfg = local;

    s->fp = fopen(path, "rb");
    if (s->fp == NULL) {
        perror(path);
        free(s);
        return NULL;
    }

    /* The file is headerless float32, so its size is its only metadata. */
    if (fseek(s->fp, 0, SEEK_END) != 0) {
        perror(path);
        goto failed;
    }
    bytes = ftell(s->fp);
    if (bytes <= 0) {
        fprintf(stderr, "sensor: %s: file is empty\n", path);
        goto failed;
    }
    if (bytes % (long)sizeof(float) != 0) {
        fprintf(stderr, "sensor: %s: %ld bytes is not a whole number of float32 values\n",
                path, bytes);
        goto failed;
    }
    floats = bytes / (long)sizeof(float);
    if (floats % (long)local.stride != 0) {
        fprintf(stderr,
                "sensor: %s: %ld values is not a whole number of %d-value records\n",
                path, floats, local.stride);
        goto failed;
    }
    s->total = (size_t)(floats / (long)local.stride);

    window_samples = SENSOR_WINDOW_SAMPLES;
    if (window_samples < local.frame_samples)
        window_samples = local.frame_samples;
    s->window_samples = window_samples;
    s->window = malloc(window_samples * (size_t)local.stride * sizeof(float));
    if (s->window == NULL) {
        fprintf(stderr, "sensor: %s: out of memory for the read-ahead window\n", path);
        goto failed;
    }

    if (fseek(s->fp, 0, SEEK_SET) != 0) {
        perror(path);
        goto failed;
    }
    return s;

failed:
    if (s->fp != NULL)
        fclose(s->fp);
    free(s->window);
    free(s);
    return NULL;
}

void sensor_close(sensor_t *s)
{
    if (s == NULL)
        return;

    if (s->fp != NULL && fclose(s->fp) != 0)
        perror("sensor: close");
    free(s->window);
    free(s);
}

/* ------------------------------------------------------------------------- */
/* Stream state                                                               */
/* ------------------------------------------------------------------------- */

size_t sensor_total_samples(const sensor_t *s)
{
    return (s != NULL) ? s->total : 0;
}

size_t sensor_remaining(const sensor_t *s)
{
    if (s == NULL)
        return 0;
    if (s->next >= s->total)
        return s->cfg.loop ? s->total : 0;   /* wraps and starts again */
    return s->total - s->next;
}

uint64_t sensor_index(const sensor_t *s)
{
    return (s != NULL) ? s->delivered : 0;
}

uint64_t sensor_timestamp_ns(const sensor_t *s)
{
    if (s == NULL || s->cfg.rate_hz == 0)
        return 0;
    return s->delivered * 1000000000ull / (uint64_t)s->cfg.rate_hz;
}

int sensor_reset(sensor_t *s)
{
    if (s == NULL)
        return -1;

    if (fseek(s->fp, 0, SEEK_SET) != 0) {
        perror("sensor: rewind");
        return -1;
    }
    s->next          = 0;
    s->delivered     = 0;
    s->window_first  = 0;
    s->window_count  = 0;
    s->pace_started  = 0;
    s->pace_start_ms = 0;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Reading                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Move the read-ahead window so that it starts at `index`. The window is read
 * whole, so a short read simply means fewer samples are available: the end of
 * the recording, not an error.
 */
static int sensor_fill(sensor_t *s, size_t index)
{
    size_t want;
    size_t got;
    long   float_offset;

    if (index > (size_t)LONG_MAX / (size_t)s->cfg.stride) {
        fprintf(stderr, "sensor: sample %lu is beyond the seekable range\n",
                (unsigned long)index);
        return -1;
    }
    float_offset = (long)(index * (size_t)s->cfg.stride);

    if (fseek(s->fp, float_offset * (long)sizeof(float), SEEK_SET) != 0) {
        fprintf(stderr, "sensor: seek to sample %lu failed\n", (unsigned long)index);
        return -1;
    }

    want = s->window_samples * (size_t)s->cfg.stride;
    got  = fread(s->window, sizeof(float), want, s->fp);
    if (got < want && ferror(s->fp)) {
        fprintf(stderr, "sensor: read at sample %lu failed\n", (unsigned long)index);
        return -1;
    }

    s->window_first = index;
    s->window_count = got / (size_t)s->cfg.stride;
    return 0;
}

int sensor_read(sensor_t *s, float *dst, size_t max_samples)
{
    size_t cap;
    size_t n = 0;

    if (s == NULL || dst == NULL)
        return -1;

    /* A device read delivers at most one frame; aggregating frames into a
     * larger block is the caller's job (see sensor_read_block()). */
    cap = s->cfg.frame_samples;
    if (max_samples < cap)
        cap = max_samples;
    if (cap == 0)
        return 0;

    while (n < cap) {
        size_t offset;

        if (s->next >= s->total) {
            if (!s->cfg.loop)
                break;
            s->next = 0;                    /* a live sensor never stops */
        }

        if (s->window_count == 0 ||
            s->next < s->window_first ||
            s->next >= s->window_first + s->window_count) {
            if (sensor_fill(s, s->next) != 0)
                return (n > 0) ? (int)n : -1;
            if (s->window_count == 0)
                break;                      /* nothing left to give */
        }

        offset = (s->next - s->window_first) * (size_t)s->cfg.stride +
                 (size_t)s->cfg.offset;
        dst[n] = s->window[offset];
        n++;
        s->next++;
        s->delivered++;
    }

    return (int)n;
}

int sensor_read_frame(sensor_t *s, float *dst)
{
    if (s == NULL)
        return -1;
    return sensor_read(s, dst, s->cfg.frame_samples);
}

int sensor_read_block(sensor_t *s, float *dst, size_t block_samples)
{
    size_t got = 0;

    if (s == NULL || dst == NULL)
        return -1;

    while (got < block_samples) {
        size_t want = block_samples - got;
        int    n;

        if (want > s->cfg.frame_samples)
            want = s->cfg.frame_samples;

        n = sensor_read(s, dst + got, want);
        if (n < 0)
            return (got > 0) ? (int)got : -1;
        if (n == 0)
            break;                          /* end of the recording */
        got += (size_t)n;
    }

    return (int)got;
}

int sensor_read_paced(sensor_t *s, float *dst, size_t max_samples)
{
    int n;

    if (s == NULL)
        return -1;

    n = sensor_read(s, dst, max_samples);
    if (n < 0)
        return -1;
    if (n > 0)
        sensor_pace(s);
    return n;
}
