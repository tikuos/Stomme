/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_gdi.c - a Windows metafile's device context: the state its
 * records set, and paths painted with the pen and brush selected.
 *
 * A point goes from the world through the world transform to the page,
 * through the window and viewport to the device, and from an EMF's
 * reference device into the picture's box; a WMF's window spans the box.
 * Paths are kept in the box's points as they are built, as GDI keeps
 * them in device pixels; the clip is a chain of steps, each a path the
 * one before is cut by, made the writer's only when something is drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_gdi.h"
#include "tiku_inflate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HAIR   0.5                  /* the thinnest line drawn, points   */
#define FAR    1e6                  /* points past the box are put here  */
#define BUDGET 8000000L             /* points one metafile may draw      */
#define PIXELS (64L << 20)          /* and bitmap pixels it may decode   */
#define CHAIN  64                   /* clip steps the writer is given    */

/** @brief One step of the clip: a path the clip before it is cut by. */
struct gdi_clip {
    int prev;                       /* the step before, -1 none          */
    int evenodd;
    int from, n;                    /* its points, in cpt                */
};

/* -------------------------------------------------------------- bytes  */

unsigned
gdi_u16(const unsigned char *b, size_t n, size_t off)
{
    return off + 2 <= n ? (unsigned)(b[off] | b[off + 1] << 8) : 0;
}

int
gdi_i16(const unsigned char *b, size_t n, size_t off)
{
    return (int)(int16_t)gdi_u16(b, n, off);
}

uint32_t
gdi_u32(const unsigned char *b, size_t n, size_t off)
{
    if (off + 4 > n) {
        return 0;
    }
    return (uint32_t)b[off] | (uint32_t)b[off + 1] << 8 |
           (uint32_t)b[off + 2] << 16 | (uint32_t)b[off + 3] << 24;
}

int32_t
gdi_i32(const unsigned char *b, size_t n, size_t off)
{
    return (int32_t)gdi_u32(b, n, off);
}

double
gdi_f32(const unsigned char *b, size_t n, size_t off)
{
    uint32_t u = gdi_u32(b, n, off);
    float f;

    memcpy(&f, &u, sizeof f);
    return f == f && fabsf(f) < 1e30f ? f : 0;
}

size_t
gdi_utf8(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18 & 7));
    out[1] = (char)(0x80 | (cp >> 12 & 0x3f));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

/* ------------------------------------------------------------ mapping  */

/** @brief @p a after @p b: the map that does @p b, then @p a. */
static gdi_mat_t
mul(const gdi_mat_t *a, const gdi_mat_t *b)
{
    gdi_mat_t r;

    r.a = a->a * b->a + a->c * b->b;
    r.b = a->b * b->a + a->d * b->b;
    r.c = a->a * b->c + a->c * b->d;
    r.d = a->b * b->c + a->d * b->d;
    r.e = a->a * b->e + a->c * b->f + a->e;
    r.f = a->b * b->e + a->d * b->f + a->f;
    return r;
}

/** @brief The identity map. */
static gdi_mat_t
identity(void)
{
    gdi_mat_t m = {1, 0, 0, 1, 0, 0};

    return m;
}

/** @brief The page's scale into the device, @p sx and @p sy, as the
 *         mapping mode has it. */
static void
page_scale(const gdi_t *g, double *sx, double *sy)
{
    const gdi_dc_t *dc = &g->dc;
    double wx = dc->wex != 0 ? dc->wex : 1, wy = dc->wey != 0 ? dc->wey : 1;
    double s;

    *sx = *sy = 1;
    if (g->wmf) {
        *sx = g->box[2] / wx;
        *sy = g->box[3] / wy;
    } else if (dc->map != GDI_MM_TEXT) {   /* the fixed modes' extents are
                                              set with them */
        *sx = dc->vex / wx;
        *sy = dc->vey / wy;
    }
    if (dc->map == GDI_MM_ISOTROPIC) {
        s = fmin(fabs(*sx), fabs(*sy));
        *sx = copysign(s, *sx);
        *sy = copysign(s, *sy);
    }
}

/** @brief The map from logical units to the box's points, made again. */
static void
make(gdi_t *g)
{
    const gdi_dc_t *dc = &g->dc;
    double sx, sy, vx = g->wmf ? g->box[0] : dc->vox;
    double vy = g->wmf ? g->box[1] : dc->voy;
    gdi_mat_t page, dev;

    page_scale(g, &sx, &sy);
    page.a = sx;
    page.b = page.c = 0;
    page.d = sy;
    page.e = vx - dc->wox * sx;
    page.f = vy - dc->woy * sy;
    dev = mul(&g->out, &page);
    g->m = mul(&dev, &dc->world);
    g->made = 1;
}

void
gdi_moved(gdi_t *g)
{
    g->made = 0;
}

/** @brief @p v kept to where the writer can say it. */
static double
near(double v)
{
    return v == v ? fmax(-FAR, fmin(FAR, v)) : 0;
}

void
gdi_map(gdi_t *g, double x, double y, double *ox, double *oy)
{
    if (!g->made) {
        make(g);
    }
    *ox = near(g->m.a * x + g->m.c * y + g->m.e);
    *oy = near(g->m.b * x + g->m.d * y + g->m.f);
}

void
gdi_unmap(gdi_t *g, double x, double y, double *lx, double *ly)
{
    double det;

    if (!g->made) {
        make(g);
    }
    det = g->m.a * g->m.d - g->m.b * g->m.c;
    if (fabs(det) < 1e-12) {
        return;
    }
    x -= g->m.e;
    y -= g->m.f;
    *lx = (g->m.d * x - g->m.c * y) / det;
    *ly = (g->m.a * y - g->m.b * x) / det;
}

/** @brief How much a logical length grows in the box, on average. */
static double
scale(gdi_t *g)
{
    if (!g->made) {
        make(g);
    }
    return sqrt(fabs(g->m.a * g->m.d - g->m.b * g->m.c));
}

uint32_t
gdi_color(const gdi_t *g, uint32_t ref)
{
    const gdi_obj_t *o;
    unsigned i = ref & 0xffff;

    if ((ref >> 24) == 1) {         /* PALETTEINDEX */
        o = g->dc.pal >= 0 && g->dc.pal < g->nobj ? &g->obj[g->dc.pal]
                                                  : NULL;
        return o != NULL && o->kind == GDI_PALETTE && (int)i < o->u.pal.n
               ? o->u.pal.c[i] : 0xff000000u;
    }
    return 0xff000000u | (ref & 0xff) << 16 | (ref & 0xff00) |
           (ref >> 16 & 0xff);
}

/* ------------------------------------------------------------ objects  */

/** @brief What object @p o holds, given back. */
static void
drop(gdi_obj_t *o)
{
    if (o->kind == GDI_PALETTE) {
        free(o->u.pal.c);
    } else if (o->kind == GDI_REGION) {
        free(o->u.rgn.r);
    }
    memset(o, 0, sizeof *o);
}

gdi_obj_t *
gdi_slot(gdi_t *g, int i)
{
    gdi_obj_t *t;
    int cap;

    if (i < 0 || i >= GDI_OBJECTS) {
        return NULL;
    }
    if (i >= g->nobj) {
        cap = g->nobj * 2 > i + 1 ? g->nobj * 2 : i + 64;
        cap = cap < GDI_OBJECTS ? cap : GDI_OBJECTS;
        if ((t = realloc(g->obj, (size_t)cap * sizeof *t)) == NULL) {
            return NULL;
        }
        memset(t + g->nobj, 0, (size_t)(cap - g->nobj) * sizeof *t);
        g->obj = t;
        g->nobj = cap;
    }
    drop(&g->obj[i]);
    return &g->obj[i];
}

/** @brief The pen GDI starts with: black, a pixel wide. */
static void
black_pen(gdi_pen_t *p)
{
    memset(p, 0, sizeof *p);
    p->color = 0xff000000u;
    p->cosmetic = 1;
}

/** @brief A stock font: @p fixed a typewriter's, else a plain sans. */
static void
stock_font(gdi_font_t *f, int fixed)
{
    memset(f, 0, sizeof *f);
    f->height = -12;
    f->weight = 400;
    strcpy(f->face, fixed ? "Courier New" : "Arial");
}

/** @brief Stock object @p i selected. */
static void
stock(gdi_t *g, unsigned i)
{
    static const uint32_t grey[] = {0xffffffffu, 0xffc0c0c0u, 0xff808080u,
                                    0xff404040u, 0xff000000u};
    gdi_dc_t *dc = &g->dc;

    if (i <= 4 || i == 18) {
        memset(&dc->brush, 0, sizeof dc->brush);
        dc->brush.color = i == 18 ? 0xffffffffu : grey[i];
    } else if (i == 5) {
        dc->brush.style = 1;        /* BS_NULL */
    } else if (i == 6 || i == 7 || i == 19) {
        black_pen(&dc->pen);
        dc->pen.color = i == 6 ? 0xffffffffu : 0xff000000u;
    } else if (i == 8) {
        dc->pen.style = 5;          /* PS_NULL */
    } else if (i >= 10 && i <= 17 && i != 15) {
        stock_font(&dc->font, i == 10 || i == 11 || i == 16);
    } else if (i == 15) {
        dc->pal = -1;
    }
}

void
gdi_select(gdi_t *g, uint32_t i)
{
    const gdi_obj_t *o;

    if (i & GDI_STOCK) {
        stock(g, i & ~GDI_STOCK);
        return;
    }
    if (i >= (uint32_t)g->nobj) {
        return;
    }
    o = &g->obj[i];
    if (o->kind == GDI_PEN) {
        g->dc.pen = o->u.pen;
    } else if (o->kind == GDI_BRUSH) {
        g->dc.brush = o->u.brush;
    } else if (o->kind == GDI_FONT) {
        g->dc.font = o->u.font;
    } else if (o->kind == GDI_PALETTE) {
        g->dc.pal = (int)i;
    } else if (o->kind == GDI_REGION) {
        gdi_clip_boxes(g, o->u.rgn.r, o->u.rgn.n, GDI_RGN_COPY, 0);
    }
}

void
gdi_delete(gdi_t *g, int i)
{
    if (i >= 0 && i < g->nobj) {
        if (g->dc.pal == i) {
            g->dc.pal = -1;
        }
        drop(&g->obj[i]);
    }
}

/* -------------------------------------------------------------- state  */

void
gdi_begin(gdi_t *g, tiku_pdf_writer_t *wr, of_faces_t *fs, int wmf,
          double x, double y, double w, double h)
{
    gdi_dc_t *dc = &g->dc;

    memset(g, 0, sizeof *g);
    g->w = wr;
    g->fs = fs;
    g->wmf = wmf;
    g->box[0] = x;
    g->box[1] = y;
    g->box[2] = w;
    g->box[3] = h;
    g->out = identity();
    g->ppx = g->ppy = 96 / 25.4;
    g->applied = -1;
    g->budget = BUDGET;
    g->pixels = PIXELS;
    dc->map = GDI_MM_TEXT;
    dc->gmode = 1;
    dc->wex = dc->wey = dc->vex = dc->vey = 1;
    dc->world = identity();
    black_pen(&dc->pen);
    dc->brush.color = 0xffffffffu;
    stock_font(&dc->font, 0);
    dc->text = 0xff000000u;
    dc->bk = 0xffffffffu;
    dc->bkmode = 2;                 /* OPAQUE */
    dc->fill = 1;                   /* ALTERNATE */
    dc->rop2 = 13;                  /* R2_COPYPEN */
    dc->arcdir = 1;                 /* AD_COUNTERCLOCKWISE */
    dc->pal = dc->clip = dc->meta = -1;
    tiku_pdf_writer_save(wr);       /* nothing past the box */
    tiku_pdf_writer_rect(wr, x, y, w, h);
    tiku_pdf_writer_clip(wr, 0);
}

void
gdi_end(gdi_t *g)
{
    int i;

    if (g->level) {
        tiku_pdf_writer_restore(g->w);
    }
    tiku_pdf_writer_restore(g->w);
    for (i = 0; i < g->nobj; i++) {
        drop(&g->obj[i]);
    }
    free(g->obj);
    free(g->stack);
    free(g->pt);
    free(g->clips);
    free(g->cpt);
    memset(g, 0, sizeof *g);
}

void
gdi_save(gdi_t *g)
{
    if (g->stack == NULL &&
        (g->stack = malloc(GDI_DEPTH * sizeof *g->stack)) == NULL) {
        g->over++;
        return;
    }
    if (g->nstack == GDI_DEPTH || g->over > 0) {
        g->over++;
        return;
    }
    g->stack[g->nstack++] = g->dc;
}

void
gdi_restore(gdi_t *g, int n)
{
    int total = g->nstack + g->over;
    int to = n < 0 ? total + n : n - 1;

    if (n == 0 || to < 0 || to >= total) {
        return;
    }
    if (to >= g->nstack) {          /* a state never kept: forget it */
        g->over = to - g->nstack;
        return;
    }
    g->dc = g->stack[to];
    g->nstack = to;
    g->over = 0;
    g->made = 0;
}

void
gdi_world(gdi_t *g, const gdi_mat_t *x, int how)
{
    gdi_mat_t *w = &g->dc.world;

    if (how == 1) {
        *w = identity();
    } else if (how == 2) {
        *w = mul(w, x);
    } else if (how == 3) {
        *w = mul(x, w);
    } else if (how == 4) {
        *w = *x;
    }
    g->made = 0;
}

/* -------------------------------------------------------------- paths  */

/** @brief A point put in the path, in the box's points.  @return 0, or
 *         -1 when the path cannot grow or the budget is spent. */
static int
put(gdi_t *g, double x, double y, int op)
{
    gdi_pt_t *t;
    int cap;

    if (g->budget-- <= 0) {
        return -1;
    }
    if (g->npt == g->ptcap) {
        cap = g->ptcap ? g->ptcap * 2 : 256;
        if ((t = realloc(g->pt, (size_t)cap * sizeof *t)) == NULL) {
            return -1;
        }
        g->pt = t;
        g->ptcap = cap;
    }
    gdi_map(g, x, y, &x, &y);
    g->pt[g->npt].x = (float)x;
    g->pt[g->npt].y = (float)y;
    g->pt[g->npt++].op = op;
    return 0;
}

void
gdi_move(gdi_t *g, double x, double y)
{
    (void)put(g, x, y, GDI_MOVE);
    g->fig = 1;
}

void
gdi_line(gdi_t *g, double x, double y)
{
    (void)put(g, x, y, GDI_LINE);
}

void
gdi_curve(gdi_t *g, const double *p)
{
    int i;

    for (i = 0; i < 3; i++) {
        (void)put(g, p[2 * i], p[2 * i + 1], GDI_CURVE);
    }
}

void
gdi_close(gdi_t *g)
{
    if (g->npt > 0 && g->pt[g->npt - 1].op != GDI_CLOSE) {
        (void)put(g, 0, 0, GDI_CLOSE);
    }
    g->fig = 0;
}

/** @brief Points @p p (@p n of them) given to the writer as a path. */
static void
emit(tiku_pdf_writer_t *w, const gdi_pt_t *p, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (p[i].op == GDI_MOVE) {
            tiku_pdf_writer_move(w, p[i].x, p[i].y);
        } else if (p[i].op == GDI_LINE) {
            tiku_pdf_writer_line(w, p[i].x, p[i].y);
        } else if (p[i].op == GDI_CURVE && i + 2 < n) {
            tiku_pdf_writer_curve(w, p[i].x, p[i].y, p[i + 1].x,
                                  p[i + 1].y, p[i + 2].x, p[i + 2].y);
            i += 2;
        } else if (p[i].op == GDI_CLOSE) {
            tiku_pdf_writer_close(w);
        }
    }
}

/* ------------------------------------------------------------ the clip  */

/** @brief A clip step made of points @p p (@p n), cutting the step
 *         @p prev.  @return its number, or @p prev when out of room. */
static int
step(gdi_t *g, const gdi_pt_t *p, int n, int evenodd, int prev)
{
    struct gdi_clip *c;
    gdi_pt_t *t;
    int cap;

    if (g->nclip == g->clipcap) {
        cap = g->clipcap ? g->clipcap * 2 : 16;
        if ((c = realloc(g->clips, (size_t)cap * sizeof *c)) == NULL) {
            return prev;
        }
        g->clips = c;
        g->clipcap = cap;
    }
    if (g->ncpt + n > g->cptcap) {
        cap = (g->ncpt + n) * 2 + 64;
        if ((t = realloc(g->cpt, (size_t)cap * sizeof *t)) == NULL) {
            return prev;
        }
        g->cpt = t;
        g->cptcap = cap;
    }
    memcpy(g->cpt + g->ncpt, p, (size_t)n * sizeof *p);
    c = &g->clips[g->nclip];
    c->prev = prev;
    c->evenodd = evenodd;
    c->from = g->ncpt;
    c->n = n;
    g->ncpt += n;
    return g->nclip++;
}


/** @brief Points @p p (@p n) combined into the clip as @p mode: cut by,
 *         cut out of (the box around everything with the path in it, by
 *         the even-odd rule), or put instead.  A union shows it all. */
static void
combine(gdi_t *g, const gdi_pt_t *p, int n, int evenodd, int mode)
{
    gdi_pt_t *all;
    int i;

    if (mode == GDI_RGN_COPY) {
        g->dc.clip = n > 0 ? step(g, p, n, evenodd, g->dc.meta)
                           : g->dc.meta;
    } else if (mode == GDI_RGN_AND && n > 0) {
        g->dc.clip = step(g, p, n, evenodd, g->dc.clip);
    } else if (mode == GDI_RGN_DIFF && n > 0) {
        if ((all = malloc((size_t)(n + 5) * sizeof *all)) == NULL) {
            return;
        }
        for (i = 0; i < 5; i++) {
            all[i].x = (float)(i == 1 || i == 2 ? FAR : -FAR);
            all[i].y = (float)(i == 2 || i == 3 ? FAR : -FAR);
            all[i].op = i == 0 ? GDI_MOVE : i == 4 ? GDI_CLOSE : GDI_LINE;
        }
        memcpy(all + 5, p, (size_t)n * sizeof *p);
        g->dc.clip = step(g, all, n + 5, 1, g->dc.clip);
        free(all);
    } else if (mode == GDI_RGN_OR || mode == GDI_RGN_XOR) {
        g->dc.clip = g->dc.meta;
    }
}

void
gdi_clip_apply(gdi_t *g)
{
    const struct gdi_clip *c;
    int i, k = 0;

    if (g->applied == g->dc.clip) {
        return;
    }
    if (g->level) {
        tiku_pdf_writer_restore(g->w);
        g->level = 0;
    }
    g->applied = g->dc.clip;
    if (g->dc.clip < 0) {
        return;
    }
    tiku_pdf_writer_save(g->w);
    g->level = 1;
    for (i = g->dc.clip; i >= 0 && k < CHAIN; i = c->prev, k++) {
        c = &g->clips[i];
        emit(g->w, g->cpt + c->from, c->n);
        tiku_pdf_writer_clip(g->w, c->evenodd);
    }
}

void
gdi_clip_meta(gdi_t *g)
{
    g->dc.meta = g->dc.clip;
}

/** @brief Box @p r (l t r b) put in the path as a figure: logical, or
 *         in device pixels when @p device. */
static void
box_figure(gdi_t *g, const double *r, int device)
{
    static const int corner[4][2] = {{0, 1}, {2, 1}, {2, 3}, {0, 3}};
    gdi_mat_t keep;
    int i;

    if (!g->made) {
        make(g);
    }
    keep = g->m;
    if (device) {                   /* the device's own points */
        g->m = g->out;
    }
    for (i = 0; i < 4; i++) {
        (void)put(g, r[corner[i][0]], r[corner[i][1]],
                  i == 0 ? GDI_MOVE : GDI_LINE);
    }
    (void)put(g, 0, 0, GDI_CLOSE);
    g->m = keep;
}

void
gdi_clip_boxes(gdi_t *g, const double *r, int n, int mode, int device)
{
    int i, keep = g->npt;

    if (n <= 0 || r == NULL) {
        if (mode == GDI_RGN_COPY) {
            g->dc.clip = g->dc.meta;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        box_figure(g, r + 4 * i, device);
    }
    combine(g, g->pt + keep, g->npt - keep, 0, mode);
    g->npt = keep;
}

/* ----------------------------------------------------------- painting  */

/** @brief Colour @p c as raster operation @p rop2 draws it.  @return 0
 *         when nothing is to be drawn. */
static int
rop_color(int rop2, uint32_t *c)
{
    if (rop2 == 11 || rop2 == 6) {  /* R2_NOP, R2_NOT */
        return 0;
    }
    if (rop2 == 1) {                /* R2_BLACK */
        *c = 0xff000000u;
    } else if (rop2 == 16) {        /* R2_WHITE */
        *c = 0xffffffffu;
    } else if (rop2 == 4 || rop2 == 3) {    /* R2_NOTCOPYPEN, MASKNOTPEN */
        *c ^= 0x00ffffffu;
    }
    return 1;
}

/** @brief Pen @p p's dashes for a line @p w points wide, into @p out.
 *  @return how many. */
static int
dashes(gdi_t *g, const gdi_pen_t *p, double w, double *out)
{
    static const double geo[5][6] = {{0}, {3, 1}, {1, 1}, {3, 1, 1, 1},
                                     {3, 1, 1, 1, 1, 1}};
    static const double pix[5][6] = {{0}, {18, 6}, {3, 3}, {9, 6, 3, 6},
                                     {9, 3, 3, 3, 3, 3}};
    static const int count[5] = {0, 2, 2, 4, 6};
    int s = p->style & 15, i, n = 0;
    double u = p->cosmetic || w < 1 ? HAIR : w;

    if (s == 7) {                   /* PS_USERSTYLE */
        for (i = 0; i < p->ndash && i < 8; i++) {
            out[n++] = fmax(p->dash[i] * (p->cosmetic ? HAIR : scale(g)),
                            0.1);
        }
    } else if (s == 8) {            /* PS_ALTERNATE */
        out[n++] = HAIR;
        out[n++] = HAIR;
    } else if (s >= 1 && s <= 4) {
        for (i = 0; i < count[s]; i++) {
            out[n++] = u * (u == HAIR ? pix[s][i] : geo[s][i]);
        }
    }
    return n;
}

/** @brief The pen's stroke put into @p p, its dashes in @p dash.
 *  @return 0 when it draws nothing. */
static int
pen_paint(gdi_t *g, tiku_pdf_paint_t *p, double *dash)
{
    const gdi_pen_t *pen = &g->dc.pen;
    int s = pen->style & 15, cap = pen->style >> 8 & 15;
    int join = pen->style >> 12 & 15;
    uint32_t c = pen->color;

    if (s == 5 || !rop_color(g->dc.rop2, &c)) {     /* PS_NULL */
        return 0;
    }
    p->stroke = c;
    p->width = pen->cosmetic || pen->width <= 0 ? HAIR
               : fmax(pen->width * scale(g), HAIR);
    p->ndash = dashes(g, pen, p->width, dash);
    p->dash = p->ndash > 0 ? dash : NULL;
    p->cap = pen->cosmetic ? 0 : cap == 1 ? 2 : cap == 2 ? 0 : 1;
    p->join = pen->cosmetic ? 0 : join == 1 ? 2 : join == 2 ? 0 : 1;
    return 1;
}

/** @brief The path's points' bounds, into @p b (x0 y0 x1 y1). */
static void
bounds(const gdi_t *g, double *b)
{
    int i;

    b[0] = b[1] = FAR;
    b[2] = b[3] = -FAR;
    for (i = 0; i < g->npt; i++) {
        if (g->pt[i].op != GDI_CLOSE) {
            b[0] = fmin(b[0], g->pt[i].x);
            b[1] = fmin(b[1], g->pt[i].y);
            b[2] = fmax(b[2], g->pt[i].x);
            b[3] = fmax(b[3], g->pt[i].y);
        }
    }
}

/** @brief The path filled with hatch @p h in colour @p c: rules 8 of
 *         the device's pixels apart (6 points in a WMF), across the
 *         path's bounds. */
static void
hatch(gdi_t *g, int h, uint32_t c)
{
    tiku_pdf_paint_t p;
    double b[4], gap = g->wmf ? 6 : fmax(8 * sqrt(fabs(g->out.a *
                                                  g->out.d)), 2);
    double span, t;

    bounds(g, b);
    span = (b[2] - b[0]) + (b[3] - b[1]);
    if (!(span > 0)) {
        return;
    }
    gap = fmax(gap, span / 2000);
    tiku_pdf_writer_save(g->w);
    emit(g->w, g->pt, g->npt);
    tiku_pdf_writer_clip(g->w, g->dc.fill == 1);
    for (t = 0; t <= span; t += gap) {
        if (h == 0 || h == 4) {     /* HS_HORIZONTAL, HS_CROSS */
            tiku_pdf_writer_move(g->w, b[0], b[1] + t);
            tiku_pdf_writer_line(g->w, b[2], b[1] + t);
        }
        if (h == 1 || h == 4) {     /* HS_VERTICAL */
            tiku_pdf_writer_move(g->w, b[0] + t, b[1]);
            tiku_pdf_writer_line(g->w, b[0] + t, b[3]);
        }
        if (h == 2 || h == 5) {     /* HS_FDIAGONAL: \ */
            tiku_pdf_writer_move(g->w, b[0] + t - span, b[1]);
            tiku_pdf_writer_line(g->w, b[0] + t, b[1] + span);
        }
        if (h == 3 || h == 5) {     /* HS_BDIAGONAL: / */
            tiku_pdf_writer_move(g->w, b[0] + t - span, b[1] + span);
            tiku_pdf_writer_line(g->w, b[0] + t, b[1]);
        }
    }
    memset(&p, 0, sizeof p);
    p.stroke = c;
    p.width = HAIR;
    tiku_pdf_writer_paint(g->w, &p);
    tiku_pdf_writer_restore(g->w);
}

/** @brief The path filled with brush @p b, if it is hatched.  @return
 *         the colour to fill with in one paint, or 0 when done here. */
static uint32_t
brush_fill(gdi_t *g, const gdi_brush_t *b)
{
    tiku_pdf_paint_t p;
    uint32_t c = b->color;

    if (b->style == 1 || !rop_color(g->dc.rop2, &c)) {  /* BS_NULL */
        return 0;
    }
    if (b->style != 2) {            /* BS_HATCHED */
        return c;
    }
    if (g->dc.bkmode == 2) {        /* the ground behind the hatch */
        memset(&p, 0, sizeof p);
        p.fill = g->dc.bk;
        p.evenodd = g->dc.fill == 1;
        emit(g->w, g->pt, g->npt);
        tiku_pdf_writer_paint(g->w, &p);
    }
    hatch(g, b->hatch, c);
    return 0;
}

/** @brief The path painted with the pen and brush, as @p what says. */
static void
draw(gdi_t *g, int what)
{
    tiku_pdf_paint_t p;
    double dash[8];

    if (g->npt == 0) {
        return;
    }
    gdi_clip_apply(g);
    memset(&p, 0, sizeof p);
    if (what & GDI_FILL) {
        p.fill = brush_fill(g, &g->dc.brush);
        p.evenodd = g->dc.fill == 1;
    }
    if (what & GDI_STROKE) {
        (void)pen_paint(g, &p, dash);
    }
    if ((p.fill >> 24) != 0 || ((p.stroke >> 24) != 0 && p.width > 0)) {
        emit(g->w, g->pt, g->npt);
        tiku_pdf_writer_paint(g->w, &p);
    }
    g->drawn = 1;
}

void
gdi_paint(gdi_t *g, int what)
{
    if (!g->inpath) {
        draw(g, what);
        g->npt = 0;
        g->fig = 0;
    }
}

void
gdi_paint_gradient(gdi_t *g, const tiku_pdf_gradient_t *gr)
{
    if (g->inpath) {
        return;
    }
    if (g->npt > 0) {
        gdi_clip_apply(g);
        emit(g->w, g->pt, g->npt);
        tiku_pdf_writer_gradient(g->w, gr);
        g->drawn = 1;
    }
    g->npt = 0;
    g->fig = 0;
}

/** @brief @p m's linear part applied to (@p x, @p y), into @p out. */
static void
apply(const gdi_mat_t *m, double x, double y, double *out)
{
    out[0] = m->a * x + m->c * y;
    out[1] = m->b * x + m->d * y;
}

void
gdi_axes(gdi_t *g, double angle, double *a, double *b, double *k)
{
    double sx, sy, yd, du[2], dv[2];
    gdi_mat_t page = identity(), dev, rest;

    page_scale(g, &sx, &sy);
    yd = sy * g->out.d >= 0 ? 1 : -1;
    page.a = sx;
    page.d = sy;
    dev = g->wmf ? g->dc.world : mul(&page, &g->dc.world);
    rest = g->wmf ? mul(&g->out, &page) : g->out;
    apply(&dev, cos(angle), -sin(angle) * yd, du);
    apply(&dev, sin(angle), cos(angle) * yd, dv);
    k[0] = hypot(du[0], du[1]);
    k[1] = hypot(dv[0], dv[1]);
    apply(&rest, k[0] > 0 ? du[0] / k[0] : 0, k[0] > 0 ? du[1] / k[0] : 0, a);
    apply(&rest, k[1] > 0 ? dv[0] / k[1] : 0, k[1] > 0 ? dv[1] / k[1] : 0, b);
}

void
gdi_path(gdi_t *g, int open)
{
    if (open == 1) {                /* BeginPath: a new one */
        g->npt = 0;
        g->wide = 0;
    } else if (open == 2) {         /* AbortPath */
        g->npt = 0;
    }
    g->fig = 0;
    g->inpath = open == 1;
}

void
gdi_path_paint(gdi_t *g, int what)
{
    gdi_pen_t pen = g->dc.pen;
    gdi_brush_t brush = g->dc.brush;

    g->inpath = 0;
    if (g->wide && what == GDI_FILL) {  /* a widened path is its stroke */
        g->dc.pen.style &= ~15;
        g->dc.brush.style = 1;
        what = GDI_STROKE;
    }
    draw(g, what);
    g->dc.pen = pen;
    g->dc.brush = brush;
    g->npt = 0;
    g->wide = 0;
    g->fig = 0;
}

void
gdi_path_clip(gdi_t *g, int mode)
{
    g->inpath = 0;
    combine(g, g->pt, g->npt, g->dc.fill == 1, mode);
    g->npt = 0;
    g->fig = 0;
}

void
gdi_boxes(gdi_t *g, const double *r, int n, const gdi_brush_t *b,
          int device, double lw)
{
    gdi_brush_t keep = g->dc.brush;
    gdi_pen_t pen = g->dc.pen;
    double all[4];
    int i;

    for (i = 0; i < n && lw <= 0; i++) {
        box_figure(g, r + 4 * i, device);
    }
    if (lw > 0 && n > 0) {          /* a frame round the region's bounds */
        all[0] = all[2] = r[0];
        all[1] = all[3] = r[1];
        for (i = 0; i < n; i++) {
            all[0] = fmin(all[0], r[4 * i]);
            all[1] = fmin(all[1], r[4 * i + 1]);
            all[2] = fmax(all[2], r[4 * i + 2]);
            all[3] = fmax(all[3], r[4 * i + 3]);
        }
        box_figure(g, all, device);
    }
    if (b != NULL) {
        g->dc.brush = *b;
    }
    if (lw > 0) {
        memset(&g->dc.pen, 0, sizeof g->dc.pen);
        g->dc.pen.color = g->dc.brush.color;
        g->dc.pen.width = lw;
    }
    gdi_paint(g, lw > 0 ? GDI_STROKE : GDI_FILL);
    g->dc.brush = keep;
    g->dc.pen = pen;
}

/* -------------------------------------------------------------- entry  */

/** @brief Where a gzip member's DEFLATE data starts in @p b; 0 for none:
 *         an .emz or .wmz is a metafile compressed. */
static size_t
gzip_body(const unsigned char *b, size_t n)
{
    size_t at = 10;
    int f;

    if (n < 18 || b[0] != 0x1f || b[1] != 0x8b || b[2] != 8) {
        return 0;
    }
    f = b[3];
    if (f & 4) {                    /* FEXTRA */
        at += 2 + gdi_u16(b, n, at);
    }
    while ((f & 8) && at < n && b[at++] != 0) {     /* FNAME */
    }
    while ((f & 16) && at < n && b[at++] != 0) {    /* FCOMMENT */
    }
    at += f & 2 ? 2 : 0;            /* FHCRC */
    return at < n ? at : 0;
}

/** @brief @p b inflated when it is gzip, into memory to free().
 *  @return it, its length in @p len; NULL when it is not. */
static unsigned char *
gunzip(const unsigned char *b, size_t n, long most, size_t *len)
{
    size_t at = gzip_body(b, n);
    unsigned char *out = NULL;
    long got;

    if (at == 0 || n - at > 0x7fffffffu) {
        return NULL;
    }
    got = tiku_inflate_alloc(b + at, (long)(n - at), &out, most, 0);
    if (got <= 0) {
        free(out);
        return NULL;
    }
    *len = (size_t)got;
    return out;
}

/** @brief Whether @p b is an EMF (1) or a WMF (2), unwrapped. */
static int
kind(const unsigned char *b, size_t n)
{
    unsigned k = gdi_u16(b, n, 0);

    if (gdi_u32(b, n, 0) == 1 && gdi_u32(b, n, 40) == 0x464d4520u) {
        return 1;                   /* EMR_HEADER, " EMF" */
    }
    if (gdi_u32(b, n, 0) == 0x9ac6cdd7u) {
        return 2;                   /* a placeable WMF */
    }
    return (k == 1 || k == 2) && gdi_u16(b, n, 2) == 9 &&
           (gdi_u16(b, n, 4) == 0x300 || gdi_u16(b, n, 4) == 0x100) ? 2 : 0;
}

int
of_metafile_is(const unsigned char *b, size_t n)
{
    unsigned char *in;
    size_t len;
    int k;

    if ((k = kind(b, n)) != 0 || (in = gunzip(b, n, 4096, &len)) == NULL) {
        return k;
    }
    k = kind(in, len);
    free(in);
    return k;
}

int
of_metafile_draw(const unsigned char *b, size_t n, of_faces_t *fs,
                 tiku_pdf_writer_t *w, float x, float y, float width,
                 float height)
{
    double box[4];
    unsigned char *in;
    size_t len;
    int k;

    box[0] = x;
    box[1] = y;
    box[2] = width;
    box[3] = height;
    if (b == NULL || !(width > 0) || !(height > 0) || w == NULL) {
        return -1;
    }
    if ((k = kind(b, n)) == 0 &&
        (in = gunzip(b, n, 64L << 20, &len)) != NULL) {
        k = kind(in, len) == 0 ? -1
            : of_metafile_draw(in, len, fs, w, x, y, width, height);
        free(in);
        return k;
    }
    return k == 1 ? of_emf_draw(b, n, fs, w, box) :
           k == 2 ? of_wmf_draw(b, n, fs, w, box) : -1;
}
