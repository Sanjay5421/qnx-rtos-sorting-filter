/*
 * sensor.h
 *
 * Virtual sensor for the sorting / median-filter service.
 *
 * There is no real sensor on the bench. This module replays a recording
 * produced by the PyBaMM generator (data/src/battery_dataset/) with the
 * behaviour of a live device: values arrive a sample at a time, at a
 * configured rate, as a stream. It exists so the client can be written and
 * tested against the same interface it would use with real hardware.
 *
 * Two on-disk layouts are supported, both headerless little-endian float32:
 *
 *   planar       one sample per float, contiguous
 *                data/artifacts/dataset/bin/channels/r1_voltage.bin
 *
 *   interleaved  `stride` floats per record, this sensor taking `offset`
 *                data/artifacts/dataset/bin/Chen2020_c1_T25_discharge.bin
 *                record = [time_s, voltage_V, current_A, temperature_K]
 *
 * The two layouts carry the same recordings, so a sensor opened on
 * r1_voltage.bin and a sensor opened on Chen2020_c1_T25_discharge.bin with
 * { offset = 1, stride = 4 } must deliver bit-identical voltage samples.
 * tests/test_sensor.c checks exactly that.
 *
 * Everything here is plain C99 plus libc, with no QNX-specific calls, so the
 * same source builds for the host (tests/ and local runs) and for the target
 * (qcc -Vgcc_ntoaarch64le).
 *
 * Deliberate simplifications, to revisit when the service needs them:
 *   - one channel per sensor; open several to cover several channels
 *   - pacing has millisecond resolution, so it is meaningful up to ~500 Hz
 */

#ifndef SENSOR_H
#define SENSOR_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Layout                                                                     */
/* ------------------------------------------------------------------------- */

/*: One float per sample, laid out contiguously (the per-channel .bin files). */
#define SENSOR_LAYOUT_PLANAR 0

/*: `stride` floats per record, this sensor taking the float at `offset`
 *  (the interleaved .bin files: 4 floats per record). */
#define SENSOR_LAYOUT_INTERLEAVED 1

/*: Samples per frame, i.e. how much a single read may deliver. Sensors
 *  deliver in frames, not one unbounded buffer; the client aggregates frames
 *  into the block it offloads. */
#define SENSOR_DEFAULT_FRAME_SAMPLES 128

/*: Virtual sample rate used for timestamps and pacing. */
#define SENSOR_DEFAULT_RATE_HZ 100

/* ------------------------------------------------------------------------- */
/* Configuration                                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    int layout;              /* SENSOR_LAYOUT_PLANAR or SENSOR_LAYOUT_INTERLEAVED */

    int offset;              /* interleaved only: float index within the record   */
    int stride;              /* interleaved only: floats per record               */

    size_t frame_samples;    /* samples per frame; 0 means the default            */
    uint32_t rate_hz;        /* sample rate for timestamps and pacing; 0 = default */

    int loop;                /* 1 = wrap around at the end of the recording and
                              *     keep delivering, like a live sensor that
                              *     never stops; 0 = report end of stream       */
} sensor_config_t;

/*
 * Fill *cfg with the defaults for a planar file: one sample per float,
 * 128 samples per frame, 100 Hz, no looping. Passing NULL as the config to
 * sensor_open() has the same effect.
 */
void sensor_config_default(sensor_config_t *cfg);

/* ------------------------------------------------------------------------- */
/* Handle                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct sensor sensor_t;

/*
 * Open `path` and prepare to deliver its samples.
 *
 * `cfg` may be NULL for the planar defaults.
 *
 * Returns a handle, or NULL on failure with a diagnostic on stderr for each
 * of: missing path, unreadable file, empty file, file size not a whole number
 * of float32 values, file size not a whole number of records, or an invalid
 * configuration (offset/stride/frame/rate out of range).
 */
sensor_t *sensor_open(const char *path, const sensor_config_t *cfg);

/* Release the handle. Passing NULL is allowed and does nothing. */
void sensor_close(sensor_t *s);

/* ------------------------------------------------------------------------- */
/* Stream state                                                               */
/* ------------------------------------------------------------------------- */

/* Samples in the recording. */
size_t sensor_total_samples(const sensor_t *s);

/* Samples left before the end of the recording. With looping enabled this
 * counts down to zero and then restarts, since the stream never ends. */
size_t sensor_remaining(const sensor_t *s);

/* Samples delivered since the sensor was opened (or since the last
 * sensor_reset()). This is the sensor's own timeline, so it keeps counting
 * across loop wraps. */
uint64_t sensor_index(const sensor_t *s);

/* Timestamp of the next sample on the sensor's virtual clock, in nanoseconds:
 * sensor_index() * 1e9 / rate_hz. */
uint64_t sensor_timestamp_ns(const sensor_t *s);

/* Rewind to the first sample and restart the timeline at zero.
 * Returns 0 on success, -1 on a seek failure. */
int sensor_reset(sensor_t *s);

/* ------------------------------------------------------------------------- */
/* Reading                                                                    */
/* ------------------------------------------------------------------------- */

/*
 * Deliver at most one frame: min(max_samples, frame_samples) samples into
 * `dst`. Returns the number of samples delivered, 0 at the end of the
 * recording (or never, if looping), or -1 on a read error.
 */
int sensor_read(sensor_t *s, float *dst, size_t max_samples);

/* Deliver exactly one frame, i.e. frame_samples samples except for the last
 * partial frame of the recording. Same return convention as sensor_read(). */
int sensor_read_frame(sensor_t *s, float *dst);

/*
 * Aggregate whole frames into one block of `block_samples` samples: this is
 * how the client assembles the >= 64 KiB payload it offloads (16384 float32
 * samples = 64 KiB). Shorter frames are packed back to back. Returns the
 * number of samples delivered, which is less than block_samples only at the
 * end of the recording, or -1 on a read error.
 */
int sensor_read_block(sensor_t *s, float *dst, size_t block_samples);

/*
 * sensor_read() followed by an optional wait, so samples come out at cfg
 * .rate_hz in real time rather than as fast as the caller can consume them.
 * The first paced read starts the pacing clock. Returns what sensor_read()
 * returns. With loop disabled, pacing still drains the whole recording; it
 * just takes as long as the recording lasts at that rate.
 */
int sensor_read_paced(sensor_t *s, float *dst, size_t max_samples);

#endif /* SENSOR_H */
