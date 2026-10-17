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
 *   4                       (worker threads: parallel merge sort + rolling median,
 *                            writing the sorted and filtered arrays into the
 *                            shared region defined below)
 *
 *   5  MsgReceive on its own channel  <-- MsgSendPulse -- (PROTO_PULSE_DONE, job id)
 *
 *   6  MsgSend(fetch, job id)  -------------------->   MsgReply(result)
 *      the result carries the median; the sorted and filtered arrays wait in
 *      the shared region it points into, so nothing large crosses twice
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
/* Where the results live: one named shared region                            */
/* ------------------------------------------------------------------------- */

/*
 * Blocks arrive by message; results do not travel back the same way. The server
 * writes the sorted block and the filtered block into a single named shared
 * memory object -- 128 KiB that the client reads in place, instead of two more
 * copies per round trip. The result message stays small: the median, and the
 * byte offsets that say where in the region to look.
 *
 * The block itself still crosses as an IOV message part (submit header + block),
 * so "at least one IOV transfer" and "64 KB payload" are untouched: this region
 * changes where the data is unpacked, not how it arrives.
 *
 * Layout: one region header, then PROTO_SHM_SLOTS slots of PROTO_SHM_SLOT_BYTES.
 *
 *     proto_shm_header_t
 *     slot 0:  proto_shm_slot_t | sorted[PROTO_BLOCK_SAMPLES] | filtered[...]
 *     slot 1:  proto_shm_slot_t | sorted[PROTO_BLOCK_SAMPLES] | filtered[...]
 *     ...
 *
 * On QNX a name that begins with '/' becomes /dev/shmem/<name>; a name without
 * the leading slash would land under the *server's current directory* instead,
 * so the slash is part of the convention rather than decoration (shm_open(2)).
 *
 * Ownership. The server creates the object (shm_open, ftruncate, mmap) before it
 * attaches its service name, so a client that can reach the service can also map
 * the region; the client maps it read-only and never writes. The object outlives
 * the process that made it unless it is unlinked, so the server unlinks and
 * recreates it at startup and the header carries a magic word, a version and a
 * generation counter -- a stale or foreign object is then rejected instead of
 * read. Which slot a job lands in is the server's business and appears in no
 * message: a result quotes absolute offsets from the start of the region.
 * One client at a time is assumed, as the project has one; the slot count is
 * what bounds how many finished results may be waiting to be collected.
 */

/*: Name of the region. Leading '/' => /dev/shmem/sortmed-results. */
#define PROTO_SHM_NAME   "/sortmed-results"

/*: Slots, i.e. how many jobs may hold a result at once. A submit that arrives
 *  when none is free is refused with PROTO_ERR_BUSY rather than overwriting a
 *  result the client has not read yet. */
#define PROTO_SHM_SLOTS  4u

/*: Bytes per float32 sample. Every offset in the region is a multiple of it. */
#define PROTO_SAMPLE_BYTES 4u

/*: Stamped into the region so that a mapping to a stale or foreign object is
 *  caught rather than read. "SMDR". */
#define PROTO_SHM_MAGIC   0x534d4452u

/*: Tied to PROTO_VERSION: the layout below cannot change without it. */
#define PROTO_SHM_VERSION PROTO_VERSION

/*
 * Region header: the first bytes of the mapping, validated before any offset
 * from a result message is trusted.
 */
typedef struct {
    uint32_t magic;        /* PROTO_SHM_MAGIC                                */
    uint32_t version;      /* PROTO_SHM_VERSION                              */
    uint32_t slots;        /* PROTO_SHM_SLOTS                                */
    uint32_t slot_bytes;   /* PROTO_SHM_SLOT_BYTES                           */
    uint64_t generation;   /* bumped by every server start, so a client that
                            * still holds an older mapping can notice         */
} proto_shm_header_t;

/*: Slot state word. Only WRITTEN means the arrays below hold a finished result. */
#define PROTO_SLOT_FREE    0u
#define PROTO_SLOT_WRITTEN 0x57325244u   /* "WR2D" */

/*
 * Start of every slot. The client reads this after being told that a result
 * exists and compares job_id with the result it is collecting: a mismatch means
 * the slot has been reused for a newer job, and the arrays must not be trusted.
 */
typedef struct {
    uint32_t job_id;       /* the job whose result is in this slot           */
    uint32_t length;       /* float32 values written to each array below      */
    uint32_t state;        /* PROTO_SLOT_FREE or PROTO_SLOT_WRITTEN          */
    uint32_t reserved;     /* 0; keeps the header exactly 16 bytes           */
} proto_shm_slot_t;

#define PROTO_SHM_HEADER_BYTES      ((uint32_t)sizeof(proto_shm_header_t))
#define PROTO_SHM_SLOT_HEADER_BYTES ((uint32_t)sizeof(proto_shm_slot_t))
#define PROTO_SHM_SLOT_BYTES        (PROTO_SHM_SLOT_HEADER_BYTES + 2u * PROTO_BLOCK_BYTES)
#define PROTO_SHM_BYTES             (PROTO_SHM_HEADER_BYTES + PROTO_SHM_SLOTS * PROTO_SHM_SLOT_BYTES)

/*: Byte offsets, from the start of the region, of a slot and of the two arrays
 *  inside it. No valid offset is 0, which is how a result says "not produced". */
#define PROTO_SHM_SLOT_BASE(i)       (PROTO_SHM_HEADER_BYTES + (i) * PROTO_SHM_SLOT_BYTES)
#define PROTO_SHM_SORTED_OFFSET(i)   (PROTO_SHM_SLOT_BASE(i) + PROTO_SHM_SLOT_HEADER_BYTES)
#define PROTO_SHM_FILTERED_OFFSET(i) (PROTO_SHM_SORTED_OFFSET(i) + PROTO_BLOCK_BYTES)

/* Both headers are read straight out of the mapping, so they must not grow
 * padding; and every offset a result can legitimately carry is non-zero. */
PROTO_STATIC_ASSERT(sizeof(proto_shm_header_t) == 24, shm_header_is_24_bytes);
PROTO_STATIC_ASSERT(sizeof(proto_shm_slot_t) == 16, shm_slot_header_is_16_bytes);
PROTO_STATIC_ASSERT(PROTO_SHM_SORTED_OFFSET(0) != 0, shm_offsets_are_nonzero);

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
 * The result. `median` is the filtered value the service computed. The two
 * offsets are byte offsets from the start of the shared region above --
 * `sorted_offset` for the sorted block, `filtered_offset` for the filtered one
 * -- and `length` is how many float32 values each of them holds. An offset of 0
 * means that array was not produced: no valid offset is 0, because the region
 * begins with its header.
 *
 * Each checksum is FNV-1a over the array it names, so the client can prove that
 * what it reads out of shared memory is what the server wrote there.
 */
typedef struct {
    uint16_t type;             /* PROTO_MSG_RESULT                            */
    uint16_t status;           /* proto_status_t                              */
    uint32_t job_id;
    float    median;           /* calculated median                           */
    uint32_t filter_width;     /* samples in the rolling window               */
    uint32_t outliers_removed; /* samples the filter rejected                 */
    uint32_t sorted_offset;    /* bytes from the region start; 0 = absent     */
    uint32_t filtered_offset;  /* bytes from the region start; 0 = absent     */
    uint32_t length;           /* float32 values at each of those offsets     */
    uint32_t sorted_checksum;  /* FNV-1a 32 over the sorted array             */
    uint32_t filtered_checksum;/* FNV-1a 32 over the filtered array           */
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
