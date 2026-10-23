/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_gdi_dib.c - a metafile's bitmaps: device-independent ones
 * of 1 to 32 bits, with a colour table, bit fields or run lengths, and
 * the JPEG and PNG files a DIB may carry whole; a WMF's one-bit device
 * bitmaps.  Each is cut to the part a record asks for and drawn over
 * the parallelogram its box maps to.
 *
 * Raster operations mixing a bitmap with what is under it cannot be
 * drawn as such; the usual ones become see-through pixels: white where
 * an AND leaves the picture alone, black where an OR or XOR does -- so
 * a mask and its picture, drawn one after the other, show the picture.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_gdi.h"

#include <stdlib.h>
#include <string.h>

#define PIXELS_MAX (32u << 20)      /* a bitmap's pixels, at most        */

#define SRCCOPY     0x00cc0020u
#define SRCPAINT    0x00ee0086u
#define SRCAND      0x008800c6u
#define SRCINVERT   0x00660046u
#define NOTSRCCOPY  0x00330008u
#define MERGEPAINT  0x00bb0226u
#define BLACKNESS   0x00000042u
#define WHITENESS   0x00ff0062u

/** @brief A DIB's header, read. */
typedef struct {
    int      w, h, top;             /* size; rows stored top first       */
    int      bpp, comp;             /* bits a pixel; BI_*                */
    size_t   table;                 /* where its colours start           */
    int      entry, ncolor;         /* a colour's bytes; how many        */
    uint32_t mask[3];               /* BI_BITFIELDS: red, green, blue    */
} dib_head_t;

/** @brief A bitmap decoded: its pixels, or a JPEG or PNG left whole. */
typedef struct {
    uint32_t            *px;        /* ARGB, top row first               */
    int                  w, h;
    int                  top;       /* its rows were stored top first    */
    const unsigned char *file;
    size_t               nfile;
} dib_img_t;

/** @brief @p d's header read into @p h.  @return 0, or -1 for one this
 *         does not know. */
static int
head(const gdi_dib_t *d, dib_head_t *h)
{
    size_t hs = gdi_u32(d->bmi, d->nbmi, 0), i;

    memset(h, 0, sizeof *h);
    if (hs == 12) {                 /* BITMAPCOREHEADER */
        h->w = (int)gdi_u16(d->bmi, d->nbmi, 4);
        h->h = (int)gdi_u16(d->bmi, d->nbmi, 6);
        h->bpp = (int)gdi_u16(d->bmi, d->nbmi, 10);
        h->entry = 3;
    } else if (hs >= 40 && hs <= d->nbmi) {
        h->w = gdi_i32(d->bmi, d->nbmi, 4);
        h->h = gdi_i32(d->bmi, d->nbmi, 8);
        h->bpp = (int)gdi_u16(d->bmi, d->nbmi, 14);
        h->comp = (int)gdi_u32(d->bmi, d->nbmi, 16);
        h->ncolor = (int)(gdi_u32(d->bmi, d->nbmi, 32) & 0xffff);
        h->entry = 4;
    } else {
        return -1;
    }
    h->top = h->h < 0;
    h->h = h->h < 0 ? -h->h : h->h;
    h->table = hs;
    if (h->comp == 3 || h->comp == 6) {     /* BI_BITFIELDS */
        for (i = 0; i < 3; i++) {
            h->mask[i] = gdi_u32(d->bmi, d->nbmi, hs >= 52 ? 40 + 4 * i
                                                           : hs + 4 * i);
        }
        h->table = hs >= 52 ? hs : hs + 12;
    }
    if (h->bpp <= 8 && (h->ncolor == 0 || h->ncolor > 1 << h->bpp)) {
        h->ncolor = 1 << h->bpp;
    }
    if (d->usage == 1) {            /* DIB_PAL_COLORS: indices */
        h->entry = 2;
    }
    return h->w > 0 && h->h > 0 &&
           (size_t)h->w * (size_t)h->h <= PIXELS_MAX ? 0 : -1;
}

/** @brief Colour @p i of @p d's table, as ARGB; black past its end. */
static uint32_t
table(gdi_t *g, const gdi_dib_t *d, const dib_head_t *h, int i)
{
    size_t at = h->table + (size_t)i * (size_t)h->entry;

    if (i >= h->ncolor || at + (size_t)h->entry > d->nbmi) {
        return 0xff000000u;
    }
    if (h->entry == 2) {
        return gdi_color(g, 0x01000000u | gdi_u16(d->bmi, d->nbmi, at));
    }
    return 0xff000000u | (uint32_t)d->bmi[at + 2] << 16 |
           (uint32_t)d->bmi[at + 1] << 8 | d->bmi[at];
}

/** @brief Field @p mask of pixel @p v as 0-255. */
static uint32_t
field(uint32_t v, uint32_t mask)
{
    if (mask == 0) {
        return 0;
    }
    while (!(mask & 1)) {
        mask >>= 1;
        v >>= 1;
    }
    return (uint32_t)((uint64_t)(v & mask) * 255 / mask);
}

/** @brief Pixel @p x of stored row @p r, as ARGB. */
static uint32_t
pixel(gdi_t *g, const gdi_dib_t *d, const dib_head_t *h,
      const unsigned char *r, int x)
{
    uint32_t v;

    switch (h->bpp) {
    case 1:
        return table(g, d, h, r[x >> 3] >> (7 - (x & 7)) & 1);
    case 4:
        return table(g, d, h, r[x >> 1] >> (x & 1 ? 0 : 4) & 15);
    case 8:
        return table(g, d, h, r[x]);
    case 16:
        v = (uint32_t)(r[2 * x] | r[2 * x + 1] << 8);
        if (h->comp != 3 && h->comp != 6) {
            return 0xff000000u | field(v, 0x7c00) << 16 |
                   field(v, 0x3e0) << 8 | field(v, 0x1f);
        }
        return 0xff000000u | field(v, h->mask[0]) << 16 |
               field(v, h->mask[1]) << 8 | field(v, h->mask[2]);
    case 24:
        return 0xff000000u | (uint32_t)r[3 * x + 2] << 16 |
               (uint32_t)r[3 * x + 1] << 8 | r[3 * x];
    default:
        v = (uint32_t)r[4 * x] | (uint32_t)r[4 * x + 1] << 8 |
            (uint32_t)r[4 * x + 2] << 16 | (uint32_t)r[4 * x + 3] << 24;
        if (h->comp == 3 || h->comp == 6) {
            return (v & 0xff000000u) | field(v, h->mask[0]) << 16 |
                   field(v, h->mask[1]) << 8 | field(v, h->mask[2]);
        }
        return v;                   /* the fourth byte kept as alpha */
    }
}

/** @brief Run-length rows (BI_RLE8, BI_RLE4) decoded into @p im; what
 *         they skip is left see-through. */
static void
runs(gdi_t *g, const gdi_dib_t *d, const dib_head_t *h, dib_img_t *im)
{
    const unsigned char *b = d->bits;
    size_t i = 0, n = d->nbits;
    int x = 0, y = 0, k, c, four = h->comp == 2;

    while (i + 1 < n && y < h->h) {
        k = b[i];
        c = b[i + 1];
        i += 2;
        if (k > 0) {                /* k pixels of c, or c's two nibbles */
            int j;

            for (j = 0; j < k; j++, x++) {
                if (x < h->w) {
                    im->px[(size_t)(h->h - 1 - y) * (size_t)h->w + (size_t)x]
                        = table(g, d, h, four ? (j & 1 ? c & 15 : c >> 4)
                                              : c);
                }
            }
            continue;
        }
        if (c == 0 || c == 1) {     /* the line's end; the bitmap's */
            x = 0;
            y = c == 0 ? y + 1 : h->h;
        } else if (c == 2) {        /* a jump */
            x += i < n ? b[i] : 0;
            y += i + 1 < n ? b[i + 1] : 0;
            i += 2;
        } else {                    /* c pixels as they are */
            for (k = 0; k < c && i + (size_t)(four ? k / 2 : k) < n; k++) {
                int v = four ? b[i + (size_t)k / 2] >> (k & 1 ? 0 : 4) & 15
                             : b[i + (size_t)k];

                if (x + k < h->w) {
                    im->px[(size_t)(h->h - 1 - y) * (size_t)h->w +
                           (size_t)(x + k)] = table(g, d, h, v);
                }
            }
            x += c;
            k = four ? (c + 1) / 2 : c;
            i += (size_t)(k + (k & 1));
        }
    }
}

/** @brief A WMF's one-bit device bitmap: set bits the ground's colour,
 *         clear ones the text's, as a colour device takes them. */
static int
mono(gdi_t *g, const gdi_dib_t *d, dib_img_t *im)
{
    int w = gdi_i16(d->bmi, d->nbmi, 2), h = gdi_i16(d->bmi, d->nbmi, 4);
    int stride = gdi_i16(d->bmi, d->nbmi, 6), x, y;
    const unsigned char *r;

    if (w <= 0 || h <= 0 || stride < (w + 7) / 8 ||
        (size_t)w * (size_t)h > PIXELS_MAX ||
        (g->pixels -= (long)w * h) < 0 ||
        (im->px = calloc((size_t)w * (size_t)h, sizeof *im->px)) == NULL) {
        return -1;
    }
    im->w = w;
    im->h = h;
    for (y = 0; y < h && (size_t)(y + 1) * (size_t)stride <= d->nbits;
         y++) {
        r = d->bits + (size_t)y * (size_t)stride;
        for (x = 0; x < w; x++) {
            im->px[(size_t)y * (size_t)w + (size_t)x] =
                r[x >> 3] >> (7 - (x & 7)) & 1 ? g->dc.bk : g->dc.text;
        }
    }
    return 0;
}

/** @brief Bitmap @p d decoded into @p im.  @return 0, or -1. */
static int
decode(gdi_t *g, const gdi_dib_t *d, dib_img_t *im)
{
    dib_head_t h;
    size_t stride;
    int x, y;

    memset(im, 0, sizeof *im);
    if (d->mono) {
        return mono(g, d, im);
    }
    if (head(d, &h) != 0) {
        return -1;
    }
    if (h.comp == 4 || h.comp == 5) {       /* BI_JPEG, BI_PNG */
        im->file = d->bits;
        im->nfile = d->nbits;
        return d->nbits > 0 ? 0 : -1;
    }
    if ((h.bpp != 1 && h.bpp != 4 && h.bpp != 8 && h.bpp != 16 &&
         h.bpp != 24 && h.bpp != 32) ||
        (g->pixels -= (long)h.w * h.h) < 0 ||  /* the file's share spent */
        (im->px = calloc((size_t)h.w * (size_t)h.h, 4)) == NULL) {
        return -1;
    }
    im->w = h.w;
    im->h = h.h;
    im->top = h.top;
    if (h.comp == 1 || h.comp == 2) {
        runs(g, d, &h, im);
        return 0;
    }
    stride = ((size_t)h.w * (size_t)h.bpp + 31) / 32 * 4;
    for (y = 0; y < h.h && (size_t)(y + 1) * stride <= d->nbits; y++) {
        const unsigned char *r = d->bits + (size_t)y * stride;
        uint32_t *o = im->px + (size_t)(h.top ? y : h.h - 1 - y) *
                               (size_t)h.w;

        for (x = 0; x < h.w; x++) {
            o[x] = pixel(g, d, &h, r, x);
        }
    }
    return 0;
}

/** @brief @p im cut to @p src (x y w h, pixels, y from the bottom when
 *         @p bottom); whole when it asks for all or none of it. */
static void
cut(dib_img_t *im, const double *src, int bottom)
{
    int x = (int)src[0], w = (int)src[2], h = (int)src[3], y, r;
    uint32_t *px;

    w = w < 0 ? -w : w;
    h = h < 0 ? -h : h;
    y = bottom ? im->h - (int)src[1] - h : (int)src[1];
    if (im->px == NULL || w == 0 || h == 0 || x < 0 || y < 0 ||
        x + w > im->w || y + h > im->h ||
        (x == 0 && y == 0 && w == im->w && h == im->h) ||
        (px = malloc((size_t)w * (size_t)h * 4)) == NULL) {
        return;
    }
    for (r = 0; r < h; r++) {
        memcpy(px + (size_t)r * (size_t)w,
               im->px + (size_t)(y + r) * (size_t)im->w + (size_t)x,
               (size_t)w * 4);
    }
    free(im->px);
    im->px = px;
    im->w = w;
    im->h = h;
}

/** @brief Channel @p c of a pixel whose alpha @p a was multiplied into
 *         it, as it was before. */
static uint32_t
unmul(uint32_t c, uint32_t a)
{
    c = c * 255 / a;
    return c > 255 ? 255 : c;
}

/** @brief @p im's pixels made as @p b says: opaque but for what its
 *         raster operation leaves alone, AlphaBlend's alpha, a colour
 *         TransparentBlt sees through. */
static void
blend(const gdi_blit_t *b, dib_img_t *im)
{
    size_t i, n = (size_t)im->w * (size_t)im->h;
    uint32_t p, a, rgb, key = (b->key & 0xff) << 16 | (b->key & 0xff00) |
                              (b->key >> 16 & 0xff);

    for (i = 0; i < n; i++) {
        p = im->px[i];
        rgb = p & 0xffffffu;
        a = b->alpha >= 0 && b->premul ? p >> 24 : 255;
        if (b->rop == NOTSRCCOPY || b->rop == MERGEPAINT) {
            rgb ^= 0xffffffu;
        }
        if (a > 0 && a < 255) {     /* multiplied in: taken out again */
            rgb = unmul(rgb >> 16 & 255, a) << 16 |
                  unmul(rgb >> 8 & 255, a) << 8 | unmul(rgb & 255, a);
        }
        if (b->alpha >= 0) {
            a = a * (uint32_t)b->alpha / 255;
        }
        if ((b->rop == SRCAND && rgb == 0xffffffu) ||
            ((b->rop == SRCPAINT || b->rop == SRCINVERT ||
              b->rop == MERGEPAINT) && rgb == 0) ||
            (b->keyed && rgb == key)) {
            a = 0;
        }
        im->px[i] = a << 24 | rgb;
    }
}

/** @brief Where @p mask is black, @p im see-through: MaskBlt's
 *         background leaves what is under it. */
static void
masked(gdi_t *g, const gdi_dib_t *mask, dib_img_t *im)
{
    dib_img_t m;
    int x, y, mx, my;

    if (mask == NULL || mask->nbmi == 0 || decode(g, mask, &m) != 0 ||
        m.px == NULL) {
        return;
    }
    for (y = 0; y < im->h; y++) {
        my = y < m.h ? y : m.h - 1;
        for (x = 0; x < im->w; x++) {
            mx = x < m.w ? x : m.w - 1;
            if ((m.px[(size_t)my * (size_t)m.w + (size_t)mx] & 0xffffffu)
                == 0) {
                im->px[(size_t)y * (size_t)im->w + (size_t)x] = 0;
            }
        }
    }
    free(m.px);
}

/** @brief Picture @p n over the parallelogram @p b names. */
static void
place(gdi_t *g, int n, const gdi_blit_t *b)
{
    double x0, y0, x1, y1, x2, y2;

    gdi_map(g, b->p[0], b->p[1], &x0, &y0);
    gdi_map(g, b->p[2], b->p[3], &x1, &y1);
    gdi_map(g, b->p[4], b->p[5], &x2, &y2);
    gdi_clip_apply(g);
    tiku_pdf_writer_save(g->w);
    tiku_pdf_writer_transform(g->w, x1 - x0, y1 - y0, x2 - x0, y2 - y0, x0,
                              y0);
    tiku_pdf_writer_draw(g->w, n, 0, 0, 1, 1);
    tiku_pdf_writer_restore(g->w);
    g->drawn = 1;
}

/** @brief No bitmap: the box filled with the brush, or black or white,
 *         as the raster operation says; nothing for any other operation. */
static void
fill(gdi_t *g, const gdi_blit_t *b)
{
    gdi_brush_t keep = g->dc.brush;
    gdi_pen_t pen = g->dc.pen;
    double p[8];

    if (b->rop == BLACKNESS || b->rop == WHITENESS) {
        memset(&g->dc.brush, 0, sizeof g->dc.brush);
        g->dc.brush.color = b->rop == BLACKNESS ? 0xff000000u : 0xffffffffu;
    } else if ((b->rop >> 16 & 0xff) != 0xf0 &&   /* PATCOPY */
               (b->rop >> 16 & 0xff) != 0xfb) {   /* PATPAINT */
        return;
    }
    p[0] = b->p[0];
    p[1] = b->p[1];
    p[2] = b->p[2];
    p[3] = b->p[3];
    p[4] = b->p[2] + b->p[4] - b->p[0];
    p[5] = b->p[3] + b->p[5] - b->p[1];
    p[6] = b->p[4];
    p[7] = b->p[5];
    g->dc.pen.style = 5;            /* PS_NULL */
    gdi_poly(g, p, 4, GDI_SHUT, 0);
    g->dc.brush = keep;
    g->dc.pen = pen;
}

void
gdi_blit(gdi_t *g, const gdi_dib_t *d, const gdi_blit_t *b,
         const gdi_dib_t *mask)
{
    dib_img_t im;
    int n;

    if (g->inpath || (b->rop & 0xffffffu) == 0x00aa0029u) {
        return;                     /* the destination left alone */
    }
    if (d == NULL || d->nbmi == 0 || b->rop == BLACKNESS ||
        b->rop == WHITENESS) {
        fill(g, b);
        return;
    }
    if (decode(g, d, &im) != 0) {
        return;
    }
    if (im.file != NULL) {
        n = tiku_pdf_writer_image(g->w, im.file, im.nfile, NULL, NULL);
    } else {
        cut(&im, b->src, b->from_bottom && !im.top);
        blend(b, &im);
        masked(g, mask, &im);
        n = tiku_pdf_writer_pixels(g->w, im.px, im.w, im.h);
        free(im.px);
    }
    if (n >= 0) {
        place(g, n, b);
    }
}

size_t
gdi_dib_size(const unsigned char *bmi, size_t n, int usage)
{
    gdi_dib_t d;
    dib_head_t h;
    size_t colors;

    memset(&d, 0, sizeof d);
    d.bmi = bmi;
    d.nbmi = n;
    d.usage = usage;
    if (head(&d, &h) != 0) {
        return 0;
    }
    colors = h.table + (size_t)h.ncolor * (size_t)h.entry;
    return colors <= n ? colors : 0;
}

uint32_t
gdi_dib_average(gdi_t *g, const gdi_dib_t *d)
{
    dib_img_t im;
    size_t i, n;
    uint64_t sum[3] = {0, 0, 0};

    if (decode(g, d, &im) != 0 || im.px == NULL) {
        return 0xff808080u;
    }
    n = (size_t)im.w * (size_t)im.h;
    for (i = 0; i < n; i++) {
        sum[0] += im.px[i] >> 16 & 255;
        sum[1] += im.px[i] >> 8 & 255;
        sum[2] += im.px[i] & 255;
    }
    free(im.px);
    return n == 0 ? 0xff808080u
                  : 0xff000000u | (uint32_t)(sum[0] / n) << 16 |
                    (uint32_t)(sum[1] / n) << 8 | (uint32_t)(sum[2] / n);
}
