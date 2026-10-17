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

#ifndef SHM_REGION_H
#define SHM_REGION_H

#include "protocol.h"
#include <stddef.h>

typedef struct {
    int fd;
    void *base;
    size_t bytes;
    int owner;
} shm_region_t;

/* Server: replace any stale object, size it, map read/write and initialize it. */
int shm_region_create(shm_region_t *region);
/* Client: map the existing region read-only. */
int shm_region_open_readonly(shm_region_t *region);
/* Unmap and close. Only the owner unlinks the name. */
void shm_region_close(shm_region_t *region);
int shm_region_valid(const shm_region_t *region);

#endif
