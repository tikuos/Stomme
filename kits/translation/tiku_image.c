/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_image.c - images read into pixels: BMP and PNG decoded here, JPEG
 * handed to tiku_jpeg, each bounded in bytes and in pixels; and an image
 * scaled into a thumbnail's square.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_image.h"

#include "tiku_inflate.h"
#include "tiku_jpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int
tiku_image_is_image(const char *path)
{
    unsigned char magic[8];
    struct stat st;
    FILE *f;
    size_t n;

    if (path == NULL) {
        return 0;
    }
    /* Only a regular file is opened: opening a FIFO with no writer
     * blocks, and a device such as a Mac's /dev/console can block the
     * read, stalling the thread that asked. */
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(magic, 1u, sizeof magic, f);
    (void)fclose(f);
    if (n >= 8 && magic[0] == 137 && memcmp(magic + 1, "PNG", 3) == 0) {
        return 1;
    }
    if (n >= 3 && magic[0] == 0xff && magic[1] == 0xd8 &&
        magic[2] == 0xff) {
        return 1;
    }
    if (n >= 2 && magic[0] == 'B' && magic[1] == 'M') {
        return 1;
    }
    return 0;
}

/** @brief Read the regular file @p path, at most TIKU_IMAGE_BYTES_MAX
 *         bytes, into @p out, which the caller frees; any other kind of
 *         file is refused without waiting on its open.
 *  @return its length, -2 for allocation refusal, or -1 otherwise. */
static long
slurp(const char *path, unsigned char **out)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    struct stat st;
    FILE *f;
    long n;
    unsigned char *buf;

    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        (f = fdopen(fd, "rb")) == NULL) {
        (void)close(fd);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        (unsigned long)n > TIKU_IMAGE_BYTES_MAX ||
        fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        return -1;
    }
    buf = malloc((size_t)n + 1u);
    if (buf == NULL) {
        (void)fclose(f);
        return -2;
    }
    if (fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        (void)fclose(f);
        return -1;
    }
    (void)fclose(f);
    *out = buf;
    return n;
}

/** @brief A big-endian 32-bit number. */
static uint32_t
be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** @brief A little-endian 32-bit number. */
static uint32_t
le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* BMP: uncompressed 24- and 32-bit, bottom-up or top-down                   */

/**
 * @brief Decode the @p n BMP bytes at @p b into @p out: uncompressed rows
 *        of 24 or 32 bits, opaque with the reserved fourth byte ignored.
 *        At most 20000 pixels a side and TIKU_IMAGE_PIXELS_MAX in all.
 * @return TIKU_IMG_OK; UNSUPPORTED for another depth, a compression or a
 *         header shorter than BITMAPINFOHEADER; BAD for a malformed file
 *         or a failed allocation.
 */
static tiku_image_err_t
load_bmp(const unsigned char *b, long n, tiku_image_t *out)
{
    uint32_t off, hdr, bpp, comp;
    int32_t w, h;
    int flip, y, x;
    long stride;

    if (n < 54) {
        return TIKU_IMG_BAD;
    }
    off = le32(b + 10);
    hdr = le32(b + 14);
    w = (int32_t)le32(b + 18);
    h = (int32_t)le32(b + 22);
    bpp = (uint32_t)(b[28] | (b[29] << 8));
    comp = le32(b + 30);
    if (hdr < 40u || (bpp != 24u && bpp != 32u) || comp != 0u) {
        return TIKU_IMG_UNSUPPORTED;
    }
    /* A negative height means the rows are stored top-down. */
    flip = (h > 0);
    if (h == INT32_MIN) return TIKU_IMG_BAD;
    if (h < 0) {
        h = -h;
    }
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) {
        return TIKU_IMG_BAD;
    }
    if ((size_t)w > TIKU_IMAGE_PIXELS_MAX / (size_t)h ||
        hdr > (uint32_t)n - 14u || off < 14u + hdr)
        return TIKU_IMG_BAD;
    /* Rows are padded to a multiple of four bytes. */
    stride = ((long)w * (long)(bpp / 8u) + 3L) & ~3L;
    if ((long)off + stride * h > n) {
        return TIKU_IMG_BAD;
    }
    out->px = malloc((size_t)w * (size_t)h * sizeof *out->px);
    if (out->px == NULL) {
        return TIKU_IMG_NO_MEMORY;
    }
    out->w = w;
    out->h = h;
    for (y = 0; y < h; y++) {
        const unsigned char *row = b + off +
            (long)(flip ? (h - 1 - y) : y) * stride;

        for (x = 0; x < w; x++) {
            const unsigned char *p = row + (long)x * (long)(bpp / 8u);
            uint32_t a = 0xffu;

            out->px[(long)y * w + x] = (a << 24) | ((uint32_t)p[2] << 16) |
                                       ((uint32_t)p[1] << 8) | p[0];
        }
    }
    return TIKU_IMG_OK;
}

/* PNG: 1 to 8 bits a sample, every filter, Adam7 interlacing                */

/** @brief Undo one scanline's PNG filter, in place. */
static void
unfilter(unsigned char *cur, const unsigned char *prev, long len, int bpp,
         int type)
{
    long i;

    for (i = 0; i < len; i++) {
        int a = (i >= bpp) ? cur[i - bpp] : 0;
        int b = (prev != NULL) ? prev[i] : 0;
        int c = (prev != NULL && i >= bpp) ? prev[i - bpp] : 0;
        int v = cur[i];

        switch (type) {
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) / 2; break;
        case 4: {
            int p = a + b - c, pa = abs(p - a), pb = abs(p - b);
            int pc = abs(p - c);

            v += (pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c);
            break;
        }
        default: break;
        }
        cur[i] = (unsigned char)v;
    }
}

/** @brief One sample of @p depth bits at index @p i of a packed row. */
static unsigned
sample(const unsigned char *row, int depth, long i)
{
    switch (depth) {
    case 8:  return row[i];
    case 4:  return (row[i >> 1] >> ((i & 1) ? 0 : 4)) & 0x0fu;
    case 2:  return (row[i >> 2] >> (6 - 2 * (i & 3))) & 0x03u;
    case 1:  return (row[i >> 3] >> (7 - (i & 7))) & 0x01u;
    default: return row[i];
    }
}

/** @brief Spread an n-bit sample over the full 0..255 range. */
static unsigned
scale_up(unsigned v, int depth)
{
    switch (depth) {
    case 8:  return v;
    case 4:  return v * 17u;                 /* 0..15  -> 0..255 */
    case 2:  return v * 85u;                 /* 0..3   -> 0..255 */
    case 1:  return v ? 255u : 0u;
    default: return v;
    }
}

/** @brief The seven Adam7 passes: x origin, y origin, x step, y step. */
static const int adam7[7][4] = {
    { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 },
    { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 }
};

/** @brief Everything one pass of a PNG needs in order to place its pixels. */
typedef struct {
    int             colour, depth, bpp, chan;
    const uint32_t *pal;
    int             npal;
} png_fmt_t;

/**
 * @brief Unfilter and place one pass (or the whole image, uninterlaced).
 *
 * @return bytes consumed, -1 for malformed data, -2 for allocation refusal.
 */
static long
png_pass(const unsigned char *raw, long avail, int pw, int ph,
         const png_fmt_t *f, tiku_image_t *out, int x0, int y0,
         int dx, int dy)
{
    long rowbytes = ((long)pw * f->chan * f->depth + 7L) / 8L;
    long used = 0;
    unsigned char *prev = NULL, *cur = NULL;
    int y;

    if (pw <= 0 || ph <= 0) {
        return 0;
    }
    prev = calloc((size_t)rowbytes, 1u);
    cur = malloc((size_t)rowbytes);
    if (prev == NULL || cur == NULL) {
        free(prev);
        free(cur);
        return -2;
    }
    for (y = 0; y < ph; y++) {
        int type, x;

        if (used + 1 + rowbytes > avail) {
            free(prev); free(cur);
            return -1;
        }
        type = raw[used++];
        if (type > 4) { free(prev); free(cur); return -1; }
        memcpy(cur, raw + used, (size_t)rowbytes);
        used += rowbytes;
        unfilter(cur, (y > 0) ? prev : NULL, rowbytes, f->bpp, type);

        for (x = 0; x < pw; x++) {
            int ox = x0 + x * dx, oy = y0 + y * dy;
            unsigned r, g, bl, a = 255u;

            if (ox >= out->w || oy >= out->h) {
                continue;
            }
            switch (f->colour) {
            case 0:                 /* greyscale */
                r = g = bl = scale_up(sample(cur, f->depth, x), f->depth);
                break;
            case 4:                 /* greyscale + alpha */
                r = g = bl = scale_up(sample(cur, f->depth, (long)x * 2),
                                      f->depth);
                a = scale_up(sample(cur, f->depth, (long)x * 2 + 1),
                             f->depth);
                break;
            case 3: {               /* palette */
                unsigned idx = sample(cur, f->depth, x);
                uint32_t e = (idx < (unsigned)f->npal) ? f->pal[idx]
                                                       : 0xff000000u;

                r = (e >> 16) & 0xffu;
                g = (e >> 8) & 0xffu;
                bl = e & 0xffu;
                a = (e >> 24) & 0xffu;
                break;
            }
            case 6:                 /* truecolour + alpha */
                r = cur[(long)x * 4];
                g = cur[(long)x * 4 + 1];
                bl = cur[(long)x * 4 + 2];
                a = cur[(long)x * 4 + 3];
                break;
            default:                /* truecolour */
                r = cur[(long)x * 3];
                g = cur[(long)x * 3 + 1];
                bl = cur[(long)x * 3 + 2];
                break;
            }
            out->px[(long)oy * out->w + ox] =
                (a << 24) | (r << 16) | (g << 8) | bl;
        }
        {
            unsigned char *t = prev;

            prev = cur;
            cur = t;
        }
    }
    free(prev);
    free(cur);
    return used;
}

/** @brief The CRC-32 of @p len bytes, as a PNG chunk carries it. */
static uint32_t png_crc(const unsigned char *bytes, size_t len)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}

/** @brief Parsed PNG chunks and their compressed pixel data. */
typedef struct {
    unsigned char *idat;
    uint32_t pal[256];
    long ilen;
    int w, h, interlace, npal, ended;
    png_fmt_t f;
} png_source_t;

/**
 * @brief Read the chunks of the @p n PNG bytes at @p b into @p src: every
 *        chunk's CRC checked, the palette's alpha from tRNS, at most 30000
 *        pixels a side and TIKU_IMAGE_PIXELS_MAX in all.
 * @return TIKU_IMG_OK with src->idat allocated; NOT_IMAGE without the
 *         signature; UNSUPPORTED for a depth other than 1, 2, 4 or 8, or
 *         other than 8 for truecolour and alpha; BAD for a malformed file
 *         and NO_MEMORY for allocation refusal. Any code but OK leaves nothing
 *         allocated.
 */
static tiku_image_err_t
png_source(const unsigned char *b, long n, png_source_t *src)
{
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    long i = 8;
    int p;
    memset(src, 0, sizeof *src);
    memset(&src->f, 0, sizeof src->f);
    for (p = 0; p < 256; p++) {
        src->pal[p] = 0xff000000u;
    }
    if (n < 8 || memcmp(b, sig, 8) != 0) {
        return TIKU_IMG_NOT_IMAGE;
    }
    while (i + 8 <= n) {
        uint32_t len = be32(b + i);
        const char *type = (const char *)(b + i + 4);

        if ((long)len > n || i + 12 + (long)len > n) {
            break;
        }
        if (png_crc(b + i + 4, (size_t)len + 4) !=
            be32(b + i + 8 + len)) {
            free(src->idat); return TIKU_IMG_BAD;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13u || i != 8 || b[i + 18] || b[i + 19] ||
                b[i + 20] > 1) { free(src->idat); return TIKU_IMG_BAD; }
            src->w = (int)be32(b + i + 8);
            src->h = (int)be32(b + i + 12);
            src->f.depth = b[i + 16];
            src->f.colour = b[i + 17];
            src->interlace = b[i + 20];
        } else if (memcmp(type, "PLTE", 4) == 0) {
            uint32_t k;

            src->npal = (int)(len / 3u);
            if (src->npal > 256) { src->npal = 256; }
            for (k = 0; k < (uint32_t)src->npal; k++) {
                src->pal[k] = 0xff000000u |
                         ((uint32_t)b[i + 8 + k * 3u] << 16) |
                         ((uint32_t)b[i + 9 + k * 3u] << 8) |
                         b[i + 10 + k * 3u];
            }
        } else if (memcmp(type, "tRNS", 4) == 0 && src->f.colour == 3) {
            uint32_t k;

            /* Per-entry alpha for the palette.  The other colour types'
             * tRNS is not read. */
            for (k = 0; k < len && k < 256u; k++) {
                src->pal[k] = (src->pal[k] & 0x00ffffffu) |
                         ((uint32_t)b[i + 8 + k] << 24);
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            unsigned char *grown = realloc(src->idat,
                                           (size_t)(src->ilen + len));

            if (grown == NULL) {
                free(src->idat);
                return TIKU_IMG_NO_MEMORY;
            }
            src->idat = grown;
            memcpy(src->idat + src->ilen, b + i + 8, len);
            src->ilen += (long)len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            src->ended = len == 0;
            break;
        }
        i += 12 + (long)len;
    }
    if (!src->ended || src->w <= 0 || src->h <= 0 || src->idat == NULL ||
        src->w > 30000 || src->h > 30000) {
        free(src->idat);
        return TIKU_IMG_BAD;
    }
    if ((size_t)src->w > TIKU_IMAGE_PIXELS_MAX / (size_t)src->h) {
        free(src->idat);
        return TIKU_IMG_BAD;
    }
    switch (src->f.colour) {
    case 0: src->f.chan = 1; break;
    case 2: src->f.chan = 3; break;
    case 3: src->f.chan = 1; break;
    case 4: src->f.chan = 2; break;
    case 6: src->f.chan = 4; break;
    default: free(src->idat); return TIKU_IMG_BAD;
    }
    /* Truecolour and alpha images are read at 8 bits a sample; PNG allows
     * the sub-byte depths to greyscale and palette images alone. */
    if ((src->f.colour == 2 || src->f.colour == 4 || src->f.colour == 6) &&
        src->f.depth != 8) {
        free(src->idat);
        return TIKU_IMG_UNSUPPORTED;
    }
    if (src->f.depth != 1 && src->f.depth != 2 && src->f.depth != 4 &&
        src->f.depth != 8) {
        free(src->idat);
        return TIKU_IMG_UNSUPPORTED;   /* 16 bits, or an undefined depth */
    }
    if (src->f.colour == 3 && src->npal == 0) {
        free(src->idat);
        return TIKU_IMG_BAD;
    }
    src->f.pal = src->pal;
    src->f.npal = src->npal;
    src->f.bpp = (src->f.chan * src->f.depth + 7) / 8;
    if (src->f.bpp < 1) { src->f.bpp = 1; }

    return TIKU_IMG_OK;
}

/**
 * @brief Decode the @p n PNG bytes at @p b into @p out: the chunks through
 *        png_source(), the pixel data inflated and unfiltered, Adam7 pass
 *        by pass.
 * @return png_source()'s code, BAD for a malformed stream, or NO_MEMORY
 *         for allocation refusal; @p out is freed unless the result is OK.
 */
static tiku_image_err_t
load_png(const unsigned char *b, long n, tiku_image_t *out)
{
    png_source_t src;
    unsigned char *raw = NULL;
    long rawlen, got;
    int p;
    tiku_image_err_t rc = png_source(b, n, &src);
    if (rc != TIKU_IMG_OK) return rc;
    rc = TIKU_IMG_BAD;
    /* An upper bound on the inflated rows: each row's bytes and filter
     * byte, doubled to cover Adam7's passes, whose rows round up apart. */
    rawlen = ((long)src.w * src.f.chan * src.f.depth / 8L + src.f.chan + 1L)
             * src.h + src.h + 64L;
    rawlen *= 2L;
    raw = malloc((size_t)rawlen);
    out->px = calloc((size_t)src.w * (size_t)src.h, sizeof *out->px);
    if (raw == NULL || out->px == NULL) {
        rc = TIKU_IMG_NO_MEMORY;
        goto done;
    }
    out->w = src.w;
    out->h = src.h;
    got = tiku_inflate_zlib(src.idat, src.ilen, raw, rawlen);
    if (got < 0) {
        goto done;
    }
    if (!src.interlace) {
        long consumed = png_pass(raw, got, src.w, src.h, &src.f, out, 0, 0, 1, 1);

        if (consumed < 0) {
            if (consumed == -2) rc = TIKU_IMG_NO_MEMORY;
            goto done;
        }
    } else {
        long used = 0;

        /* Adam7: seven sub-images, each with its own width and its own
         * filtered rows. */
        for (p = 0; p < 7; p++) {
            int x0 = adam7[p][0], y0 = adam7[p][1];
            int dx = adam7[p][2], dy = adam7[p][3];
            int pw = (src.w - x0 + dx - 1) / dx;
            int ph = (src.h - y0 + dy - 1) / dy;
            long k = png_pass(raw + used, got - used, pw, ph, &src.f, out,
                              x0, y0, dx, dy);

            if (k < 0) {
                if (k == -2) rc = TIKU_IMG_NO_MEMORY;
                goto done;
            }
            used += k;
        }
    }
    rc = TIKU_IMG_OK;
done:
    free(src.idat);
    free(raw);
    if (rc != TIKU_IMG_OK) {
        tiku_image_free(out);
    }
    return rc;
}

tiku_image_err_t
tiku_image_load(const char *path, tiku_image_t *out)
{
    unsigned char *b = NULL;
    long n;
    tiku_image_err_t rc;

    if (path == NULL || out == NULL) {
        return TIKU_IMG_BAD;
    }
    n = slurp(path, &b);
    if (n < 0) {
        return n == -2 ? TIKU_IMG_NO_MEMORY : TIKU_IMG_BAD;
    }
    rc = tiku_image_decode(b, (size_t)n, out);
    free(b);
    return rc;
}

tiku_image_err_t
tiku_image_decode(const void *bytes, size_t len, tiku_image_t *result)
{
    const unsigned char *b = bytes;
    /* Decoded into a temporary, so a failed decode leaves result as it
     * was. */
    tiku_image_t decoded = {0};
    tiku_image_t *out = &decoded;
    tiku_image_err_t rc;
    if (!b || !result || !len || len > TIKU_IMAGE_BYTES_MAX)
        return TIKU_IMG_BAD;
    long n = (long)len;
    if (n >= 2 && b[0] == 'B' && b[1] == 'M') {
        rc = load_bmp(b, n, out);
    } else if (n >= 8 && b[0] == 137 && b[1] == 'P') {
        rc = load_png(b, n, out);
    } else if (n >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) {
        int jw = 0, jh = 0, jrc;
        uint32_t *jpx = NULL;

        jrc = tiku_jpeg_decode(b, n, &jw, &jh, &jpx);
        if (jrc == 0) {
            out->w = jw;
            out->h = jh;
            out->px = jpx;
            rc = TIKU_IMG_OK;
        } else {
            rc = jrc == -2    ? TIKU_IMG_NO_MEMORY
                 : (jrc == 1) ? TIKU_IMG_UNSUPPORTED
                              : TIKU_IMG_BAD;
        }
    } else {
        rc = TIKU_IMG_NOT_IMAGE;
    }
    if (rc == TIKU_IMG_OK) *result = decoded;
    else tiku_image_free(&decoded);
    return rc;
}

void
tiku_image_free(tiku_image_t *im)
{
    if (im != NULL) {
        free(im->px);
        im->px = NULL;
        im->w = 0;
        im->h = 0;
    }
}

int
tiku_image_thumb(const tiku_image_t *src, int size,
                     tiku_image_t *out)
{
    int tw, th, ox, oy, y, x;

    if (src == NULL || out == NULL || src->px == NULL || size <= 0 ||
        src->w <= 0 || src->h <= 0) {
        return -1;
    }
    /* The longer side sets the scale, so the whole picture fits and keeps
     * its shape. */
    if (src->w >= src->h) {
        tw = size;
        th = (int)(((long)src->h * size + src->w / 2) / src->w);
    } else {
        th = size;
        tw = (int)(((long)src->w * size + src->h / 2) / src->h);
    }
    if (tw < 1) { tw = 1; }
    if (th < 1) { th = 1; }
    ox = (size - tw) / 2;
    oy = (size - th) / 2;

    out->w = size;
    out->h = size;
    out->px = calloc((size_t)size * (size_t)size, sizeof *out->px);
    if (out->px == NULL) {
        return -1;
    }
    /* calloc leaves the margins at 0, which is fully transparent. */
    for (y = 0; y < th; y++) {
        long sy = (long)y * src->h / th;

        for (x = 0; x < tw; x++) {
            long sx = (long)x * src->w / tw;

            out->px[(long)(y + oy) * size + (x + ox)] =
                src->px[sy * src->w + sx];
        }
    }
    return 0;
}
