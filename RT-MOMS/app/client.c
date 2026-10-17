/*
 * QNX client for the M3 block service.
 *
 * Usage:
 *   sortmed-client <file.bin> planar|interleaved <time|current|voltage|temperature> <rate_hz>
 *
 * Start sortmed-server first. The client uses the loader already covered by
 * the host tests, submits each full 64 KiB block with MsgSendv, waits for the
 * completion pulse, then fetches the small result and reads the arrays in shm.
 *
 * Output files (written to the current directory when the run completes):
 *   raw.bin       float32, same layout as the input file, one channel
 *   sorted.bin    float32, the whole recording sorted ascending
 *   filtered.bin  float32, the raw signal with outliers replaced (Hampel filter)
 *   medians.csv   text, one row per block: index, samples, median, outliers
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
/* 60000 / 16384 = 4 blocks max for the current dataset; 64 leaves room. */
#define MAX_BLOCKS 64u

/* ------------------------------------------------------------------------- */
/* Output helpers                                                             */
/* ------------------------------------------------------------------------- */

/* Write `n` float32 samples to `path` in native order (little-endian on both
 * the host and the aarch64le target), matching the input file's layout. */
static int write_floats(const char *path, const float *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return -1;
    }
    if (n > 0 && fwrite(data, sizeof(float), n, f) != n) {
        fprintf(stderr, "client: %s: short write (%zu samples)\n", path, n);
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        perror(path);
        return -1;
    }
    return 0;
}

/* Per-block statistics as CSV with a header row. */
static int write_medians_csv(const char *path,
                             const float    *medians,
                             const uint32_t *samples,
                             const uint32_t *outliers,
                             unsigned        count)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return -1;
    }
    fprintf(f, "block_index,samples,median,outliers_removed\n");
    for (unsigned i = 0; i < count; i++) {
        fprintf(f, "%u,%u,%.9g,%u\n",
                i, samples[i], (double)medians[i], outliers[i]);
    }
    if (fclose(f) != 0) {
        perror(path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Protocol helpers                                                           */
/* ------------------------------------------------------------------------- */

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

/*
 * Wait for this job's completion pulse. The pulse carries two things now: the
 * job id in its code (PROTO_PULSE_DONE + job_id) and the calculated median in
 * its value word, as the float's 32 bits. A pulse for some other job, which
 * cannot normally happen with one job outstanding, is ignored.
 */
static int wait_for_done(name_attach_t *attach, uint32_t expected_job,
                         float *median_out)
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
        if (pulse.code == PROTO_PULSE_DONE + (int)expected_job) {
            if (median_out != NULL)
                *median_out = proto_pulse_to_float(pulse.value.sival_int);
            return 0;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Main                                                                       */
/* ------------------------------------------------------------------------- */

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
    size_t recording_total = 0;
    float server_global_median = 0.0f;
    int have_server_median = 0;
    uint32_t job_id = 1;
    int status = EXIT_FAILURE;

    /* Per-block statistics collected during the loop, written at the end. */
    float    block_medians[MAX_BLOCKS];
    uint32_t block_samples[MAX_BLOCKS];
    uint32_t block_outliers[MAX_BLOCKS];
    unsigned block_count = 0;

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
    recording_total = block_loader_total_samples(loader);

    for (;;) {
        proto_block_t block;
        proto_block_desc_t desc;
        proto_submit_t submit;
        proto_ack_t ack;
        iov_t send_iov[2], reply_iov;
        int n;
        proto_fetch_t fetch;
        proto_result_t result;
        float pulse_median;
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
        /*
         * Tell the server when the recording ends: that block's completion is
         * what gives the whole recording a global median. The loader knows the
         * recording's length, so the client can flag it here instead of making
         * the server infer it from the data.
         */
        submit.flags = (total + (size_t)n == recording_total)
                       ? PROTO_SUBMIT_LAST : 0u;
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
        pulse_median = 0.0f;
        if (wait_for_done(pulse_attach, job_id, &pulse_median) != 0) goto cleanup;
        /* One line per completion: code, job and the median the pulse carried. */
        printf("pulse: job=%u code=%d median=%.7g%s\n",
               job_id, PROTO_PULSE_DONE + (int)job_id, (double)pulse_median,
               (submit.flags & PROTO_SUBMIT_LAST) ? "  (global)" : "");

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
        /*
         * On the recording's last block the server has published the run: the
         * global median -- also delivered in the pulse -- and the memory-offset
         * descriptor of the whole recording, merged into one sorted array.
         * Check that the descriptor is complete, that its array matches its own
         * checksum, and that the two copies of the global median (pulse value
         * and descriptor) are the same number.
         */
        if ((submit.flags & PROTO_SUBMIT_LAST) != 0) {
            const proto_run_t *run = (const proto_run_t *)(const void *)
                (base + PROTO_RUN_BASE);
            const float *global_sorted = (const float *)(const void *)
                (base + PROTO_RUN_SORTED_OFFSET);
            size_t global_bytes = (size_t)run->length * sizeof(float);

            if (run->magic != PROTO_RUN_MAGIC ||
                run->version != PROTO_RUN_VERSION ||
                run->jobs != job_id ||
                run->total_samples != (uint32_t)recording_total ||
                run->length != (uint32_t)recording_total ||
                run->sorted_offset != PROTO_RUN_SORTED_OFFSET ||
                global_bytes > region.bytes - PROTO_RUN_SORTED_OFFSET) {
                fprintf(stderr, "client: run descriptor failed validation\n");
                goto cleanup;
            }
            if (proto_checksum(global_sorted, global_bytes) != run->sorted_checksum) {
                fprintf(stderr, "client: globally sorted array checksum mismatch\n");
                goto cleanup;
            }
            if (proto_float_to_pulse(run->global_median) !=
                proto_float_to_pulse(pulse_median)) {
                fprintf(stderr, "client: pulse median and run descriptor disagree\n");
                goto cleanup;
            }
            server_global_median = run->global_median;
            have_server_median = 1;
        }
        memcpy(sorted_all + total, shared_sorted, bytes);
        memcpy(filtered_all + total, shared_filtered, bytes);

        /* Save this block's statistics for medians.csv. */
        if (block_count < MAX_BLOCKS) {
            block_medians[block_count]  = result.median;
            block_samples[block_count]  = (uint32_t)n;
            block_outliers[block_count] = result.outliers_removed;
            block_count++;
        }

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

    /* Global sort of the whole recording (needed for global_median and
     * sorted.bin). The per-block sorted chunks are irrelevant here — this
     * re-sorts the concatenation of them, which is equivalent to sorting raw
     * from scratch. */
    if (ms_sort_serial(sorted_all, total) != 0) {
        fprintf(stderr, "client: global sort failed\n"); goto cleanup;
    }

    /* Global rolling filter over the raw signal, so block seams have context.
     * This overwrites filtered_all with the seamless version. */
    {
        median_config_t cfg;
        uint32_t outliers = 0;
        float global_median = median_of_sorted(sorted_all, total);
        median_config_default(&cfg);
        if (median_filter_signal(&cfg, raw, total, filtered_all, &outliers) != 0) {
            fprintf(stderr, "client: final rolling filter failed\n"); goto cleanup;
        }
        printf("recording complete: samples=%lu global_median=%.9g "
               "rolling_filter_width=%u global_outliers=%u\n",
               (unsigned long)total, (double)global_median, cfg.width, outliers);
        /*
         * Cross-check. The server merged its four blocks into one sorted array
         * and took the median of that; the client sorted the concatenation of
         * the same blocks itself. Same values, same median_of_sorted, so the two
         * must agree bit for bit -- testing bit patterns rather than ==, because
         * +0.0 and -0.0 compare equal but are different answers.
         */
        if (have_server_median) {
            if (proto_float_to_pulse(server_global_median) !=
                proto_float_to_pulse(global_median)) {
                fprintf(stderr, "client: server and client global medians differ: "
                                "%.9g vs %.9g\n",
                        (double)server_global_median, (double)global_median);
                goto cleanup;
            }
            printf("cross-check: server pulse/descriptor median %.9g == client %.9g\n",
                   (double)server_global_median, (double)global_median);
        }
    }

    /* Persist the outputs. Each of the three .bin files has the same shape as
     * the input file — float32, one value per sample, in sample order. */
    if (write_floats("raw.bin",      raw,          total) != 0) goto cleanup;
    if (write_floats("sorted.bin",   sorted_all,   total) != 0) goto cleanup;
    if (write_floats("filtered.bin", filtered_all, total) != 0) goto cleanup;
    if (write_medians_csv("medians.csv",
                          block_medians, block_samples, block_outliers,
                          block_count) != 0) goto cleanup;

    printf("output written: raw.bin (%lu bytes), sorted.bin (%lu bytes), "
           "filtered.bin (%lu bytes), medians.csv (%u blocks)\n",
           (unsigned long)(total * sizeof(float)),
           (unsigned long)(total * sizeof(float)),
           (unsigned long)(total * sizeof(float)),
           block_count);

    status = EXIT_SUCCESS;

cleanup:
    if (loader) block_loader_close(loader);
    if (server_coid != -1) (void)name_close(server_coid);
    if (pulse_attach) name_detach(pulse_attach, 0);
    if (region_opened) shm_region_close(&region);
    free(raw); free(sorted_all); free(filtered_all);
    return status;
}
