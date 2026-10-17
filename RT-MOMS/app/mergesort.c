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
 * mergesort.c
 *
 * Parallel merge sort; the contract is in mergesort.h.
 *
 * The workers exist to run tasks, not to be scheduled one per phase: the caller
 * that invokes ms_sort() drives the phases and does no merging itself, and the
 * workers pull tasks from a shared queue until the phase is drained. That is
 * what makes the phase boundary a barrier on worker completion rather than a
 * sleep: the caller blocks on phase_done until tasks_done reaches ntasks, and
 * only then posts the merges that depend on those runs.
 */

#include "mergesort.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

/* Below this many values the pool costs more than it saves, so the run is
 * sorted single-threaded. */
#define MS_PARALLEL_MIN 4096

/* Smallest slice worth a worker of its own; this is what sets the partition
 * count for a given n. */
#define MS_PARTITION_MIN 1024

enum {
    MS_TASK_PARTITION = 0,   /* sort [lo, hi) in place; scratch is dst+lo      */
    MS_TASK_MERGE     = 1    /* merge [lo, mid) and [mid, hi) into dst         */
};

typedef struct {
    int    kind;
    size_t lo;
    size_t mid;
    size_t hi;
} ms_task_t;

struct mergesort_pool {
    unsigned   nthreads;
    pthread_t *threads;

    pthread_mutex_t lock;          /* guards everything below              */
    pthread_cond_t  work_ready;    /* workers wait here for tasks          */
    pthread_cond_t  phase_done;    /* the caller waits here for completion */

    ms_task_t *tasks;              /* the current phase's tasks            */
    unsigned   ntasks;
    unsigned   next_task;
    unsigned   tasks_done;

    float *src;                    /* a phase reads src and writes dst     */
    float *dst;
    int    running;
    int    quit;

    ms_stats_t stats;
};

static void *worker_main(void *arg);

/* ------------------------------------------------------------------------- */
/* Merging                                                                    */
/* ------------------------------------------------------------------------- */

/* Stable: on a tie the value from the left run goes first, so equal values keep
 * the order they arrived in. */
static void merge_runs(const float *a, size_t na,
                       const float *b, size_t nb, float *out)
{
    size_t i = 0;
    size_t j = 0;
    size_t k = 0;

    while (i < na && j < nb)
        out[k++] = (a[i] <= b[j]) ? a[i++] : b[j++];
    while (i < na)
        out[k++] = a[i++];
    while (j < nb)
        out[k++] = b[j++];
}

/* Recursive merge sort against an n-float scratch buffer. The two halves use
 * disjoint halves of the scratch, so recursion needs no allocation. */
static void sort_serial_rec(float *a, float *scratch, size_t n)
{
    size_t mid;

    if (n < 2)
        return;

    mid = n / 2;
    sort_serial_rec(a, scratch, mid);
    sort_serial_rec(a + mid, scratch + mid, n - mid);
    merge_runs(a, mid, a + mid, n - mid, scratch);
    memcpy(a, scratch, n * sizeof(float));
}

/* ------------------------------------------------------------------------- */
/* Pool plumbing                                                              */
/* ------------------------------------------------------------------------- */

static unsigned online_cpus(void)
{
#if defined(_WIN32)
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return (si.dwNumberOfProcessors > 0) ? (unsigned)si.dwNumberOfProcessors : 1u;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);

    return (n > 0) ? (unsigned)n : 1u;
#endif
}

void mergesort_options_default(mergesort_options_t *opt)
{
    if (opt != NULL)
        opt->threads = 0;
}

/*
 * Hand the workers a phase's tasks and wait for every one of them to finish.
 *
 * No lock on entry and none on exit. Waiting here is what orders the phases:
 * the merges posted next read runs that the tasks just completed produced.
 */
static void post_phase(mergesort_pool_t *p, unsigned ntasks)
{
    pthread_mutex_lock(&p->lock);
    p->ntasks     = ntasks;
    p->next_task  = 0;
    p->tasks_done = 0;
    pthread_cond_broadcast(&p->work_ready);
    while (p->tasks_done < p->ntasks)
        pthread_cond_wait(&p->phase_done, &p->lock);
    pthread_mutex_unlock(&p->lock);
}

mergesort_pool_t *ms_pool_create(const mergesort_options_t *opt)
{
    mergesort_pool_t *p;
    unsigned          threads;
    unsigned          created = 0;
    int               i;

    threads = (opt != NULL && opt->threads > 0) ? opt->threads : online_cpus();
    if (threads < 1)
        threads = 1;
    if (threads > MS_MAX_THREADS)
        threads = MS_MAX_THREADS;

    p = calloc(1, sizeof(*p));
    if (p == NULL) {
        fprintf(stderr, "mergesort: out of memory for the pool\n");
        return NULL;
    }

    p->threads = calloc(threads, sizeof(pthread_t));
    p->tasks   = calloc(threads, sizeof(ms_task_t));
    if (p->threads == NULL || p->tasks == NULL) {
        fprintf(stderr, "mergesort: out of memory for %u workers\n", threads);
        free(p->tasks);
        free(p->threads);
        free(p);
        return NULL;
    }

    if (pthread_mutex_init(&p->lock, NULL) != 0 ||
        pthread_cond_init(&p->work_ready, NULL) != 0 ||
        pthread_cond_init(&p->phase_done, NULL) != 0) {
        fprintf(stderr, "mergesort: could not initialise the synchronisation objects\n");
        free(p->tasks);
        free(p->threads);
        free(p);
        return NULL;
    }

    for (i = 0; i < (int)threads; i++) {
        if (pthread_create(&p->threads[i], NULL, worker_main, p) != 0) {
            fprintf(stderr, "mergesort: worker %d of %u would not start\n",
                    i + 1, threads);
            break;
        }
        created++;
    }

    if (created == 0) {
        fprintf(stderr, "mergesort: no worker threads could be created\n");
        pthread_cond_destroy(&p->phase_done);
        pthread_cond_destroy(&p->work_ready);
        pthread_mutex_destroy(&p->lock);
        free(p->tasks);
        free(p->threads);
        free(p);
        return NULL;
    }
    if (created < threads)
        fprintf(stderr, "mergesort: running with %u of %u workers\n", created, threads);
    p->nthreads = created;
    return p;
}

void ms_pool_destroy(mergesort_pool_t *p)
{
    unsigned i;

    if (p == NULL)
        return;

    pthread_mutex_lock(&p->lock);
    p->quit = 1;
    pthread_cond_broadcast(&p->work_ready);
    pthread_mutex_unlock(&p->lock);

    for (i = 0; i < p->nthreads; i++)
        pthread_join(p->threads[i], NULL);

    pthread_cond_destroy(&p->phase_done);
    pthread_cond_destroy(&p->work_ready);
    pthread_mutex_destroy(&p->lock);
    free(p->tasks);
    free(p->threads);
    free(p);
}

unsigned ms_pool_threads(const mergesort_pool_t *p)
{
    return (p != NULL) ? p->nthreads : 0;
}

void ms_stats(mergesort_pool_t *p, ms_stats_t *out)
{
    if (p == NULL || out == NULL)
        return;

    pthread_mutex_lock(&p->lock);
    *out = p->stats;
    pthread_mutex_unlock(&p->lock);
}

/* ------------------------------------------------------------------------- */
/* The workers                                                                */
/* ------------------------------------------------------------------------- */

static void *worker_main(void *arg)
{
    mergesort_pool_t *p = (mergesort_pool_t *)arg;

    for (;;) {
        ms_task_t task;

        pthread_mutex_lock(&p->lock);
        while (!p->quit && p->next_task >= p->ntasks)
            pthread_cond_wait(&p->work_ready, &p->lock);
        if (p->quit) {
            pthread_mutex_unlock(&p->lock);
            break;
        }
        task = p->tasks[p->next_task++];
        pthread_mutex_unlock(&p->lock);

        /* The buffers and the task array are stable while a phase runs: the
         * caller changes them only between phases, which is after every task
         * has been counted as done. */
        if (task.kind == MS_TASK_PARTITION) {
            sort_serial_rec(p->src + task.lo, p->dst + task.lo, task.hi - task.lo);
        } else {
            merge_runs(p->src + task.lo, task.mid - task.lo,
                       p->src + task.mid, task.hi - task.mid,
                       p->dst + task.lo);
        }

        pthread_mutex_lock(&p->lock);
        p->tasks_done++;
        p->stats.tasks++;
        if (p->tasks_done == p->ntasks)
            pthread_cond_signal(&p->phase_done);
        pthread_mutex_unlock(&p->lock);
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Sorting                                                                    */
/* ------------------------------------------------------------------------- */

int ms_sort_serial(float *data, size_t n)
{
    float *scratch;

    if (data == NULL)
        return -1;
    if (n < 2)
        return 0;

    scratch = malloc(n * sizeof(float));
    if (scratch == NULL) {
        fprintf(stderr, "mergesort: out of memory for %lu samples\n",
                (unsigned long)n);
        return -1;
    }
    sort_serial_rec(data, scratch, n);
    free(scratch);
    return 0;
}

int ms_sort(mergesort_pool_t *p, float *data, size_t n)
{
    float   *scratch;
    float   *read_buf;
    float   *write_buf;
    size_t   part;
    unsigned k;
    unsigned runs;
    unsigned phases = 0;
    unsigned i;
    int      rc = 0;

    if (p == NULL || data == NULL)
        return -1;
    if (n < 2)
        return 0;

    pthread_mutex_lock(&p->lock);
    if (p->running) {
        pthread_mutex_unlock(&p->lock);
        return -1;
    }
    p->running = 1;
    memset(&p->stats, 0, sizeof(p->stats));
    p->stats.threads = p->nthreads;
    pthread_mutex_unlock(&p->lock);

    if (n < MS_PARALLEL_MIN) {
        rc = ms_sort_serial(data, n);
        pthread_mutex_lock(&p->lock);
        p->stats.partitions = 1;
        p->running = 0;
        pthread_mutex_unlock(&p->lock);
        return rc;
    }

    scratch = malloc(n * sizeof(float));
    if (scratch == NULL) {
        fprintf(stderr, "mergesort: out of memory for %lu samples\n",
                (unsigned long)n);
        pthread_mutex_lock(&p->lock);
        p->running = 0;
        pthread_mutex_unlock(&p->lock);
        return -1;
    }

    /* Phase 0: one partition per worker, each sorted single-threaded against
     * its own slice of the scratch buffer. */
    k = (unsigned)(n / MS_PARTITION_MIN);
    if (k > p->nthreads)
        k = p->nthreads;
    if (k < 1)
        k = 1;
    part = (n + k - 1) / k;

    pthread_mutex_lock(&p->lock);
    p->src = data;
    p->dst = scratch;
    p->stats.partitions = k;
    pthread_mutex_unlock(&p->lock);

    for (i = 0; i < k; i++) {
        p->tasks[i].kind = MS_TASK_PARTITION;
        p->tasks[i].lo   = (size_t)i * part;
        p->tasks[i].mid  = p->tasks[i].lo;
        p->tasks[i].hi   = p->tasks[i].lo + part;
        if (p->tasks[i].hi > n)
            p->tasks[i].hi = n;
    }
    post_phase(p, k);

    /* Merge phases: halve the number of runs until one covers the array. Each
     * phase reads the buffer the previous phase wrote and fills the other. */
    read_buf  = data;
    write_buf = scratch;
    runs      = k;

    while (runs > 1) {
        size_t   run_size = part << phases;
        unsigned nt = 0;
        unsigned r;

        for (r = 0; r + 1 < runs; r += 2) {
            size_t lo  = (size_t)r * run_size;
            size_t mid = lo + run_size;
            size_t hi  = mid + run_size;

            if (mid > n)
                mid = n;
            if (hi > n)
                hi = n;
            if (mid >= hi)
                break;                     /* the second run is empty */

            p->tasks[nt].kind = MS_TASK_MERGE;
            p->tasks[nt].lo   = lo;
            p->tasks[nt].mid  = mid;
            p->tasks[nt].hi   = hi;
            nt++;
        }

        if (nt > 0) {
            float *swap;

            pthread_mutex_lock(&p->lock);
            p->src = read_buf;
            p->dst = write_buf;
            pthread_mutex_unlock(&p->lock);

            post_phase(p, nt);

            swap      = read_buf;
            read_buf  = write_buf;
            write_buf = swap;
        }

        runs = (runs + 1) / 2;
        phases++;
    }

    /* An odd number of merge phases leaves the answer in the scratch buffer. */
    if (read_buf != data)
        memcpy(data, read_buf, n * sizeof(float));

    free(scratch);

    pthread_mutex_lock(&p->lock);
    p->stats.phases = phases;
    p->running      = 0;
    pthread_mutex_unlock(&p->lock);
    return 0;
}
