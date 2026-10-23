/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_png_write.c - pixels written as a PNG, filtered and deflated.
 *
 * Each row is filtered five ways -- None, Sub, Up, Average, Paeth -- and
 * sent the way whose bytes, read as signed, have the smallest sum of
 * absolute values: the heuristic the PNG specification suggests.  The
 * picture is written as RGB, or as RGBA when the caller asks for alpha.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_image.h"
#include "tiku_inflate.h"
#include "tiku_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief @p v as four bytes, the most significant first. */
static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/** @brief A chunk onto @p f: its length, type, data and CRC.
 *  @return 0, -1 when it could not be written. */
static int chunk(FILE *f, const char *type, const unsigned char *data,
                 size_t n)
{
    unsigned char head[8], tail[4];

    be32(head, (uint32_t)n);
    memcpy(head + 4, type, 4);
    be32(tail, tiku_crc32(tiku_crc32(0, head + 4, 4), data, n));
    return fwrite(head, 1, 8, f) == 8 && (n == 0 ||
           fwrite(data, 1, n, f) == n) && fwrite(tail, 1, 4, f) == 4 ? 0 : -1;
}

/** @brief Paeth's guess from the byte left, above and above-left. */
static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/** @brief Row @p cur filtered by @p kind against @p prev (zeros above
 *         the first row) into @p out.  @return the sum of the filtered
 *         bytes' absolute values, read as signed, which the choice of
 *         filter is made on. */
static long filter(int kind, const unsigned char *cur,
                   const unsigned char *prev, size_t len, int bpp,
                   unsigned char *out)
{
    long sum = 0;

    for (size_t i = 0; i < len; i++) {
        int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i];
        int c = i >= (size_t)bpp ? prev[i - bpp] : 0;
        int guess = kind == 0 ? 0 : kind == 1 ? a : kind == 2 ? b
                  : kind == 3 ? (a + b) / 2 : paeth(a, b, c);

        out[i] = (unsigned char)(cur[i] - guess);
        sum += (signed char)out[i] < 0 ? -(signed char)out[i] : out[i];
    }
    return sum;
}

/**
 * @brief The picture as PNG's filtered rows, each led by its filter's
 *        byte, into a buffer of its own.  @return it, its length in
 *        @p len; NULL out of memory.
 */
static unsigned char *rows(const uint32_t *px, int w, int h, int bpp,
                           size_t *len)
{
    size_t row = (size_t)w * (size_t)bpp;
    unsigned char *raw = malloc((size_t)h * (row + 1));
    unsigned char *line = calloc(3 * row + 1, 1), *prev, *cur, *best;

    if (!raw || !line) {
        free(raw);
        free(line);
        return NULL;
    }
    prev = line;                            /* zeros above the first row */
    cur = line + row;
    best = line + 2 * row;
    for (int y = 0; y < h; y++) {
        unsigned char *o = raw + (size_t)y * (row + 1);
        long least = -1;

        for (int x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * (size_t)w + (size_t)x];
            unsigned char *q = cur + (size_t)x * (size_t)bpp;

            q[0] = (unsigned char)(p >> 16);
            q[1] = (unsigned char)(p >> 8);
            q[2] = (unsigned char)p;
            if (bpp == 4) q[3] = (unsigned char)(p >> 24);
        }
        for (int kind = 0; kind < 5; kind++) {
            long sum = filter(kind, cur, prev, row, bpp, best);

            if (least < 0 || sum < least) {
                least = sum;
                o[0] = (unsigned char)kind;
                memcpy(o + 1, best, row);
            }
        }
        memcpy(prev, cur, row);
    }
    free(line);
    *len = (size_t)h * (row + 1);
    return raw;
}

/**
 * @brief @p px encoded as a PNG and written to @p path, or to @p f when
 *        @p path is NULL.
 *
 * A file at @p path is opened only once the encoding has succeeded, and
 * removed again when writing it fails.
 *
 * @return 0, or -1.
 */
static int write_to(const char *path, FILE *f, const uint32_t *px,
                    int w, int h, int alpha)
{
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26,
                                          10 };
    unsigned char ihdr[13], *raw, *z = NULL;
    size_t rawlen = 0, zlen = 0;
    int ok = 0;

    if ((!path && !f) || !px || w <= 0 || h <= 0 ||
        (size_t)w > TIKU_IMAGE_PIXELS_MAX / (size_t)h) return -1;
    raw = rows(px, w, h, alpha ? 4 : 3, &rawlen);
    if (raw) z = tiku_deflate_zlib(raw, rawlen, &zlen);
    free(raw);
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;                            /* bits a sample */
    ihdr[9] = alpha ? 6 : 2;                /* RGBA or RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;     /* deflate, adaptive, no Adam7 */
    /* Do not open/truncate a destination before encoding succeeds. */
    if (z && zlen <= UINT32_MAX && (!path || (f = fopen(path,"wb")) != NULL))
        ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13) == 0 &&
             chunk(f, "IDAT", z, zlen) == 0 && chunk(f, "IEND", NULL, 0) == 0;
    free(z);
    if (path && f) {
        if (fclose(f)) ok = 0;
        if (!ok) remove(path);
    }
    return ok ? 0 : -1;
}

int tiku_png_write_stream(FILE *f, const uint32_t *px, int w, int h, int alpha)
{
    return write_to(NULL,f,px,w,h,alpha);
}

int tiku_png_write(const char *path, const uint32_t *px, int w, int h, int alpha)
{
    return write_to(path,NULL,px,w,h,alpha);
}
