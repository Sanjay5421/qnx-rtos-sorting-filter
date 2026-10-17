
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
