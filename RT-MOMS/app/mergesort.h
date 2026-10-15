/*
 * mergesort.h
 *
 * Milestone 2: a parallel merge sort for the service's 64 KiB blocks, built on
 * POSIX worker threads with a mutex and condition variables holding the phase
 * boundaries.
 *
 * Shape of a sort of n values over k partitions:
 *
 *   phase 0     the workers sort one partition each, single-threaded
 *   phase 1     pairs of partitions are merged into the alternate buffer
 *   phase 2     pairs of those results are merged again
 *   ...         until a single run covers the whole array
 *
 * Every phase must finish before the next begins, because a merge reads two
 * runs that the previous phase produced. That dependency is precisely what the
 * mutex and the condition variables are for: the workers claim tasks from a
 * shared queue under the mutex, the worker that finishes the last task of a
 * phase signals on a condition variable, and the caller waits on that before
 * posting the next phase. Which merging phase runs is therefore decided by
 * worker completion, not by a sleep or a fixed delay.
 *
 * Properties worth knowing:
 *
 *   - In place. The values are sorted in the caller's buffer on return; the
 *     only extra memory is one n-float scratch buffer per sort.
 *   - Stable. Merges take from the left run on ties, so equal values keep their
 *     input order. Two pools with different thread counts produce bit-identical
 *     output, which test_mergesort relies on.
 *   - Deterministic. The partition scheme and the merge order are fixed by n and
 *     the thread count, so repeated sorts of the same input agree exactly.
 *   - No NaN. Values are compared with <=, which orders NaN nowhere useful. The
 *     payload contains none and test_mergesort asserts that.
 *   - One sort at a time per pool. ms_sort() refuses to run while another sort
 *     is in progress on the same pool.
 *
 * Small inputs are sorted by the plain single-threaded path, because the pool
 * costs more than it saves below a few thousand values.
 */

#ifndef MERGESORT_H
#define MERGESORT_H

#include <stddef.h>
#include <stdint.h>

#define MS_MAX_THREADS 64

typedef struct mergesort_pool mergesort_pool_t;

typedef struct {
    unsigned threads;    /* 0 means one per online CPU */
} mergesort_options_t;

/* Fills *opt with the defaults (threads = 0). NULL is allowed. */
void mergesort_options_default(mergesort_options_t *opt);

/*
 * Create a pool of worker threads. Returns NULL on failure, with a diagnostic
 * on stderr. `threads` is clamped to [1, MS_MAX_THREADS].
 */
mergesort_pool_t *ms_pool_create(const mergesort_options_t *opt);

/* Stop and join the workers, then release the pool. NULL is allowed. */
void ms_pool_destroy(mergesort_pool_t *pool);

/* Worker threads actually created. */
unsigned ms_pool_threads(const mergesort_pool_t *pool);

/*
 * Sort `n` float values ascending, in place, using the pool.
 *
 * Returns 0 on success, -1 if the pool or the buffer is NULL or a sort is
 * already running on this pool. n < 2 needs no work and succeeds.
 */
int ms_sort(mergesort_pool_t *pool, float *data, size_t n);

/*
 * The same sort with one thread, used for the partitions and for inputs below
 * the parallel threshold. Returns 0, or -1 if the scratch buffer cannot be
 * allocated.
 */
int ms_sort_serial(float *data, size_t n);

/* What the most recent sort on this pool actually did. */
typedef struct {
    unsigned threads;      /* worker threads in the pool        */
    unsigned partitions;   /* phase 0 partitions                */
    unsigned phases;       /* merge phases after phase 0        */
    uint64_t tasks;        /* partition and merge tasks run      */
} ms_stats_t;

/* Copy the counters. NULL pool or out leaves nothing written. */
void ms_stats(mergesort_pool_t *pool, ms_stats_t *out);

#endif /* MERGESORT_H */
