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
 * protocol.c
 *
 * Helpers for the common message definition in protocol.h: the block
 * checksum, the validation the server performs on an incoming submit, and the
 * names used in log output.
 *
 * Deliberately free of QNX headers, so the same code runs in the server on the
 * target and in the host tests, which is what lets the validation logic be
 * tested before any QNX code exists. The only project include beyond its own
 * header is median.h, for the width rule the submit message carries.
 */

#include "protocol.h"
#include "median.h"

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Checksum                                                                   */
/* ------------------------------------------------------------------------- */

uint32_t proto_checksum(const void *data, size_t bytes)
{
    const unsigned char *p = (const unsigned char *)data;
    uint32_t             hash = 0x811c9dc5u;   /* FNV-1a 32-bit offset basis */
    size_t               i;

    if (p == NULL)
        return 0;

    for (i = 0; i < bytes; i++) {
        hash ^= (uint32_t)p[i];
        hash *= 0x01000193u;                   /* FNV-1a 32-bit prime        */
    }
    return hash;
}

/* ------------------------------------------------------------------------- */
/* Validation                                                                 */
/* ------------------------------------------------------------------------- */

int proto_block_desc_valid(const proto_block_desc_t *desc)
{
    if (desc == NULL)
        return 0;
    if (desc->samples == 0 || desc->samples > PROTO_BLOCK_SAMPLES)
        return 0;
    if (desc->rate_hz == 0)
        return 0;
    if (desc->channel >= (uint32_t)PROTO_CHANNEL_COUNT)
        return 0;
    return 1;
}

int proto_submit_valid(const proto_submit_t *msg)
{
    if (msg == NULL)
        return 0;
    if (msg->type != PROTO_MSG_SUBMIT)
        return 0;
    if (msg->version != PROTO_VERSION)
        return 0;
    /* An unknown flag means the sender expects behaviour this build does not
     * have, so the submit is refused rather than quietly treated as ordinary. */
    if ((msg->flags & ~PROTO_SUBMIT_FLAGS_KNOWN) != 0)
        return 0;
    /* The filter width is the client's request for this job, so it must be a
     * width the filter can run: odd, MEDIAN_MIN_WIDTH..MEDIAN_MAX_WIDTH. */
    if (!median_width_valid(msg->filter_width))
        return 0;
    return proto_block_desc_valid(&msg->desc);
}

int proto_submit_verify(const proto_submit_t *msg, const proto_block_t *block)
{
    uint32_t checksum;

    if (!proto_submit_valid(msg) || block == NULL)
        return 0;

    /*
     * Only the samples the descriptor claims are covered. A short final block
     * has an unused tail, and checksumming that tail would make the value
     * depend on whatever happened to be in the buffer.
     */
    checksum = proto_checksum(block->samples,
                              (size_t)msg->desc.samples * sizeof(float));

    return (checksum == msg->desc.checksum) ? 1 : 0;
}

void proto_submit_prepare(proto_submit_t *msg, uint32_t job_id,
                          const proto_block_desc_t *desc, uint32_t filter_width)
{
    if (msg == NULL)
        return;

    memset(msg, 0, sizeof(*msg));      /* no stale padding on the wire */
    msg->type         = (uint16_t)PROTO_MSG_SUBMIT;
    msg->version      = (uint16_t)PROTO_VERSION;
    msg->job_id       = job_id;
    msg->filter_width = filter_width;

    if (desc != NULL)
        msg->desc = *desc;
}

/* ------------------------------------------------------------------------- */
/* The median in a pulse                                                      */
/* ------------------------------------------------------------------------- */

/*
 * memcpy rather than a union or a pointer cast: all are bit-exact, but this one
 * raises no type-punning question under -std=c99 -Wall -Wextra -pedantic, and a
 * compiler lowers it to a register move. Every float32 pattern survives both
 * directions, which test_protocol checks over a table of them.
 */

int32_t proto_float_to_pulse(float value)
{
    int32_t word;
    memcpy(&word, &value, sizeof(word));
    return word;
}

float proto_pulse_to_float(int32_t word)
{
    float value;
    memcpy(&value, &word, sizeof(value));
    return value;
}

/* ------------------------------------------------------------------------- */
/* Names for logging                                                          */
/* ------------------------------------------------------------------------- */

const char *proto_status_str(uint16_t status)
{
    switch (status) {
    case PROTO_OK:              return "ok";
    case PROTO_ERR_ARGUMENT:    return "bad argument";
    case PROTO_ERR_VERSION:     return "protocol version mismatch";
    case PROTO_ERR_CHECKSUM:    return "block checksum mismatch";
    case PROTO_ERR_NO_JOB:      return "unknown job";
    case PROTO_ERR_BUSY:        return "server busy";
    case PROTO_ERR_CAPACITY:    return "block larger than the server accepts";
    default:                    return "unknown status";
    }
}

const char *proto_type_str(uint16_t type)
{
    switch (type) {
    case PROTO_MSG_SUBMIT: return "SUBMIT";
    case PROTO_MSG_ACK:    return "ACK";
    case PROTO_MSG_FETCH:  return "FETCH";
    case PROTO_MSG_RESULT: return "RESULT";
    /*
     * Not a message type: included because the legacy view of a received pulse
     * mirrors the pulse code in the same leading field, and naming it makes a
     * debug dump readable.
     */
    case PROTO_PULSE_DONE: return "PULSE_DONE";
    default:               return "unknown message type";
    }
}

const char *proto_channel_str(uint32_t channel)
{
    switch (channel) {
    case PROTO_CHANNEL_TIME:        return "time";
    case PROTO_CHANNEL_CURRENT:     return "current";
    case PROTO_CHANNEL_VOLTAGE:     return "voltage";
    case PROTO_CHANNEL_TEMPERATURE: return "temperature";
    default:                        return "unknown channel";
    }
}
