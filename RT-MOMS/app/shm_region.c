#include "shm_region.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void clear_region(shm_region_t *r)
{
    if (r != NULL) {
        r->fd = -1;
        r->base = MAP_FAILED;
        r->bytes = 0;
        r->owner = 0;
    }
}

int shm_region_create(shm_region_t *r)
{
    int fd;
    void *p;
    proto_shm_header_t *h;

    if (r == NULL) { errno = EINVAL; return -1; }
    clear_region(r);
    (void)shm_unlink(PROTO_SHM_NAME);
    fd = shm_open(PROTO_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) {
        perror("shm_region: shm_open");
        return -1;
    }
    if (ftruncate(fd, (off_t)PROTO_SHM_BYTES) == -1) {
        perror("shm_region: ftruncate");
        close(fd); shm_unlink(PROTO_SHM_NAME); return -1;
    }
    p = mmap(NULL, PROTO_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("shm_region: mmap");
        close(fd); shm_unlink(PROTO_SHM_NAME); return -1;
    }
    memset(p, 0, PROTO_SHM_BYTES);
    h = (proto_shm_header_t *)p;
    h->magic = PROTO_SHM_MAGIC;
    h->version = PROTO_SHM_VERSION;
    h->slots = PROTO_SHM_SLOTS;
    h->slot_bytes = PROTO_SHM_SLOT_BYTES;
    h->generation = (uint64_t)time(NULL);
    r->fd = fd; r->base = p; r->bytes = PROTO_SHM_BYTES; r->owner = 1;
    return 0;
}

int shm_region_open_readonly(shm_region_t *r)
{
    int fd;
    struct stat st;
    void *p;
    const proto_shm_header_t *h;

    if (r == NULL) { errno = EINVAL; return -1; }
    clear_region(r);
    fd = shm_open(PROTO_SHM_NAME, O_RDONLY, 0);
    if (fd == -1) { perror("shm_region: shm_open client"); return -1; }
    if (fstat(fd, &st) == -1 || (size_t)st.st_size < PROTO_SHM_BYTES) {
        perror("shm_region: invalid shared region size");
        close(fd); errno = EINVAL; return -1;
    }
    p = mmap(NULL, PROTO_SHM_BYTES, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("shm_region: mmap client"); close(fd); return -1;
    }
    h = (const proto_shm_header_t *)p;
    if (h->magic != PROTO_SHM_MAGIC || h->version != PROTO_SHM_VERSION ||
        h->slots != PROTO_SHM_SLOTS || h->slot_bytes != PROTO_SHM_SLOT_BYTES) {
        fprintf(stderr, "shm_region: incompatible shared-memory header\n");
        munmap(p, PROTO_SHM_BYTES); close(fd); errno = EPROTO; return -1;
    }
    r->fd = fd; r->base = p; r->bytes = PROTO_SHM_BYTES; r->owner = 0;
    return 0;
}

void shm_region_close(shm_region_t *r)
{
    if (r == NULL) return;
    if (r->base != MAP_FAILED && r->base != NULL)
        (void)munmap(r->base, r->bytes);
    if (r->fd >= 0) (void)close(r->fd);
    if (r->owner) (void)shm_unlink(PROTO_SHM_NAME);
    clear_region(r);
}

int shm_region_valid(const shm_region_t *r)
{
    const proto_shm_header_t *h;
    if (r == NULL || r->base == NULL || r->base == MAP_FAILED ||
        r->bytes < PROTO_SHM_BYTES) return 0;
    h = (const proto_shm_header_t *)r->base;
    return h->magic == PROTO_SHM_MAGIC && h->version == PROTO_SHM_VERSION &&
           h->slots == PROTO_SHM_SLOTS && h->slot_bytes == PROTO_SHM_SLOT_BYTES;
}
