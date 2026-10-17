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
 * block_loader.c
 *
 * Implementation of the block loader; the contract lives in block_loader.h.
 *
 * The work is deliberately thin: the virtual sensor already delivers a stream,
 * and protocol.c already owns the checksum and the descriptor rules. What is
 * left here is the part that must be got right and nowhere else - mapping a
 * protocol channel onto a position in a file, and keeping the descriptor
 * consistent with the bytes that go on the wire.
 */

#include "block_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct block_loader {
    sensor_t        *sensor;
    proto_channel_t  channel;
    uint32_t         rate_hz;
    uint64_t         sequence;   /* blocks emitted so far         */
    uint64_t         samples;    /* samples emitted so far        */
};

int block_loader_channel_offset(proto_channel_t channel)
{
    switch (channel) {
    case PROTO_CHANNEL_TIME:        return 0;
    case PROTO_CHANNEL_VOLTAGE:     return 1;   /* not 2: see the header note */
    case PROTO_CHANNEL_CURRENT:     return 2;
    case PROTO_CHANNEL_TEMPERATURE: return 3;
    default:                        return -1;
    }
}

block_loader_t *block_loader_open(const char *path, int layout,
                                  proto_channel_t channel, uint32_t rate_hz)
{
    block_loader_t *l;
    sensor_config_t cfg;

    if (path == NULL) {
        fprintf(stderr, "block_loader: open called with a NULL path\n");
        return NULL;
    }
    if (channel < 0 || channel >= PROTO_CHANNEL_COUNT) {
        fprintf(stderr, "block_loader: %s: unknown channel %d\n",
                path, (int)channel);
        return NULL;
    }
    if (rate_hz == 0) {
        fprintf(stderr, "block_loader: %s: rate_hz must be at least 1\n", path);
        return NULL;
    }
    if (layout != SENSOR_LAYOUT_PLANAR && layout != SENSOR_LAYOUT_INTERLEAVED) {
        fprintf(stderr, "block_loader: %s: unknown layout %d\n", path, layout);
        return NULL;
    }

    l = calloc(1, sizeof(*l));
    if (l == NULL) {
        fprintf(stderr, "block_loader: %s: out of memory\n", path);
        return NULL;
    }

    sensor_config_default(&cfg);
    cfg.layout        = layout;
    cfg.rate_hz       = rate_hz;
    /* A block is the natural frame: one read of the sensor fills one block.
     * For an interleaved file that makes the sensor's read-ahead window
     * 16384 * 4 floats = 256 KiB, which is one fread() per block. */
    cfg.frame_samples = PROTO_BLOCK_SAMPLES;

    if (layout == SENSOR_LAYOUT_INTERLEAVED) {
        int offset = block_loader_channel_offset(channel);

        if (offset < 0) {
            fprintf(stderr, "block_loader: %s: channel %d has no offset in an "
                            "interleaved record\n", path, (int)channel);
            free(l);
            return NULL;
        }
        cfg.stride = 4;
        cfg.offset = offset;
    } else {
        cfg.stride = 1;
        cfg.offset = 0;
    }

    l->sensor = sensor_open(path, &cfg);
    if (l->sensor == NULL) {
        free(l);
        return NULL;
    }

    l->channel = channel;
    l->rate_hz = rate_hz;
    return l;
}

void block_loader_close(block_loader_t *l)
{
    if (l == NULL)
        return;
    sensor_close(l->sensor);
    free(l);
}

int block_loader_next(block_loader_t *l, proto_block_t *block,
                      proto_block_desc_t *desc)
{
    float min_value;
    float max_value;
    int   got;
    int   i;

    if (l == NULL || block == NULL || desc == NULL)
        return -1;

    got = sensor_read_block(l->sensor, block->samples, PROTO_BLOCK_SAMPLES);
    if (got < 0)
        return -1;
    if (got == 0)
        return 0;

    /*
     * A short final block has an unused tail. Clear it: the whole 64 KiB block
     * crosses to another process, and uninitialised bytes should never travel.
     * It also makes the block reproducible, so two runs over the same
     * recording produce identical bytes and identical checksums.
     */
    if ((uint32_t)got < PROTO_BLOCK_SAMPLES) {
        memset(&block->samples[got], 0,
               (size_t)(PROTO_BLOCK_SAMPLES - (uint32_t)got) * sizeof(float));
    }

    min_value = block->samples[0];
    max_value = block->samples[0];
    for (i = 1; i < got; i++) {
        if (block->samples[i] < min_value)
            min_value = block->samples[i];
        if (block->samples[i] > max_value)
            max_value = block->samples[i];
    }

    memset(desc, 0, sizeof(*desc));
    desc->sequence     = (uint32_t)l->sequence;
    desc->samples      = (uint32_t)got;
    desc->first_sample = l->samples;
    desc->rate_hz      = l->rate_hz;
    desc->channel      = (uint32_t)l->channel;
    desc->checksum     = proto_checksum(block->samples,
                                        (size_t)got * sizeof(float));
    desc->min_value    = min_value;
    desc->max_value    = max_value;

    l->sequence++;
    l->samples += (uint64_t)got;
    return got;
}

size_t block_loader_total_samples(const block_loader_t *l)
{
    return (l != NULL) ? sensor_total_samples(l->sensor) : 0;
}

uint64_t block_loader_blocks_emitted(const block_loader_t *l)
{
    return (l != NULL) ? l->sequence : 0;
}

uint64_t block_loader_samples_emitted(const block_loader_t *l)
{
    return (l != NULL) ? l->samples : 0;
}
