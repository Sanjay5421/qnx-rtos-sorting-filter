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

/*
 * QNX client for the M3 block service.
 *
 * Usage:
 *   sortmed-client [-v] [-w width] <file.bin> planar|interleaved
 *                  <time|current|voltage|temperature> <rate_hz>
 *
 *   -w width  the rolling-filter window for this run: odd, MEDIAN_MIN_WIDTH..
 *             MEDIAN_MAX_WIDTH, default MEDIAN_DEFAULT_WIDTH. The server
 *             filters each block with it and the client's own full-recording
 *             filter uses the same width, so `filtered.bin` is at that width.
 *   -v        one timing line per block, in addition to the summary
 *
 * Start sortmed-server first, or within the next ten seconds: the client
 * retries name_open() to a 10-second deadline (prerequisite 7) and gives up
 * with a message after that. It then uses the loader already covered by the
 * host tests, submits each full 64 KiB block with MsgSendv, waits for the
 * completion pulse, and fetches the small result and reads the arrays in shm.
 *
 * Output files (written to the current directory when the run completes):
 *   raw.bin       float32, same layout as the input file, one channel
 *   sorted.bin    float32, the whole recording sorted ascending
 *   filtered.bin  float32, the raw signal with outliers replaced (Hampel filter)
 *   medians.csv   text, one row per block: index, samples, median, outliers
 *
 * The timing lines at the end (and per block with -v) are what the M4 study
 * reads: `service` is the wait from the ack to the completion pulse -- the
 * server's own per-block work plus the delivery -- and it is compared against
 * the server's elapsed_ns for the same block, which excludes the IPC.
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
#include <time.h>

#define SERVER_NAME "sortmed"
#define CLIENT_NAME "sortmed-client"
#define MAX_RECORDING_SAMPLES 60000u
/* 60000 / 16384 = 4 blocks max for the current dataset; 64 leaves room. */
#define MAX_BLOCKS 64u

/*
 * Prerequisite 7: the client waits for the server and gives up after ten
 * seconds. The retry interval only decides how often name_open() is tried; the
 * deadline itself is measured from CLOCK_MONOTONIC.
 */
#define CONNECT_DEADLINE_NS (10ull * 1000000000ull)
#define CONNECT_RETRY_NS    (250ull * 1000000ull)

/*
 * How long one job may take before the client calls it lost: the completion
 * pulse, and a reply to the submit or the fetch. Generous -- the server turns a
 * 64 KiB block around in single-digit milliseconds -- so these only fire when
 * the server has died or a pulse has been lost, and then the run ends with a
 * message instead of hanging forever.
 */
#define PULSE_DEADLINE_NS   (5ull * 1000000000ull)
#define REPLY_DEADLINE_NS   (5ull * 1000000000ull)

/* ------------------------------------------------------------------------- */
/* Time, and waiting for the server                                           */
/* ------------------------------------------------------------------------- */

/* Monotonic nanoseconds, 0 if the clock cannot be read (reported as 0 rather
 * than as a plausible-looking number). */
static uint64_t now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Mean of a per-block total, for the summary line. */
static uint64_t mean_ns(uint64_t total_ns, unsigned count)
{
    return (count > 0) ? total_ns / count : 0;
}

/*
 * Prerequisite 7, in code: wait for the service to appear rather than failing
 * the moment the client is started. Retries name_open() until the deadline
 * expires, and reports how long it waited and how many attempts it took, so
 * the client-wait test can check the deadline was reached and not something
 * earlier. Returns the connection, or -1 with the last name_open errno.
 */
static int connect_to_server(const char *name, uint64_t deadline_ns,
                             uint64_t *waited_ns, int *attempts)
{
    uint64_t start = now_ns();
    int      tries = 0;

    for (;;) {
        struct timespec pause;
        uint64_t        elapsed;
        int             coid = name_open(name, 0);

        tries++;
        if (coid != -1) {
            if (waited_ns != NULL) *waited_ns = now_ns() - start;
            if (attempts != NULL)  *attempts  = tries;
            return coid;
        }
        elapsed = now_ns() - start;
        if (elapsed >= deadline_ns) {
            if (waited_ns != NULL) *waited_ns = elapsed;
            if (attempts != NULL)  *attempts  = tries;
            return -1;                  /* errno is the last name_open failure */
        }
        pause.tv_sec  = 0;
        pause.tv_nsec = (long)CONNECT_RETRY_NS;
        (void)nanosleep(&pause, NULL);  /* EINTR: the deadline check wins */
    }
}

/*
 * Arm the deadline for the reply to the next MsgSend*(). Best effort: if the
 * kernel refuses the call the message is still sent, because refusing to send
 * would turn a missing safety net into a dead service. Warned about once.
 */
static void arm_reply_deadline(const char *what)
{
    static int warned;
    uint64_t   timeout = REPLY_DEADLINE_NS;

    if (TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_REPLY, NULL, &timeout,
                     NULL) == 0)
        return;
    if (!warned) {
        warned = 1;
        fprintf(stderr, "sortmed-client: %s: cannot arm the %.1f s reply "
                        "deadline (%s); continuing without one\n",
                what, (double)REPLY_DEADLINE_NS / 1e9, strerror(errno));
    }
}

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
 * Wait for this job's completion pulse. The pulse carries two things: the job
 * id in its code (PROTO_PULSE_DONE + job_id) and the calculated median in its
 * value word, as the float's 32 bits. A pulse for some other job, which cannot
 * normally happen with one job outstanding, is ignored.
 *
 * The wait is bounded by an absolute deadline. With no server left to send the
 * pulse this used to block forever, and a client that hangs is worse than one
 * that says what it is waiting for and exits.
 */
static int wait_for_done(name_attach_t *attach, uint32_t expected_job,
                         float *median_out, uint64_t deadline_ns)
{
    uint64_t start = now_ns();

    for (;;) {
        struct _pulse pulse;
        uint64_t      elapsed = now_ns() - start;
        uint64_t      remaining;
        int           rcvid;

        if (elapsed >= deadline_ns) {
            fprintf(stderr, "sortmed-client: no completion pulse for job %u "
                            "within %.1f s; giving up\n",
                    expected_job, (double)deadline_ns / 1e9);
            return -1;
        }
        remaining = deadline_ns - elapsed;

        if (TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE, NULL,
                         &remaining, NULL) != 0) {
            static int warned;

            if (!warned) {
                warned = 1;
                fprintf(stderr, "sortmed-client: cannot arm the %.1f s pulse "
                                "deadline (%s); continuing without one\n",
                        (double)deadline_ns / 1e9, strerror(errno));
            }
        }

        rcvid = MsgReceive(attach->chid, &pulse, sizeof(pulse), NULL);
        if (rcvid == -1) {
            if (errno == EINTR) continue;
            if (errno == ETIMEDOUT) {
                fprintf(stderr, "sortmed-client: no completion pulse for job %u "
                                "within %.1f s; giving up\n",
                        expected_job, (double)deadline_ns / 1e9);
                return -1;
            }
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

/*
 * How to run it. It sits next to main() because argv is parsed there, so the
 * text and the parsing cannot drift apart.
 */
static void usage(FILE *out, const char *program)
{
    fprintf(out, "usage: %s [-v] [-w width] <file.bin> planar|interleaved "
                 "<time|current|voltage|temperature> <rate_hz>\n"
                 "  -w width  rolling-filter window, odd %u..%u, default %u:\n"
                 "            the server filters this run's blocks with it and\n"
                 "            the client's full-recording filter uses it too\n"
                 "  -v        a timing line per block as well as the summary\n",
            program, (unsigned)MEDIAN_MIN_WIDTH, (unsigned)MEDIAN_MAX_WIDTH,
            (unsigned)MEDIAN_DEFAULT_WIDTH);
}

int main(int argc, char **argv)
{
    int layout;
    proto_channel_t channel;
    uint32_t rate;
    uint32_t width = MEDIAN_DEFAULT_WIDTH;   /* this run's filter width      */
    int verbose = 0;                         /* per-block timing lines       */
    int first = 1;                           /* argv index of <file.bin>     */
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

    /* Timing, all in nanoseconds and all filled in as the run proceeds:
     * the four per-block sums are the client's view of the exchange, and the
     * post-processing totals are what the client spends after the last block. */
    uint64_t sum_submit_ns = 0;   /* submit sent -> ack in hand                */
    uint64_t sum_service_ns = 0;  /* ack -> completion pulse                   */
    uint64_t sum_fetch_ns = 0;    /* fetch sent -> result in hand              */
    uint64_t sum_block_ns = 0;    /* submit -> result: the whole per-block path */
    uint64_t sum_server_ns = 0;   /* the server's own elapsed_ns, summed       */
    uint64_t run_start_ns = 0;
    uint64_t run_ns = 0;
    uint64_t post_sort_ns = 0;
    uint64_t post_filter_ns = 0;
    uint64_t write_ns = 0;
    uint64_t t0;

    region.fd = -1;
    region.base = (void *)-1;
    region.bytes = 0;
    region.owner = 0;

    /* Options first, then the four positional arguments. */
    while (first < argc && argv[first][0] == '-' && argv[first][1] != '\0') {
        if (strcmp(argv[first], "-v") == 0) {
            verbose = 1;
            first++;
        } else if (strcmp(argv[first], "-w") == 0 && first + 1 < argc) {
            if (median_width_parse(argv[first + 1], &width) != 0) {
                fprintf(stderr, "sortmed-client: -w wants an odd width in "
                                "%u..%u, not '%s'\n",
                        (unsigned)MEDIAN_MIN_WIDTH, (unsigned)MEDIAN_MAX_WIDTH,
                        argv[first + 1]);
                return EXIT_FAILURE;
            }
            first += 2;
        } else if (strcmp(argv[first], "-h") == 0) {
            usage(stdout, argv[0]);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "sortmed-client: unknown option '%s'\n",
                    argv[first]);
            usage(stderr, argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (argc - first != 4) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[first + 1], "planar") == 0) layout = SENSOR_LAYOUT_PLANAR;
    else if (strcmp(argv[first + 1], "interleaved") == 0) layout = SENSOR_LAYOUT_INTERLEAVED;
    else {
        fprintf(stderr, "client: layout must be planar or interleaved\n");
        return EXIT_FAILURE;
    }
    if (parse_channel(argv[first + 2], &channel) != 0) {
        fprintf(stderr, "client: unknown channel '%s'\n", argv[first + 2]);
        return EXIT_FAILURE;
    }
    errno = 0;
    parsed_rate = strtoul(argv[first + 3], &end, 10);
    if (errno || *argv[first + 3] == '\0' || *end != '\0' ||
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

    /*
     * Prerequisite 7: wait for the server instead of failing the moment the
     * client starts, and give up after CONNECT_DEADLINE_NS. Start the server
     * while the client is waiting and the run simply begins when it appears.
     */
    {
        uint64_t waited = 0;
        int attempts = 0;

        server_coid = connect_to_server(SERVER_NAME, CONNECT_DEADLINE_NS,
                                        &waited, &attempts);
        if (server_coid == -1) {
            fprintf(stderr, "sortmed-client: no server named '%s' after "
                            "%.1f s (%d attempts): %s\n",
                    SERVER_NAME, (double)waited / 1e9, attempts,
                    strerror(errno));
            goto cleanup;
        }
        if (attempts > 1)
            printf("connected: server '%s' after %.3f s (%d attempts)\n",
                   SERVER_NAME, (double)waited / 1e9, attempts);
    }

    if (shm_region_open_readonly(&region) != 0) goto cleanup;
    region_opened = 1;

    loader = block_loader_open(argv[first], layout, channel, rate);
    if (!loader) goto cleanup;
    recording_total = block_loader_total_samples(loader);

    run_start_ns = now_ns();
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
        uint64_t t_submit, t_ack, t_pulse, t_fetch;

        memset(&block, 0, sizeof(block));
        n = block_loader_next(loader, &block, &desc);
        if (n < 0) { fprintf(stderr, "client: recording read failed\n"); goto cleanup; }
        if (n == 0) break;
        if (total + (size_t)n > MAX_RECORDING_SAMPLES) {
            fprintf(stderr, "client: recording exceeds %u samples\n", MAX_RECORDING_SAMPLES);
            goto cleanup;
        }
        memcpy(raw + total, block.samples, (size_t)n * sizeof(float));

        proto_submit_prepare(&submit, job_id, &desc, width);
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
        arm_reply_deadline("submit");
        t_submit = now_ns();
        if (MsgSendv(server_coid, send_iov, 2, &reply_iov, 1) == -1) {
            if (errno == ETIMEDOUT)
                fprintf(stderr, "client: submit %u: no reply within %.1f s\n",
                        job_id, (double)REPLY_DEADLINE_NS / 1e9);
            else
                perror("client: MsgSendv submit");
            goto cleanup;
        }
        if (ack.type != PROTO_MSG_ACK || ack.job_id != job_id ||
            ack.status != PROTO_OK) {
            fprintf(stderr, "client: submit %u rejected: %s\n",
                    job_id, proto_status_str(ack.status));
            goto cleanup;
        }
        t_ack = now_ns();
        pulse_median = 0.0f;
        if (wait_for_done(pulse_attach, job_id, &pulse_median,
                          PULSE_DEADLINE_NS) != 0)
            goto cleanup;
        t_pulse = now_ns();
        /* One line per completion: code, job and the median the pulse carried. */
        printf("pulse: job=%u code=%d median=%.7g%s\n",
               job_id, PROTO_PULSE_DONE + (int)job_id, (double)pulse_median,
               (submit.flags & PROTO_SUBMIT_LAST) ? "  (global)" : "");

        memset(&fetch, 0, sizeof(fetch));
        fetch.type = PROTO_MSG_FETCH;
        fetch.job_id = job_id;
        memset(&result, 0, sizeof(result));
        arm_reply_deadline("fetch");
        if (MsgSend(server_coid, &fetch, sizeof(fetch),
                    &result, sizeof(result)) == -1) {
            if (errno == ETIMEDOUT)
                fprintf(stderr, "client: fetch %u: no reply within %.1f s\n",
                        job_id, (double)REPLY_DEADLINE_NS / 1e9);
            else
                perror("client: MsgSend fetch");
            goto cleanup;
        }
        t_fetch = now_ns();
        if (result.type != PROTO_MSG_RESULT || result.status != PROTO_OK ||
            result.job_id != job_id || result.length != (uint32_t)n) {
            fprintf(stderr, "client: invalid result for job %u (status=%s)\n",
                    job_id, proto_status_str(result.status));
            goto cleanup;
        }
        /*
         * The width echo. The server must have filtered with the width this run
         * asked for, because the client's own full-recording filter below runs
         * at that width: a mismatch would mean the service's per-block arrays
         * and the client's final filtered.bin were two different filters.
         */
        if (result.filter_width != width) {
            fprintf(stderr, "client: server used filter width %u, not %u\n",
                    result.filter_width, width);
            goto cleanup;
        }
        sum_submit_ns  += t_ack - t_submit;
        sum_service_ns += t_pulse - t_ack;
        sum_fetch_ns   += t_fetch - t_pulse;
        sum_block_ns   += t_fetch - t_submit;
        sum_server_ns  += result.elapsed_ns;
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
        if (verbose) {
            printf("timing: job=%u submit=%llu service=%llu fetch=%llu "
                   "total=%llu (ns)\n",
                   job_id,
                   (unsigned long long)(t_ack - t_submit),
                   (unsigned long long)(t_pulse - t_ack),
                   (unsigned long long)(t_fetch - t_pulse),
                   (unsigned long long)(t_fetch - t_submit));
        }
        ++job_id;
    }

    run_ns = now_ns() - run_start_ns;

    if (total == 0) {
        fprintf(stderr, "client: recording is empty\n");
        goto cleanup;
    }

    /* Global sort of the whole recording (needed for global_median and
     * sorted.bin). The per-block sorted chunks are irrelevant here — this
     * re-sorts the concatenation of them, which is equivalent to sorting raw
     * from scratch. */
    t0 = now_ns();
    if (ms_sort_serial(sorted_all, total) != 0) {
        fprintf(stderr, "client: global sort failed\n"); goto cleanup;
    }
    post_sort_ns = now_ns() - t0;

    /* Global rolling filter over the raw signal, so block seams have context.
     * This overwrites filtered_all with the seamless version. */
    {
        median_config_t cfg;
        uint32_t outliers = 0;
        float global_median = median_of_sorted(sorted_all, total);

        median_config_default(&cfg);
        cfg.width = width;         /* the same window the server was asked for */
        t0 = now_ns();
        if (median_filter_signal(&cfg, raw, total, filtered_all, &outliers) != 0) {
            fprintf(stderr, "client: final rolling filter failed\n"); goto cleanup;
        }
        post_filter_ns = now_ns() - t0;
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
    t0 = now_ns();
    if (write_floats("raw.bin",      raw,          total) != 0) goto cleanup;
    if (write_floats("sorted.bin",   sorted_all,   total) != 0) goto cleanup;
    if (write_floats("filtered.bin", filtered_all, total) != 0) goto cleanup;
    if (write_medians_csv("medians.csv",
                          block_medians, block_samples, block_outliers,
                          block_count) != 0) goto cleanup;
    write_ns = now_ns() - t0;

    printf("output written: raw.bin (%lu bytes), sorted.bin (%lu bytes), "
           "filtered.bin (%lu bytes), medians.csv (%u blocks)\n",
           (unsigned long)(total * sizeof(float)),
           (unsigned long)(total * sizeof(float)),
           (unsigned long)(total * sizeof(float)),
           block_count);

    /*
     * Timing. `run_ns` is the offload of the whole recording: block 1 leaving
     * the client to the last result arriving, so it covers the IPC and not the
     * client's own post-processing below it. The means are per block, and
     * `server_avg_ns` is the server's own elapsed_ns over the same blocks, so
     * `service_avg_ns - server_avg_ns` is what the pulse and the scheduling
     * around it cost. tools/filter_sweep.py reads these lines.
     */
    printf("timing: run_ns=%llu blocks=%u submit_avg_ns=%llu service_avg_ns=%llu "
           "fetch_avg_ns=%llu block_avg_ns=%llu server_avg_ns=%llu\n",
           (unsigned long long)run_ns, block_count,
           (unsigned long long)mean_ns(sum_submit_ns, block_count),
           (unsigned long long)mean_ns(sum_service_ns, block_count),
           (unsigned long long)mean_ns(sum_fetch_ns, block_count),
           (unsigned long long)mean_ns(sum_block_ns, block_count),
           (unsigned long long)mean_ns(sum_server_ns, block_count));
    printf("timing: post_sort_ns=%llu post_filter_ns=%llu write_ns=%llu\n",
           (unsigned long long)post_sort_ns,
           (unsigned long long)post_filter_ns,
           (unsigned long long)write_ns);

    status = EXIT_SUCCESS;

cleanup:
    if (loader) block_loader_close(loader);
    if (server_coid != -1) (void)name_close(server_coid);
    if (pulse_attach) name_detach(pulse_attach, 0);
    if (region_opened) shm_region_close(&region);
    free(raw); free(sorted_all); free(filtered_all);
    return status;
}
