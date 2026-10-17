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


#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/*
 * Milestone 3: shared-memory layout for one recording channel.
 * Maximum recording capacity: 60,000 float samples.
 */

#define SHM_NAME "/sortmed_shm"

#define SHM_MAX_SAMPLES       60000u
#define SHM_BLOCK_SAMPLES     16384u
#define SHM_MAX_BLOCKS        4u

#define SHM_BUFFER_BYTES \
    ((size_t)SHM_MAX_SAMPLES * sizeof(float))

#define SHM_MAGIC   0x534D4544u
#define SHM_VERSION 1u

typedef enum {
    SHM_STATE_EMPTY = 0,
    SHM_STATE_WRITING,
    SHM_STATE_READY,
    SHM_STATE_PROCESSING,
    SHM_STATE_COMPLETE,
    SHM_STATE_ERROR
} shm_state_t;

/*
 * Offsets are relative to the start of the mapped shared-memory region.
 * Never store process-specific pointers in this structure.
 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t capacity_samples;
    uint32_t recording_samples;
    uint32_t block_count;
    uint32_t completed_blocks;
    uint32_t state;
    uint32_t reserved;

    uint64_t raw_offset;
    uint64_t sorted_offset;
    uint64_t scratch_offset;
    uint64_t filtered_offset;

    float global_median;
    uint32_t total_outliers;
} shm_header_t;

/* Per-block metadata shared between client and server. */
typedef struct {
    uint32_t sequence;
    uint32_t samples;
    uint64_t first_sample;
    uint32_t checksum;
    uint32_t state;
    uint32_t outliers_removed;
    uint32_t reserved;
} shm_block_meta_t;

/* Validate a byte range without overflowing offset + length. */
int shm_range_valid(size_t region_bytes, uint64_t offset, size_t length);

/* Validate that an array of floats fits in the shared region. */
int shm_float_range_valid(size_t region_bytes,
                          uint64_t offset,
                          size_t sample_count);

#endif /* SHARED_MEMORY_H */
