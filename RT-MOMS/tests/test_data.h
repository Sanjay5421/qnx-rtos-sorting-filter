/*
 * test_data.h
 *
 * Locating the generated payload for the tests that need it.
 *
 * The payload is git-ignored and regenerated with
 *     cd data && uv run python -m battery_dataset
 * so the tests that use it skip cleanly when it is absent, and must find it
 * whether they are run from tests/ or from the repository root.
 *
 * The helpers are file-static, so include this header only in the test binary
 * that actually needs the payload.
 */

#ifndef TEST_DATA_H
#define TEST_DATA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_util.h"      /* for T_MAYBE_UNUSED */

/*: Directory of the generated payload, or "" when it was not found. */
static char td_dir[512];

static int td_join(char *out, size_t cap, const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);

    if (la + lb + 1 > cap)
        return -1;
    memcpy(out, a, la);
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return 0;
}

static int td_exists(const char *dir, const char *relative)
{
    char  path[600];
    FILE *fp;

    if (dir == NULL || td_join(path, sizeof(path), dir, relative) != 0)
        return 0;
    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
}

/*
 * Resolve the payload directory: first argument, then BATTERY_DATASET_DIR,
 * then the locations the tests are normally run from. Returns 1 and fills
 * td_dir, or 0 and leaves it empty.
 */
static int td_find_dataset(int argc, char **argv)
{
    static const char *fallback[3] = {
        "../../data/artifacts/dataset",
        "../data/artifacts/dataset",
        "data/artifacts/dataset"
    };
    const char *dir = (argc > 1) ? argv[1] : getenv("BATTERY_DATASET_DIR");
    int         i;

    td_dir[0] = '\0';

    if (!td_exists(dir, "/bin/channels/r1_voltage.bin")) {
        dir = NULL;
        for (i = 0; i < 3; i++) {
            if (td_exists(fallback[i], "/bin/channels/r1_voltage.bin")) {
                dir = fallback[i];
                break;
            }
        }
    }

    if (dir == NULL || strlen(dir) >= sizeof(td_dir))
        return 0;

    memcpy(td_dir, dir, strlen(dir) + 1);
    return 1;
}

/*: Recording r1 (1C) as a planar per-channel file: one float32 per sample.
 *  A test binary may need only one of the two layouts, so both are marked. */
static T_MAYBE_UNUSED const char *td_voltage_path(void)
{
    static char path[600];

    if (td_dir[0] == '\0' ||
        td_join(path, sizeof(path), td_dir, "/bin/channels/r1_voltage.bin") != 0)
        return NULL;
    return path;
}

/*: The same recording as the interleaved sensor record: 4 floats per sample. */
static T_MAYBE_UNUSED const char *td_interleaved_path(void)
{
    static char path[600];

    if (td_dir[0] == '\0' ||
        td_join(path, sizeof(path), td_dir,
                "/bin/Chen2020_c1_T25_discharge.bin") != 0)
        return NULL;
    return path;
}

#endif /* TEST_DATA_H */
