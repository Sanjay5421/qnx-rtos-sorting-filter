/*
 * loader_demo.c
 *
 * Print the block stream the client will offload: for every 64 KiB block, the
 * descriptor the server will see, and the verdict of exactly the validation the
 * server applies (proto_submit_valid / proto_submit_verify). It is the
 * client-to-server payload as a dry run, with no IPC involved.
 *
 *   loader_demo <file.bin> [planar|interleaved] [time|current|voltage|temperature] [rate_hz]
 *
 * Examples
 *   loader_demo r1_voltage.bin
 *       planar, voltage, 100 Hz
 *   loader_demo Chen2020_c1_T25_discharge.bin interleaved voltage 100
 *       the same recording through the 4-float interleaved record
 */

#include "../app/block_loader.h"
#include "../app/protocol.h"
#include "../app/sensor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(int argc, char **argv)
{
    proto_block_t      *block;
    proto_submit_t      msg;
    proto_block_desc_t  desc;
    block_loader_t     *l;
    const char         *path;
    int                 layout  = SENSOR_LAYOUT_PLANAR;
    int                 channel = PROTO_CHANNEL_VOLTAGE;
    uint32_t            rate    = 100;
    unsigned long       blocks  = 0;
    unsigned long       full    = 0;
    unsigned long       accepted = 0;
    unsigned long       payload_bytes = 0;
    unsigned long       sample_bytes = 0;
    double              first_value = 0.0;
    int                 n;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.bin> [planar|interleaved] "
                        "[time|current|voltage|temperature] [rate_hz]\n", argv[0]);
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

    l = block_loader_open(path, layout, (proto_channel_t)channel, rate);
    if (l == NULL)
        return 1;

    block = malloc(sizeof(*block));
    if (block == NULL) {
        fprintf(stderr, "%s: out of memory for a 64 KiB block\n", argv[0]);
        block_loader_close(l);
        return 1;
    }

    printf("file    : %s\n", path);
    printf("layout  : %s\n",
           (layout == SENSOR_LAYOUT_PLANAR) ? "planar" : "interleaved");
    printf("channel : %s (offset %d in an interleaved record)\n",
           proto_channel_str((uint32_t)channel),
           block_loader_channel_offset((proto_channel_t)channel));
    printf("rate    : %lu Hz\n", (unsigned long)rate);
    printf("samples : %lu\n\n", (unsigned long)block_loader_total_samples(l));

    printf("  block  seq  first  samples       min         max      checksum  server\n");
    printf("  -----  ---  -----  -------  ----------  ----------  ----------  ------\n");

    while ((n = block_loader_next(l, block, &desc)) > 0) {
        int ok_struct;
        int ok_payload;

        /* This is the client's side of the wire format: header plus block. */
        proto_submit_prepare(&msg, (uint32_t)blocks, &desc);
        ok_struct  = proto_submit_valid(&msg);
        ok_payload = proto_submit_verify(&msg, block);

        printf("  %5lu  %3u  %5lu  %7u  %10.5g  %10.5g  0x%08lx  %s\n",
               blocks, (unsigned int)desc.sequence,
               (unsigned long)desc.first_sample, (unsigned int)desc.samples,
               (double)desc.min_value, (double)desc.max_value,
               (unsigned long)desc.checksum,
               (ok_struct && ok_payload)
                   ? "accepted"
                   : (ok_struct ? "checksum mismatch" : "rejected"));

        if (blocks == 0)
            first_value = (double)block->samples[0];

        if (ok_struct && ok_payload)
            accepted++;
        if (desc.samples == PROTO_BLOCK_SAMPLES)
            full++;
        payload_bytes += PROTO_BLOCK_BYTES;      /* the whole 64 KiB crosses */
        sample_bytes  += (unsigned long)desc.samples * 4u;
        blocks++;
    }

    if (n < 0) {
        fprintf(stderr, "%s: read error after %lu blocks\n", argv[0], blocks);
        free(block);
        block_loader_close(l);
        return 1;
    }

    printf("\n  blocks           : %lu (%lu full, %lu short)\n",
           blocks, full, blocks - full);
    printf("  samples          : %lu\n", (unsigned long)block_loader_samples_emitted(l));
    printf("  block bytes sent : %lu B = %lu x %u B\n",
           payload_bytes, blocks, (unsigned int)PROTO_BLOCK_BYTES);
    printf("  sample bytes     : %lu B\n", sample_bytes);
    printf("  accepted         : %lu of %lu\n", accepted, blocks);
    if (blocks > 0)
        printf("  first sample     : %.6g\n", first_value);

    free(block);
    block_loader_close(l);
    return (accepted == blocks) ? 0 : 1;
}
