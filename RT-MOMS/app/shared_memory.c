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


#include "shared_memory.h"

#include <limits.h>

/*
 * Validate a byte range without overflowing offset + length.
 * A zero-length range is valid if its offset is within the region.
 */
int shm_range_valid(size_t region_bytes, uint64_t offset, size_t length)
{
    if (offset > (uint64_t)region_bytes)
        return 0;

    if (length > region_bytes - (size_t)offset)
        return 0;

    return 1;
}

/* Validate a float array's byte range safely. */
int shm_float_range_valid(size_t region_bytes,
                          uint64_t offset,
                          size_t sample_count)
{
    if (sample_count > SIZE_MAX / sizeof(float))
        return 0;

    return shm_range_valid(region_bytes,
                           offset,
                           sample_count * sizeof(float));
}
