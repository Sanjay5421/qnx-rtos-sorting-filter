/*
 * median.c
 *
 * The rolling median (Hampel) filter and the median of a sorted array. See
 * median.h for what the filter does and why it is shaped this way.
 *
 * The windows are tiny (at most MEDIAN_MAX_WIDTH samples), so each one is
 * copied into a stack array and ordered by insertion sort. That is
 * O(n * width^2) for a block, a few million operations for a 64 KiB block at
 * width 9, and it needs no allocation and no shared state.
 */

#include "median.h"

#include <math.h>
#include <stdio.h>

/* 1 / 0.6745: turns a median absolute deviation into a standard deviation for
 * Gaussian noise. */
#define MEDIAN_MAD_TO_SIGMA 1.4826f

void median_config_default(median_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->width = MEDIAN_DEFAULT_WIDTH;
    cfg->threshold = MEDIAN_DEFAULT_THRESHOLD;
}

int median_width_valid(uint32_t width)
{
    if (width < MEDIAN_MIN_WIDTH || width > MEDIAN_MAX_WIDTH) {
        return 0;
    }
    return (width & 1u) != 0u;          /* a window needs a centre sample */
}

int median_width_parse(const char *text, uint32_t *width)
{
    const char *p;
    uint32_t    value = 0;

    if (text == NULL || width == NULL || *text == '\0') {
        return -1;
    }
    for (p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        value = value * 10u + (uint32_t)(*p - '0');
        if (value > MEDIAN_MAX_WIDTH) {  /* already too large to become valid */
            return -1;
        }
    }
    if (!median_width_valid(value)) {
        return -1;
    }
    *width = value;
    return 0;
}

int median_config_valid(const median_config_t *cfg)
{
    if (cfg == NULL) {
        return 0;
    }
    if (!median_width_valid(cfg->width)) {
        return 0;
    }
    if (!(cfg->threshold > 0.0f)) {     /* also rejects NaN */
        return 0;
    }
    return 1;
}

/* Order `count` values ascending. Windows are small, so insertion sort wins. */
static void sort_small(float *values, size_t count)
{
    size_t i;

    for (i = 1; i < count; i++) {
        float key = values[i];
        size_t j = i;

        while (j > 0 && values[j - 1] > key) {
            values[j] = values[j - 1];
            j--;
        }
        values[j] = key;
    }
}

/* Median of `count` already ordered values (count >= 1). */
static float middle_of(const float *ordered, size_t count)
{
    if ((count & 1u) != 0u) {
        return ordered[count / 2u];
    }
    return (ordered[count / 2u - 1u] + ordered[count / 2u]) * 0.5f;
}

float median_of_sorted(const float *sorted, size_t n)
{
    if (sorted == NULL || n == 0) {
        return 0.0f;
    }
    return middle_of(sorted, n);
}

/*
 * The sample at logical position j, where 0 is cur[0], negative positions are
 * the tail of `left` and positions >= n are the head of `right`. The caller
 * guarantees j lies inside [-nleft, n + nright).
 */
static float sample_at(ptrdiff_t j, const float *left, ptrdiff_t nleft,
                       const float *cur, ptrdiff_t n, const float *right)
{
    if (j < 0) {
        return left[nleft + j];
    }
    if (j >= n) {
        return right[j - n];
    }
    return cur[j];
}

int median_filter_block(const median_config_t *cfg,
                        const float *left, size_t nleft,
                        const float *cur, size_t n,
                        const float *right, size_t nright,
                        float *out, uint32_t *outliers)
{
    float window[MEDIAN_MAX_WIDTH];
    float deviation[MEDIAN_MAX_WIDTH];
    ptrdiff_t half;
    ptrdiff_t usable_left;
    ptrdiff_t usable_right;
    ptrdiff_t i;
    uint32_t replaced = 0;

    if (!median_config_valid(cfg)) {
        fprintf(stderr, "median: width must be odd in %u..%u and the "
                        "threshold positive\n",
                MEDIAN_MIN_WIDTH, MEDIAN_MAX_WIDTH);
        return -1;
    }
    if (cur == NULL || out == NULL) {
        fprintf(stderr, "median: NULL block or output\n");
        return -1;
    }
    if ((nleft > 0 && left == NULL) || (nright > 0 && right == NULL)) {
        fprintf(stderr, "median: context length given without a pointer\n");
        return -1;
    }
    if (out == cur && n > 0) {
        fprintf(stderr, "median: output must not overlap the input block\n");
        return -1;
    }

    half = (ptrdiff_t)MEDIAN_HALF_WIDTH(cfg->width);
    /* Only a half-window of context can ever fall inside a window. */
    usable_left = (nleft < (size_t)half) ? (ptrdiff_t)nleft : half;
    usable_right = (nright < (size_t)half) ? (ptrdiff_t)nright : half;

    for (i = 0; i < (ptrdiff_t)n; i++) {
        /* The window, clipped to the samples that exist. */
        ptrdiff_t lo = i - half;
        ptrdiff_t hi = i + half;
        size_t count;
        size_t k;
        float centre = cur[i];
        float median;
        float mad;

        if (lo < -usable_left) {
            lo = -usable_left;
        }
        if (hi > (ptrdiff_t)n - 1 + usable_right) {
            hi = (ptrdiff_t)n - 1 + usable_right;
        }
        count = (size_t)(hi - lo + 1);

        for (k = 0; k < count; k++) {
            window[k] = sample_at(lo + (ptrdiff_t)k, left, (ptrdiff_t)nleft,
                                  cur, (ptrdiff_t)n, right);
        }
        sort_small(window, count);
        median = middle_of(window, count);

        /* Median absolute deviation of the same window. */
        for (k = 0; k < count; k++) {
            deviation[k] = fabsf(window[k] - median);
        }
        sort_small(deviation, count);
        mad = middle_of(deviation, count);

        if (fabsf(centre - median) >
            cfg->threshold * MEDIAN_MAD_TO_SIGMA * mad) {
            out[i] = median;
            replaced++;
        } else {
            out[i] = centre;
        }
    }

    if (outliers != NULL) {
        *outliers = replaced;
    }
    return 0;
}

int median_filter_signal(const median_config_t *cfg,
                         const float *in, size_t n,
                         float *out, uint32_t *outliers)
{
    return median_filter_block(cfg, NULL, 0, in, n, NULL, 0, out, outliers);
}
