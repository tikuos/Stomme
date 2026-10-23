/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jpx_jp2.c - a JP2 file (15444-1 Annex I) around its codestream:
 * the boxes that say what the components are -- a colour space, a
 * palette they index, which is opacity -- and the components themselves
 * made into the channels a picture is drawn from.
 *
 * Each channel comes out on the finest component's grid, a component
 * sampled more coarsely repeated to fill it; sYCC is turned to RGB here.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jpx.h"
#include "tiku_jpx_private.h"

#include <stdlib.h>
#include <string.h>

#define PAL_MAX 1024                /* palette entries, at most          */

/** @brief What a JP2 file says of its codestream. */
typedef struct {
    int                  space;     /* TIKU_JPX_GRAY ...                 */
    int                  npal, ncol;    /* palette entries and columns  */
    int                  pbits[TIKU_JPX_COMPS];  /* each column's bits  */
    uint32_t            *pal;       /* npal * ncol values                */
    int                  nmap;      /* channels the component map gives  */
    struct {
        int cmp, type, col;         /* a component, direct or by palette */
    } map[TIKU_JPX_COMPS];
    int                  ndef;      /* channel definitions               */
    struct {
        int ch, type, asoc;         /* colour, opacity; which colour     */
    } def[TIKU_JPX_COMPS];
    const unsigned char *cs;        /* the codestream                    */
    long                 csn;
} jp2_t;

/** @brief Big-endian 16 bits. */
static uint32_t
be16(const unsigned char *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

/** @brief Big-endian 32 bits. */
static uint32_t
be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

/**
 * @brief The box at @p p (I.4): its type into @p type and where its
 *        contents start into @p hdr.  @return its whole length, or -1
 *        when it does not fit.
 */
static long
box(const unsigned char *p, long n, uint32_t *type, long *hdr)
{
    uint64_t len;

    if (n < 8) {
        return -1;
    }
    len = be32(p);
    *type = be32(p + 4);
    *hdr = 8;
    if (len == 1) {                 /* the length in eight bytes */
        if (n < 16) {
            return -1;
        }
        len = (uint64_t)be32(p + 8) << 32 | be32(p + 12);
        *hdr = 16;
    } else if (len == 0) {          /* to the end of the file */
        len = (uint64_t)n;
    }
    return len < (uint64_t)*hdr || len > (uint64_t)n ? -1 : (long)len;
}

/** @brief The colour space an enumerated colr names (I.5.3.3). */
static int
enum_space(uint32_t e)
{
    switch (e) {
    case 16: case 20: case 21: return TIKU_JPX_SRGB;  /* sRGB, e-sRGB, ROMM */
    case 17: return TIKU_JPX_GRAY;
    case 18: case 24: return TIKU_JPX_SYCC;
    case 12: return TIKU_JPX_CMYK;
    default: return TIKU_JPX_OTHER;
    }
}

/** @brief A palette box (I.5.3.4): its entries, each column's value in
 *         as many bytes as its bits need.  @return 0, or -1. */
static int
palette(jp2_t *f, const unsigned char *p, long n)
{
    long at, e;
    int c;

    if (n < 3 || f->pal != NULL) {
        return -1;
    }
    f->npal = (int)be16(p);
    f->ncol = p[2];
    if (f->npal < 1 || f->npal > PAL_MAX || f->ncol < 1 ||
        f->ncol > TIKU_JPX_COMPS || n < 3 + f->ncol) {
        return -1;
    }
    for (c = 0; c < f->ncol; c++) {
        f->pbits[c] = (p[3 + c] & 0x7F) + 1;
        if (f->pbits[c] > 16) {
            return -1;
        }
    }
    f->pal = calloc((size_t)f->npal * (size_t)f->ncol, sizeof *f->pal);
    if (f->pal == NULL) {
        return -1;
    }
    for (at = 3 + f->ncol, e = 0; e < f->npal; e++) {
        for (c = 0; c < f->ncol; c++) {
            int nb = (f->pbits[c] + 7) / 8;

            if (at + nb > n) {
                return -1;
            }
            f->pal[e * f->ncol + c] = nb == 1 ? p[at] : be16(p + at);
            at += nb;
        }
    }
    return 0;
}

/** @brief The header box's children (I.5.3): colour, palette, the
 *         component map and the channel definitions. */
static int
jp2_header(jp2_t *f, const unsigned char *p, long n)
{
    while (n >= 8) {
        uint32_t type;
        long hdr, len = box(p, n, &type, &hdr), i;
        const unsigned char *c = p + hdr;
        long cn = len - hdr;

        if (len < 0) {
            return -1;
        }
        if (type == 0x636F6C72u && f->space == TIKU_JPX_UNKNOWN && cn >= 3) {
            f->space = p[hdr] == 1 && cn >= 7 ? enum_space(be32(c + 3))
                                                : TIKU_JPX_OTHER;
        } else if (type == 0x70636C72u && palette(f, c, cn) != 0) {
            return -1;
        } else if (type == 0x636D6170u) {           /* cmap */
            for (i = 0; i + 4 <= cn && f->nmap < TIKU_JPX_COMPS; i += 4) {
                f->map[f->nmap].cmp = (int)be16(c + i);
                f->map[f->nmap].type = c[i + 2];
                f->map[f->nmap++].col = c[i + 3];
            }
        } else if (type == 0x63646566u && cn >= 2) {  /* cdef */
            for (i = 0; i < (long)be16(c) && 2 + 6 * i + 6 <= cn &&
                        f->ndef < TIKU_JPX_COMPS; i++) {
                f->def[f->ndef].ch = (int)be16(c + 2 + 6 * i);
                f->def[f->ndef].type = (int)be16(c + 4 + 6 * i);
                f->def[f->ndef++].asoc = (int)be16(c + 6 + 6 * i);
            }
        }
        p += len;
        n -= len;
    }
    return 0;
}

/** @brief A JP2 file's boxes, or a bare codestream: where the
 *         codestream is and what its components are.  @return 0, -1. */
static int
jp2_parse(jp2_t *f, const unsigned char *p, long n)
{
    memset(f, 0, sizeof *f);
    if (n >= 2 && p[0] == 0xFF && p[1] == 0x4F) {
        f->cs = p;                  /* a bare codestream */
        f->csn = n;
        return 0;
    }
    while (n >= 8 && f->cs == NULL) {
        uint32_t type;
        long hdr, len = box(p, n, &type, &hdr);

        if (len < 0) {
            return -1;
        }
        if (type == 0x6A703268u && jp2_header(f, p + hdr, len - hdr) != 0) {
            return -1;
        }
        if (type == 0x6A703263u) {  /* jp2c: the (first) codestream */
            f->cs = p + hdr;
            f->csn = len - hdr;
        }
        p += len;
        n -= len;
    }
    return f->cs != NULL ? 0 : -1;
}

/** @brief The grid the channels are made on: the finest component's,
 *         so a component sampled more coarsely is spread over it. */
typedef struct {
    int64_t gx, gy;                 /* its sampling on the reference grid */
    int64_t x0, y0;                 /* its first sample's place          */
    long    w, h;                   /* its samples across and down       */
} grid_t;

/** @brief Component @p c's sampling of the reference grid, across and
 *         down, as decoded: its own, times the resolutions left out. */
static void
sampling(const jpx_t *j, int c, int64_t *sx, int64_t *sy)
{
    *sx = (int64_t)j->cm[c].dx << j->reduce;
    *sy = (int64_t)j->cm[c].dy << j->reduce;
}

/**
 * @brief A channel into @p out: component @p c of @p planes, or the
 *        palette column @p col it indexes, on grid @p g, eight bits kept
 *        when there are more.  @return its bits.
 */
static int
channel(const jpx_t *j, const jp2_t *f, uint16_t **planes, int c, int col,
        const grid_t *g, unsigned char *out)
{
    int64_t dx, dy, cx0, cy0;
    long cw, ch, x, y;
    int bits = col >= 0 ? f->pbits[col] : j->cm[c].prec, shift;

    sampling(j, c, &dx, &dy);
    cx0 = (j->xosiz + dx - 1) / dx;
    cy0 = (j->yosiz + dy - 1) / dy;
    cw = (long)((j->xsiz + dx - 1) / dx - cx0);
    ch = (long)((j->ysiz + dy - 1) / dy - cy0);
    shift = bits > 8 ? bits - 8 : 0;
    if (cw < 1 || ch < 1) {         /* sampled too coarsely to have any */
        memset(out, 0, (size_t)g->w * (size_t)g->h);
        return bits;
    }
    for (y = 0; y < g->h; y++) {
        long sy = (long)((g->y0 + y) * g->gy / dy - cy0);
        const uint16_t *row = planes[c] + (sy < 0 ? 0 : sy >= ch ? ch - 1
                                                                  : sy) * cw;

        for (x = 0; x < g->w; x++) {
            long sx = (long)((g->x0 + x) * g->gx / dx - cx0);
            uint32_t v = row[sx < 0 ? 0 : sx >= cw ? cw - 1 : sx];

            if (col >= 0) {
                v = v < (uint32_t)f->npal ? f->pal[v * f->ncol + col] : 0;
            }
            out[y * g->w + x] = (unsigned char)(v >> shift);
        }
    }
    return bits;
}

/** @brief Grid @p g for the @p n components the channels are made of:
 *         the finest sampling among them, across and down apart. */
static void
grid(const jpx_t *j, const jp2_t *f, int n, grid_t *g)
{
    int64_t sx, sy;
    int i;

    g->gx = g->gy = INT64_MAX;
    for (i = 0; i < n; i++) {
        int c = f->nmap > 0 ? f->map[i].cmp : i;

        if (c < j->ncomp && c < TIKU_JPX_COMPS) {
            sampling(j, c, &sx, &sy);
            g->gx = sx < g->gx ? sx : g->gx;
            g->gy = sy < g->gy ? sy : g->gy;
        }
    }
    if (g->gx == INT64_MAX) {
        sampling(j, 0, &g->gx, &g->gy);
    }
    g->x0 = (j->xosiz + g->gx - 1) / g->gx;
    g->y0 = (j->yosiz + g->gy - 1) / g->gy;
    g->w = (long)((j->xsiz + g->gx - 1) / g->gx - g->x0);
    g->h = (long)((j->ysiz + g->gy - 1) / g->gy - g->y0);
}

/** @brief The channels in the order the channel definitions give: the
 *         colours by what they are the colour of, then the opacity, then
 *         the rest. */
static void
reorder(const jp2_t *f, tiku_jpx_t *out)
{
    unsigned char *comp[TIKU_JPX_COMPS];
    int prec[TIKU_JPX_COMPS], n = 0, i, k, a;

    for (a = 1; a <= out->ncomp; a++) {         /* colours, by asoc */
        for (i = 0; i < f->ndef; i++) {
            k = f->def[i].ch;
            if (f->def[i].type == 0 && f->def[i].asoc == a && k < out->ncomp &&
                out->comp[k] != NULL && n < TIKU_JPX_COMPS) {
                comp[n] = out->comp[k];
                prec[n++] = out->prec[k];
                out->comp[k] = NULL;
            }
        }
    }
    for (i = 0; i < f->ndef; i++) {             /* the opacity */
        k = f->def[i].ch;
        if ((f->def[i].type == 1 || f->def[i].type == 2) && k < out->ncomp &&
            out->comp[k] != NULL && out->alpha < 0) {
            out->alpha = n;
            out->premultiplied = f->def[i].type == 2;
            comp[n] = out->comp[k];
            prec[n++] = out->prec[k];
            out->comp[k] = NULL;
        }
    }
    for (k = 0; k < out->ncomp; k++) {          /* whatever else */
        if (out->comp[k] != NULL) {
            comp[n] = out->comp[k];
            prec[n++] = out->prec[k];
        }
    }
    memcpy(out->comp, comp, (size_t)n * sizeof *comp);
    memcpy(out->prec, prec, (size_t)n * sizeof *prec);
}

/** @brief sYCC's first three channels to RGB (Annex I's YCbCr), when
 *         each has eight bits or had more. */
static void
sycc_to_rgb(tiku_jpx_t *out)
{
    long i, n = (long)out->w * out->h;

    if (out->ncomp < 3 || out->prec[0] < 8 || out->prec[1] < 8 ||
        out->prec[2] < 8) {
        return;
    }
    for (i = 0; i < n; i++) {
        float y = out->comp[0][i], cb = out->comp[1][i] - 128.0f;
        float cr = out->comp[2][i] - 128.0f;
        float rgb[3] = {y + 1.402f * cr, y - 0.344136f * cb - 0.714136f * cr,
                        y + 1.772f * cb};
        int k;

        for (k = 0; k < 3; k++) {
            float v = rgb[k] + 0.5f;

            out->comp[k][i] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
    out->space = TIKU_JPX_SRGB;
}

/** @brief The codestream's components made into @p out's channels: by
 *         the component map when there is one, else one each. */
static int
channels(const jpx_t *j, const jp2_t *f, uint16_t **planes, tiku_jpx_t *out)
{
    int n = f->nmap > 0 ? f->nmap : j->ncomp, i;
    grid_t g;

    n = n < TIKU_JPX_COMPS ? n : TIKU_JPX_COMPS;

    grid(j, f, n, &g);
    if (g.w < 1 || g.h < 1) {
        return -1;
    }
    out->w = (int)g.w;
    out->h = (int)g.h;
    out->ncomp = n;
    out->alpha = -1;
    for (i = 0; i < n; i++) {
        int c = f->nmap > 0 ? f->map[i].cmp : i;
        int col = f->nmap > 0 && f->map[i].type == 1 ? f->map[i].col : -1;

        if (c >= j->ncomp || c >= TIKU_JPX_COMPS ||
            (col >= 0 && (f->pal == NULL || col >= f->ncol)) ||
            (out->comp[i] = malloc((size_t)out->w * (size_t)out->h)) == NULL) {
            return -1;
        }
        out->prec[i] = channel(j, f, planes, c, col, &g, out->comp[i]);
    }
    out->space = f->space;
    if (out->space == TIKU_JPX_UNKNOWN) {       /* no colr: by count */
        out->space = n <= 2 ? TIKU_JPX_GRAY : n == 4 ? TIKU_JPX_CMYK
                                                    : TIKU_JPX_SRGB;
    }
    if (f->ndef > 0) {
        reorder(f, out);
    }
    if (out->space == TIKU_JPX_SYCC) {
        sycc_to_rgb(out);
    }
    return 0;
}

int
tiku_jpx_decode(const unsigned char *data, long n, long max_pixels,
                tiku_jpx_t *out)
{
    jp2_t f;
    jpx_t j;
    uint16_t *planes[TIKU_JPX_COMPS];
    int rc, c;

    memset(out, 0, sizeof *out);
    out->alpha = -1;
    memset(&f, 0, sizeof f);
    memset(&j, 0, sizeof j);
    memset(planes, 0, sizeof planes);
    if (data == NULL || n <= 0 || jp2_parse(&f, data, n) != 0) {
        free(f.pal);
        return -1;
    }
    rc = jpx_codestream(&j, f.cs, f.csn, max_pixels, planes);
    if (rc == 0) {
        rc = channels(&j, &f, planes, out);
    }
    for (c = 0; c < TIKU_JPX_COMPS; c++) {
        free(planes[c]);
    }
    jpx_done(&j);
    free(f.pal);
    if (rc != 0) {
        tiku_jpx_free(out);
    }
    return rc;
}

void
tiku_jpx_free(tiku_jpx_t *img)
{
    int c;

    if (img == NULL) {
        return;
    }
    for (c = 0; c < TIKU_JPX_COMPS; c++) {
        free(img->comp[c]);
        img->comp[c] = NULL;
    }
}
