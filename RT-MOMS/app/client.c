/*
 * QNX client for the M3 block service.
 *
 * Usage:
 *   sortmed-client <file.bin> planar|interleaved <time|current|voltage|temperature> <rate_hz>
 *
 * Start sortmed-server first. The client uses the loader already covered by
 * the host tests, submits each full 64 KiB block with MsgSendv, waits for the
 * completion pulse, then fetches the small result and reads the arrays in shm.
 */
#include "protocol.h"
#include "block_loader.h"
#include "median.h"
#include "mergesort.h"
#include "shm_region.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/iomsg.h>

#define SERVER_NAME "sortmed"
#define CLIENT_NAME "sortmed-client"
#define MAX_RECORDING_SAMPLES 60000u

static int parse_channel(const char *s, proto_channel_t *out)
{
    if (strcmp(s, "time") == 0) *out = PROTO_CHANNEL_TIME;
    else if (strcmp(s, "current") == 0) *out = PROTO_CHANNEL_CURRENT;
    else if (strcmp(s, "voltage") == 0) *out = PROTO_CHANNEL_VOLTAGE;
    else if (strcmp(s, "temperature") == 0) *out = PROTO_CHANNEL_TEMPERATURE;
    else return -1;
    return 0;
}

static int find_slot_for_offset(uint32_t sorted_offset, unsigned *slot_out)
{
    unsigned i;
    for (i = 0; i < PROTO_SHM_SLOTS; ++i) {
        if (sorted_offset == PROTO_SHM_SORTED_OFFSET(i)) {
            *slot_out = i;
            return 0;
        }
    }
    return -1;
}

static int wait_for_done(name_attach_t *attach, uint32_t expected_job)
{
    for (;;) {
        struct _pulse pulse;
        int rcvid = MsgReceive(attach->chid, &pulse, sizeof(pulse), NULL);
        if (rcvid == -1) {
            if (errno == EINTR) continue;
            perror("sortmed-client: MsgReceive pulse");
            return -1;
        }
        if (rcvid != 0) {
            (void)MsgError(rcvid, ENOSYS);
            continue;
        }
        if (pulse.code == PROTO_PULSE_DONE &&
            (uint32_t)pulse.value.sival_int == expected_job)
            return 0;
    }
}

int main(int argc, char **argv)
{
    int layout;
    proto_channel_t channel;
    uint32_t rate;
    char *end;
    unsigned long parsed_rate;
    name_attach_t *pulse_attach = NULL;
    int server_coid = -1;
    block_loader_t *loader = NULL;
    shm_region_t region;
    int region_opened = 0;
    float *raw = NULL, *sorted_all = NULL, *filtered_all = NULL;
    size_t total = 0;
    uint32_t job_id = 1;
    int status = EXIT_FAILURE;

    region.fd = -1;
    region.base = (void *)-1;
    region.bytes = 0;
    region.owner = 0;

    if (argc != 5) {
        fprintf(stderr, "usage: %s <file.bin> planar|interleaved "
                        "<time|current|voltage|temperature> <rate_hz>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[2], "planar") == 0) layout = SENSOR_LAYOUT_PLANAR;
    else if (strcmp(argv[2], "interleaved") == 0) layout = SENSOR_LAYOUT_INTERLEAVED;
    else {
        fprintf(stderr, "client: layout must be planar or interleaved\n");
        return EXIT_FAILURE;
    }
    if (parse_channel(argv[3], &channel) != 0) {
        fprintf(stderr, "client: unknown channel '%s'\n", argv[3]);
        return EXIT_FAILURE;
    }
    errno = 0;
    parsed_rate = strtoul(argv[4], &end, 10);
    if (errno || *argv[4] == '\0' || *end != '\0' ||
        parsed_rate == 0 || parsed_rate > UINT32_MAX) {
        fprintf(stderr, "client: rate_hz must be a positive integer\n");
        return EXIT_FAILURE;
    }
    rate = (uint32_t)parsed_rate;

    raw = (float *)malloc(MAX_RECORDING_SAMPLES * sizeof(float));
    sorted_all = (float *)malloc(MAX_RECORDING_SAMPLES * sizeof(float));
    filtered_all = (float *)malloc(MAX_RECORDING_SAMPLES * sizeof(float));
    if (!raw || !sorted_all || !filtered_all) {
        perror("client: allocate recording buffers");
        goto cleanup;
    }

    /* Register the pulse endpoint before the first submit. */
    pulse_attach = name_attach(NULL, CLIENT_NAME, 0);
    if (!pulse_attach) { perror("client: name_attach"); goto cleanup; }
    server_coid = name_open(SERVER_NAME, 0);
    if (server_coid == -1) { perror("client: name_open server (start server first)"); goto cleanup; }
    if (shm_region_open_readonly(&region) != 0) goto cleanup;
    region_opened = 1;

    loader = block_loader_open(argv[1], layout, channel, rate);
    if (!loader) goto cleanup;

    for (;;) {
        proto_block_t block;
        proto_block_desc_t desc;
        proto_submit_t submit;
        proto_ack_t ack;
        iov_t send_iov[2], reply_iov;
        int n;
        proto_fetch_t fetch;
        proto_result_t result;
        unsigned slot_index;
        const unsigned char *base;
        const proto_shm_slot_t *slot;
        const float *shared_sorted, *shared_filtered;
        size_t bytes;

        memset(&block, 0, sizeof(block));
        n = block_loader_next(loader, &block, &desc);
        if (n < 0) { fprintf(stderr, "client: recording read failed\n"); goto cleanup; }
        if (n == 0) break;
        if (total + (size_t)n > MAX_RECORDING_SAMPLES) {
            fprintf(stderr, "client: recording exceeds %u samples\n", MAX_RECORDING_SAMPLES);
            goto cleanup;
        }
        memcpy(raw + total, block.samples, (size_t)n * sizeof(float));

        proto_submit_prepare(&submit, job_id, &desc);
        SETIOV(&send_iov[0], &submit, sizeof(submit));
        SETIOV(&send_iov[1], &block, sizeof(block));
        SETIOV(&reply_iov, &ack, sizeof(ack));
        if (MsgSendv(server_coid, send_iov, 2, &reply_iov, 1) == -1) {
            perror("client: MsgSendv submit"); goto cleanup;
        }
        if (ack.type != PROTO_MSG_ACK || ack.job_id != job_id ||
            ack.status != PROTO_OK) {
            fprintf(stderr, "client: submit %u rejected: %s\n",
                    job_id, proto_status_str(ack.status));
            goto cleanup;
        }
        if (wait_for_done(pulse_attach, job_id) != 0) goto cleanup;

        memset(&fetch, 0, sizeof(fetch));
        fetch.type = PROTO_MSG_FETCH;
        fetch.job_id = job_id;
        memset(&result, 0, sizeof(result));
        if (MsgSend(server_coid, &fetch, sizeof(fetch),
                    &result, sizeof(result)) == -1) {
            perror("client: MsgSend fetch"); goto cleanup;
        }
        if (result.type != PROTO_MSG_RESULT || result.status != PROTO_OK ||
            result.job_id != job_id || result.length != (uint32_t)n) {
            fprintf(stderr, "client: invalid result for job %u (status=%s)\n",
                    job_id, proto_status_str(result.status));
            goto cleanup;
        }
        if (find_slot_for_offset(result.sorted_offset, &slot_index) != 0 ||
            result.filtered_offset != PROTO_SHM_FILTERED_OFFSET(slot_index)) {
            fprintf(stderr, "client: invalid shared-memory offsets\n"); goto cleanup;
        }
        base = (const unsigned char *)region.base;
        slot = (const proto_shm_slot_t *)(base + PROTO_SHM_SLOT_BASE(slot_index));
        bytes = (size_t)result.length * sizeof(float);
        if (slot->state != PROTO_SLOT_WRITTEN || slot->job_id != job_id ||
            slot->length != result.length ||
            result.sorted_offset > region.bytes || bytes > region.bytes - result.sorted_offset ||
            result.filtered_offset > region.bytes || bytes > region.bytes - result.filtered_offset) {
            fprintf(stderr, "client: shared-memory slot failed validation\n"); goto cleanup;
        }
        shared_sorted = (const float *)(const void *)(base + result.sorted_offset);
        shared_filtered = (const float *)(const void *)(base + result.filtered_offset);
        if (proto_checksum(shared_sorted, bytes) != result.sorted_checksum ||
            proto_checksum(shared_filtered, bytes) != result.filtered_checksum) {
            fprintf(stderr, "client: shared-memory checksum mismatch\n"); goto cleanup;
        }
        memcpy(sorted_all + total, shared_sorted, bytes);
        memcpy(filtered_all + total, shared_filtered, bytes);
        total += (size_t)n;
        printf("job=%u samples=%d block_median=%.7g outliers=%u elapsed=%llu ns\n",
               job_id, n, (double)result.median, result.outliers_removed,
               (unsigned long long)result.elapsed_ns);
        ++job_id;
    }

    if (total == 0) {
        fprintf(stderr, "client: recording is empty\n");
        goto cleanup;
    }

    /* Merge-combiner fallback: sort all received sorted chunks for the global median. */
    if (ms_sort_serial(sorted_all, total) != 0) {
        fprintf(stderr, "client: global sort failed\n"); goto cleanup;
    }
    {
        median_config_t cfg;
        uint32_t outliers = 0;
        float global_median = median_of_sorted(sorted_all, total);
        /* Refilter the original sequence as one signal so block seams have context. */
        median_config_default(&cfg);
        if (median_filter_signal(&cfg, raw, total, filtered_all, &outliers) != 0) {
            fprintf(stderr, "client: final rolling filter failed\n"); goto cleanup;
        }
        printf("recording complete: samples=%lu global_median=%.9g "
               "rolling_filter_width=%u global_outliers=%u\n",
               (unsigned long)total, (double)global_median, cfg.width, outliers);
    }
    status = EXIT_SUCCESS;

cleanup:
    if (loader) block_loader_close(loader);
    if (server_coid != -1) (void)name_close(server_coid);
    if (pulse_attach) name_detach(pulse_attach, 0);
    if (region_opened) shm_region_close(&region);
    free(raw); free(sorted_all); free(filtered_all);
    return status;
}
