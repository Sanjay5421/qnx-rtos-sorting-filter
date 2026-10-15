/*
 * protocol.h
 *
 * The common message definition shared by the client and the server. This is
 * the file the project prerequisites call "the message definition shall be in a
 * common header file", and it is the M1 interface specification in code form:
 * the block format, the descriptors, the message types and the exchange.
 *
 * ===========================================================================
 * The exchange
 * ===========================================================================
 *
 *   client                                            server
 *   ------                                            ------
 *   1  name_open("sortmed")  ---------------------->   name_attach("sortmed")
 *
 *   2  MsgSendv({submit, block})  -- 2-part IOV --->   MsgReceivev (header only)
 *      blocks until the ack                           MsgReadv   (the 64 KiB block)
 *                                                     validate, then queue the job
 *
 *   3                            <--- MsgReply(ack) --  job id; the client unblocks
 *
 *   4                       (worker threads: parallel merge sort + rolling median)
 *
 *   5  MsgReceive on its own channel  <-- MsgSendPulse -- (PROTO_PULSE_DONE, job id)
 *
 *   6  MsgSend(fetch, job id)  -------------------->   MsgReply(result)
 *      the result carries the median and the memory-offset descriptor
 *
 * Step 2 is the IOV transfer: two parts, a fixed header and then the block, so
 * neither side needs one contiguous 64 KiB buffer.
 *
 * Step 3 before step 5 is deliberate. The client must not be waiting for a
 * reply when the work finishes, because an rcvid is only usable until it has
 * been replied to: a server cannot deliver an event through an rcvid it has
 * already answered. So the ack is sent as soon as the job is accepted, the
 * client goes back to MsgReceive on its own channel, and the completion pulse
 * arrives later, on a connection the client owns.
 *
 * Step 6 exists because a pulse carries one byte of code and one machine word
 * of data, which is enough for a job id but not for a median plus an offset
 * plus a length. The pulse is the notification; the fetch is the payload.
 *
 * ===========================================================================
 * Assumptions
 * ===========================================================================
 *
 *   - Same node. The client and the server run on the same board, so the
 *     messages are plain structs with no byte-order or padding negotiation.
 *   - One header, one ABI. Both sides compile this header with the same
 *     compiler for the same target (aarch64le), so struct layout matches.
 *     There is no version-sniffing beyond PROTO_VERSION.
 *   - Message types must stay clear of the QNX system range. <sys/iomsg.h>
 *     defines the system types as _IO_BASE = 0x100 through _IO_MAX = 0x1FF;
 *     everything above is application space, which is where PROTO_TYPE_BASE
 *     sits. The guard at the end of this header re-checks that on the target.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Static assertion that works in C99 (the build uses -std=c99 -pedantic) and
 * on the target, instead of C11's _Static_assert.
 */
#define PROTO_STATIC_ASSERT(cond, tag) \
    typedef char proto_static_assert_##tag[(cond) ? 1 : -1]

/* ------------------------------------------------------------------------- */
/* Version                                                                    */
/* ------------------------------------------------------------------------- */

/*: Bumped whenever the layout of any message in this header changes. */
#define PROTO_VERSION 1u

/* ------------------------------------------------------------------------- */
/* Message types                                                              */
/* ------------------------------------------------------------------------- */

/*
 * Application message types, clear of 0x100..0x1FF. The offset from the system
 * range is the point, not the exact value: the only messages a plain channel
 * server sees besides ours are the _IO_CONNECT from name_open() and pulses,
 * and pulses arrive with an rcvid of 0 rather than as a typed message.
 */
#define PROTO_TYPE_BASE   0x1000u
#define PROTO_MSG_SUBMIT  (PROTO_TYPE_BASE + 1u)   /* client -> server, with the block */
#define PROTO_MSG_ACK     (PROTO_TYPE_BASE + 2u)   /* server -> client, in the reply   */
#define PROTO_MSG_FETCH   (PROTO_TYPE_BASE + 3u)   /* client -> server, asks for results */
#define PROTO_MSG_RESULT  (PROTO_TYPE_BASE + 4u)   /* server -> client, in the reply   */

/*
 * Completion pulse. 16 keeps well clear of the small codes the kernel and the
 * process manager reserve for themselves, and sits inside the user range
 * _PULSE_CODE_MINAVAIL.._PULSE_CODE_MAXAVAIL (checked below on the target).
 */
#define PROTO_PULSE_DONE 16

/* ------------------------------------------------------------------------- */
/* The 64 KiB block                                                           */
/* ------------------------------------------------------------------------- */

/*
 * 16384 float32 samples is exactly 65536 bytes, which is what makes the
 * payload satisfy the "at least 64 KB" prerequisite without any padding.
 */
#define PROTO_BLOCK_SAMPLES 16384u
#define PROTO_BLOCK_BYTES   (PROTO_BLOCK_SAMPLES * 4u)

/*
 * One transport block. This is the unit the client offloads and the unit the
 * server partitions across its worker threads.
 *
 * Only desc.samples of the array are meaningful: a recording that is not a
 * whole number of blocks ends with a short block. The loader zeroes the unused
 * tail, so a block is deterministic byte for byte and can be checksummed,
 * compared across runs, or written to a file for later analysis.
 */
typedef struct {
    float samples[PROTO_BLOCK_SAMPLES];
} proto_block_t;

PROTO_STATIC_ASSERT(sizeof(proto_block_t) == PROTO_BLOCK_BYTES, block_is_64_kib);

/* ------------------------------------------------------------------------- */
/* Channels                                                                   */
/* ------------------------------------------------------------------------- */

/*
 * The physical channels, in the order the generator stores them
 * (data/src/battery_dataset/config.py: time_s, current_A, voltage_V,
 * temperature_K).
 *
 * Careful: the interleaved export uses a different order on disk
 * (time_s, voltage_V, current_A, temperature_K). That mapping belongs to the
 * loader, which is the only place that should know about file layouts;
 * block_loader_channel_offset() is the single source of truth for it.
 */
typedef enum {
    PROTO_CHANNEL_TIME = 0,
    PROTO_CHANNEL_CURRENT,
    PROTO_CHANNEL_VOLTAGE,
    PROTO_CHANNEL_TEMPERATURE,
    PROTO_CHANNEL_COUNT
} proto_channel_t;

/* ------------------------------------------------------------------------- */
/* Status                                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Reply status. Transport-level failures use MsgError(); these describe what
 * the server decided about the job itself, so the client can report something
 * specific instead of "send failed".
 */
typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_ARGUMENT,     /* malformed message, bad descriptor, or both     */
    PROTO_ERR_VERSION,      /* the two sides disagree about PROTO_VERSION     */
    PROTO_ERR_CHECKSUM,     /* the block did not arrive intact                */
    PROTO_ERR_NO_JOB,       /* fetch for a job the server does not know       */
    PROTO_ERR_BUSY,         /* no free job slot                               */
    PROTO_ERR_CAPACITY,     /* more samples than the server accepts           */
    PROTO_STATUS_COUNT
} proto_status_t;

/* ------------------------------------------------------------------------- */
/* Messages                                                                   */
/* ------------------------------------------------------------------------- */

/*
 * Block descriptor: what the sender knows about the samples that follow it.
 * It stays outside the 64 KiB block on purpose, which is what allows the
 * server to receive the header alone and then pull the block with MsgReadv().
 */
typedef struct {
    uint32_t sequence;      /* 0, 1, 2, ... within one recording              */
    uint32_t samples;       /* valid entries in the block, <= BLOCK_SAMPLES   */
    uint64_t first_sample;  /* index of samples[0] in the source recording    */
    uint32_t rate_hz;       /* rate the samples were captured at              */
    uint32_t channel;       /* proto_channel_t                                */
    uint32_t checksum;      /* FNV-1a 32 over the first `samples` floats      */
    float    min_value;     /* range of this block, filled in by the loader   */
    float    max_value;
} proto_block_desc_t;

/*
 * Part 1 of the two-part submit. The 64 KiB block is part 2.
 *
 * The client chooses job_id and the server echoes it in the ack, the pulse and
 * the result, which is how the client pairs an asynchronous completion with
 * the request that caused it.
 */
typedef struct {
    uint16_t type;          /* PROTO_MSG_SUBMIT                               */
    uint16_t version;       /* PROTO_VERSION                                  */
    uint32_t job_id;
    proto_block_desc_t desc;
} proto_submit_t;

/* The server's reply to a submit: the job was accepted, or why it was not. */
typedef struct {
    uint16_t type;          /* PROTO_MSG_ACK                                  */
    uint16_t status;        /* proto_status_t                                 */
    uint32_t job_id;
} proto_ack_t;

/* Asks the server for the result of a job announced by a PROTO_PULSE_DONE. */
typedef struct {
    uint16_t type;          /* PROTO_MSG_FETCH                                 */
    uint16_t reserved;      /* 0                                               */
    uint32_t job_id;
} proto_fetch_t;

/*
 * The result. `median` is the filtered value the service computed, and
 * `offset`/`length`/`checksum` are the memory-offset descriptor: where the
 * service put the processed samples, how many there are, and how to tell they
 * arrived intact.
 */
typedef struct {
    uint16_t type;             /* PROTO_MSG_RESULT                            */
    uint16_t status;           /* proto_status_t                              */
    uint32_t job_id;
    float    median;           /* calculated median                           */
    uint32_t filter_width;     /* samples in the rolling window               */
    uint32_t outliers_removed; /* samples the filter rejected                 */
    uint32_t offset;           /* memory-offset descriptor: start of results  */
    uint32_t length;           /* float32 values available at that offset     */
    uint32_t checksum;         /* FNV-1a 32 over those values                 */
    uint64_t elapsed_ns;       /* server-side processing time                 */
} proto_result_t;

/*
 * The kernel dispatches on the first two bytes of a message, so every message
 * must start with its type at offset 0. Checked rather than assumed.
 */
PROTO_STATIC_ASSERT(offsetof(proto_submit_t, type) == 0, submit_type_first);
PROTO_STATIC_ASSERT(offsetof(proto_ack_t, type) == 0, ack_type_first);
PROTO_STATIC_ASSERT(offsetof(proto_fetch_t, type) == 0, fetch_type_first);
PROTO_STATIC_ASSERT(offsetof(proto_result_t, type) == 0, result_type_first);

/* ------------------------------------------------------------------------- */
/* Helpers, implemented in protocol.c                                         */
/* ------------------------------------------------------------------------- */

/* FNV-1a, 32-bit. Not cryptographic: it is here to catch a block that was
 * truncated, reordered or corrupted on the way across, nothing more. */
uint32_t proto_checksum(const void *data, size_t bytes);

/* Is this descriptor self-consistent? */
int proto_block_desc_valid(const proto_block_desc_t *desc);

/* Is this a submit we can act on (type, version and descriptor)? */
int proto_submit_valid(const proto_submit_t *msg);

/* proto_submit_valid() plus "the block matches the descriptor's checksum". */
int proto_submit_verify(const proto_submit_t *msg, const proto_block_t *block);

/* Fill in a submit header: type, version, job id and a copy of the descriptor.
 * It clears the whole struct first, so no stale padding goes on the wire. */
void proto_submit_prepare(proto_submit_t *msg, uint32_t job_id,
                          const proto_block_desc_t *desc);

/* Names for logging; never return NULL. */
const char *proto_status_str(uint16_t status);
const char *proto_type_str(uint16_t type);
const char *proto_channel_str(uint32_t channel);

/* ------------------------------------------------------------------------- */
/* Assumptions that can only be re-checked where the QNX headers exist        */
/* ------------------------------------------------------------------------- */

#if defined(_IO_MAX)
PROTO_STATIC_ASSERT(PROTO_TYPE_BASE > (unsigned)_IO_MAX, types_above_system_range);
#endif

#if defined(_PULSE_CODE_MINAVAIL) && defined(_PULSE_CODE_MAXAVAIL)
PROTO_STATIC_ASSERT(PROTO_PULSE_DONE >= _PULSE_CODE_MINAVAIL &&
                    PROTO_PULSE_DONE <= _PULSE_CODE_MAXAVAIL,
                    pulse_code_in_user_range);
#endif

#endif /* PROTOCOL_H */
