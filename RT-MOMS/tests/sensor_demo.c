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
 * sensor_demo.c
 *
 * Watch the virtual sensor deliver samples the way a client sees a real
 * device: one sample at a time, at a configured rate, each with a timestamp.
 *
 *   sensor_demo <file.bin> [rate_hz] [offset] [stride] [trace_samples]
 *
 * Examples
 *   sensor_demo r1_voltage.bin
 *       planar file, default 10 Hz, 20 traced samples
 *
 *   sensor_demo Chen2020_c1_T25_discharge.bin 5 1 4 20
 *       the interleaved sensor record, voltage channel, 5 Hz
 *
 * After the live trace it replays the file as fast as it can and reports what
 * the client would offload: how the recording splits into 64 KiB blocks, and
 * the range of the signal it just streamed.
 */

#include "../app/sensor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define TRACE_DEFAULT     20
#define RATE_DEFAULT      10
#define BLOCK_SAMPLES     16384   /* 64 KiB of float32 samples */

int main(int argc, char **argv)
{
    sensor_config_t cfg;
    sensor_t       *s;
    const char     *path;
    int             rate  = RATE_DEFAULT;
    int             offset = 0;
    int             stride = 1;
    int             trace = TRACE_DEFAULT;
    int             i;
    int             n;

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <file.bin> [rate_hz] [offset] [stride] [trace_samples]\n",
                argv[0]);
        return 2;
    }

    path = argv[1];
    if (argc > 2) rate   = atoi(argv[2]);
    if (argc > 3) offset = atoi(argv[3]);
    if (argc > 4) stride = atoi(argv[4]);
    if (argc > 5) trace  = atoi(argv[5]);
    if (rate < 1)  rate  = RATE_DEFAULT;
    if (trace < 0) trace = 0;

    sensor_config_default(&cfg);
    cfg.layout        = (stride > 1) ? SENSOR_LAYOUT_INTERLEAVED
                                     : SENSOR_LAYOUT_PLANAR;
    cfg.offset        = offset;
    cfg.stride        = stride;
    cfg.frame_samples = 1;               /* one sample per read: a live trace */
    cfg.rate_hz       = (uint32_t)rate;

    s = sensor_open(path, &cfg);
    if (s == NULL)
        return 1;

    printf("file    : %s\n", path);
    printf("layout  : %s (offset %d, stride %d)\n",
           (cfg.layout == SENSOR_LAYOUT_INTERLEAVED) ? "interleaved" : "planar",
           offset, stride);
    printf("samples : %lu\n", (unsigned long)sensor_total_samples(s));
    printf("rate    : %d Hz\n", rate);
    printf("\nlive trace:\n");

    for (i = 0; i < trace; i++) {
        float value;

        n = sensor_read_paced(s, &value, 1);
        if (n <= 0) {
            printf("  end of recording after %d samples\n", i);
            break;
        }
        printf("  t=%9.3f s   sample %6lu   value %.6g\n",
               (double)sensor_timestamp_ns(s) / 1000000000.0,
               (unsigned long)(sensor_index(s) - 1),
               (double)value);
    }

    sensor_close(s);

    /* Second pass, unpaced: what the client would actually offload. */
    cfg.frame_samples = SENSOR_DEFAULT_FRAME_SAMPLES;

    s = sensor_open(path, &cfg);
    if (s == NULL)
        return 1;

    {
        float  *block = malloc((size_t)BLOCK_SAMPLES * sizeof(float));
        size_t  total = 0;
        size_t  full  = 0;
        size_t  tail  = 0;
        double  sum   = 0.0;
        float   min_value = 0.0f;
        float   max_value = 0.0f;
        int     first = 1;

        if (block == NULL) {
            fprintf(stderr, "%s: out of memory\n", argv[0]);
            sensor_close(s);
            return 1;
        }

        while ((n = sensor_read_block(s, block, BLOCK_SAMPLES)) > 0) {
            for (i = 0; i < n; i++) {
                if (first || block[i] < min_value)
                    min_value = block[i];
                if (first || block[i] > max_value)
                    max_value = block[i];
                sum += (double)block[i];
                first = 0;
            }
            total += (size_t)n;
            if (n == BLOCK_SAMPLES)
                full++;
            else
                tail = (size_t)n;
        }

        printf("\npayload summary (one pass over the whole recording):\n");
        printf("  samples streamed        : %lu\n", (unsigned long)total);
        printf("  %d-sample blocks (%2d KiB): %lu full + %lu samples\n",
               BLOCK_SAMPLES, BLOCK_SAMPLES * (int)sizeof(float) / 1024,
               (unsigned long)full, (unsigned long)tail);
        printf("  min / max / mean        : %.6g / %.6g / %.6g\n",
               (double)min_value, (double)max_value,
               (total > 0) ? sum / (double)total : 0.0);

        free(block);
    }

    sensor_close(s);
    return 0;
}
