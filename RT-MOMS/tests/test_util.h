/*
 * test_util.h
 *
 * Minimal assertion helpers for the host-side test binaries.
 *
 * Deliberately dependency-free: the tests build with nothing but a C
 * toolchain, which means they run on the Windows host today, before QNX is
 * installed, and on the target later with no changes at all.
 *
 * Include this header in exactly one translation unit per test binary (the
 * test file itself), because the counters are file-static.
 *
 *   mingw32-make check
 */

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int t_checks  = 0;
static int t_failed  = 0;
static int t_skipped = 0;

/* ------------------------------------------------------------------------- */
/* Assertions                                                                 */
/* ------------------------------------------------------------------------- */

#define T_CHECK(cond)                                                        \
    do {                                                                     \
        t_checks++;                                                          \
        if (!(cond)) {                                                       \
            t_failed++;                                                      \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                    \
    } while (0)

#define T_CHECK_INT(got, want)                                               \
    do {                                                                     \
        long t_got = (long)(got);                                            \
        long t_want = (long)(want);                                          \
        t_checks++;                                                          \
        if (t_got != t_want) {                                               \
            t_failed++;                                                      \
            printf("    FAIL %s:%d: got %ld, want %ld\n",                    \
                   __FILE__, __LINE__, t_got, t_want);                       \
        }                                                                    \
    } while (0)

#define T_CHECK_SIZE(got, want)                                              \
    do {                                                                     \
        unsigned long t_got = (unsigned long)(got);                          \
        unsigned long t_want = (unsigned long)(want);                        \
        t_checks++;                                                          \
        if (t_got != t_want) {                                               \
            t_failed++;                                                      \
            printf("    FAIL %s:%d: got %lu, want %lu\n",                    \
                   __FILE__, __LINE__, t_got, t_want);                       \
        }                                                                    \
    } while (0)

/*
 * 64-bit values are compared and reported through double, which is exact up
 * to 2^53. The old MinGW msvcrt behind gcc 6.3 cannot print a uint64_t
 * portably, and a test harness is not worth a format-string fight.
 */
#define T_CHECK_U64(got, want)                                               \
    do {                                                                     \
        double t_got = (double)(got);                                        \
        double t_want = (double)(want);                                      \
        t_checks++;                                                          \
        if (t_got != t_want) {                                               \
            t_failed++;                                                      \
            printf("    FAIL %s:%d: got %.0f, want %.0f\n",                  \
                   __FILE__, __LINE__, t_got, t_want);                       \
        }                                                                    \
    } while (0)

/* Absolute tolerance, for values that are documented rather than derived. */
#define T_CHECK_NEAR(got, want, tol)                                         \
    do {                                                                     \
        double t_got = (double)(got);                                        \
        double t_want = (double)(want);                                      \
        double t_diff = (t_got > t_want) ? (t_got - t_want) : (t_want - t_got); \
        t_checks++;                                                          \
        if (t_diff > (double)(tol)) {                                        \
            t_failed++;                                                      \
            printf("    FAIL %s:%d: got %.9g, want %.9g (tolerance %g)\n",   \
                   __FILE__, __LINE__, t_got, t_want, (double)(tol));        \
        }                                                                    \
    } while (0)

/*
 * GCC's "may be unused" marker. A test binary that never compares floats bit
 * for bit would otherwise trip -Wunused-function for t_bits below. qcc is GCC,
 * so this is available on the host and on the target alike.
 */
#if defined(__GNUC__)
#  define T_MAYBE_UNUSED __attribute__((unused))
#else
#  define T_MAYBE_UNUSED
#endif

static T_MAYBE_UNUSED uint32_t t_bits(float f)
{
    uint32_t bits;

    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/*
 * Bit-exact float comparison. Used where the value must survive untouched
 * (float32 read back from a file, two file layouts holding the same samples),
 * as opposed to T_CHECK_NEAR, which allows for rounding in a written literal.
 */
#define T_CHECK_SAME_FLOAT(got, want)                                        \
    do {                                                                     \
        float    t_g = (got);                                                \
        float    t_w = (want);                                               \
        uint32_t t_gb = t_bits(t_g);                                         \
        uint32_t t_wb = t_bits(t_w);                                         \
        t_checks++;                                                          \
        if (t_gb != t_wb) {                                                  \
            t_failed++;                                                      \
            printf("    FAIL %s:%d: got %.9g (0x%08lx), want %.9g (0x%08lx)\n", \
                   __FILE__, __LINE__, (double)t_g, (unsigned long)t_gb,     \
                   (double)t_w, (unsigned long)t_wb);                        \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Runner                                                                     */
/* ------------------------------------------------------------------------- */

#define T_RUN(fn)                                                            \
    do {                                                                     \
        int t_before = t_failed;                                             \
        printf("  %-36s ", #fn);                                             \
        fn();                                                                \
        printf("%s\n", (t_failed == t_before) ? "ok" : "FAILED");            \
    } while (0)

#define T_SKIP(why)                                                          \
    do {                                                                     \
        t_skipped++;                                                         \
        printf("  SKIP: %s\n", (why));                                       \
    } while (0)

static int t_summary(void)
{
    printf("\n  %d checks, %d failed, %d skipped\n", t_checks, t_failed, t_skipped);
    return (t_failed == 0) ? 0 : 1;
}

#endif /* TEST_UTIL_H */
