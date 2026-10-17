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


#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#include "../app/shared_memory.h"

static int checks = 0;
static int failures = 0;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        failures++; \
        printf("FAIL line %d: %s\n", __LINE__, #expr); \
    } \
} while (0)

int main(void)
{
    const size_t region = 1024;

    CHECK(shm_range_valid(region, 0, 0));
    CHECK(shm_range_valid(region, 0, region));
    CHECK(shm_range_valid(region, 512, 512));
    CHECK(shm_range_valid(region, region, 0));

    CHECK(!shm_range_valid(region, region, 1));
    CHECK(!shm_range_valid(region, 1025, 0));
    CHECK(!shm_range_valid(region, 900, 125));
    CHECK(!shm_range_valid(region, UINT64_MAX, 1));

    CHECK(shm_float_range_valid(region, 0, 256));
    CHECK(shm_float_range_valid(region, 4, 255));
    CHECK(!shm_float_range_valid(region, 4, 256));
    CHECK(!shm_float_range_valid(region, 1024, 1));
    CHECK(!shm_float_range_valid(region, 0, SIZE_MAX));

    printf("Shared-memory bounds tests: %d checks, %d failures\n",
           checks, failures);

    return failures == 0 ? 0 : 1;
}
