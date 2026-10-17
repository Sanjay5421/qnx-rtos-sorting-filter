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
 * pipeline_demo.c
 *
 * The compute half of the service, without the IPC: load a recording as 64 KiB
 * blocks, sort each block on a pool of worker threads, and report what a client
 * would get back - the median, the range, and the memory-offset descriptor.
 * It is the same blocks, the same loader and the same sort the server will run
 * behind MsgReceive(); only the transport is missing.
 *
 *   pipeline_demo <file.bin> [planar|interleaved] [time|current|voltage|temperature] [rate_hz] [threads]
 *
 * Examples
 *   pipeline_demo r1_voltage.bin
 *       planar, voltage, 100 Hz, one worker per online CPU
 *   pipeline_demo Chen2020_c1_T25_discharge.bin interleaved voltage 100 4
 *       the interleaved record, four workers, as the target will run it
 *
 * Two things it demonstrates on purpose:
 *   - the block that arrives is not the buffer that gets sorted. The checksum
 *     in the descriptor describes the block as it arrived, so the service sorts
 *     a copy it owns, exactly as the server will.
 *   - the sorted result is checked again here, so a run of this tool is also a
 *     pass/fail statement about the sort on real payload.
 */

#include "../app/block_loader.h"
#include "../app/mergesort.h"
#include "../app/protocol.h"
#include "../app/sensor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <time.h>
#endif

/* ------------------------------------------------------------------------- */
/* Wall clock                                                                 */
/* ------------------------------------------------------------------------- */

/* Microseconds from a monotonic source; 0.0 if the platform will not say. */
static double now_us(void)
{
#if defined(_WIN32)
    LARGE_INTEGER freq;
    LARGE_INTEGER now;

    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
        return 0.0;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1.0e6 / (double)freq.QuadPart;
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1.0e6 + (double)ts.tv_nsec / 1000.0;
#endif
}

/* ------------------------------------------------------------------------- */
/* Arguments                                                                  */
/* ------------------------------------------------------------------------- */

static int layout_from_name(const char *name)
{
    if (strcmp(name, "planar") == 0)
        return SENSOR_LAYOUT_PLANAR;
    if (strcmp(name, "interleaved") == 0)
        return SENSOR_LAYOUT_INTERLEAVED;
    return -1;
}

static int channel_from_name(const char *name)
{
    if (strcmp(name, "time") == 0)
        return PROTO_CHANNEL_TIME;
    if (strcmp(name, "current") == 0)
        return PROTO_CHANNEL_CURRENT;
    if (strcmp(name, "voltage") == 0)
        return PROTO_CHANNEL_VOLTAGE;
    if (strcmp(name, "temperature") == 0)
        return PROTO_CHANNEL_TEMPERATURE;
    return -1;
}

/* ------------------------------------------------------------------------- */

static int is_sorted(const float *a, size_t n)
{
    size_t i;

    for (i = 1; i < n; i++)
        if (!(a[i - 1] <= a[i]))
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    block_loader_t     *l;
    mergesort_pool_t   *pool;
    proto_block_t      *block;
    proto_block_desc_t  desc;
    mergesort_options_t options;
    float              *work;
    const char         *path;
    int                 layout  = SENSOR_LAYOUT_PLANAR;
    int                 channel = PROTO_CHANNEL_VOLTAGE;
    uint32_t            rate    = 100;
    unsigned            threads = 0;
    unsigned long       blocks = 0;
    unsigned long       full = 0;
    unsigned long       sorted_ok = 0;
    unsigned long       samples = 0;
    double              total_us = 0.0;
    int                 n;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.bin> [planar|interleaved] "
                        "[time|current|voltage|temperature] [rate_hz] [threads]\n",
                argv[0]);
        return 2;
    }

    path = argv[1];

    if (argc > 2) {
        layout = layout_from_name(argv[2]);
        if (layout < 0) {
            fprintf(stderr, "%s: unknown layout '%s'\n", argv[0], argv[2]);
            return 2;
        }
    }
    if (argc > 3) {
        channel = channel_from_name(argv[3]);
        if (channel < 0) {
            fprintf(stderr, "%s: unknown channel '%s'\n", argv[0], argv[3]);
            return 2;
        }
    }
    if (argc > 4) {
        long value = atol(argv[4]);

        if (value < 1) {
            fprintf(stderr, "%s: rate must be at least 1 Hz\n", argv[0]);
            return 2;
        }
        rate = (uint32_t)value;
    }
    if (argc > 5) {
        long value = atol(argv[5]);

        if (value < 0) {
            fprintf(stderr, "%s: threads cannot be negative\n", argv[0]);
            return 2;
        }
        threads = (unsigned)value;
    }

    l = block_loader_open(path, layout, (proto_channel_t)channel, rate);
    if (l == NULL)
        return 1;

    block = malloc(sizeof(*block));
    work  = malloc(sizeof(block->samples));
    if (block == NULL || work == NULL) {
        fprintf(stderr, "%s: out of memory for a 64 KiB block\n", argv[0]);
        free(work);
        free(block);
        block_loader_close(l);
        return 1;
    }

    mergesort_options_default(&options);
    options.threads = threads;
    pool = ms_pool_create(&options);
    if (pool == NULL) {
        block_loader_close(l);
        free(work);
        free(block);
        return 1;
    }

    printf("file    : %s\n", path);
    printf("layout  : %s\n",
           (layout == SENSOR_LAYOUT_PLANAR) ? "planar" : "interleaved");
    printf("channel : %s (offset %d in an interleaved record)\n",
           proto_channel_str((uint32_t)channel),
           block_loader_channel_offset((proto_channel_t)channel));
    printf("rate    : %lu Hz\n", (unsigned long)rate);
    printf("workers : %u\n", ms_pool_threads(pool));
    printf("samples : %lu\n\n", (unsigned long)block_loader_total_samples(l));

    printf("  block  seq  samples         min      median         max"
           "   sort us   MiB/s  order\n");
    printf("  -----  ---  -------  ----------  ----------  ----------"
           "  --------  ------  -----\n");

    while ((n = block_loader_next(l, block, &desc)) > 0) {
        size_t count = desc.samples;
        size_t bytes = count * sizeof(float);
        double started;
        double elapsed;
        int    rc;

        /*
         * The block belongs to the transport: the descriptor's checksum covers
         * it as it arrived. The service sorts its own copy of the samples the
         * descriptor declares - not the zeroed tail of a short final block.
         */
        memcpy(work, block->samples, bytes);

        started = now_us();
        rc = ms_sort(pool, work, count);
        elapsed = now_us() - started;

        if (rc != 0) {
            fprintf(stderr, "%s: block %lu would not sort\n", argv[0], blocks);
            ms_pool_destroy(pool);
            block_loader_close(l);
            free(work);
            free(block);
            return 1;
        }

        {
            int ordered = is_sorted(work, count);

            if (ordered)
                sorted_ok++;

            printf("  %5lu  %3u  %7lu  %10.5g  %10.5g  %10.5g  %8.1f  %6.2f  %s\n",
                   blocks, (unsigned)desc.sequence, (unsigned long)count,
                   (double)work[0], (double)work[count / 2],
                   (double)work[count - 1], elapsed,
                   (elapsed > 0.0) ? ((double)bytes / elapsed) : 0.0,
                   ordered ? "ok" : "OUT OF ORDER");
        }

        if (count == PROTO_BLOCK_SAMPLES)
            full++;
        samples  += (unsigned long)count;
        total_us += elapsed;
        blocks++;
    }

    if (n < 0) {
        fprintf(stderr, "%s: read error after %lu blocks\n", argv[0], blocks);
        ms_pool_destroy(pool);
        block_loader_close(l);
        free(work);
        free(block);
        return 1;
    }

    if (blocks > 0) {
        ms_stats_t stats;

        ms_stats(pool, &stats);

        printf("\n  blocks        : %lu (%lu full 64 KiB, %lu short)\n",
               blocks, full, blocks - full);
        printf("  samples sorted: %lu\n", samples);
        printf("  sort time     : %.3f ms total, %.3f ms per block\n",
               total_us / 1000.0, total_us / 1000.0 / (double)blocks);
        printf("  throughput    : %.2f MiB/s, %.0f samples/s\n",
               (total_us > 0.0)
                   ? ((double)samples * 4.0 / (1024.0 * 1024.0)) / (total_us / 1.0e6)
                   : 0.0,
               (total_us > 0.0) ? ((double)samples * 1.0e6 / total_us) : 0.0);
        printf("  last block    : %u partitions, %u merge phases, %.0f tasks, "
               "%u workers\n",
               stats.partitions, stats.phases, (double)stats.tasks, stats.threads);
    }

    printf("  order checked : %lu of %lu blocks sorted\n", sorted_ok, blocks);

    ms_pool_destroy(pool);
    block_loader_close(l);
    free(work);
    free(block);

    return (sorted_ok == blocks) ? 0 : 1;
}
