/* memfd_reader - live H.264 frame source from Creality cam_app's shared memfd.
 *
 * cam_app (the stock Creality camera daemon) hardware-encodes H.264 into a
 * double-buffered memfd and exposes it as an fd. We mmap it read-only and hand
 * out the latest *complete* access unit (Annex-B, leading 00 00 00 01) each
 * time a new one appears. This is the same buffer/stability logic proven in
 * k1/k1_mods/h264cam/memfd_h264_dump.c, repackaged as a pull API.
 *
 * Zero decode, zero re-encode: we just forward cam_app's hardware bitstream.
 */
#ifndef MEMFD_READER_H_
#define MEMFD_READER_H_

#include <stddef.h>
#include <stdint.h>

typedef struct MemfdReader MemfdReader;

/* Open the cam_app memfd at `path` (e.g. /proc/<campid>/fd/<n>). Returns NULL
 * on failure. */
MemfdReader* mr_open(const char* path);

/* If a new complete frame is available since the last call, point *out at an
 * internal buffer holding it (valid until the next mr_next_frame call) and set
 * *size, returning 1. Returns 0 when no new frame is ready yet. */
int mr_next_frame(MemfdReader* mr, uint8_t** out, int* size);

/* Most recent SPS/PPS NAL units seen in the buffer (for priming a new viewer
 * before the next IDR). Returns NULL/0 if not yet seen. */
const uint8_t* mr_sps(MemfdReader* mr, int* size);
const uint8_t* mr_pps(MemfdReader* mr, int* size);

void mr_close(MemfdReader* mr);

#endif  // MEMFD_READER_H_
