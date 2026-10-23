/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_inflate.h - DEFLATE (RFC 1951) decoded and encoded.
 *
 * tiku_inflate.c decodes a whole stream, raw or in a zlib wrapper (RFC
 * 1950), into a buffer of known size or one that grows;
 * tiku_inflate_stream.c decodes a stream as its bytes arrive, Deflate64
 * included; tiku_deflate.c encodes a zlib stream for the PNG and PDF
 * writers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_INFLATE_H_
#define TIKU_INFLATE_H_

#include <stddef.h>

/**
 * @brief Inflate a raw DEFLATE stream into a buffer of known size.
 *
 * @param src   Compressed bytes, with no zlib or gzip wrapper.
 * @param out   Where to write, @p omax bytes.
 * @return Bytes written, or -1 for a malformed stream or one that decodes
 *         to more than @p omax bytes.
 */
long tiku_inflate(const unsigned char *src, long slen,
                      unsigned char *out, long omax);

/**
 * @brief Inflate a zlib stream (RFC 1950): a two-byte header, then DEFLATE.
 *
 * A header naming another method or a preset dictionary, or failing its
 * check, is refused.  The trailing Adler-32 is not checked.
 *
 * @return Bytes written, or -1 as tiku_inflate().
 */
long tiku_inflate_zlib(const unsigned char *src, long slen,
                           unsigned char *out, long omax);

/**
 * @brief Inflate into memory this allocates, zlib-wrapped or raw, for a
 *        stream whose decoded size is not known.
 *
 * A stream cut short or corrupt past some point gives the bytes decoded
 * before that point.
 * @param omax  The most @p out may grow to; decoding stops there.
 * @param zlib  Nonzero when a zlib header may lead; it is skipped when
 *              there and not required.
 * @return Bytes written to @p out, which the caller frees; -1 with @p out
 *         untouched when nothing decoded.
 */
long tiku_inflate_alloc(const unsigned char *src, long slen,
                        unsigned char **out, long omax, int zlib);

/* ------------------------------------------------------------------ */
/* Streams: decoded as their bytes arrive.                             */

/** @brief Where a stream's compressed bytes come from: up to @p max into
 *         @p buf.  @return how many, 0 at the end, -1 for an error. */
typedef long (*tiku_inflate_fill_t)(void *ctx, unsigned char *buf, long max);

/** @brief A DEFLATE stream being decoded; see tiku_inflate_stream.c. */
typedef struct tiku_inflater tiku_inflater_t;

/** @brief tiku_inflater_new()'s flag for Deflate64, ZIP method 9, whose
 *         window is 64 KB. */
#define TIKU_INFLATE_DEFLATE64 1

/** @brief A decoder asking @p fill for its input as it goes; @p flags
 *         may name Deflate64.  @return it, or NULL out of memory or
 *         without @p fill. */
tiku_inflater_t *tiku_inflater_new(tiku_inflate_fill_t fill, void *ctx,
                                   int flags);

/**
 * @brief Up to @p max decoded bytes into @p out.
 * @return how many; 0 once the final block has ended; -1 for a stream
 *         that is malformed, cut short, or whose @c fill failed (bytes
 *         decoded before that are handed out first).
 */
long tiku_inflater_read(tiku_inflater_t *z, unsigned char *out, long max);

/** @brief Whether the final block ended and everything was handed out. */
int tiku_inflater_done(const tiku_inflater_t *z);

/** @brief Once done, the bytes it read past the stream's end, up to @p max
 *         of them, taken out of it: a gzip trailer, the next member.
 *  @return how many. */
long tiku_inflater_tail(tiku_inflater_t *z, unsigned char *out, long max);

/** @brief Free @p z and its buffers; NULL is ignored. */
void tiku_inflater_free(tiku_inflater_t *z);

/* ------------------------------------------------------------------ */
/* Writing: a zlib stream, made by tiku_deflate.c.                    */

/** @brief @p n bytes as a zlib stream (RFC 1950 around RFC 1951): LZ77
 *         over a 32 KB window, in one block of the fixed codes.
 *  @return a malloc'd buffer, its length in @p outlen; NULL out of
 *          memory. */
unsigned char *tiku_deflate_zlib(const unsigned char *in, size_t n,
                                 size_t *outlen);

#endif /* TIKU_INFLATE_H_ */
