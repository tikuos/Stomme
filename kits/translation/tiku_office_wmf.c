/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_wmf.c - a Windows Metafile's records played into the
 * device context of tiku_office_gdi.c.
 *
 * A WMF names no device: its window is mapped onto the box it is drawn
 * in.  The window starts as the placeable header's bounds, else the first
 * window the records set, else the bounds of what they draw; records may
 * set it again.  Objects go in the lowest free slot of the table, as GDI
 * puts them: records name objects by that index.  A WMF made from an EMF
 * carries the EMF in comments; when all of it is there, the EMF is drawn
 * in place of the records.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_gdi.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EMF_MAX (32u << 20)         /* an EMF carried inside, at most    */

/** @brief One record: its bytes, its size in them. */
typedef struct {
    const unsigned char *p;
    size_t               n;
} rec_t;

/** @brief A WMF being played: the player, and where free slots start. */
typedef struct {
    gdi_t g;
    int   free;                     /* no slot below this one is free    */
} wmf_t;

/** @brief Parameter @p k of record @p r, signed. */
static int
pi(const rec_t *r, size_t k)
{
    return gdi_i16(r->p, r->n, 6 + 2 * k);
}

/** @brief Parameters @p k and @p k + 1 as one unsigned 32-bit number. */
static uint32_t
pu(const rec_t *r, size_t k)
{
    return gdi_u32(r->p, r->n, 6 + 2 * k);
}

/** @brief A box of parameters from @p k, stored bottom, right, top,
 *         left, into @p b (l t r b). */
static void
box_at(const rec_t *r, size_t k, double *b)
{
    b[0] = pi(r, k + 3);
    b[1] = pi(r, k + 2);
    b[2] = pi(r, k + 1);
    b[3] = pi(r, k);
}

/* ------------------------------------------------------------ objects  */

/** @brief The lowest free slot of the table, emptied.  NULL when full. */
static gdi_obj_t *
slot(wmf_t *m)
{
    int i;

    for (i = m->free; i < GDI_OBJECTS; i++) {
        if (i >= m->g.nobj || m->g.obj[i].kind == GDI_NONE) {
            m->free = i + 1;
            return gdi_slot(&m->g, i);
        }
    }
    return NULL;
}

/** @brief A LOGFONT's name, 32 bytes of the system's code page at @p at,
 *         into @p f as UTF-8. */
static void
face_name(const rec_t *r, size_t at, gdi_font_t *f)
{
    gdi_font_t plain;
    size_t i, k = 0;

    memset(&plain, 0, sizeof plain);
    for (i = 0; i < 32 && at + i < r->n && r->p[at + i] &&
                k + 4 < sizeof f->face; i++) {
        k += gdi_utf8(gdi_char(&plain, r->p[at + i]), f->face + k);
    }
    f->face[k] = '\0';
}

/** @brief A region object's boxes, into a list to free().  @return it,
 *         with how many in @p n; its bounds alone when it has no scans. */
static double *
region(const rec_t *r, int *n)
{
    unsigned scans = gdi_u16(r->p, r->n, 16), s, c, i;
    size_t at = 28, cap = 1, k = 0;
    double *b;

    for (s = 0, i = 28; s < scans && i + 6 <= r->n; s++) {
        c = gdi_u16(r->p, r->n, i);
        cap += c / 2;
        i += 8 + 2 * (size_t)c;
    }
    *n = 0;
    if (cap > 1u << 20 || (b = malloc(cap * 4 * sizeof *b)) == NULL) {
        return NULL;
    }
    for (s = 0; s < scans && at + 6 <= r->n; s++) {
        c = gdi_u16(r->p, r->n, at);
        for (i = 0; i + 1 < c && k + 1 < cap; i += 2) {
            b[4 * k] = gdi_i16(r->p, r->n, at + 6 + 2 * i);
            b[4 * k + 1] = gdi_i16(r->p, r->n, at + 2);
            b[4 * k + 2] = gdi_i16(r->p, r->n, at + 8 + 2 * i);
            b[4 * k + 3] = gdi_i16(r->p, r->n, at + 4);
            k++;
        }
        at += 8 + 2 * (size_t)c;
    }
    if (k == 0) {                   /* no scans: its bounds */
        b[0] = gdi_i16(r->p, r->n, 20);
        b[1] = gdi_i16(r->p, r->n, 22);
        b[2] = gdi_i16(r->p, r->n, 24);
        b[3] = gdi_i16(r->p, r->n, 26);
        k = 1;
    }
    *n = (int)k;
    return b;
}

/** @brief The DIB of a record from byte @p at to its end, into @p d. */
static void
dib_at(const rec_t *r, size_t at, int usage, gdi_dib_t *d)
{
    size_t head;

    memset(d, 0, sizeof *d);
    d->usage = usage;
    if (at >= r->n) {
        return;
    }
    head = gdi_dib_size(r->p + at, r->n - at, usage);
    if (head > 0 && head < r->n - at) {
        d->bmi = r->p + at;
        d->nbmi = head;
        d->bits = r->p + at + head;
        d->nbits = r->n - at - head;
    }
}

/** @brief A record that makes an object.  @return 1 when it was one. */
static int
make(wmf_t *m, unsigned fn, const rec_t *r)
{
    gdi_t *g = &m->g;
    gdi_obj_t *o;
    gdi_dib_t d;
    int i, n;

    if (fn != 0x02fa && fn != 0x02fb && fn != 0x02fc && fn != 0x0142 &&
        fn != 0x01f9 && fn != 0x00f7 && fn != 0x06ff) {
        return 0;
    }
    if ((o = slot(m)) == NULL) {
        return 1;
    }
    o->kind = GDI_OTHER;
    if (fn == 0x02fa) {             /* CREATEPENINDIRECT */
        o->kind = GDI_PEN;
        o->u.pen.style = pi(r, 0) & 0xffff;
        o->u.pen.width = pi(r, 1) > 0 ? pi(r, 1) : 0;
        o->u.pen.cosmetic = pi(r, 1) <= 1;
        o->u.pen.color = gdi_color(g, pu(r, 3));
    } else if (fn == 0x02fc) {      /* CREATEBRUSHINDIRECT */
        o->kind = GDI_BRUSH;
        o->u.brush.style = pi(r, 0);
        o->u.brush.color = gdi_color(g, pu(r, 1));
        o->u.brush.hatch = pi(r, 3);
    } else if (fn == 0x0142 || fn == 0x01f9) {  /* pattern brushes */
        o->kind = GDI_BRUSH;
        if (fn == 0x0142) {
            dib_at(r, 10, pi(r, 1) == 1, &d);
        } else {
            memset(&d, 0, sizeof d);
            d.mono = 1;
            d.bmi = r->p + 6;
            d.nbmi = r->n > 16 ? 10 : 0;
            d.bits = r->p + (r->n > 38 ? 38 : r->n);
            d.nbits = r->n > 38 ? r->n - 38 : 0;
        }
        o->u.brush.color = d.nbmi ? gdi_dib_average(g, &d) : 0xff808080u;
    } else if (fn == 0x02fb) {      /* CREATEFONTINDIRECT */
        o->kind = GDI_FONT;
        o->u.font.height = pi(r, 0);
        o->u.font.width = abs(pi(r, 1));
        o->u.font.escape = pi(r, 2);
        o->u.font.weight = pi(r, 4);
        o->u.font.italic = r->n > 16 && r->p[16];
        o->u.font.underline = r->n > 17 && r->p[17];
        o->u.font.strike = r->n > 18 && r->p[18];
        o->u.font.charset = r->n > 19 ? r->p[19] : 0;
        face_name(r, 24, &o->u.font);
    } else if (fn == 0x00f7) {      /* CREATEPALETTE */
        n = (int)gdi_u16(r->p, r->n, 8);
        n = r->n < 10 ? 0 : n < (int)((r->n - 10) / 4) ? n
                                                       : (int)((r->n - 10) / 4);
        if ((o->u.pal.c = malloc((size_t)(n > 0 ? n : 1) * 4)) != NULL) {
            o->kind = GDI_PALETTE;
            o->u.pal.n = n;
            for (i = 0; i < n; i++) {
                o->u.pal.c[i] = gdi_color(g, gdi_u32(r->p, r->n, 10 + 4 *
                                                     (size_t)i) & 0xffffffu);
            }
        }
    } else {                        /* CREATEREGION */
        if ((o->u.rgn.r = region(r, &o->u.rgn.n)) != NULL) {
            o->kind = GDI_REGION;
        }
    }
    return 1;
}

/** @brief Object @p i, a region: its boxes and how many.  NULL for
 *         anything else. */
static const double *
region_of(const gdi_t *g, int i, int *n)
{
    if (i < 0 || i >= g->nobj || g->obj[i].kind != GDI_REGION) {
        *n = 0;
        return NULL;
    }
    *n = g->obj[i].u.rgn.n;
    return g->obj[i].u.rgn.r;
}

/** @brief A record that picks, deletes or uses an object.  @return 1
 *         when it was one. */
static int
use(wmf_t *m, unsigned fn, const rec_t *r)
{
    gdi_t *g = &m->g;
    const double *rg;
    gdi_brush_t b;
    int n, i = pi(r, 0) & 0xffff;

    switch (fn) {
    case 0x012d: case 0x0234:       /* SELECTOBJECT, SELECTPALETTE */
        gdi_select(g, (uint32_t)i);
        break;
    case 0x01f0:                    /* DELETEOBJECT */
        gdi_delete(g, i);
        m->free = i < m->free ? i : m->free;
        break;
    case 0x012c:                    /* SELECTCLIPREGION */
        rg = region_of(g, i, &n);
        gdi_clip_boxes(g, rg, n, GDI_RGN_COPY, 0);
        break;
    case 0x0228: case 0x0429: case 0x012b:  /* Fill-, Frame-, PaintRegion */
        rg = region_of(g, i, &n);
        b = g->dc.brush;
        if (fn != 0x012b && pi(r, 1) >= 0 && pi(r, 1) < g->nobj &&
            g->obj[pi(r, 1)].kind == GDI_BRUSH) {
            b = g->obj[pi(r, 1)].u.brush;
        }
        gdi_boxes(g, rg, n, &b, 0, fn == 0x0429 ? fmax(pi(r, 3), 1) : 0);
        break;
    default:
        return 0;
    }
    return 1;
}

/* -------------------------------------------------------------- state  */

/** @brief A record that sets the state.  @return 1 when it was one. */
static int
state(wmf_t *m, unsigned fn, const rec_t *r)
{
    gdi_t *g = &m->g;
    gdi_dc_t *dc = &g->dc;

    switch (fn) {
    case 0x001e: gdi_save(g);                              break;
    case 0x0127: gdi_restore(g, pi(r, 0));                 break;
    case 0x0102: dc->bkmode = pi(r, 0);                    break;
    case 0x0104: dc->rop2 = pi(r, 0);                      break;
    case 0x0106: dc->fill = pi(r, 0);                      break;
    case 0x0108: dc->extra = pi(r, 0);                     break;
    case 0x012e: dc->align = pi(r, 0) & 0xffff;            break;
    case 0x0201: dc->bk = gdi_color(g, pu(r, 0));          break;
    case 0x0209: dc->text = gdi_color(g, pu(r, 0));        break;
    case 0x0103:                    /* SETMAPMODE: square or not */
        dc->map = pi(r, 0) == GDI_MM_ISOTROPIC ? GDI_MM_ISOTROPIC
                                               : GDI_MM_ANISOTROPIC;
        gdi_moved(g);
        break;
    case 0x020b: dc->wox = pi(r, 1); dc->woy = pi(r, 0); gdi_moved(g); break;
    case 0x020c: dc->wex = pi(r, 1); dc->wey = pi(r, 0); gdi_moved(g); break;
    case 0x020f:                    /* OFFSETWINDOWORG */
        dc->wox += pi(r, 1);
        dc->woy += pi(r, 0);
        gdi_moved(g);
        break;
    case 0x0410:                    /* SCALEWINDOWEXT */
        if (pi(r, 0) != 0 && pi(r, 2) != 0) {
            dc->wex = dc->wex * pi(r, 3) / pi(r, 2);
            dc->wey = dc->wey * pi(r, 1) / pi(r, 0);
            gdi_moved(g);
        }
        break;
    case 0x0214:                    /* MOVETO */
        dc->x = pi(r, 1);
        dc->y = pi(r, 0);
        g->fig = 0;
        break;
    default:
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------- shapes  */

/** @brief Points of a record: @p n pairs from byte @p at, into a list to
 *         free(); NULL when the record does not hold them. */
static double *
points(const rec_t *r, size_t at, int n)
{
    double *p;
    int i;

    if (n <= 0 || at > r->n || (size_t)n > (r->n - at) / 4 ||
        (p = malloc((size_t)n * 2 * sizeof *p)) == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        p[2 * i] = gdi_i16(r->p, r->n, at + 4 * (size_t)i);
        p[2 * i + 1] = gdi_i16(r->p, r->n, at + 4 * (size_t)i + 2);
    }
    return p;
}

/** @brief POLYPOLYGON: the counts, then each polygon's points. */
static void
polypolygon(gdi_t *g, const rec_t *r)
{
    int n = pi(r, 0) & 0xffff, i, c, k = 0;
    long total = 0;
    double *p;

    for (i = 0; i < n && total <= (long)(r->n / 4); i++) {
        total += pi(r, 1 + (size_t)i) & 0xffff;
    }
    if (total > (long)(r->n / 4) ||   /* more than the record holds */
        (p = points(r, 8 + 2 * (size_t)n, (int)total)) == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        c = pi(r, 1 + (size_t)i) & 0xffff;
        c = c < (int)total - k ? c : (int)total - k;
        gdi_poly(g, p + 2 * k, c, GDI_SHUT | GDI_MORE, 0);
        k += c;
    }
    gdi_paint(g, GDI_FILL | GDI_STROKE);
    free(p);
}

/** @brief A record that draws a shape or clips to a box.  @return 1 when
 *         it was one. */
static int
shapes(gdi_t *g, unsigned fn, const rec_t *r)
{
    double b[4], p[2], q[2], *pts;

    switch (fn) {
    case 0x0213:                    /* LINETO */
        if (!g->fig || g->npt == 0) {
            gdi_move(g, g->dc.x, g->dc.y);
        }
        gdi_line(g, pi(r, 1), pi(r, 0));
        g->dc.x = pi(r, 1);
        g->dc.y = pi(r, 0);
        gdi_paint(g, GDI_STROKE);
        break;
    case 0x041b: case 0x0418:       /* RECTANGLE, ELLIPSE */
        box_at(r, 0, b);
        if (fn == 0x041b) {
            gdi_rect(g, b[0], b[1], b[2], b[3]);
        } else {
            gdi_ellipse(g, b[0], b[1], b[2], b[3]);
        }
        break;
    case 0x061c:                    /* ROUNDRECT */
        box_at(r, 2, b);
        gdi_round(g, b, pi(r, 1), pi(r, 0));
        break;
    case 0x0817: case 0x081a: case 0x0830:  /* ARC, PIE, CHORD */
        box_at(r, 4, b);
        q[0] = pi(r, 1);
        q[1] = pi(r, 0);
        p[0] = pi(r, 3);
        p[1] = pi(r, 2);
        gdi_arc(g, b, p, q, fn == 0x0817 ? 0 : fn == 0x0830 ? 2 : 3);
        break;
    case 0x0324: case 0x0325:       /* POLYGON, POLYLINE */
        if ((pts = points(r, 8, pi(r, 0) & 0xffff)) != NULL) {
            gdi_poly(g, pts, pi(r, 0) & 0xffff,
                     fn == 0x0324 ? GDI_SHUT : 0, 0);
            free(pts);
        }
        break;
    case 0x0538:
        polypolygon(g, r);
        break;
    case 0x0415: case 0x0416:       /* EXCLUDE-, INTERSECTCLIPRECT */
        box_at(r, 0, b);
        gdi_clip_boxes(g, b, 1, fn == 0x0416 ? GDI_RGN_AND : GDI_RGN_DIFF,
                       0);
        break;
    default:
        return 0;
    }
    return 1;
}

/* --------------------------------------------------------------- text  */

/** @brief @p n bytes of text from byte @p at drawn at @p pt, their
 *         advances from byte @p dx when the record holds them. */
static void
text(gdi_t *g, const rec_t *r, size_t at, int n, const double *pt,
     int opts, const double *rect, size_t dx)
{
    gdi_text_t t;
    uint32_t *ch;
    double *adv = NULL;
    int i;

    memset(&t, 0, sizeof t);
    t.x = pt[0];
    t.y = pt[1];
    t.opts = opts & (GDI_ETO_OPAQUE | GDI_ETO_CLIPPED);
    if (rect != NULL) {
        memcpy(t.rect, rect, sizeof t.rect);
    }
    if (n <= 0 || at > r->n || (size_t)n > r->n - at ||
        (ch = malloc((size_t)n * sizeof *ch)) == NULL) {
        gdi_text(g, &t);            /* its box, if it fills one */
        return;
    }
    for (i = 0; i < n; i++) {
        ch[i] = gdi_char(&g->dc.font, r->p[at + (size_t)i]);
    }
    if (dx > 0 && dx <= r->n && (size_t)n <= (r->n - dx) / 2 &&
        (adv = malloc((size_t)n * sizeof *adv)) != NULL) {
        for (i = 0; i < n; i++) {
            adv[i] = gdi_i16(r->p, r->n, dx + 2 * (size_t)i);
        }
    }
    t.ch = ch;
    t.n = n;
    t.dx = adv;
    gdi_text(g, &t);
    free(adv);
    free(ch);
}

/** @brief TEXTOUT and EXTTEXTOUT.  @return 1 when it was one. */
static int
texts(gdi_t *g, unsigned fn, const rec_t *r)
{
    double pt[2], rect[4];
    int n, opts, clip;
    size_t at;

    if (fn == 0x0521) {             /* TEXTOUT: length, string, y, x */
        n = pi(r, 0) & 0xffff;
        at = 8 + (size_t)((n + 1) & ~1);
        pt[0] = gdi_i16(r->p, r->n, at + 2);
        pt[1] = gdi_i16(r->p, r->n, at);
        text(g, r, 8, n, pt, 0, NULL, 0);
        return 1;
    }
    if (fn != 0x0a32) {
        return 0;
    }
    pt[1] = pi(r, 0);               /* EXTTEXTOUT: y, x, length, options */
    pt[0] = pi(r, 1);
    n = pi(r, 2) & 0xffff;
    opts = pi(r, 3) & 0xffff;
    clip = (opts & (GDI_ETO_OPAQUE | GDI_ETO_CLIPPED)) != 0;
    rect[0] = pi(r, 4);
    rect[1] = pi(r, 5);
    rect[2] = pi(r, 6);
    rect[3] = pi(r, 7);
    at = clip ? 22 : 14;
    text(g, r, at, n, pt, opts, clip ? rect : NULL,
         at + (size_t)((n + 1) & ~1));
    return 1;
}

/* ------------------------------------------------------------ bitmaps  */

/** @brief The box a blit draws in, from four parameters from @p k:
 *         height, width, y, x. */
static void
blit_box(const rec_t *r, size_t k, gdi_blit_t *b)
{
    double h = pi(r, k), w = pi(r, k + 1), y = pi(r, k + 2),
           x = pi(r, k + 3);

    b->p[0] = x;
    b->p[1] = y;
    b->p[2] = x + w;
    b->p[3] = y;
    b->p[4] = x;
    b->p[5] = y + h;
}

/** @brief A WMF's device bitmap from byte @p at, into @p d: one bit a
 *         pixel only. */
static void
bitmap16(const rec_t *r, size_t at, gdi_dib_t *d)
{
    memset(d, 0, sizeof *d);
    if (at + 10 <= r->n && r->p[at + 8] == 1 && r->p[at + 9] == 1) {
        d->mono = 1;
        d->bmi = r->p + at;
        d->nbmi = 10;
        d->bits = r->p + at + 10;
        d->nbits = r->n - at - 10;
    }
}

/** @brief A record that draws a bitmap, or a box with a raster
 *         operation.  @return 1 when it was one. */
static int
bitmaps(gdi_t *g, unsigned fn, const rec_t *r)
{
    int bare = r->n == ((fn >> 8) + 3) * 2;    /* no bitmap with it */
    int stretch = fn == 0x0b23 || fn == 0x0b41;
    gdi_blit_t b;
    gdi_dib_t d;

    memset(&b, 0, sizeof b);
    memset(&d, 0, sizeof d);
    b.alpha = -1;
    b.rop = pu(r, 0);
    if (fn == 0x061d) {             /* PATBLT */
        blit_box(r, 2, &b);
    } else if (fn == 0x0922 || fn == 0x0b23 || fn == 0x0940 ||
               fn == 0x0b41) {      /* BITBLT, STRETCHBLT, their DIBs */
        size_t k = stretch ? 2 : 0, rest = bare ? 1 : 0;

        b.src[3] = stretch ? pi(r, 2) : pi(r, 4 + rest);
        b.src[2] = stretch ? pi(r, 3) : pi(r, 5 + rest);
        b.src[1] = pi(r, 2 + k);
        b.src[0] = pi(r, 3 + k);
        blit_box(r, 4 + k + rest, &b);
        if (!bare && (fn == 0x0922 || fn == 0x0b23)) {
            bitmap16(r, 6 + 2 * (8 + k), &d);
        } else if (!bare) {
            dib_at(r, 6 + 2 * (8 + k), 0, &d);
        }
    } else if (fn == 0x0f43) {      /* STRETCHDIB */
        b.src[3] = pi(r, 3);
        b.src[2] = pi(r, 4);
        b.src[1] = pi(r, 5);
        b.src[0] = pi(r, 6);
        b.from_bottom = 1;
        blit_box(r, 7, &b);
        dib_at(r, 28, pi(r, 2) == 1, &d);
    } else if (fn == 0x0d33) {      /* SETDIBTODEV */
        b.rop = 0x00cc0020u;
        b.src[1] = pi(r, 3);
        b.src[0] = pi(r, 4);
        b.from_bottom = 1;
        blit_box(r, 5, &b);
        b.src[3] = pi(r, 5);
        b.src[2] = pi(r, 6);
        dib_at(r, 24, pi(r, 0) == 1, &d);
    } else {
        return 0;
    }
    gdi_blit(g, d.nbmi ? &d : NULL, &b, NULL);
    return 1;
}

/* -------------------------------------------------------------- start  */

/** @brief Where the records begin, after the headers; the placeable
 *         header's bounds into @p bound (all 0 without one).  0 for no
 *         WMF. */
static size_t
header(const unsigned char *b, size_t n, double *bound)
{
    size_t at = 0;
    int i;

    memset(bound, 0, 4 * sizeof *bound);
    if (gdi_u32(b, n, 0) == 0x9ac6cdd7u) {
        for (i = 0; i < 4; i++) {
            bound[i] = gdi_i16(b, n, 6 + 2 * (size_t)i);
        }
        at = 22;
    }
    if ((gdi_u16(b, n, at) != 1 && gdi_u16(b, n, at) != 2) ||
        gdi_u16(b, n, at + 2) < 9) {
        return 0;
    }
    return at + 2 * gdi_u16(b, n, at + 2);
}

/** @brief Bounds @p lo, @p hi grown to hold (@p x, @p y). */
static void
grow(double *lo, double *hi, double x, double y)
{
    lo[0] = fmin(lo[0], x);
    lo[1] = fmin(lo[1], y);
    hi[0] = fmax(hi[0], x);
    hi[1] = fmax(hi[1], y);
}

/** @brief The bounds of the lines, polygons, rectangles and ellipses the
 *         records at @p at draw, into @p bound: the window for a WMF that
 *         sets none. */
static void
measure(const unsigned char *b, size_t n, size_t at, double *bound)
{
    double lo[2] = {1e9, 1e9}, hi[2] = {-1e9, -1e9};
    rec_t r;
    unsigned fn;
    size_t k, np;

    for (; at + 6 <= n; at += r.n) {
        r.p = b + at;
        r.n = 2 * (size_t)gdi_u32(b, n, at);
        fn = gdi_u16(b, n, at + 4);
        if (r.n < 6 || r.n > n - at || fn == 0) {
            break;
        }
        if (fn == 0x0324 || fn == 0x0325) {     /* x y pairs */
            np = (size_t)(pi(&r, 0) & 0xffff);
            for (k = 0; k < np && 10 + 4 * k <= r.n; k++) {
                grow(lo, hi, pi(&r, 1 + 2 * k), pi(&r, 2 + 2 * k));
            }
        } else if (fn == 0x0213 || fn == 0x0214 || fn == 0x041b ||
                   fn == 0x0418) {      /* y x pairs */
            np = fn == 0x0213 || fn == 0x0214 ? 1 : 2;
            for (k = 0; k < np; k++) {
                grow(lo, hi, pi(&r, 2 * k + 1), pi(&r, 2 * k));
            }
        }
    }
    if (hi[0] > lo[0] && hi[1] > lo[1]) {
        bound[0] = lo[0];
        bound[1] = lo[1];
        bound[2] = hi[0];
        bound[3] = hi[1];
    }
}

/** @brief The first window the records at @p at set, into @p bound.
 *  @return 1 when they set one. */
static int
first_window(const unsigned char *b, size_t n, size_t at, double *bound)
{
    int org = 0, ext = 0;
    size_t sz;
    unsigned fn;

    for (; at + 6 <= n && !ext; at += sz) {
        sz = 2 * (size_t)gdi_u32(b, n, at);
        fn = gdi_u16(b, n, at + 4);
        if (sz < 6 || sz > n - at || fn == 0) {
            break;
        }
        if (fn == 0x020b && !org) {
            bound[0] = gdi_i16(b, n, at + 8);
            bound[1] = gdi_i16(b, n, at + 6);
            org = 1;
        } else if (fn == 0x020c) {
            bound[2] = gdi_i16(b, n, at + 8);
            bound[3] = gdi_i16(b, n, at + 6);
            ext = 1;
        }
    }
    if (ext) {
        bound[2] += org ? bound[0] : 0;
        bound[3] += org ? bound[1] : 0;
        bound[0] = org ? bound[0] : 0;
        bound[1] = org ? bound[1] : 0;
    }
    return ext && bound[2] != bound[0] && bound[3] != bound[1];
}

/** @brief The EMF a WMF made from one carries in its comments, whole,
 *         into memory to free().  NULL when it has none, or not all. */
static unsigned char *
inner_emf(const unsigned char *b, size_t n, size_t at, size_t *len)
{
    unsigned char *e = NULL;
    size_t sz, got = 0, total = 0, chunk;

    for (; at + 6 <= n; at += sz) {
        sz = 2 * (size_t)gdi_u32(b, n, at);
        if (sz < 6 || sz > n - at || gdi_u16(b, n, at + 4) == 0) {
            break;
        }
        if (gdi_u16(b, n, at + 4) != 0x0626 || gdi_u16(b, n, at + 6) != 15 ||
            gdi_u32(b, n, at + 10) != 0x43464d57u) {    /* MFCOMMENT WMFC */
            continue;
        }
        chunk = gdi_u32(b, n, at + 32);
        if (e == NULL) {
            total = gdi_u32(b, n, at + 40);
            if (total == 0 || total > EMF_MAX ||
                (e = malloc(total)) == NULL) {
                return NULL;
            }
        }
        if (chunk > total - got || 44 + chunk > sz) {
            break;
        }
        memcpy(e + got, b + at + 44, chunk);
        got += chunk;
    }
    if (e != NULL && got == total) {
        *len = total;
        return e;
    }
    free(e);
    return NULL;
}

/** @brief The records from @p at played; @p bound the window they
 *         start with. */
static void
play(wmf_t *m, const unsigned char *b, size_t n, size_t at,
     const double *bound)
{
    gdi_dc_t *dc = &m->g.dc;
    unsigned fn;
    rec_t r;

    dc->map = GDI_MM_ANISOTROPIC;
    dc->wox = bound[0];
    dc->woy = bound[1];
    dc->wex = bound[2] - bound[0];
    dc->wey = bound[3] - bound[1];
    gdi_moved(&m->g);
    for (; at + 6 <= n && m->g.budget > 0; at += r.n) {
        r.p = b + at;
        r.n = 2 * (size_t)gdi_u32(b, n, at);
        fn = gdi_u16(b, n, at + 4);
        if (r.n < 6 || r.n > n - at || fn == 0) {   /* META_EOF */
            break;
        }
        if (!state(m, fn, &r) && !make(m, fn, &r) && !use(m, fn, &r) &&
            !shapes(&m->g, fn, &r) && !texts(&m->g, fn, &r)) {
            (void)bitmaps(&m->g, fn, &r);
        }
    }
}

int
of_wmf_draw(const unsigned char *b, size_t n, of_faces_t *fs,
            tiku_pdf_writer_t *w, const double *box)
{
    double bound[4];
    size_t at = header(b, n, bound), len = 0;
    unsigned char *emf;
    wmf_t m;
    int ok;

    if (at == 0) {
        return -1;
    }
    if ((emf = inner_emf(b, n, at, &len)) != NULL) {
        ok = of_emf_draw(emf, len, fs, w, box);
        free(emf);
        if (ok == 0) {
            return 0;
        }
    }
    if ((bound[2] == bound[0] || bound[3] == bound[1]) &&
        !first_window(b, n, at, bound)) {
        measure(b, n, at, bound);
    }
    if (bound[2] == bound[0] || bound[3] == bound[1]) {
        return -1;
    }
    memset(&m, 0, sizeof m);
    gdi_begin(&m.g, w, fs, 1, box[0], box[1], box[2], box[3]);
    play(&m, b, n, at, bound);
    gdi_end(&m.g);
    return 0;
}
