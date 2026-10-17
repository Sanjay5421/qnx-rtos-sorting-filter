/*
 * median.h
 *
 * Milestone 3: the rolling median filter, and the median of a sorted array.
 *
 * Two different medians live in this service and they must not be confused:
 *
 *   - The GLOBAL median is taken over the whole recording after the blocks
 *     have been sorted and merged (median_of_sorted below). It is a statistic
 *     of the RAW sensor data and says nothing about time order.
 *   - The ROLLING median is a time-domain filter. It slides a window along the
 *     signal in time order and removes outliers (median_filter_block below).
 *     Sorting a block destroys the order this filter needs, so the server
 *     filters one copy of a block and sorts another; neither feeds the other.
 *
 * The filter is a Hampel-style median filter. For every sample it looks at the
 * window of `width` samples centred on it, computes the window median and the
 * window's median absolute deviation (MAD), and replaces the sample with the
 * window median only if it lies further than `threshold` robust standard
 * deviations from it (sigma = 1.4826 * MAD). Samples that fit the local trend
 * are copied through untouched. That is deliberate: a plain median filter
 * replaces every sample and biases ramps and steps (figure 03 of the dataset
 * report); this one removes the injected spikes and leaves the signal alone.
 *
 * Properties worth knowing:
 *
 *   - Windows read the ORIGINAL samples, never already-filtered ones, so the
 *     result does not depend on the direction the filter runs in.
 *   - Block boundaries are invisible. A block is filtered together with up to
 *     (width - 1) / 2 samples of context from the block before it and the
 *     block after it, and the result is bit-identical to filtering the whole
 *     signal in one call. test_median asserts that.
 *   - At the two ends of the signal the window is clipped to the samples that
 *     exist (it is not padded), so the first and last samples are judged on a
 *     smaller window.
 *   - Deterministic and stateless: no allocation, no threads, nothing QNX
 *     specific, so the same source runs on the host, in the VM and on the Pi.
 *   - No NaN. Values are compared with < and >, which order NaN nowhere
 *     useful. The payload contains none.
 *
 * Limits: an outlier burst longer than (width - 1) / 2 samples dominates its
 * own window and survives. Widen the window to remove longer bursts.
 */

#ifndef MEDIAN_H
#define MEDIAN_H

#include <stddef.h>
#include <stdint.h>

#define MEDIAN_MIN_WIDTH      3u
#define MEDIAN_MAX_WIDTH      31u
#define MEDIAN_DEFAULT_WIDTH  9u    /* the sweet spot in figure 03             */
/* Robust sigmas. Chosen from a 100-seed sweep at width 9 over 10-40 sigma
 * spikes: threshold 4 misses 0.28 % of spikes and flags 0.9 % of clean samples,
 * threshold 5 misses 0.91 % and flags 0.4 %. A missed spike is the failure the
 * filter exists to prevent; a false alarm swaps a sample for its neighbourhood
 * median and barely moves the error, so err on the side of catching it. */
#define MEDIAN_DEFAULT_THRESHOLD 4.0f

/* Samples of context a block needs on each side. */
#define MEDIAN_HALF_WIDTH(width) (((width) - 1u) / 2u)

typedef struct {
    uint32_t width;      /* odd, MEDIAN_MIN_WIDTH..MEDIAN_MAX_WIDTH            */
    float    threshold;  /* > 0, in robust standard deviations                 */
} median_config_t;

/* Fill in the defaults (width 9, threshold 4). */
void median_config_default(median_config_t *cfg);

/* Is this configuration usable? Returns 1 if so, 0 if not. Silent. */
int median_config_valid(const median_config_t *cfg);

/*
 * Is this a usable window width: odd, MEDIAN_MIN_WIDTH..MEDIAN_MAX_WIDTH?
 * The one place that rule lives, so the filter, the message validation and the
 * command-line front ends cannot drift apart.
 */
int median_width_valid(uint32_t width);

/*
 * Parse a decimal width: digits only, no sign, no trailing characters, and the
 * value must pass median_width_valid(). Returns 0 and stores the width on
 * success, -1 on failure, leaving *width untouched.
 */
int median_width_parse(const char *text, uint32_t *width);

/*
 * Filter one block of `n` time-ordered samples.
 *
 *   left / nleft    the nleft samples that immediately PRECEDE cur[0]
 *                   (left[nleft - 1] is the sample just before cur[0]).
 *                   Pass NULL, 0 for the first block of a signal. At most
 *                   MEDIAN_HALF_WIDTH(width) of them are used.
 *   cur / n         the block to filter. Not modified.
 *   right / nright  the nright samples that immediately FOLLOW cur[n - 1]
 *                   (right[0] is the sample just after it). Pass NULL, 0 for
 *                   the last block of a signal.
 *   out             room for n floats. Must not overlap `cur`: the windows
 *                   read the original values while the result is written.
 *   outliers        if not NULL, receives the number of samples replaced.
 *
 * Supply a full half-window of context wherever neighbouring samples exist;
 * with less, the window at that edge is clipped as if the signal ended there.
 *
 * Returns 0 on success. On a bad argument it returns -1 and says why on
 * stderr, leaving `out` untouched.
 */
int median_filter_block(const median_config_t *cfg,
                        const float *left, size_t nleft,
                        const float *cur, size_t n,
                        const float *right, size_t nright,
                        float *out, uint32_t *outliers);

/* Filter a whole signal in one call: median_filter_block with no context. */
int median_filter_signal(const median_config_t *cfg,
                         const float *in, size_t n,
                         float *out, uint32_t *outliers);

/*
 * The median of an already sorted array: the middle value for odd n, the mean
 * of the two middle values for even n, 0 for n == 0. This is what the server's
 * combiner calls on the merged raw data to produce the global median.
 */
float median_of_sorted(const float *sorted, size_t n);

#endif /* MEDIAN_H */
