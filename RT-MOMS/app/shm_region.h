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
