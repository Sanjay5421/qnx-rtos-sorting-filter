/*
 * QNX server for the M3 block service.
 *
 * Run on the target as: sortmed-server
 * The server owns /sortmed-results and the "sortmed" message service.
 * A single client at a time is supported by the project protocol.
 */
#include "protocol.h"
#include "mergesort.h"
#include "median.h"
#include "shm_region.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/iomsg.h>

#define SERVER_NAME "sortmed"
#define CLIENT_NAME "sortmed-client"

typedef struct {
    int used;
    uint32_t job_id;
    unsigned slot;
    proto_result_t result;
} job_entry_t;

static uint64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int find_job(job_entry_t jobs[PROTO_SHM_SLOTS], uint32_t id)
{
    unsigned i;
    for (i = 0; i < PROTO_SHM_SLOTS; ++i)
        if (jobs[i].used && jobs[i].job_id == id) return (int)i;
    return -1;
}

static int find_free(job_entry_t jobs[PROTO_SHM_SLOTS])
{
    unsigned i;
    for (i = 0; i < PROTO_SHM_SLOTS; ++i)
        if (!jobs[i].used) return (int)i;
    return -1;
}

static int process_submit(shm_region_t *region, mergesort_pool_t *pool,
                          const proto_submit_t *msg, const proto_block_t *block,
                          job_entry_t *job)
{
    unsigned char *base = (unsigned char *)region->base;
    proto_shm_slot_t *slot;
    float *sorted;
    float *filtered;
    median_config_t cfg;
    uint32_t outliers = 0;
    size_t n = msg->desc.samples;
    uint64_t start = now_ns();
    unsigned i = job->slot;

    slot = (proto_shm_slot_t *)(base + PROTO_SHM_SLOT_BASE(i));
    sorted = (float *)(void *)(base + PROTO_SHM_SORTED_OFFSET(i));
    filtered = (float *)(void *)(base + PROTO_SHM_FILTERED_OFFSET(i));

    slot->state = PROTO_SLOT_FREE;
    slot->job_id = msg->job_id;
    slot->length = (uint32_t)n;
    slot->reserved = 0;

    /* Keep the time-ordered input untouched for the rolling filter. */
    memcpy(sorted, block->samples, n * sizeof(float));
    if (ms_sort(pool, sorted, n) != 0) return -1;

    median_config_default(&cfg);
    if (median_filter_block(&cfg, NULL, 0, block->samples, n, NULL, 0,
                            filtered, &outliers) != 0)
        return -1;

    job->result.type = PROTO_MSG_RESULT;
    job->result.status = PROTO_OK;
    job->result.job_id = msg->job_id;
    job->result.median = median_of_sorted(sorted, n); /* this block's median */
    job->result.filter_width = cfg.width;
    job->result.outliers_removed = outliers;
    job->result.sorted_offset = PROTO_SHM_SORTED_OFFSET(i);
    job->result.filtered_offset = PROTO_SHM_FILTERED_OFFSET(i);
    job->result.length = (uint32_t)n;
    job->result.sorted_checksum = proto_checksum(sorted, n * sizeof(float));
    job->result.filtered_checksum = proto_checksum(filtered, n * sizeof(float));
    job->result.elapsed_ns = now_ns() - start;

    /* Publish metadata last so a client never treats partial arrays as ready. */
    slot->state = PROTO_SLOT_WRITTEN;
    return 0;
}

int main(void)
{
    name_attach_t *attach;
    shm_region_t region;
    mergesort_pool_t *pool;
    mergesort_options_t sort_options;
    job_entry_t jobs[PROTO_SHM_SLOTS];
    int client_coid = -1;

    memset(jobs, 0, sizeof(jobs));
    if (shm_region_create(&region) != 0) return EXIT_FAILURE;

    mergesort_options_default(&sort_options);
    sort_options.threads = 4;
    pool = ms_pool_create(&sort_options);
    if (pool == NULL) {
        shm_region_close(&region);
        return EXIT_FAILURE;
    }

    attach = name_attach(NULL, SERVER_NAME, 0);
    if (attach == NULL) {
        perror("sortmed-server: name_attach");
        ms_pool_destroy(pool);
        shm_region_close(&region);
        return EXIT_FAILURE;
    }
    printf("sortmed-server: service '%s' ready; shm=%s (%u bytes)\n",
           SERVER_NAME, PROTO_SHM_NAME, (unsigned)PROTO_SHM_BYTES);
    fflush(stdout);

    for (;;) {
        proto_submit_t submit;
        proto_block_t block;
        struct _msg_info info;
        iov_t riov[2];
        int rcvid;
        uint16_t type;

        memset(&submit, 0, sizeof(submit));
        memset(&block, 0, sizeof(block));
        SETIOV(&riov[0], &submit, sizeof(submit));
        SETIOV(&riov[1], &block, sizeof(block));
        rcvid = MsgReceivev(attach->chid, riov, 2, &info);
        if (rcvid == -1) {
            if (errno == EINTR) continue;
            perror("sortmed-server: MsgReceivev");
            break;
        }
        if (rcvid == 0) {
            /* This service currently has no pulse commands. */
            continue;
        }
        type = submit.type;

        if (type == PROTO_MSG_SUBMIT) {
            proto_ack_t ack;
            int free_index;
            unsigned j;
            memset(&ack, 0, sizeof(ack));
            ack.type = PROTO_MSG_ACK;
            ack.job_id = submit.job_id;

            if (info.msglen != (int)(sizeof(submit) + PROTO_BLOCK_BYTES)) {
                ack.status = PROTO_ERR_ARGUMENT;
                (void)MsgReply(rcvid, 0, &ack, sizeof(ack));
                continue;
            }
            if (submit.version != PROTO_VERSION) {
                ack.status = PROTO_ERR_VERSION;
                (void)MsgReply(rcvid, 0, &ack, sizeof(ack));
                continue;
            }
            if (!proto_submit_valid(&submit)) {
                ack.status = PROTO_ERR_ARGUMENT;
                (void)MsgReply(rcvid, 0, &ack, sizeof(ack));
                continue;
            }
            if (!proto_submit_verify(&submit, &block)) {
                ack.status = PROTO_ERR_CHECKSUM;
                (void)MsgReply(rcvid, 0, &ack, sizeof(ack));
                continue;
            }
            free_index = find_free(jobs);
            if (free_index < 0) {
                ack.status = PROTO_ERR_BUSY;
                (void)MsgReply(rcvid, 0, &ack, sizeof(ack));
                continue;
            }
            jobs[free_index].used = 1;
            jobs[free_index].job_id = submit.job_id;
            jobs[free_index].slot = (unsigned)free_index;
            for (j = 0; j < PROTO_SHM_SLOTS; ++j) {
                proto_shm_slot_t *s = (proto_shm_slot_t *)
                    ((unsigned char *)region.base + PROTO_SHM_SLOT_BASE(j));
                if (j == (unsigned)free_index) s->state = PROTO_SLOT_FREE;
            }
            ack.status = PROTO_OK;
            if (MsgReply(rcvid, 0, &ack, sizeof(ack)) == -1) {
                jobs[free_index].used = 0;
                continue;
            }

            if (process_submit(&region, pool, &submit, &block,
                               &jobs[free_index]) != 0) {
                jobs[free_index].result.type = PROTO_MSG_RESULT;
                jobs[free_index].result.status = PROTO_ERR_ARGUMENT;
                jobs[free_index].result.job_id = submit.job_id;
            }

            if (client_coid == -1)
                client_coid = name_open(CLIENT_NAME, 0);
            if (client_coid == -1 ||
                MsgSendPulse(client_coid, SIGEV_PULSE_PRIO_INHERIT,
                             PROTO_PULSE_DONE, (int)submit.job_id) == -1) {
                perror("sortmed-server: completion pulse");
                /* Keep the result fetchable; client can retry the fetch. */
            }
        } else if (type == PROTO_MSG_FETCH) {
            proto_fetch_t fetch;
            proto_result_t result;
            int ji;
            memcpy(&fetch, &submit, sizeof(fetch));
            ji = find_job(jobs, fetch.job_id);
            if (ji < 0) {
                memset(&result, 0, sizeof(result));
                result.type = PROTO_MSG_RESULT;
                result.status = PROTO_ERR_NO_JOB;
                result.job_id = fetch.job_id;
                (void)MsgReply(rcvid, 0, &result, sizeof(result));
                continue;
            }
            result = jobs[ji].result;
            if (MsgReply(rcvid, 0, &result, sizeof(result)) == -1) {
                perror("sortmed-server: reply fetch");
                continue;
            }
            /* Client reads the shared arrays before submitting its next block. */
            /* Leave state WRITTEN until the next submit reuses this slot.
             * The client reads the mapped arrays immediately after this reply. */
            jobs[ji].used = 0;
        } else {
            (void)MsgError(rcvid, ENOSYS);
        }
    }

    if (client_coid != -1) (void)name_close(client_coid);
    name_detach(attach, 0);
    ms_pool_destroy(pool);
    shm_region_close(&region);
    return EXIT_FAILURE;
}
