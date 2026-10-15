/*
 * block_loader.h
 *
 * The loader: it turns a recording on disk into the 64 KiB blocks and block
 * descriptors defined in protocol.h, which is exactly what the client offloads
 * to the server.
 *
 * It sits between the virtual sensor (app/sensor.c) and the client's message
 * send, and it is the only module that knows how the two on-disk layouts map
 * onto protocol channels. On the target this code is linked into the client;
 * on the host the same code feeds the tests, which is the point of keeping it
 * free of anything QNX-specific.
 */

#ifndef BLOCK_LOADER_H
#define BLOCK_LOADER_H

#include "protocol.h"
#include "sensor.h"

#include <stddef.h>
#include <stdint.h>

typedef struct block_loader block_loader_t;

/*
 * Offset of a channel inside an interleaved record.
 *
 * The interleaved export stores each sample as
 *     [time_s, voltage_V, current_A, temperature_K]
 * (metadata.json, binary_export.interleaved.fields), which is a different
 * order from the per-channel files and from the PROTO_CHANNEL_* enumeration.
 * The mapping therefore lives in exactly one place. Returns -1 for an unknown
 * channel.
 */
int block_loader_channel_offset(proto_channel_t channel);

/*
 * Open a recording as a source of blocks.
 *
 *   path     raw float32 file, headerless little-endian: either the planar
 *            per-channel export (r<n>_<channel>.bin) or the interleaved
 *            sensor record (<run_id>.bin)
 *   layout   SENSOR_LAYOUT_PLANAR or SENSOR_LAYOUT_INTERLEAVED
 *   channel  the physical channel the blocks carry. For an interleaved file it
 *            selects which float of each record is taken; for a planar file
 *            the file already holds a single channel and this is descriptive
 *            (it is what the descriptors and the logs report).
 *   rate_hz  sample rate recorded in every descriptor
 *
 * Returns NULL on failure, with a diagnostic on stderr naming the problem.
 */
block_loader_t *block_loader_open(const char *path, int layout,
                                  proto_channel_t channel, uint32_t rate_hz);

void block_loader_close(block_loader_t *l);

/*
 * Fill one block and complete its descriptor.
 *
 * Returns the number of samples placed in the block (1..PROTO_BLOCK_SAMPLES),
 * 0 at the end of the recording, or -1 on a read error. The unused tail of a
 * short final block is zeroed, so a block is deterministic byte for byte.
 *
 * The descriptor is filled with the sequence number within the stream, the
 * index of the block's first sample in the recording, the sample count, the
 * rate, the channel, the block's own min/max, and the FNV-1a checksum of the
 * samples it covers.
 */
int block_loader_next(block_loader_t *l, proto_block_t *block,
                      proto_block_desc_t *desc);

/* Counters, for the report and for tests. */
size_t   block_loader_total_samples(const block_loader_t *l);
uint64_t block_loader_blocks_emitted(const block_loader_t *l);
uint64_t block_loader_samples_emitted(const block_loader_t *l);

#endif /* BLOCK_LOADER_H */
