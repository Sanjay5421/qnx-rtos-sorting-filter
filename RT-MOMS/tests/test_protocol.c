/*
 * test_protocol.c
 *
 * Tests for the common message definition (app/protocol.h): the interface
 * specification the client and the server are both built against.
 *
 * None of this needs QNX. The properties that matter - the block really is
 * 64 KiB, application message types sit above the system range, the checksum
 * catches corruption, and the server's validation rejects what it must - are
 * all checkable on the host, which is the point of testing them now.
 */

#include "../app/protocol.h"
#include "test_util.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*: QNX system message types are 0x100..0x1FF (<sys/iomsg.h>). */
#define QNX_SYSTEM_TYPE_MAX 0x1FFu

/* ------------------------------------------------------------------------- */

static void test_block_is_64kib(void)
{
    T_CHECK_SIZE(sizeof(proto_block_t), 65536);
    T_CHECK_SIZE(PROTO_BLOCK_BYTES, 65536);
    T_CHECK_SIZE((size_t)PROTO_BLOCK_SAMPLES * sizeof(float), 65536);

    /* A whole recording divides into blocks with a remainder, not a fraction:
     * 60000 = 3 * 16384 + 10848. */
    T_CHECK_SIZE(60000u / PROTO_BLOCK_SAMPLES, 3);
    T_CHECK_SIZE(60000u % PROTO_BLOCK_SAMPLES, 10848);
}

static void test_message_types(void)
{
    /* Above the system range, distinct from each other, and small enough to
     * fit the 16-bit type field the kernel reads from the message. */
    T_CHECK(PROTO_MSG_SUBMIT > QNX_SYSTEM_TYPE_MAX);
    T_CHECK(PROTO_MSG_ACK > QNX_SYSTEM_TYPE_MAX);
    T_CHECK(PROTO_MSG_FETCH > QNX_SYSTEM_TYPE_MAX);
    T_CHECK(PROTO_MSG_RESULT > QNX_SYSTEM_TYPE_MAX);

    T_CHECK(PROTO_MSG_SUBMIT != PROTO_MSG_ACK);
    T_CHECK(PROTO_MSG_SUBMIT != PROTO_MSG_FETCH);
    T_CHECK(PROTO_MSG_SUBMIT != PROTO_MSG_RESULT);
    T_CHECK(PROTO_MSG_ACK != PROTO_MSG_FETCH);
    T_CHECK(PROTO_MSG_ACK != PROTO_MSG_RESULT);
    T_CHECK(PROTO_MSG_FETCH != PROTO_MSG_RESULT);

    T_CHECK(PROTO_MSG_SUBMIT <= 0xFFFFu);
    T_CHECK(PROTO_MSG_RESULT <= 0xFFFFu);
}

static void test_pulse_code(void)
{
    /* 8-bit field, and it must stay clear of the small codes the kernel and
     * the process manager reserve for themselves. The compile-time guard in
     * protocol.h re-checks the documented user range on the target. */
    T_CHECK(PROTO_PULSE_DONE > 0);
    T_CHECK(PROTO_PULSE_DONE < 128);
    T_CHECK(PROTO_PULSE_DONE != PROTO_MSG_SUBMIT);   /* not a message type */
}

static void test_field_layout(void)
{
    /*
     * The kernel dispatches on the first two bytes of the message, and a pulse
     * arrives in the same buffer, so the type must be first and 16 bits wide.
     */
    T_CHECK_SIZE(offsetof(proto_submit_t, type), 0);
    T_CHECK_SIZE(offsetof(proto_ack_t, type), 0);
    T_CHECK_SIZE(offsetof(proto_fetch_t, type), 0);
    T_CHECK_SIZE(offsetof(proto_result_t, type), 0);

    T_CHECK_SIZE(sizeof(((proto_submit_t *)0)->type), 2);
    T_CHECK_SIZE(sizeof(((proto_ack_t *)0)->type), 2);
    T_CHECK_SIZE(sizeof(((proto_fetch_t *)0)->type), 2);
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->type), 2);

    /* The block bookkeeping is 64-bit where it has to be: a recording longer
     * than 2^32 samples would overflow a 32-bit index. */
    T_CHECK_SIZE(sizeof(((proto_block_desc_t *)0)->first_sample), 8);
    T_CHECK_SIZE(sizeof(((proto_block_desc_t *)0)->samples), 4);
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->elapsed_ns), 8);
}

static void test_shm_name(void)
{
    const char *name = PROTO_SHM_NAME;
    size_t      len  = strlen(name);

    /*
     * shm_open() maps a leading '/' to /dev/shmem/<name>; without it the object
     * would be created under the server's current directory, where the client
     * would never find it. Exactly one slash: a second one names a directory.
     */
    T_CHECK(len >= 2);
    T_CHECK(name[0] == '/');
    T_CHECK(strchr(name + 1, '/') == NULL);

    /* Nothing that needs quoting, and short enough to sit comfortably in the
     * /dev/shmem namespace (only PATH_MAX is enforced there). */
    T_CHECK(strpbrk(name, " \t\r\n") == NULL);
    T_CHECK(len < 64);
}

static void test_shm_layout(void)
{
    unsigned i;

    /* Fixed-size headers: both are read straight out of the mapping. */
    T_CHECK_SIZE(sizeof(proto_shm_header_t), 24);
    T_CHECK_SIZE(sizeof(proto_shm_slot_t), 16);
    T_CHECK_SIZE(PROTO_SHM_HEADER_BYTES, 24);
    T_CHECK_SIZE(PROTO_SHM_SLOT_HEADER_BYTES, 16);

    /* A slot is its header plus two full blocks: the sorted one and the
     * filtered one. */
    T_CHECK_SIZE(PROTO_SHM_SLOT_BYTES, 16 + 2 * 65536);
    T_CHECK_SIZE(PROTO_SHM_BYTES, 24 + PROTO_SHM_SLOTS * PROTO_SHM_SLOT_BYTES);
    T_CHECK_INT(PROTO_SHM_SLOTS, 4);

    /* The magic, the version and the state word are what tell a live region
     * from a stale one. */
    T_CHECK(PROTO_SHM_MAGIC != 0);
    T_CHECK(PROTO_SHM_MAGIC != PROTO_SLOT_FREE);
    T_CHECK(PROTO_SHM_MAGIC != PROTO_SLOT_WRITTEN);
    T_CHECK_SIZE(PROTO_SHM_VERSION, PROTO_VERSION);
    T_CHECK(PROTO_SLOT_FREE != PROTO_SLOT_WRITTEN);

    /*
     * Every slot's two arrays sit inside their own slot, stay aligned to the
     * sample size, and are reached by offsets that are never 0 -- 0 is how a
     * result says "this array was not produced".
     */
    for (i = 0; i < PROTO_SHM_SLOTS; i++) {
        uint32_t slot     = PROTO_SHM_SLOT_BASE(i);
        uint32_t sorted   = PROTO_SHM_SORTED_OFFSET(i);
        uint32_t filtered = PROTO_SHM_FILTERED_OFFSET(i);

        T_CHECK(slot >= PROTO_SHM_HEADER_BYTES);
        T_CHECK(sorted != 0 && filtered != 0);
        T_CHECK(sorted % PROTO_SAMPLE_BYTES == 0);
        T_CHECK(filtered % PROTO_SAMPLE_BYTES == 0);
        T_CHECK(sorted >= slot + PROTO_SHM_SLOT_HEADER_BYTES);
        T_CHECK(filtered == sorted + PROTO_BLOCK_BYTES);
        T_CHECK(filtered + PROTO_BLOCK_SAMPLES * PROTO_SAMPLE_BYTES
                <= PROTO_SHM_BYTES);
    }

    /* Slots do not overlap, and the last one ends inside the region. */
    T_CHECK(PROTO_SHM_SLOT_BASE(1) >=
            PROTO_SHM_SLOT_BASE(0) + PROTO_SHM_SLOT_BYTES);
    T_CHECK(PROTO_SHM_SLOT_BASE(PROTO_SHM_SLOTS - 1) + PROTO_SHM_SLOT_BYTES
            <= PROTO_SHM_BYTES);

    /* The whole region fits the 32-bit offsets a result carries. */
    T_CHECK(PROTO_SHM_BYTES <= 0xFFFFFFFFu);
}

static void test_result_describes_the_region(void)
{
    /* 32-bit offsets, lengths and checksums: one region is far smaller than
     * 4 GiB, so a 32-bit field is enough and cannot be mistaken for a count of
     * something else. */
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->sorted_offset), 4);
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->filtered_offset), 4);
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->length), 4);
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->sorted_checksum), 4);
    T_CHECK_SIZE(sizeof(((proto_result_t *)0)->filtered_checksum), 4);

    /* A result may say "not produced" (offset 0), but byte 0 of the region is
     * its header, so no real array ever starts there. */
    T_CHECK(PROTO_SHM_SORTED_OFFSET(0) != 0);
    T_CHECK(PROTO_SHM_FILTERED_OFFSET(0) != 0);
}

/* ------------------------------------------------------------------------- */

static void test_checksum(void)
{
    unsigned char a[8] = { 'h', 'e', 'l', 'l', 'o', 0, 0, 0 };
    unsigned char b[8];
    float         zero    = 0.0f;
    float         negzero = -0.0f;
    float         block_a[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float         block_b[4];

    /* The definition: FNV-1a over nothing is the offset basis. */
    T_CHECK_SIZE(proto_checksum("", 0), 0x811c9dc5u);

    /* Deterministic. */
    T_CHECK_SIZE(proto_checksum(a, 5), proto_checksum(a, 5));

    /* A single changed byte changes the result. */
    memcpy(b, a, sizeof(b));
    b[0] = 'H';
    T_CHECK(proto_checksum(b, 5) != proto_checksum(a, 5));

    /*
     * It checksums bits, not values: +0.0 and -0.0 are equal as floats but
     * differ in their representation, and the blocks are bit patterns.
     */
    T_CHECK(proto_checksum(&zero, sizeof(zero)) !=
            proto_checksum(&negzero, sizeof(negzero)));

    /* A one-bit change in one sample of many is caught. */
    memcpy(block_b, block_a, sizeof(block_a));
    block_b[3] = 4.000001f;
    T_CHECK(proto_checksum(block_b, sizeof(block_b)) !=
            proto_checksum(block_a, sizeof(block_a)));

    /* A NULL buffer is reported rather than dereferenced. */
    T_CHECK_SIZE(proto_checksum(NULL, 16), 0);
}

static void test_submit_prepare_is_deterministic(void)
{
    /*
     * Two identical submits prepared from differently dirtied structs must be
     * byte-identical, because the whole struct crosses to the server and stale
     * padding would make two "same" messages differ.
     */
    proto_submit_t     dirty;
    proto_submit_t     clean;
    proto_block_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.sequence     = 3;
    desc.samples      = 16384;
    desc.first_sample = 49152;
    desc.rate_hz      = 100;
    desc.channel      = PROTO_CHANNEL_VOLTAGE;
    desc.checksum     = 0x12345678u;
    desc.min_value    = 2.5f;
    desc.max_value    = 4.25f;

    memset(&dirty, 0xAA, sizeof(dirty));
    memset(&clean, 0x00, sizeof(clean));

    proto_submit_prepare(&dirty, 42, &desc);
    proto_submit_prepare(&clean, 42, &desc);

    T_CHECK(memcmp(&dirty, &clean, sizeof(dirty)) == 0);
    T_CHECK_INT(dirty.type, PROTO_MSG_SUBMIT);
    T_CHECK_INT(dirty.version, PROTO_VERSION);
    T_CHECK_INT(clean.job_id, 42);
    T_CHECK_INT(clean.desc.samples, 16384);

    proto_submit_prepare(NULL, 1, NULL);   /* must not crash */
}

static void test_submit_validation(void)
{
    proto_submit_t     msg;
    proto_block_desc_t desc;
    proto_block_t     *block = malloc(sizeof(*block));
    int                i;

    T_CHECK(block != NULL);
    if (block == NULL)
        return;

    for (i = 0; i < (int)PROTO_BLOCK_SAMPLES; i++)
        block->samples[i] = (float)i / 8.0f;

    memset(&desc, 0, sizeof(desc));
    desc.sequence     = 0;
    desc.samples      = 100;
    desc.first_sample = 0;
    desc.rate_hz      = 100;
    desc.channel      = PROTO_CHANNEL_VOLTAGE;
    desc.checksum     = proto_checksum(block->samples, 100 * sizeof(float));
    desc.min_value    = 0.0f;
    desc.max_value    = 12.375f;

    proto_submit_prepare(&msg, 7, &desc);
    T_CHECK_INT(proto_submit_valid(&msg), 1);
    T_CHECK_INT(proto_submit_verify(&msg, block), 1);

    /* Wrong version. */
    msg.version = (uint16_t)(PROTO_VERSION + 1);
    T_CHECK_INT(proto_submit_valid(&msg), 0);
    msg.version = (uint16_t)PROTO_VERSION;

    /* Wrong type: an ack must never be accepted as a submit. */
    msg.type = (uint16_t)PROTO_MSG_ACK;
    T_CHECK_INT(proto_submit_valid(&msg), 0);
    msg.type = (uint16_t)PROTO_MSG_SUBMIT;

    /* Descriptor boundaries. */
    msg.desc.samples = 0;
    T_CHECK_INT(proto_submit_valid(&msg), 0);
    msg.desc.samples = PROTO_BLOCK_SAMPLES;
    T_CHECK_INT(proto_submit_valid(&msg), 1);
    msg.desc.samples = PROTO_BLOCK_SAMPLES + 1;
    T_CHECK_INT(proto_submit_valid(&msg), 0);
    msg.desc.samples = 100;

    msg.desc.rate_hz = 0;
    T_CHECK_INT(proto_submit_valid(&msg), 0);
    msg.desc.rate_hz = 100;

    msg.desc.channel = (uint32_t)PROTO_CHANNEL_COUNT;
    T_CHECK_INT(proto_submit_valid(&msg), 0);
    msg.desc.channel = (uint32_t)PROTO_CHANNEL_VOLTAGE;

    T_CHECK_INT(proto_submit_valid(NULL), 0);
    T_CHECK_INT(proto_block_desc_valid(NULL), 0);
    T_CHECK_INT(proto_submit_verify(&msg, NULL), 0);

    /* A corrupted block passes the structural check but fails the checksum. */
    block->samples[5] += 1.0f;
    T_CHECK_INT(proto_submit_valid(&msg), 1);
    T_CHECK_INT(proto_submit_verify(&msg, block), 0);

    free(block);
}

static void test_partial_block_checksum(void)
{
    /*
     * Only the samples the descriptor claims are covered, so the unused tail of
     * a short final block cannot affect the result. A recording whose length is
     * not a whole number of blocks depends on this.
     */
    proto_submit_t     msg;
    proto_block_desc_t desc;
    proto_block_t     *block = malloc(sizeof(*block));
    int                i;

    T_CHECK(block != NULL);
    if (block == NULL)
        return;

    memset(block, 0xFF, sizeof(*block));          /* dirty tail */
    for (i = 0; i < 100; i++)
        block->samples[i] = (float)i;

    memset(&desc, 0, sizeof(desc));
    desc.samples   = 100;
    desc.rate_hz   = 50;
    desc.channel   = (uint32_t)PROTO_CHANNEL_TEMPERATURE;
    desc.checksum  = proto_checksum(block->samples, 100 * sizeof(float));

    proto_submit_prepare(&msg, 1, &desc);
    T_CHECK_INT(proto_submit_verify(&msg, block), 1);

    block->samples[100] = 12345.0f;               /* touch the tail only */
    T_CHECK_INT(proto_submit_verify(&msg, block), 1);

    block->samples[99] = 124.0f;                  /* now change a real sample */
    T_CHECK_INT(proto_submit_verify(&msg, block), 0);

    free(block);
}

static void test_names(void)
{
    int i;

    for (i = 0; i < (int)PROTO_STATUS_COUNT; i++) {
        const char *name = proto_status_str((uint16_t)i);

        T_CHECK(name != NULL);
        T_CHECK(name[0] != '\0');
    }

    /* Distinct names, so a log line tells the failure modes apart. */
    T_CHECK(strcmp(proto_status_str(PROTO_OK),
                   proto_status_str(PROTO_ERR_CHECKSUM)) != 0);
    T_CHECK(strcmp(proto_status_str(PROTO_ERR_NO_JOB),
                   proto_status_str(PROTO_ERR_BUSY)) != 0);

    /* Unknown values are named, not NULL. */
    T_CHECK(proto_status_str(9999) != NULL);
    T_CHECK(proto_type_str(0) != NULL);
    T_CHECK(proto_channel_str((uint32_t)PROTO_CHANNEL_COUNT) != NULL);

    T_CHECK(strcmp(proto_type_str(PROTO_MSG_SUBMIT), "SUBMIT") == 0);
    T_CHECK(strcmp(proto_channel_str(PROTO_CHANNEL_VOLTAGE), "voltage") == 0);
}

/* ------------------------------------------------------------------------- */

int main(void)
{
    printf("protocol tests (the common message definition)\n\n");

    T_RUN(test_block_is_64kib);
    T_RUN(test_message_types);
    T_RUN(test_pulse_code);
    T_RUN(test_field_layout);
    T_RUN(test_shm_name);
    T_RUN(test_shm_layout);
    T_RUN(test_result_describes_the_region);
    T_RUN(test_checksum);
    T_RUN(test_submit_prepare_is_deterministic);
    T_RUN(test_submit_validation);
    T_RUN(test_partial_block_checksum);
    T_RUN(test_names);

    return t_summary();
}
