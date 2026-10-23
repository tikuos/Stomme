/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_png_stream.c - bounded RGBA PNG output with stored DEFLATE blocks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_image.h"
#include "tiku_crypto.h"
#include <string.h>

/** @brief Maximum raw scanline bytes per stored DEFLATE block. */
#define PNG_BLOCK 4096u

/** @brief Write a big-endian PNG integer. */
static void
be32(unsigned char *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) out[i] = value >> (24 - i * 8);
}

size_t
tiku_png_rgba_size(unsigned width, unsigned height)
{
    size_t raw;

    if (!width || !height || width > TIKU_IMAGE_PIXELS_MAX / height) return 0;
    raw = ((size_t)width * 4 + 1) * height;
    return raw + ((raw + PNG_BLOCK - 1) / PNG_BLOCK) * 5 + 63;
}

/** @brief Serialize raw filtered bytes and update the stream's Adler-32. */
static void
scanline(unsigned char *out, size_t at, size_t length,
    const uint32_t *pixels, unsigned width, uint32_t *a, uint32_t *b)
{
    static const unsigned shift[] = {16, 8, 0, 24};
    size_t stride = (size_t)width * 4 + 1;

    for (size_t i = 0; i < length; i++, at++) {
        size_t column = at % stride;
        unsigned char byte = 0;

        if (column) {
            size_t pixel = at / stride * width + (column - 1) / 4;

            byte = pixels[pixel] >> shift[(column - 1) % 4];
        }
        out[i] = byte;
        *a = (*a + byte) % 65521u;
        *b = (*b + *a) % 65521u;
    }
}

/** @brief Emit one IDAT payload and its CRC, followed by the IEND chunk. */
static int
data(const uint32_t *pixels, unsigned width, size_t raw,
    int (*emit)(void *, const void *, size_t), void *context)
{
    unsigned char block[PNG_BLOCK + 5], end[20];
    const unsigned char zlib[] = {0x78, 0x01};
    uint32_t crc = tiku_crc32(0, "IDAT", 4), a = 1, b = 0;
    size_t at = 0;

    crc = tiku_crc32(crc, zlib, sizeof zlib);
    if (emit(context, zlib, sizeof zlib)) return -1;
    while (at < raw) {
        size_t n = raw - at < PNG_BLOCK ? raw - at : PNG_BLOCK;

        block[0] = at + n == raw;
        block[1] = n;
        block[2] = n >> 8;
        block[3] = ~n;
        block[4] = (~n >> 8);
        scanline(block + 5, at, n, pixels, width, &a, &b);
        crc = tiku_crc32(crc, block, n + 5);
        if (emit(context, block, n + 5)) return -1;
        at += n;
    }
    be32(end, (b << 16) | a);
    be32(end + 4, tiku_crc32(crc, end, 4));
    be32(end + 8, 0);
    memcpy(end + 12, "IEND", 4);
    be32(end + 16, tiku_crc32(0, "IEND", 4));
    return emit(context, end, sizeof end) ? -1 : 0;
}

int
tiku_png_rgba_stream(const uint32_t *pixels, unsigned width, unsigned height,
    int (*emit)(void *, const void *, size_t), void *context)
{
    unsigned char head[41] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    size_t size = tiku_png_rgba_size(width, height);

    if (!pixels || !emit || !size) return -1;
    be32(head + 8, 13);
    memcpy(head + 12, "IHDR", 4);
    be32(head + 16, width);
    be32(head + 20, height);
    head[24] = 8;
    head[25] = 6;
    be32(head + 29, tiku_crc32(0, head + 12, 17));
    be32(head + 33, (uint32_t)(size - 57));
    memcpy(head + 37, "IDAT", 4);
    if (emit(context, head, sizeof head)) return -1;
    return data(pixels, width, ((size_t)width * 4 + 1) * height, emit, context);
}
