#ifndef FR_INFLATE_H
#define FR_INFLATE_H
#include <stdint.h>
#include <stddef.h>

/* Decompress raw deflate data (RFC 1951 — no zlib/gzip wrapper).
   Returns decompressed size on success, -1 on error. */
int inflate_raw(const uint8_t *in, size_t in_len,
                uint8_t *out, size_t out_max);

#endif
