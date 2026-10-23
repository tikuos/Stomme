/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_emf.c - an Enhanced Metafile's records played into the
 * device context of tiku_office_gdi.c.
 *
 * The header's frame, in hundredths of a millimetre, is where the
 * picture lies on its reference device; that device's pixels are mapped
 * so the frame fills the box.  Records are read only within their own
 * size, their counts checked against it.  EMF+ records ride in comments
 * beside the GDI ones Office also writes, which are what is drawn; for a
 * file with EMF+ alone, which draws nothing, of_emf_draw() returns -1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_gdi.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define POINTS_MAX (1u << 22)       /* points one record may hold        */

/** @brief One record: its bytes, its type and size in them. */
typedef struct {
    const unsigned char *p;
    size_t               n;
} rec_t;

/** @brief Record @p r's signed number at @p off. */
static int32_t
ri(const rec_t *r, size_t off)
{
    return gdi_i32(r->p, r->n, off);
}

/** @brief Record @p r's unsigned number at @p off. */
static uint32_t
ru(const rec_t *r, size_t off)
{
    return gdi_u32(r->p, r->n, off);
}

/** @brief A box of four numbers at @p off, into @p b (l t r b). */
static void
box_at(const rec_t *r, size_t off, double *b)
{
    int i;

    for (i = 0; i < 4; i++) {
        b[i] = ri(r, off + 4 * (size_t)i);
    }
}

/** @brief @p n points at @p off, 32-bit or (@p small) 16-bit pairs.
 *  @return them, to free(), or NULL when the record does not hold them. */
static double *
points(const rec_t *r, size_t off, uint32_t n, int small)
{
    size_t each = small ? 4 : 8, i;
    double *p;

    if (n == 0 || n > POINTS_MAX || off > r->n || n > (r->n - off) / each ||
        (p = malloc((size_t)n * 2 * sizeof *p)) == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        p[2 * i] = small ? gdi_i16(r->p, r->n, off + 4 * i)
                         : ri(r, off + 8 * i);
        p[2 * i + 1] = small ? gdi_i16(r->p, r->n, off + 4 * i + 2)
                             : ri(r, off + 8 * i + 4);
    }
    return p;
}

/* ------------------------------------------------------------- header  */

/** @brief The reference device and the frame from header @p h: the
 *         device's pixels mapped so the frame fills the box.
 *  @return 0, or -1 when neither frame nor bounds has an extent. */
static int
frame(gdi_t *g, const rec_t *h)
{
    double mx = ri(h, 80), my = ri(h, 84), f[4], x0, y0, x1, y1;

    if (h->n >= 108 && fabs(ri(h, 100) / 1000.0 - mx) < 1 &&
        fabs(ri(h, 104) / 1000.0 - my) < 1) {
        mx = ri(h, 100) / 1000.0;   /* micrometres, finer, when they agree */
        my = ri(h, 104) / 1000.0;
    }
    if (ri(h, 72) > 0 && ri(h, 76) > 0 && mx > 0 && my > 0) {
        g->ppx = ri(h, 72) / mx;
        g->ppy = ri(h, 76) / my;
    }
    box_at(h, 24, f);
    x0 = f[0] * 0.01 * g->ppx;
    y0 = f[1] * 0.01 * g->ppy;
    x1 = f[2] * 0.01 * g->ppx;
    y1 = f[3] * 0.01 * g->ppy;
    if (!(x1 - x0 > 0.5) || !(y1 - y0 > 0.5)) {
        box_at(h, 8, f);            /* the bounds, in device pixels */
        x0 = f[0];
        y0 = f[1];
        x1 = f[2] + 1;
        y1 = f[3] + 1;
    }
    if (!(x1 - x0 > 0) || !(y1 - y0 > 0)) {
        return -1;
    }
    g->out.a = g->box[2] / (x1 - x0);
    g->out.d = g->box[3] / (y1 - y0);
    g->out.b = g->out.c = 0;
    g->out.e = g->box[0] - x0 * g->out.a;
    g->out.f = g->box[1] - y0 * g->out.d;
    if (ri(h, 72) > 0 && ri(h, 76) > 0) {  /* a window set alone spans */
        g->dc.wex = g->dc.vex = ri(h, 72);  /* the device, as LibreOffice */
        g->dc.wey = g->dc.vey = ri(h, 76);  /* takes it                  */
    }
    gdi_moved(g);
    return 0;
}

/* -------------------------------------------------------------- state  */

/** @brief Mapping mode @p mode set, with the extents GDI gives it. */
static void
map_mode(gdi_t *g, int mode)
{
    static const double mm[] = {0.1, 0.01, 0.254, 0.0254, 25.4 / 1440};
    gdi_dc_t *dc = &g->dc;

    if (mode < GDI_MM_TEXT || mode > GDI_MM_ANISOTROPIC) {
        return;
    }
    dc->map = mode;
    if (mode == GDI_MM_TEXT) {
        dc->wex = dc->wey = dc->vex = dc->vey = 1;
    } else if (mode <= GDI_MM_TWIPS) {
        dc->wex = dc->wey = 1;
        dc->vex = mm[mode - GDI_MM_LOMETRIC] * g->ppx;
        dc->vey = -mm[mode - GDI_MM_LOMETRIC] * g->ppy;
    }
    gdi_moved(g);
}

/** @brief An extent set or scaled (@p scale: by the record's two
 *         ratios): the window's or the viewport's, where the mode lets
 *         them change. */
static void
extent(gdi_t *g, const rec_t *r, int viewport, int scale)
{
    double *x = viewport ? &g->dc.vex : &g->dc.wex;
    double *y = viewport ? &g->dc.vey : &g->dc.wey;

    if (g->dc.map != GDI_MM_ISOTROPIC && g->dc.map != GDI_MM_ANISOTROPIC) {
        return;
    }
    if (!scale) {
        *x = ri(r, 8);
        *y = ri(r, 12);
    } else if (ri(r, 12) != 0 && ri(r, 20) != 0) {
        *x = *x * ri(r, 8) / ri(r, 12);
        *y = *y * ri(r, 16) / ri(r, 20);
    }
    gdi_moved(g);
}

/** @brief An XFORM at @p off. */
static gdi_mat_t
xform(const rec_t *r, size_t off)
{
    gdi_mat_t m;

    m.a = gdi_f32(r->p, r->n, off);
    m.b = gdi_f32(r->p, r->n, off + 4);
    m.c = gdi_f32(r->p, r->n, off + 8);
    m.d = gdi_f32(r->p, r->n, off + 12);
    m.e = gdi_f32(r->p, r->n, off + 16);
    m.f = gdi_f32(r->p, r->n, off + 20);
    return m;
}

/** @brief A record that sets the state.  @return 1 when it was one. */
static int
state(gdi_t *g, uint32_t t, const rec_t *r)
{
    gdi_dc_t *dc = &g->dc;
    gdi_mat_t m;

    switch (t) {
    case 9:  extent(g, r, 0, 0);                       break;
    case 11: extent(g, r, 1, 0);                       break;
    case 31: extent(g, r, 1, 1);                       break;
    case 32: extent(g, r, 0, 1);                       break;
    case 10: dc->wox = ri(r, 8); dc->woy = ri(r, 12); gdi_moved(g); break;
    case 12: dc->vox = ri(r, 8); dc->voy = ri(r, 12); gdi_moved(g); break;
    case 17: map_mode(g, ri(r, 8));                    break;
    case 18: dc->bkmode = ri(r, 8);                    break;
    case 19: dc->fill = ri(r, 8);                      break;
    case 20: dc->rop2 = ri(r, 8);                      break;
    case 22: dc->align = ri(r, 8);                     break;
    case 24: dc->text = gdi_color(g, ru(r, 8));        break;
    case 25: dc->bk = gdi_color(g, ru(r, 8));          break;
    case 57: dc->arcdir = ri(r, 8);                    break;
    case 27:
        dc->x = ri(r, 8);
        dc->y = ri(r, 12);
        g->fig = 0;                 /* a new figure starts here */
        break;
    case 33: gdi_save(g);                              break;
    case 34: gdi_restore(g, ri(r, 8));                 break;
    case 35:
        m = xform(r, 8);
        gdi_world(g, &m, 4);
        break;
    case 36:
        m = xform(r, 8);
        gdi_world(g, &m, ri(r, 32));
        break;
    default:
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ objects  */

/** @brief A LOGFONTW at @p off read into @p f. */
static void
logfont(const rec_t *r, size_t off, gdi_font_t *f)
{
    uint32_t cp;
    size_t i, k = 0;

    memset(f, 0, sizeof *f);
    f->height = ri(r, off);
    f->width = abs(ri(r, off + 4));
    f->escape = ri(r, off + 8);
    f->weight = ri(r, off + 16);
    f->italic = off + 20 < r->n && r->p[off + 20];
    f->underline = off + 21 < r->n && r->p[off + 21];
    f->strike = off + 22 < r->n && r->p[off + 22];
    f->charset = off + 23 < r->n ? r->p[off + 23] : 0;
    for (i = 0; i < 32 && k + 4 < sizeof f->face; i++) {
        cp = gdi_u16(r->p, r->n, off + 28 + 2 * i);
        if (cp == 0) {
            break;
        }
        k += gdi_utf8(cp, f->face + k);
    }
    f->face[k] = '\0';
}

/** @brief A pen from EMR_CREATEPEN (@p ext 0) or EMR_EXTCREATEPEN. */
static void
pen(gdi_t *g, const rec_t *r, int ext)
{
    gdi_obj_t *o = gdi_slot(g, ri(r, 8));
    gdi_pen_t *p;
    uint32_t i;

    if (o == NULL) {
        return;
    }
    o->kind = GDI_PEN;
    p = &o->u.pen;
    p->style = ri(r, ext ? 28 : 12);
    p->width = ri(r, ext ? 32 : 16);
    p->color = gdi_color(g, ru(r, ext ? 40 : 24));
    p->cosmetic = ext ? !(p->style & 0x10000) : p->width == 0;
    if (ext && ru(r, 36) == 1) {    /* BS_NULL: a pen that draws nothing */
        p->style = 5;
    }
    for (i = 0; ext && i < ru(r, 48) && i < 8 && 52 + 4 * i + 4 <= r->n;
         i++) {
        p->dash[p->ndash++] = ri(r, 52 + 4 * i);
    }
}

/** @brief The bitmap a record carries at the four numbers from @p at:
 *         its header's offset and size, its bits'. */
static void
dib_at(const rec_t *r, size_t at, int usage, gdi_dib_t *d)
{
    uint32_t ob = ru(r, at), nb = ru(r, at + 4);
    uint32_t os = ru(r, at + 8), ns = ru(r, at + 12);

    memset(d, 0, sizeof *d);
    d->usage = usage;
    if (nb > 0 && ob <= r->n && nb <= r->n - ob) {
        d->bmi = r->p + ob;
        d->nbmi = nb;
    }
    if (ns > 0 && os <= r->n && ns <= r->n - os) {
        d->bits = r->p + os;
        d->nbits = ns;
    }
    if (d->bits == NULL) {
        d->nbmi = 0;
    }
}

/** @brief A record that makes or picks an object.  @return 1 when it
 *         was one. */
static int
objects(gdi_t *g, uint32_t t, const rec_t *r)
{
    gdi_obj_t *o;
    gdi_dib_t d;
    uint32_t i, n;

    switch (t) {
    case 37: gdi_select(g, ru(r, 8));                  break;
    case 40: case 101: gdi_delete(g, ri(r, 8));         break;
    case 38: pen(g, r, 0);                             break;
    case 95: pen(g, r, 1);                             break;
    case 39:
        if ((o = gdi_slot(g, ri(r, 8))) != NULL) {
            o->kind = GDI_BRUSH;
            o->u.brush.style = ri(r, 12);
            o->u.brush.color = gdi_color(g, ru(r, 16));
            o->u.brush.hatch = ri(r, 20);
        }
        break;
    case 93: case 94:               /* a pattern brush: its average */
        dib_at(r, 16, ri(r, 12) == 1, &d);
        if ((o = gdi_slot(g, ri(r, 8))) != NULL) {
            o->kind = GDI_BRUSH;
            o->u.brush.color = d.nbmi ? gdi_dib_average(g, &d) : 0xff808080u;
        }
        break;
    case 82:
        if ((o = gdi_slot(g, ri(r, 8))) != NULL) {
            o->kind = GDI_FONT;
            logfont(r, 12, &o->u.font);
        }
        break;
    case 48: gdi_select(g, ru(r, 8));                  break;
    case 49:
        n = gdi_u16(r->p, r->n, 14);
        n = n < (r->n - 16) / 4 ? n : (uint32_t)(r->n - 16) / 4;
        if ((o = gdi_slot(g, ri(r, 8))) == NULL ||
            (o->u.pal.c = malloc((n ? n : 1) * sizeof(uint32_t))) == NULL) {
            break;
        }
        o->kind = GDI_PALETTE;
        o->u.pal.n = (int)n;
        for (i = 0; i < n; i++) {
            o->u.pal.c[i] = gdi_color(g, ru(r, 16 + 4 * i) & 0xffffffu);
        }
        break;
    case 99: case 122:              /* a colour space: a slot, no more */
        if ((o = gdi_slot(g, ri(r, 8))) != NULL) {
            o->kind = GDI_OTHER;
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------- shapes  */

/** @brief Box @p b (x y ...) filled with colour @p c, no outline: a
 *         pixel, a triangle, however many points @p n it has. */
static void
solid(gdi_t *g, const double *b, int n, uint32_t c)
{
    gdi_brush_t keep = g->dc.brush;
    gdi_pen_t pen = g->dc.pen;

    memset(&g->dc.brush, 0, sizeof g->dc.brush);
    g->dc.brush.color = c;
    g->dc.pen.style = 5;            /* PS_NULL */
    gdi_poly(g, b, n, GDI_SHUT, 0);
    g->dc.brush = keep;
    g->dc.pen = pen;
}

/** @brief SetPixelV: the pixel at (@p x, @p y) a logical unit square. */
static void
pixel(gdi_t *g, double x, double y, uint32_t c)
{
    double b[8];

    b[0] = b[6] = x;
    b[1] = b[3] = y;
    b[2] = b[4] = x + 1;
    b[5] = b[7] = y + 1;
    solid(g, b, 4, c);
}

/** @brief A record that draws a shape.  @return 1 when it was one. */
static int
shapes(gdi_t *g, uint32_t t, const rec_t *r)
{
    double b[4], p[2], q[2];

    box_at(r, 8, b);
    p[0] = ri(r, 24);
    p[1] = ri(r, 28);
    q[0] = ri(r, 32);
    q[1] = ri(r, 36);
    switch (t) {
    case 42: gdi_ellipse(g, b[0], b[1], b[2], b[3]);   break;
    case 43: gdi_rect(g, b[0], b[1], b[2], b[3]);      break;
    case 44: gdi_round(g, b, p[0], p[1]);              break;
    case 45: gdi_arc(g, b, p, q, 0);                   break;
    case 55: gdi_arc(g, b, p, q, 1);                   break;
    case 46: gdi_arc(g, b, p, q, 2);                   break;
    case 47: gdi_arc(g, b, p, q, 3);                   break;
    case 41:
        gdi_angle_arc(g, ri(r, 8), ri(r, 12), ru(r, 16),
                      gdi_f32(r->p, r->n, 20), gdi_f32(r->p, r->n, 24));
        break;
    case 54:
        if (!g->fig || g->npt == 0) {
            gdi_move(g, g->dc.x, g->dc.y);
        }
        gdi_line(g, ri(r, 8), ri(r, 12));
        g->dc.x = ri(r, 8);
        g->dc.y = ri(r, 12);
        gdi_paint(g, GDI_STROKE);
        break;
    case 15:
        pixel(g, ri(r, 8), ri(r, 12), gdi_color(g, ru(r, 16)));
        break;
    default:
        return 0;
    }
    return 1;
}

/** @brief Poly-polygons and poly-polylines: the counts, then the points
 *         of each, 16-bit when @p small. */
static void
polypoly(gdi_t *g, const rec_t *r, int shut, int small)
{
    uint32_t n = ru(r, 24), total = ru(r, 28), i, k = 0, c;
    double *p;

    if (n == 0 || n > POINTS_MAX || 32 + (size_t)n * 4 > r->n ||
        (p = points(r, 32 + (size_t)n * 4, total, small)) == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        c = ru(r, 32 + 4 * i);
        if (c > total - k) {
            break;
        }
        gdi_poly(g, p + 2 * k, (int)c, (shut ? GDI_SHUT : 0) | GDI_MORE, 0);
        k += c;
    }
    gdi_paint(g, shut ? GDI_FILL | GDI_STROKE : GDI_STROKE);
    free(p);
}

/** @brief A record of points: a polyline, polygon, Béziers, PolyDraw.
 *  @return 1 when it was one. */
static int
polys(gdi_t *g, uint32_t t, const rec_t *r)
{
    int small = t >= 85 && t <= 92, k = small ? (int)t - 83 : (int)t;
    uint32_t n = ru(r, 24);
    double *p;

    if (k == 7 || k == 8) {
        polypoly(g, r, k == 8, small);
        return 1;
    }
    if ((k < 2 || k > 6) && t != 56 && t != 92) {
        return 0;
    }
    if ((p = points(r, 28, n, small)) == NULL) {
        return 1;
    }
    if (t == 56 || t == 92) {       /* PolyDraw: its kinds after them */
        size_t at = 28 + (size_t)n * (small ? 4 : 8);

        if (at + n <= r->n) {
            gdi_polydraw(g, p, r->p + at, (int)n);
        }
    } else if (k == 2 || k == 5) {
        gdi_beziers(g, p, (int)n, k == 5);
    } else {
        gdi_poly(g, p, (int)n, k == 3 ? GDI_SHUT : 0, k == 6);
    }
    free(p);
    return 1;
}

/* ------------------------------------------------------ paths and clip  */

/** @brief A region's boxes at @p off (an RGNDATA of @p size bytes),
 *         into a list to free().  @return it, with their number in @p n. */
static double *
region(const rec_t *r, size_t off, uint32_t size, int *n)
{
    uint32_t hs = ru(r, off), count = ru(r, off + 8), i;
    double *b;

    *n = 0;
    if (size < 32 || off > r->n || size > r->n - off || hs < 32 ||
        hs > size || count > (size - hs) / 16 ||
        (b = malloc(((size_t)count + 1) * 4 * sizeof *b)) == NULL) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        box_at(r, off + hs + 16 * i, b + 4 * i);
    }
    *n = (int)count;
    return b;
}

/** @brief A brush by its number, stock or made, into @p b. */
static void
brush_of(gdi_t *g, uint32_t i, gdi_brush_t *b)
{
    gdi_brush_t keep = g->dc.brush;

    gdi_select(g, i);
    *b = g->dc.brush;
    g->dc.brush = keep;
}

/** @brief A record of the path or the clip.  @return 1 when it was one. */
static int
paths(gdi_t *g, uint32_t t, const rec_t *r)
{
    double b[4], *rg;
    gdi_brush_t br;
    int n;

    switch (t) {
    case 59: gdi_path(g, 1);                           break;
    case 60: gdi_path(g, 0);                           break;
    case 68: gdi_path(g, 2);                           break;
    case 61: gdi_close(g);                             break;
    case 62: gdi_path_paint(g, GDI_FILL);              break;
    case 63: gdi_path_paint(g, GDI_FILL | GDI_STROKE); break;
    case 64: gdi_path_paint(g, GDI_STROKE);            break;
    case 66: g->wide = 1;                              break;
    case 67: gdi_path_clip(g, ri(r, 8));               break;
    case 28: gdi_clip_meta(g);                         break;
    case 29: case 30:
        box_at(r, 8, b);
        gdi_clip_boxes(g, b, 1, t == 30 ? GDI_RGN_AND : GDI_RGN_DIFF, 0);
        break;
    case 75:
        rg = region(r, 16, ru(r, 8), &n);
        gdi_clip_boxes(g, rg, n, ri(r, 12), 1);
        free(rg);
        break;
    case 71: case 72: case 74:      /* FillRgn, FrameRgn, PaintRgn */
        rg = region(r, t == 74 ? 28 : t == 71 ? 32 : 40, ru(r, 24), &n);
        br = g->dc.brush;
        if (t != 74) {
            brush_of(g, ru(r, 28), &br);
        }
        gdi_boxes(g, rg, n, &br, 1, t == 72 ? fmax(ri(r, 32), 1) : 0);
        free(rg);
        break;
    default:
        return t == 65 || t == 26 || t == 73;   /* nothing to draw */
    }
    return 1;
}

/* --------------------------------------------------------------- text  */

/** @brief Whether the UTF-16 unit @p i of @p n at @p off begins a pair;
 *         the character the pair makes into @p c when it does. */
static int
pair(const rec_t *r, uint32_t off, uint32_t i, uint32_t n, uint32_t *c)
{
    uint32_t lo = i + 1 < n ? gdi_u16(r->p, r->n, off + 2 * i + 2) : 0;

    if (*c < 0xd800 || *c >= 0xdc00 || lo < 0xdc00 || lo >= 0xe000) {
        return 0;
    }
    *c = 0x10000 + ((*c - 0xd800) << 10) + (lo - 0xdc00);
    return 1;
}

/** @brief An EMRTEXT at @p at, of a string of @p wide characters or
 *         bytes, drawn. */
static void
text_at(gdi_t *g, const rec_t *r, size_t at, int wide)
{
    uint32_t n = ru(r, at + 8), off = ru(r, at + 12), dxo = ru(r, at + 36);
    uint32_t i, k = 0, c, *ch;
    gdi_text_t t;
    double *dx;

    memset(&t, 0, sizeof t);
    t.opts = ri(r, at + 16);
    t.x = ri(r, at);
    t.y = ri(r, at + 4);
    box_at(r, at + 20, t.rect);
    if (n == 0 || n > 65536 || off > r->n ||
        n > (r->n - off) / (wide ? 2 : 1)) {
        gdi_text(g, &t);            /* its box, if it fills one */
        return;
    }
    ch = malloc((size_t)n * sizeof *ch);
    dx = malloc((size_t)n * 2 * sizeof *dx);
    if (ch == NULL || dx == NULL) {
        free(ch);
        free(dx);
        return;
    }
    t.dx = dxo != 0 && dxo <= r->n &&
           (size_t)n * ((t.opts & GDI_ETO_PDY) ? 8 : 4) <= r->n - dxo
           ? dx : NULL;
    for (i = 0; i < n; i++) {
        c = wide ? gdi_u16(r->p, r->n, off + 2 * i)
                 : gdi_char(&g->dc.font, r->p[off + i]);
        dx[k] = t.dx ? ri(r, dxo + 4 * i * ((t.opts & GDI_ETO_PDY) ? 2 : 1))
                     : 0;
        dx[n + k] = (t.opts & GDI_ETO_PDY) ? ri(r, dxo + 8 * i + 4) : 0;
        if (wide && !(t.opts & GDI_ETO_GLYPHS) && pair(r, off, i, n, &c)) {
            i++;                    /* the pair's advances, together */
            dx[k] += t.dx ? ri(r, dxo + 4 * i *
                               ((t.opts & GDI_ETO_PDY) ? 2 : 1)) : 0;
        }
        ch[k++] = c;
    }
    t.ch = ch;
    t.n = (int)k;
    t.dy = (t.opts & GDI_ETO_PDY) && t.dx ? dx + n : NULL;
    gdi_text(g, &t);
    free(ch);
    free(dx);
}

/** @brief EMR_SMALLTEXTOUT: a string of bytes or of wide characters, with
 *         no advances; the record holds a clip box unless ETO_NO_RECT is
 *         set. */
static void
small_text(gdi_t *g, const rec_t *r)
{
    uint32_t n = ru(r, 16), opts = ru(r, 20), i, *ch;
    size_t off = opts & 0x100 ? 36 : 52;
    int wide = !(opts & 0x200);
    gdi_text_t t;

    memset(&t, 0, sizeof t);
    if (n == 0 || n > 65536 || off > r->n ||
        n > (r->n - off) / (wide ? 2 : 1) ||
        (ch = malloc((size_t)n * sizeof *ch)) == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        ch[i] = wide ? gdi_u16(r->p, r->n, off + 2 * i)
                     : gdi_char(&g->dc.font, r->p[off + i]);
    }
    t.x = ri(r, 8);
    t.y = ri(r, 12);
    t.opts = (int)(opts & ~0x300u);
    if (!(opts & 0x100)) {
        box_at(r, 36, t.rect);
    } else {
        t.opts &= ~(GDI_ETO_OPAQUE | GDI_ETO_CLIPPED);
    }
    t.ch = ch;
    t.n = (int)n;
    gdi_text(g, &t);
    free(ch);
}

/** @brief A record of text.  @return 1 when it was one. */
static int
texts(gdi_t *g, uint32_t t, const rec_t *r)
{
    uint32_t i, n;

    if (t == 83 || t == 84) {
        text_at(g, r, 36, t == 84);
    } else if (t == 96 || t == 97) {    /* PolyTextOut: several */
        n = ru(r, 36);
        for (i = 0; i < n && 40 + 40 * (size_t)(i + 1) <= r->n; i++) {
            text_at(g, r, 40 + 40 * (size_t)i, t == 97);
        }
    } else if (t == 108) {
        small_text(g, r);
    } else {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ bitmaps  */

/** @brief The box a blit draws in: @p x, @p y, @p w by @p h logical. */
static void
blit_box(gdi_blit_t *b, double x, double y, double w, double h)
{
    b->p[0] = x;
    b->p[1] = y;
    b->p[2] = x + w;
    b->p[3] = y;
    b->p[4] = x;
    b->p[5] = y + h;
}

/** @brief BitBlt and its kin: StretchBlt, AlphaBlend, TransparentBlt,
 *         MaskBlt; their shared layout read. */
static void
blt(gdi_t *g, uint32_t t, const rec_t *r)
{
    gdi_blit_t b;
    gdi_dib_t d, m;
    gdi_mat_t x = xform(r, 52);
    int stretch = t != 76 && t != 78;

    memset(&b, 0, sizeof b);
    blit_box(&b, ri(r, 24), ri(r, 28), ri(r, 32), ri(r, 36));
    b.rop = ru(r, 40);
    b.alpha = -1;
    b.src[0] = ri(r, 44) * (x.a != 0 ? x.a : 1) + x.e;
    b.src[1] = ri(r, 48) * (x.d != 0 ? x.d : 1) + x.f;
    b.src[2] = stretch ? ri(r, 100) : ri(r, 32) * (x.a != 0 ? x.a : 1);
    b.src[3] = stretch ? ri(r, 104) : ri(r, 36) * (x.d != 0 ? x.d : 1);
    dib_at(r, 84, ri(r, 80) == 1, &d);
    if (t == 114) {                 /* AlphaBlend: its blend function */
        b.alpha = r->n > 42 ? r->p[42] : 255;
        b.premul = r->n > 43 && (r->p[43] & 1);
        b.rop = 0x00cc0020u;
    } else if (t == 116) {          /* TransparentBlt: its colour */
        b.keyed = 1;
        b.key = ru(r, 40);
        b.rop = 0x00cc0020u;
    } else if (t == 78) {           /* MaskBlt: the foreground's */
        b.rop &= 0x00ffffffu;
        dib_at(r, 112, ri(r, 108) == 1, &m);
    }
    gdi_blit(g, d.nbmi ? &d : NULL, &b, t == 78 ? &m : NULL);
}

/** @brief PlgBlt: the bitmap over three corners. */
static void
plg(gdi_t *g, const rec_t *r)
{
    gdi_blit_t b;
    gdi_dib_t d, m;
    int i;

    memset(&b, 0, sizeof b);
    for (i = 0; i < 6; i++) {
        b.p[i] = ri(r, 24 + 4 * (size_t)i);
    }
    b.rop = 0x00cc0020u;
    b.alpha = -1;
    b.src[0] = ri(r, 48);
    b.src[1] = ri(r, 52);
    b.src[2] = ri(r, 56);
    b.src[3] = ri(r, 60);
    dib_at(r, 96, ri(r, 92) == 1, &d);
    dib_at(r, 124, ri(r, 120) == 1, &m);
    if (d.nbmi) {
        gdi_blit(g, &d, &b, &m);
    }
}

/** @brief StretchDIBits (@p t 81) and SetDIBitsToDevice (80). */
static void
dibits(gdi_t *g, uint32_t t, const rec_t *r)
{
    gdi_blit_t b;
    gdi_dib_t d;

    memset(&b, 0, sizeof b);
    b.alpha = -1;
    b.from_bottom = 1;
    b.src[0] = ri(r, 32);
    b.src[1] = ri(r, 36);
    b.src[2] = ri(r, 40);
    b.src[3] = ri(r, 44);
    dib_at(r, 48, ri(r, 64) == 1, &d);
    if (t == 81) {
        b.rop = ru(r, 68);
        blit_box(&b, ri(r, 24), ri(r, 28), ri(r, 72), ri(r, 76));
    } else {
        b.rop = 0x00cc0020u;
        blit_box(&b, ri(r, 24), ri(r, 28), ri(r, 40), ri(r, 44));
    }
    gdi_blit(g, d.nbmi ? &d : NULL, &b, NULL);
}

/** @brief The average of three colours. */
static uint32_t
mix(const uint32_t *c)
{
    uint32_t r = 0, gr = 0, b = 0;
    int i;

    for (i = 0; i < 3; i++) {
        r += c[i] >> 16 & 255;
        gr += c[i] >> 8 & 255;
        b += c[i] & 255;
    }
    return 0xff000000u | r / 3 << 16 | gr / 3 << 8 | b / 3;
}

/** @brief GradientFill: its boxes shaded across or down, its triangles
 *         filled with their corners' average. */
static void
gradient(gdi_t *g, const rec_t *r)
{
    uint32_t nv = ru(r, 24), nt = ru(r, 28), mode = ru(r, 32), i, k;
    size_t at = 36 + (size_t)nv * 16, each = mode == 2 ? 12 : 8;
    uint32_t c[3], v;
    double b[6];

    if (nv > POINTS_MAX || nt > POINTS_MAX || at > r->n ||
        nt > (r->n - at) / each) {
        return;
    }
    for (i = 0; i < nt; i++) {
        for (k = 0; k < (mode == 2 ? 3u : 2u); k++) {
            v = ru(r, at + each * i + 4 * k);
            v = v < nv ? v : 0;
            b[2 * k] = ri(r, 36 + 16 * (size_t)v);
            b[2 * k + 1] = ri(r, 40 + 16 * (size_t)v);
            c[k] = 0xff000000u |
                   (uint32_t)(gdi_u16(r->p, r->n, 44 + 16 * v) >> 8) << 16 |
                   (uint32_t)(gdi_u16(r->p, r->n, 46 + 16 * v) >> 8) << 8 |
                   (uint32_t)(gdi_u16(r->p, r->n, 48 + 16 * v) >> 8);
        }
        if (mode == 2) {
            solid(g, b, 3, mix(c));
        } else {
            gdi_gradient(g, b, c[0], c[1], mode == 1);
        }
    }
}

/** @brief A record of bitmaps or shading.  @return 1 when it was one. */
static int
bitmaps(gdi_t *g, uint32_t t, const rec_t *r)
{
    switch (t) {
    case 76: case 77: case 78: case 114: case 116:
        blt(g, t, r);
        break;
    case 79:
        plg(g, r);
        break;
    case 80: case 81:
        dibits(g, t, r);
        break;
    case 118:
        gradient(g, r);
        break;
    default:
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------- player  */

int
of_emf_draw(const unsigned char *b, size_t n, of_faces_t *fs,
            tiku_pdf_writer_t *w, const double *box)
{
    gdi_t g;
    rec_t r;
    size_t off;
    uint32_t t;
    int plus = 0, drawn;

    r.p = b;
    r.n = gdi_u32(b, n, 4) <= n ? gdi_u32(b, n, 4) : n;
    gdi_begin(&g, w, fs, 0, box[0], box[1], box[2], box[3]);
    if (r.n < 88 || frame(&g, &r) != 0) {
        gdi_end(&g);
        return -1;
    }
    for (off = r.n; off + 8 <= n && g.budget > 0; off += r.n) {
        t = gdi_u32(b, n, off);
        r.p = b + off;
        r.n = gdi_u32(b, n, off + 4);
        if (r.n < 8 || r.n > n - off || t == 14) {  /* EMR_EOF */
            break;
        }
        if (t == 70 && gdi_u32(r.p, r.n, 12) == 0x2b464d45u) {
            plus = 1;               /* "EMF+": what GDI+ says, beside */
        } else if (!state(&g, t, &r) && !objects(&g, t, &r) &&
                   !shapes(&g, t, &r) && !polys(&g, t, &r) &&
                   !paths(&g, t, &r) && !texts(&g, t, &r)) {
            (void)bitmaps(&g, t, &r);
        }
    }
    drawn = g.drawn;
    gdi_end(&g);
    return drawn || !plus ? 0 : -1;
}
