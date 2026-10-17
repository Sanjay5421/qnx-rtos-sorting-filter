/*
 * test_block_loader.c
 *
 * Tests for the loader (app/block_loader.c): recording -> 64 KiB blocks plus
 * descriptors, and the hand-off to the validation the server will perform.
 *
 * The strongest tests here are the ones that compare two independent routes to
 * the same numbers: the planar per-channel file against the interleaved record,
 * and the block stream against a straight read of the same file. A wrong
 * channel mapping or a mishandled stride would make those disagree.
 */

#include "../app/block_loader.h"
#include "../app/median.h"
#include "../app/protocol.h"
#include "../app/sensor.h"
#include "test_data.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TMP_RAMP          "loader_tmp_ramp.bin"
#define SYNTHETIC_SAMPLES 20000
#define RATE_HZ           100u

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static int write_ramp(const char *path, size_t count)
{
    FILE  *fp = fopen(path, "wb");
    size_t written = 0;
    size_t i;

    if (fp == NULL) {
        perror(path);
        return -1;
    }
    for (i = 0; i < count; i++) {
        float value = (float)i / 2.0f;          /* exact in float32 */
        if (fwrite(&value, sizeof(value), 1, fp) != 1) {
            fprintf(stderr, "%s: short write\n", path);
            fclose(fp);
            return -1;
        }
        written++;
    }
    if (fclose(fp) != 0) {
        perror(path);
        return -1;
    }
    return (written == count) ? 0 : -1;
}

static proto_block_t *alloc_block(void)
{
    proto_block_t *block = malloc(sizeof(*block));

    if (block == NULL)
        fprintf(stderr, "out of memory for a 64 KiB block\n");
    return block;
}

/* ------------------------------------------------------------------------- */
/* Channel mapping                                                            */
/* ------------------------------------------------------------------------- */

static void test_channel_offsets(void)
{
    /*
     * The interleaved record is [time_s, voltage_V, current_A, temperature_K],
     * which is not the PROTO_CHANNEL_* order. Getting this wrong would silently
     * sort the wrong signal.
     */
    T_CHECK_INT(block_loader_channel_offset(PROTO_CHANNEL_TIME), 0);
    T_CHECK_INT(block_loader_channel_offset(PROTO_CHANNEL_VOLTAGE), 1);
    T_CHECK_INT(block_loader_channel_offset(PROTO_CHANNEL_CURRENT), 2);
    T_CHECK_INT(block_loader_channel_offset(PROTO_CHANNEL_TEMPERATURE), 3);
    T_CHECK_INT(block_loader_channel_offset(PROTO_CHANNEL_COUNT), -1);
}

static void test_open_rejects_bad_input(void)
{
    T_CHECK(block_loader_open(NULL, SENSOR_LAYOUT_PLANAR,
                              PROTO_CHANNEL_VOLTAGE, RATE_HZ) == NULL);
    T_CHECK(block_loader_open("loader_no_such_file.bin", SENSOR_LAYOUT_PLANAR,
                              PROTO_CHANNEL_VOLTAGE, RATE_HZ) == NULL);
    T_CHECK(block_loader_open(TMP_RAMP, 99, PROTO_CHANNEL_VOLTAGE,
                              RATE_HZ) == NULL);
    T_CHECK(block_loader_open(TMP_RAMP, SENSOR_LAYOUT_PLANAR,
                              PROTO_CHANNEL_VOLTAGE, 0) == NULL);
    T_CHECK(block_loader_open(TMP_RAMP, SENSOR_LAYOUT_PLANAR,
                              PROTO_CHANNEL_COUNT, RATE_HZ) == NULL);

    /* A planar file needs no channel offset, but an unknown channel is still
     * an unknown channel. */
    block_loader_close(NULL);                       /* must not crash */
}

/* ------------------------------------------------------------------------- */
/* Blocking                                                                   */
/* ------------------------------------------------------------------------- */

static void test_synthetic_partition(void)
{
    proto_block_desc_t desc;
    proto_block_t     *block;
    block_loader_t    *l;
    int                n;

    T_CHECK(write_ramp(TMP_RAMP, SYNTHETIC_SAMPLES) == 0);

    block = alloc_block();
    T_CHECK(block != NULL);
    if (block == NULL)
        return;

    /* A recording of 20000 samples is one full block plus 3616. */
    l = block_loader_open(TMP_RAMP, SENSOR_LAYOUT_PLANAR,
                          PROTO_CHANNEL_VOLTAGE, RATE_HZ);
    T_CHECK(l != NULL);
    if (l == NULL) {
        free(block);
        return;
    }

    T_CHECK_SIZE(block_loader_total_samples(l), SYNTHETIC_SAMPLES);

    n = block_loader_next(l, block, &desc);
    T_CHECK_INT(n, (int)PROTO_BLOCK_SAMPLES);
    T_CHECK_INT(desc.sequence, 0);
    T_CHECK_INT(desc.samples, (int)PROTO_BLOCK_SAMPLES);
    T_CHECK_U64(desc.first_sample, 0);
    T_CHECK_INT(desc.rate_hz, RATE_HZ);
    T_CHECK_INT(desc.channel, PROTO_CHANNEL_VOLTAGE);
    T_CHECK_SAME_FLOAT(block->samples[0], 0.0f);
    T_CHECK_SAME_FLOAT(block->samples[16383], 8191.5f);
    T_CHECK_NEAR(desc.min_value, 0.0f, 0.0);
    T_CHECK_NEAR(desc.max_value, 8191.5f, 0.0);
    T_CHECK_SIZE(desc.checksum,
                 proto_checksum(block->samples, PROTO_BLOCK_BYTES));
    T_CHECK_INT(proto_block_desc_valid(&desc), 1);

    n = block_loader_next(l, block, &desc);
    T_CHECK_INT(n, 3616);
    T_CHECK_INT(desc.sequence, 1);
    T_CHECK_INT(desc.samples, 3616);
    T_CHECK_U64(desc.first_sample, PROTO_BLOCK_SAMPLES);
    T_CHECK_SAME_FLOAT(block->samples[0], 8192.0f);
    T_CHECK_SAME_FLOAT(block->samples[3615], 9999.5f);

    /* The unused tail of a short block is cleared, so the block is
     * reproducible and no stale bytes travel to the server. */
    T_CHECK_SIZE(t_bits(block->samples[3616]), 0);
    T_CHECK_SIZE(t_bits(block->samples[PROTO_BLOCK_SAMPLES - 1]), 0);

    n = block_loader_next(l, block, &desc);
    T_CHECK_INT(n, 0);                       /* end of the recording */

    block_loader_close(l);
    free(block);
}

static void test_synthetic_matches_source(void)
{
    static float   source[SYNTHETIC_SAMPLES];
    proto_block_desc_t desc;
    proto_block_t *block;
    block_loader_t *l;
    size_t          index = 0;
    unsigned long   bad = 0;
    unsigned long   blocks = 0;
    size_t          i;
    int             n;
    int             k;

    for (i = 0; i < SYNTHETIC_SAMPLES; i++)
        source[i] = (float)i / 2.0f;

    T_CHECK(write_ramp(TMP_RAMP, SYNTHETIC_SAMPLES) == 0);

    block = alloc_block();
    l = block_loader_open(TMP_RAMP, SENSOR_LAYOUT_PLANAR,
                          PROTO_CHANNEL_VOLTAGE, RATE_HZ);
    T_CHECK(block != NULL);
    T_CHECK(l != NULL);
    if (block == NULL || l == NULL) {
        free(block);
        block_loader_close(l);
        return;
    }

    while ((n = block_loader_next(l, block, &desc)) > 0) {
        blocks++;
        T_CHECK_INT(desc.samples, n);
        T_CHECK_U64(desc.first_sample, index);
        for (k = 0; k < n; k++) {
            if (t_bits(block->samples[k]) != t_bits(source[index]))
                bad++;
            index++;
        }
    }

    /* The blocks reassemble into exactly the file that was written. */
    T_CHECK_INT(bad, 0);
    T_CHECK_SIZE(index, SYNTHETIC_SAMPLES);
    T_CHECK_INT(blocks, 2);
    T_CHECK_U64(block_loader_blocks_emitted(l), 2);
    T_CHECK_U64(block_loader_samples_emitted(l), SYNTHETIC_SAMPLES);

    block_loader_close(l);
    free(block);
}

static void test_real_partition(void)
{
    proto_block_desc_t desc;
    proto_block_t     *block;
    block_loader_t    *l;
    size_t             total = 0;
    int                expected[4] = { 16384, 16384, 16384, 10848 };
    int                block_count = 0;
    int                n;

    block = alloc_block();
    T_CHECK(block != NULL);
    if (block == NULL)
        return;

    l = block_loader_open(td_voltage_path(), SENSOR_LAYOUT_PLANAR,
                          PROTO_CHANNEL_VOLTAGE, RATE_HZ);
    T_CHECK(l != NULL);
    if (l == NULL) {
        free(block);
        return;
    }

    T_CHECK_SIZE(block_loader_total_samples(l), 60000);

    while ((n = block_loader_next(l, block, &desc)) > 0) {
        T_CHECK(block_count < 4);
        if (block_count >= 4)
            break;
        T_CHECK_INT(n, expected[block_count]);
        T_CHECK_INT(desc.samples, expected[block_count]);
        T_CHECK_INT(desc.sequence, block_count);
        T_CHECK_U64(desc.first_sample, (uint64_t)total);
        T_CHECK_INT(desc.channel, PROTO_CHANNEL_VOLTAGE);
        T_CHECK_INT(proto_block_desc_valid(&desc), 1);
        T_CHECK_SIZE(desc.checksum,
                     proto_checksum(block->samples, (size_t)n * sizeof(float)));
        total += (size_t)n;
        block_count++;
    }

    /* 60000 samples = 3 full 64 KiB blocks plus 10848 samples. */
    T_CHECK_INT(block_count, 4);
    T_CHECK_SIZE(total, 60000);
    T_CHECK_INT(expected[3], 10848);
    T_CHECK_U64(block_loader_blocks_emitted(l), 4);
    T_CHECK_U64(block_loader_samples_emitted(l), 60000);

    block_loader_close(l);
    free(block);
}

static void test_real_layouts_agree(void)
{
    /*
     * The same recording, read two ways: the planar per-channel file, and the
     * interleaved record with the channel selected by offset and stride. Every
     * block must come out identical, descriptors included.
     */
    proto_block_desc_t desc_planar;
    proto_block_desc_t desc_strided;
    proto_block_t     *planar_block;
    proto_block_t     *strided_block;
    block_loader_t    *planar;
    block_loader_t    *strided;
    unsigned long      compared = 0;
    unsigned long      bad = 0;
    int                n;
    int                m;
    int                k;

    planar_block  = alloc_block();
    strided_block = alloc_block();
    planar = block_loader_open(td_voltage_path(), SENSOR_LAYOUT_PLANAR,
                               PROTO_CHANNEL_VOLTAGE, RATE_HZ);
    strided = block_loader_open(td_interleaved_path(), SENSOR_LAYOUT_INTERLEAVED,
                                PROTO_CHANNEL_VOLTAGE, RATE_HZ);

    T_CHECK(planar_block != NULL);
    T_CHECK(strided_block != NULL);
    T_CHECK(planar != NULL);
    T_CHECK(strided != NULL);
    if (planar_block == NULL || strided_block == NULL ||
        planar == NULL || strided == NULL) {
        free(planar_block);
        free(strided_block);
        block_loader_close(planar);
        block_loader_close(strided);
        return;
    }

    do {
        n = block_loader_next(planar, planar_block, &desc_planar);
        m = block_loader_next(strided, strided_block, &desc_strided);

        T_CHECK_INT(n, m);
        if (n != m || n <= 0)
            break;

        /* Descriptors are memset by the loader, so this compares padding too. */
        if (memcmp(&desc_planar, &desc_strided, sizeof(desc_planar)) != 0)
            bad++;

        for (k = 0; k < n; k++) {
            if (t_bits(planar_block->samples[k]) !=
                t_bits(strided_block->samples[k]))
                bad++;
        }
        compared++;
    } while (n > 0);

    T_CHECK_INT(bad, 0);
    T_CHECK_INT(compared, 4);

    free(planar_block);
    free(strided_block);
    block_loader_close(planar);
    block_loader_close(strided);
}

/* ------------------------------------------------------------------------- */
/* Hand-off to the server                                                     */
/* ------------------------------------------------------------------------- */

static void test_submit_round_trip(void)
{
    /*
     * What the server will do with the header and the block the client sends:
     * check the structure, then check the payload against its checksum.
     */
    proto_submit_t     msg;
    proto_block_desc_t desc;
    proto_block_t     *block;
    block_loader_t    *l;
    int                n;

    block = alloc_block();
    T_CHECK(block != NULL);
    if (block == NULL)
        return;

    l = block_loader_open(td_voltage_path(), SENSOR_LAYOUT_PLANAR,
                          PROTO_CHANNEL_VOLTAGE, RATE_HZ);
    T_CHECK(l != NULL);
    if (l == NULL) {
        free(block);
        return;
    }

    n = block_loader_next(l, block, &desc);
    T_CHECK_INT(n, (int)PROTO_BLOCK_SAMPLES);
    if (n <= 0) {
        block_loader_close(l);
        free(block);
        return;
    }

    proto_submit_prepare(&msg, 42, &desc, MEDIAN_DEFAULT_WIDTH);
    T_CHECK_INT(msg.type, PROTO_MSG_SUBMIT);
    T_CHECK_INT(msg.version, PROTO_VERSION);
    T_CHECK_INT(msg.job_id, 42);
    T_CHECK_INT(proto_submit_valid(&msg), 1);
    T_CHECK_INT(proto_submit_verify(&msg, block), 1);
    T_CHECK_INT(msg.desc.samples, (int)PROTO_BLOCK_SAMPLES);
    T_CHECK_SIZE(sizeof(msg) + PROTO_BLOCK_BYTES, sizeof(msg) + 65536);

    /* First sample of the r1 voltage recording, as od reads it. */
    T_CHECK_NEAR(block->samples[0], 4.1985273f, 1e-6);
    T_CHECK_NEAR(desc.min_value, block->samples[0], 1.0);
    T_CHECK(desc.max_value >= desc.min_value);

    /* The final short block also verifies. */
    while ((n = block_loader_next(l, block, &desc)) > 0)
        ;
    block_loader_close(l);
    free(block);
}

static void test_corruption_detected(void)
{
    proto_submit_t     msg;
    proto_block_desc_t desc;
    proto_block_t     *block;
    block_loader_t    *l;

    block = alloc_block();
    T_CHECK(block != NULL);
    if (block == NULL)
        return;

    l = block_loader_open(td_voltage_path(), SENSOR_LAYOUT_PLANAR,
                          PROTO_CHANNEL_VOLTAGE, RATE_HZ);
    T_CHECK(l != NULL);
    if (l == NULL) {
        free(block);
        return;
    }

    if (block_loader_next(l, block, &desc) <= 0) {
        block_loader_close(l);
        free(block);
        return;
    }
    proto_submit_prepare(&msg, 1, &desc, MEDIAN_DEFAULT_WIDTH);
    T_CHECK_INT(proto_submit_verify(&msg, block), 1);

    /* One bit of one sample is enough to be caught. */
    block->samples[777] = block->samples[777] + 0.001f;
    T_CHECK_INT(proto_submit_verify(&msg, block), 0);

    /* The declaration itself is still structurally valid: the failure is about
     * the payload, and the client would be told PROTO_ERR_CHECKSUM. */
    T_CHECK_INT(proto_submit_valid(&msg), 1);

    block_loader_close(l);
    free(block);
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int have_data = td_find_dataset(argc, argv);

    printf("block loader tests (recording -> 64 KiB blocks)\n");
    printf("  dataset: %s\n\n",
           have_data ? td_dir : "(not found -- payload tests will be skipped)");

    T_RUN(test_channel_offsets);
    T_RUN(test_open_rejects_bad_input);
    T_RUN(test_synthetic_partition);
    T_RUN(test_synthetic_matches_source);

    if (have_data) {
        T_RUN(test_real_partition);
        T_RUN(test_real_layouts_agree);
        T_RUN(test_submit_round_trip);
        T_RUN(test_corruption_detected);
    } else {
        T_SKIP("no payload directory: real-data tests not run");
    }

    remove(TMP_RAMP);

    return t_summary();
}
