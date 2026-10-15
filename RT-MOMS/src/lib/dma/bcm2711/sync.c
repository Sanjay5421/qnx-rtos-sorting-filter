/*
 * Copyright (c) 2022, BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/slog2.h>
#include "bcm2711_dmac.h"

/* shared memory object */
typedef struct {
    /* This global mutex is used to guard the access of all global resouces which includes
     * rsrcdb_create_resources(), bcm2711_dmac_sync_process_cnt_incr(),
     * bcm2711_dmac_sync_process_cnt_decr() call and global registers.
     */
    pthread_mutex_t mutex;
    uint32_t process_cnt;
} bcm2711_dmac_shmem_t;

static bcm2711_dmac_shmem_t * shmem_ptr = NULL;

int bcm2711_dmac_sync_init(void)
{
    static const char* const shmem_name = "/bcm2711_dmac_mutex";
    int status = EOK;
    int fd = 0;
    bool shmem_created = (bool)false;
    const mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

    if (shmem_ptr != NULL) {
        return 0;
    }

    fd = shm_open(shmem_name, O_RDWR | O_CREAT | O_EXCL, mode);
    if (fd >= 0) {
        //size the newly allocated memory
        status = ftruncate(fd, (off_t )sizeof(bcm2711_dmac_shmem_t));
        if (status == -1) {
            bcm2711_dma_slogf(SLOG2_ERROR, "Failed to call ftruncate(%s)", strerror(errno));
            goto fail;
        }
        shmem_created = (bool)true;
    } else {
        if (errno != EEXIST) {
            bcm2711_dma_slogf(SLOG2_ERROR, "Failed to create shared memory object(%s)", strerror(errno));
            return -1;
        }

        fd = shm_open(shmem_name, O_RDWR, 0);
        if (fd < 0) {
            bcm2711_dma_slogf(SLOG2_ERROR, "Failed to open shared memory object(%s)", strerror(errno));
            return -1;
        }
    }

    //map it to our control structure
    shmem_ptr = mmap(NULL, sizeof(bcm2711_dmac_shmem_t),
                        PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shmem_ptr == MAP_FAILED) {
        status = errno;
        bcm2711_dma_slogf(SLOG2_ERROR, "Failed to mmap shared memory(%s)", strerror(errno));
        goto fail;
    }

    if (shmem_created == true) {
        pthread_mutexattr_t mutex_attr;
        // Initialized the inter-process mutex
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr,PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&(shmem_ptr->mutex),&mutex_attr);
        pthread_mutexattr_destroy(&mutex_attr);
    }

    return 0;

fail:
    close(fd);
    shmem_ptr = NULL;
    errno = status;
    return -1;
}

void bcm2711_dmac_sync_dinit(void)
{
    munmap(shmem_ptr,sizeof(bcm2711_dmac_shmem_t));
    shmem_ptr = NULL;
}

pthread_mutex_t * dmac_sync_mutex_get(void)
{
    return &(shmem_ptr->mutex);
}

int bcm2711_dmac_sync_is_first_process(void)
{
    if (shmem_ptr->process_cnt == 1) {
        return 1;
    } else {
        return 0;
    }
}

void bcm2711_dmac_sync_process_cnt_incr(void)
{
    shmem_ptr->process_cnt++;
}

void bcm2711_dmac_sync_process_cnt_decr(void)
{
    shmem_ptr->process_cnt--;
}
