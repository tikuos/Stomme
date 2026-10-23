/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_dml_draw.c - shapes drawn: fill, then outline, then text.
 *
 * A shape's properties are its own, else a placeholder's it inherits,
 * else what its style refers to in the theme.  Its geometry is made in
 * its own space and carried to the page through its transform -- offset,
 * turn, flips, and every group it is in -- point by point, so a line keeps
 * the width it asks for however a group is stretched.  Pictures are drawn
 * through the writer's own transform, clipped to their shape.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_dml.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU    12700.0              /* EMUs to a point                   */
#define DEPTH  24                   /* groups within groups              */

/* --------------------------------------------------------- transforms  */

dml_mat_t
dml_mul(const dml_mat_t *m, const dml_mat_t *n)
{
    dml_mat_t r;

    r.a = m->a * n->a + m->b * n->c;
    r.b = m->a * n->b + m->b * n->d;
    r.c = m->c * n->a + m->d * n->c;
    r.d = m->c * n->b + m->d * n->d;
    r.e = m->e * n->a + m->f * n->c + n->e;
    r.f = m->e * n->b + m->f * n->d + n->f;
    return r;
}

/** @brief A point through @p m. */
static void
apply(const dml_mat_t *m, double x, double y, double *ox, double *oy)
{
    *ox = m->a * x + m->c * y + m->e;
    *oy = m->b * x + m->d * y + m->f;
}

/** @brief An attribute in EMUs, as points. */
static double
emu(const tiku_xml_node_t *e, const char *name)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL ? atof(v) / EMU : 0;
}

void
dml_xfrm(const tiku_xml_node_t *xfrm, const dml_mat_t *parent, dml_mat_t *m,
         float *w, float *h, dml_mat_t *child)
{
    const tiku_xml_node_t *off = tiku_xml_child(xfrm, "a:off");
    const tiku_xml_node_t *ext = tiku_xml_child(xfrm, "a:ext");
    const char *rot = tiku_xml_attr(xfrm, "rot");
    const char *fh = tiku_xml_attr(xfrm, "flipH");
    const char *fv = tiku_xml_attr(xfrm, "flipV");
    double a = rot != NULL ? atof(rot) / 60000.0 * M_PI / 180 : 0;
    double sx = fh != NULL && (fh[0] == '1' || fh[0] == 't') ? -1 : 1;
    double sy = fv != NULL && (fv[0] == '1' || fv[0] == 't') ? -1 : 1;
    dml_mat_t local;

    *w = (float)emu(ext, "cx");
    *h = (float)emu(ext, "cy");
    local.a = cos(a) * sx;          /* flip, turn, about the middle */
    local.b = sin(a) * sx;
    local.c = -sin(a) * sy;
    local.d = cos(a) * sy;
    local.e = emu(off, "x") + *w / 2 - (local.a * *w / 2 + local.c * *h / 2);
    local.f = emu(off, "y") + *h / 2 - (local.b * *w / 2 + local.d * *h / 2);
    *m = dml_mul(&local, parent);
    if (child != NULL) {
        const tiku_xml_node_t *co = tiku_xml_child(xfrm, "a:chOff");
        const tiku_xml_node_t *ce = tiku_xml_child(xfrm, "a:chExt");
        double cw = emu(ce, "cx"), ch = emu(ce, "cy");
        dml_mat_t to = {cw > 0 ? *w / cw : 1, 0, 0, ch > 0 ? *h / ch : 1, 0,
                        0};

        to.e = -emu(co, "x") * to.a;
        to.f = -emu(co, "y") * to.d;
        *child = dml_mul(&to, m);
    }
}

/* --------------------------------------------------------------- fills  */

/** @brief One fill element (a:solidFill, a:gradFill ...) into @p f.
 *  @return 1 when it is one. */
static int
fill_of(const dml_ctx_t *c, const tiku_xml_node_t *e, dml_fill_t *f)
{
    const tiku_xml_node_t *gs, *blip, *src;
    const char *v;

    memset(f, 0, sizeof *f);
    if (tiku_xml_is(e, "a:noFill")) {
        return 1;
    }
    if (tiku_xml_is(e, "a:solidFill")) {
        f->kind = DML_SOLID;
        f->color = dml_color(c, e);
        return 1;
    }
    if (tiku_xml_is(e, "a:gradFill")) {
        f->kind = DML_GRADIENT;
        for (gs = tiku_xml_path(e, "a:gsLst/a:gs"); gs != NULL &&
             f->nstop < 16; gs = tiku_xml_next(gs, "a:gs")) {
            v = tiku_xml_attr(gs, "pos");
            f->stop[f->nstop].at = v != NULL ? atof(v) / 100000.0 : 0;
            f->stop[f->nstop++].argb = dml_color(c, gs);
        }
        v = tiku_xml_attr(tiku_xml_child(e, "a:lin"), "ang");
        f->angle = v != NULL ? atof(v) / 60000.0 : 90;
        f->radial = tiku_xml_child(e, "a:path") != NULL;
        return f->nstop > 0;
    }
    if (tiku_xml_is(e, "a:blipFill") || tiku_xml_is(e, "pic:blipFill") ||
        tiku_xml_is(e, "p:blipFill")) {
        const of_rel_t *r;

        blip = tiku_xml_child(e, "a:blip");
        r = of_pkg_rel(c->pkg, c->part, tiku_xml_attr(blip, "r:embed"));
        f->kind = DML_PICTURE;
        snprintf(f->picture, sizeof f->picture, "%s", r != NULL &&
                 !r->external ? r->target : "");
        src = tiku_xml_child(e, "a:srcRect");
        f->crop[0] = (float)(atof(tiku_xml_attr(src, "l") ?
                     tiku_xml_attr(src, "l") : "0") / 100000.0);
        f->crop[1] = (float)(atof(tiku_xml_attr(src, "t") ?
                     tiku_xml_attr(src, "t") : "0") / 100000.0);
        f->crop[2] = (float)(atof(tiku_xml_attr(src, "r") ?
                     tiku_xml_attr(src, "r") : "0") / 100000.0);
        f->crop[3] = (float)(atof(tiku_xml_attr(src, "b") ?
                     tiku_xml_attr(src, "b") : "0") / 100000.0);
        dml_blip(c, blip, &f->fx);
        return 1;
    }
    if (tiku_xml_is(e, "a:pattFill")) {
        f->kind = DML_PATTERN;
        f->color = dml_color(c, tiku_xml_child(e, "a:fgClr"));
        f->back = dml_color(c, tiku_xml_child(e, "a:bgClr"));
        return 1;
    }
    if (tiku_xml_is(e, "a:grpFill")) {
        f->kind = DML_GROUPFILL;
        return 1;
    }
    return 0;
}

int
dml_fill(const dml_ctx_t *c, const tiku_xml_node_t *e, dml_fill_t *f)
{
    const tiku_xml_node_t *k;

    for (k = tiku_xml_child(e, NULL); k != NULL; k = tiku_xml_next(k, NULL))
        if (fill_of(c, k, f)) {
            return 1;
        }
    memset(f, 0, sizeof *f);
    return 0;
}

/** @brief The @p idx'th (from 1) child of @p list, or NULL. */
static const tiku_xml_node_t *
nth(const tiku_xml_node_t *list, long idx)
{
    const tiku_xml_node_t *k = tiku_xml_child(list, NULL);

    while (k != NULL && --idx > 0) {
        k = tiku_xml_next(k, NULL);
    }
    return idx == 0 ? k : NULL;
}

int
dml_ref_fill(dml_ctx_t *c, const tiku_xml_node_t *ref, int bg, dml_fill_t *f)
{
    const char *v = tiku_xml_attr(ref, "idx");
    long idx = v != NULL ? atol(v) : 0;
    const tiku_xml_node_t *e;
    dml_ctx_t in_theme;

    memset(f, 0, sizeof *f);
    if (ref == NULL || c->theme == NULL || idx == 0) {
        return 0;
    }
    c->ph = dml_color(c, ref);
    e = idx >= 1000 || bg ? nth(c->theme->bgfills, idx % 1000)
                          : nth(c->theme->fills, idx);
    if (e == NULL) {
        return 0;
    }
    in_theme = *c;                  /* its pictures are the theme's */
    in_theme.part = c->theme->part;
    return fill_of(&in_theme, e, f);
}

/* --------------------------------------------------------------- lines  */

/** @brief A preset dash's pattern, in widths of the line. */
static int
dash_of(const char *v, double *d)
{
    static const struct { const char *name; double d[6]; int n; } p[] = {
        {"dot", {1, 1}, 2}, {"dash", {4, 3}, 2}, {"lgDash", {8, 3}, 2},
        {"dashDot", {4, 3, 1, 3}, 4}, {"lgDashDot", {8, 3, 1, 3}, 4},
        {"lgDashDotDot", {8, 3, 1, 3, 1, 3}, 6}, {"sysDash", {3, 1}, 2},
        {"sysDot", {1, 1}, 2}, {"sysDashDot", {3, 1, 1, 1}, 4},
        {"sysDashDotDot", {3, 1, 1, 1, 1, 1}, 6}};
    size_t i;

    for (i = 0; v != NULL && i < sizeof p / sizeof p[0]; i++) {
        if (strcmp(v, p[i].name) == 0) {
            memcpy(d, p[i].d, sizeof p[i].d);
            return p[i].n;
        }
    }
    return 0;
}

/** @brief A line end: its kind and size, from a:headEnd or a:tailEnd. */
static void
line_end(const tiku_xml_node_t *e, int *kind, float *w, float *l)
{
    const char *t = tiku_xml_attr(e, "type"), *sw = tiku_xml_attr(e, "w");
    const char *sl = tiku_xml_attr(e, "len");

    if (e == NULL) {
        return;
    }
    *kind = t == NULL || strcmp(t, "none") == 0 ? DML_END_NONE :
            strcmp(t, "stealth") == 0 ? DML_END_STEALTH :
            strcmp(t, "diamond") == 0 ? DML_END_DIAMOND :
            strcmp(t, "oval") == 0 ? DML_END_OVAL :
            strcmp(t, "arrow") == 0 ? DML_END_ARROW : DML_END_TRIANGLE;
    *w = sw == NULL || strcmp(sw, "med") == 0 ? 3 : sw[0] == 's' ? 2 : 5;
    *l = sl == NULL || strcmp(sl, "med") == 0 ? 3 : sl[0] == 's' ? 2 : 5;
}

int
dml_line(const dml_ctx_t *c, const tiku_xml_node_t *ln, dml_line_t *l)
{
    const tiku_xml_node_t *k;
    const char *cap = tiku_xml_attr(ln, "cap");
    dml_fill_t f;

    if (ln == NULL) {
        return 0;
    }
    if (tiku_xml_attr(ln, "w") != NULL) {
        l->width = (float)emu(ln, "w");
        l->width = l->width > 0 ? l->width : 0.5f;
    }
    if (cap != NULL) {
        l->cap = strcmp(cap, "rnd") == 0 ? 1 : strcmp(cap, "sq") == 0 ? 2 : 0;
    }
    if (dml_fill(c, ln, &f)) {
        l->color = f.kind == DML_SOLID ? f.color : f.kind == DML_GRADIENT
                   ? f.stop[0].argb : 0;
        if (f.kind == DML_NOFILL) {
            l->width = 0;
        }
    }
    for (k = tiku_xml_child(ln, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        if (tiku_xml_is(k, "a:prstDash")) {
            l->ndash = dash_of(tiku_xml_attr(k, "val"), l->dash);
        } else if (tiku_xml_is(k, "a:round")) {
            l->join = 1;
        } else if (tiku_xml_is(k, "a:bevel")) {
            l->join = 2;
        } else if (tiku_xml_is(k, "a:miter")) {
            l->join = 0;
        } else if (tiku_xml_is(k, "a:headEnd")) {
            line_end(k, &l->head, &l->head_w, &l->head_l);
        } else if (tiku_xml_is(k, "a:tailEnd")) {
            line_end(k, &l->tail, &l->tail_w, &l->tail_l);
        }
    }
    return 1;
}

int
dml_ref_line(dml_ctx_t *c, const tiku_xml_node_t *ref, dml_line_t *l)
{
    const char *v = tiku_xml_attr(ref, "idx");
    long idx = v != NULL ? atol(v) : 0;

    if (ref == NULL || c->theme == NULL || idx == 0) {
        return 0;
    }
    c->ph = dml_color(c, ref);
    return dml_line(c, nth(c->theme->lines, idx), l);
}

/* ------------------------------------------------------------- painting  */

/** @brief Subpath @p s through @p m as the writer's path. */
static void
trace(tiku_pdf_writer_t *w, const dml_sub_t *s, const dml_mat_t *m)
{
    int i = 0;
    double x[3], y[3];

    while (i < s->n) {
        char op = (char)s->pt[i++];
        int k, n = op == 'C' ? 3 : op == 'Z' ? 0 : 1;

        for (k = 0; k < n && i + 1 < s->n; k++, i += 2) {
            apply(m, s->pt[i], s->pt[i + 1], &x[k], &y[k]);
        }
        if (op == 'M') {
            tiku_pdf_writer_move(w, x[0], y[0]);
        } else if (op == 'L') {
            tiku_pdf_writer_line(w, x[0], y[0]);
        } else if (op == 'C' && k == 3) {
            tiku_pdf_writer_curve(w, x[0], y[0], x[1], y[1], x[2], y[2]);
        } else if (op == 'Z') {
            tiku_pdf_writer_close(w);
        }
    }
}

/** @brief Two colours mixed half and half: a pattern's tone. */
static uint32_t
mix(uint32_t a, uint32_t b)
{
    return 0xff000000u | (((a >> 16 & 0xff) + (b >> 16 & 0xff)) / 2) << 16 |
           (((a >> 8 & 0xff) + (b >> 8 & 0xff)) / 2) << 8 |
           ((a & 0xff) + (b & 0xff)) / 2;
}

/** @brief A gradient's line or circle for the box @p w by @p h, through
 *         @p m, its stops as the fill says. */
static void
gradient(const dml_fill_t *f, const dml_mat_t *m, float w, float h,
         tiku_pdf_gradient_t *g)
{
    double a = f->angle * M_PI / 180, len = fabs(w * cos(a)) +
               fabs(h * sin(a)), cx = w / 2.0, cy = h / 2.0;

    memset(g, 0, sizeof *g);
    g->stop = f->stop;
    g->nstop = f->nstop;
    if (f->radial) {
        double scale = sqrt(fabs(m->a * m->d - m->b * m->c));

        g->radial = 1;
        apply(m, cx, cy, &g->x0, &g->y0);
        g->x1 = g->x0;
        g->y1 = g->y0;
        g->r1 = sqrt(cx * cx + cy * cy) * scale;
        return;
    }
    apply(m, cx - cos(a) * len / 2, cy - sin(a) * len / 2, &g->x0, &g->y0);
    apply(m, cx + cos(a) * len / 2, cy + sin(a) * len / 2, &g->x1, &g->y1);
}

/** @brief A picture over the box @p w by @p h through @p m, cut as @p f
 *         says, inside geometry @p g; a grey box when it cannot be read. */
static void
picture(dml_ctx_t *c, const dml_fill_t *f, const dml_geom_t *g,
        const dml_mat_t *m, float w, float h)
{
    int n = dml_picture(c, f->picture, &f->fx), i;
    double cw = 1 - f->crop[0] - f->crop[2], ch = 1 - f->crop[1] - f->crop[3];
    tiku_pdf_paint_t p;
    size_t len = 0;

    const unsigned char *meta = n < 0 ? dml_metafile(c, f->picture, &len)
                                      : NULL;

    tiku_pdf_writer_save(c->w);
    for (i = 0; i < g->nsub; i++) {
        trace(c->w, &g->sub[i], m);
    }
    if ((n < 0 && meta == NULL) || cw < 0.01 || ch < 0.01) {
        memset(&p, 0, sizeof p);
        p.fill = 0xffe4e4e4u;
        tiku_pdf_writer_paint(c->w, &p);
        tiku_pdf_writer_restore(c->w);
        return;
    }
    tiku_pdf_writer_clip(c->w, 0);
    tiku_pdf_writer_transform(c->w, m->a, m->b, m->c, m->d, m->e, m->f);
    if (n >= 0) {
        tiku_pdf_writer_draw(c->w, n, -f->crop[0] * w / cw,
                             -f->crop[1] * h / ch, w / cw, h / ch);
    } else if (of_metafile_draw(meta, len, c->fs, c->w,
                                (float)(-f->crop[0] * w / cw),
                                (float)(-f->crop[1] * h / ch),
                                (float)(w / cw), (float)(h / ch)) != 0) {
        memset(&p, 0, sizeof p);    /* nothing it could draw */
        p.fill = 0xffe4e4e4u;
        tiku_pdf_writer_rect(c->w, 0, 0, w, h);
        tiku_pdf_writer_paint(c->w, &p);
    }
    tiku_pdf_writer_restore(c->w);
}

void
dml_paint_fill(dml_ctx_t *c, const dml_fill_t *f, const dml_geom_t *g,
               const dml_mat_t *m, float w, float h)
{
    tiku_pdf_paint_t p;
    tiku_pdf_gradient_t gr;
    int i;

    if (f->kind == DML_PICTURE) {
        picture(c, f, g, m, w, h);
        return;
    }
    for (i = 0; i < g->nsub && f->kind != DML_NOFILL; i++) {
        if (!g->sub[i].fill) {
            continue;
        }
        trace(c->w, &g->sub[i], m);
        if (f->kind == DML_GRADIENT) {
            gradient(f, m, w, h, &gr);
            tiku_pdf_writer_gradient(c->w, &gr);
        } else {
            memset(&p, 0, sizeof p);
            p.fill = f->kind == DML_PATTERN ? mix(f->color, f->back)
                                            : f->color;
            tiku_pdf_writer_paint(c->w, &p);
        }
    }
}

/** @brief The first point of subpath @p s and the direction into it; or
 *         its last point and the direction out of it.  @return 0 when it
 *         has no length. */
static int
end_of(const dml_sub_t *s, int last, double *x, double *y, double *dx,
       double *dy)
{
    double px = 0, py = 0, qx = 0, qy = 0;
    int i = 0, n = 0;

    while (i < s->n) {
        char op = (char)s->pt[i++];
        int k = op == 'C' ? 3 : op == 'Z' ? 0 : 1;

        for (; k > 0 && i + 1 < s->n; k--, i += 2) {
            px = qx;
            py = qy;
            qx = s->pt[i];
            qy = s->pt[i + 1];
            if (++n == 2 && !last) {
                *x = px;            /* the start, heading to the next */
                *y = py;
                *dx = px - qx;
                *dy = py - qy;
                return hypot(*dx, *dy) > 1e-6;
            }
        }
    }
    *x = qx;
    *y = qy;
    *dx = qx - px;
    *dy = qy - py;
    return n >= 2 && hypot(*dx, *dy) > 1e-6;
}

/** @brief An arrowhead of kind @p kind at (@p x, @p y) pointing along
 *         (@p dx, @p dy), in page space, for line @p l: @p wide and
 *         @p len line widths across and long. */
static void
arrowhead(dml_ctx_t *c, int kind, double x, double y, double dx, double dy,
          const dml_line_t *l, float wide, float len)
{
    double d = hypot(dx, dy), ux = dx / d, uy = dy / d;
    double hw = l->width * wide / 2 + 0.5, hl = l->width * len + 1;
    double bx = x - ux * hl, by = y - uy * hl;
    tiku_pdf_paint_t p;

    memset(&p, 0, sizeof p);
    p.fill = kind == DML_END_ARROW ? 0 : l->color;
    p.stroke = kind == DML_END_ARROW ? l->color : 0;
    p.width = l->width;
    p.join = 1;
    if (kind == DML_END_OVAL) {
        double r = hw;

        tiku_pdf_writer_move(c->w, x + r, y);
        tiku_pdf_writer_curve(c->w, x + r, y + r * 0.55, x + r * 0.55, y + r,
                              x, y + r);
        tiku_pdf_writer_curve(c->w, x - r * 0.55, y + r, x - r, y + r * 0.55,
                              x - r, y);
        tiku_pdf_writer_curve(c->w, x - r, y - r * 0.55, x - r * 0.55, y - r,
                              x, y - r);
        tiku_pdf_writer_curve(c->w, x + r * 0.55, y - r, x + r, y - r * 0.55,
                              x + r, y);
        tiku_pdf_writer_close(c->w);
    } else {
        tiku_pdf_writer_move(c->w, bx - uy * hw, by + ux * hw);
        tiku_pdf_writer_line(c->w, x, y);
        tiku_pdf_writer_line(c->w, bx + uy * hw, by - ux * hw);
        if (kind == DML_END_STEALTH || kind == DML_END_DIAMOND) {
            double k = kind == DML_END_STEALTH ? 0.6 : 2;

            tiku_pdf_writer_line(c->w, x - ux * hl * k, y - uy * hl * k);
        }
        if (kind != DML_END_ARROW) {
            tiku_pdf_writer_close(c->w);
        }
    }
    tiku_pdf_writer_paint(c->w, &p);
}

void
dml_paint_line(dml_ctx_t *c, const dml_line_t *l, const dml_geom_t *g,
               const dml_mat_t *m)
{
    tiku_pdf_paint_t p;
    double dash[8], x, y, dx, dy, tx, ty;
    int i, k;

    if (l->width <= 0 || (l->color >> 24) == 0) {
        return;
    }
    memset(&p, 0, sizeof p);
    p.stroke = l->color;
    p.width = l->width;
    p.cap = l->cap;
    p.join = l->join;
    for (k = 0; k < l->ndash && k < 8; k++) {
        dash[k] = l->dash[k] * l->width;
    }
    p.dash = l->ndash ? dash : NULL;
    p.ndash = l->ndash;
    for (i = 0; i < g->nsub; i++) {
        if (!g->sub[i].stroke) {
            continue;
        }
        trace(c->w, &g->sub[i], m);
        tiku_pdf_writer_paint(c->w, &p);
        if (l->head && end_of(&g->sub[i], 0, &x, &y, &dx, &dy)) {
            apply(m, x, y, &tx, &ty);
            apply(m, x + dx, y + dy, &dx, &dy);
            arrowhead(c, l->head, tx, ty, dx - tx, dy - ty, l, l->head_w,
                      l->head_l);
        }
        if (l->tail && end_of(&g->sub[i], 1, &x, &y, &dx, &dy)) {
            apply(m, x, y, &tx, &ty);
            apply(m, x + dx, y + dy, &dx, &dy);
            arrowhead(c, l->tail, tx, ty, dx - tx, dy - ty, l, l->tail_w,
                      l->tail_l);
        }
    }
}

/* -------------------------------------------------------------- shapes  */

/** @brief @p e's child whose local name is @p local, whatever its prefix
 *         (p:spPr, wps:spPr, dsp:spPr ...). */
static const tiku_xml_node_t *
local_child(const tiku_xml_node_t *e, const char *local)
{
    const tiku_xml_node_t *k;

    for (k = tiku_xml_child(e, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        const char *colon = strchr(k->name, ':');

        if (strcmp(colon != NULL ? colon + 1 : k->name, local) == 0) {
            return k;
        }
    }
    return NULL;
}

/** @brief Whether @p e's local name is @p local. */
static int
is_local(const tiku_xml_node_t *e, const char *local)
{
    const char *colon = e != NULL && e->name ? strchr(e->name, ':') : NULL;

    return colon != NULL && strcmp(colon + 1, local) == 0;
}

/** @brief A shape's fill: its own, the placeholder's, or its style's. */
static void
shape_fill(dml_ctx_t *c, const tiku_xml_node_t *sp,
           const tiku_xml_node_t *sppr, const tiku_xml_node_t *inherit,
           dml_fill_t *f)
{
    if (dml_fill(c, sppr, f) || dml_fill(c, inherit, f)) {
        return;
    }
    (void)dml_ref_fill(c, tiku_xml_path(local_child(sp, "style"),
                                        "a:fillRef"), 0, f);
}

/** @brief A shape's line: its style's, the placeholder's over that, its
 *         own over both. */
static void
shape_line(dml_ctx_t *c, const tiku_xml_node_t *sp,
           const tiku_xml_node_t *sppr, const tiku_xml_node_t *inherit,
           dml_line_t *l)
{
    memset(l, 0, sizeof *l);
    l->color = 0xff000000u;
    if (!dml_ref_line(c, tiku_xml_path(local_child(sp, "style"), "a:lnRef"),
                      l)) {
        l->width = 0;
    }
    (void)dml_line(c, tiku_xml_child(inherit, "a:ln"), l);
    (void)dml_line(c, tiku_xml_child(sppr, "a:ln"), l);
}

/** @brief Where a shape is, from its a:xfrm @p xfrm: its transform into
 *         @p m and size; at the top, with none, the box the context says.
 *  @return 0 when it is nowhere. */
static int
place(const dml_ctx_t *c, const tiku_xml_node_t *xfrm, const dml_mat_t *pm,
      dml_mat_t *m, float *w, float *h)
{
    dml_mat_t none;

    if (xfrm == NULL && c->depth == 0 && c->fit_w > 0 && c->fit_h > 0) {
        *m = *pm;
        *w = c->fit_w;
        *h = c->fit_h;
        return 1;
    }
    if (xfrm == NULL) {
        return 0;
    }
    dml_xfrm(xfrm, pm, m, w, h, &none);
    return 1;
}

/** @brief A shape or connector: its geometry filled and outlined, then its
 *         text as the caller sets it. */
static int
shape(dml_ctx_t *c, const tiku_xml_node_t *sp, const dml_mat_t *pm,
      const tiku_xml_node_t *inherit)
{
    const tiku_xml_node_t *sppr = local_child(sp, "spPr");
    const tiku_xml_node_t *xfrm = tiku_xml_child(sppr, "a:xfrm");
    const tiku_xml_node_t *geo = tiku_xml_child(sppr, "a:prstGeom") ||
                                 tiku_xml_child(sppr, "a:custGeom")
                                 ? sppr : inherit;
    dml_geom_t g;
    dml_fill_t f;
    dml_line_t l;
    dml_mat_t m;
    float w, h;

    if (!place(c, xfrm != NULL ? xfrm : tiku_xml_child(inherit, "a:xfrm"),
               pm, &m, &w, &h)) {
        return 0;                   /* nowhere to draw it */
    }
    if (dml_geometry(geo, w, h, &g) != 0) {
        dml_geom_free(&g);
        return -1;
    }
    shape_fill(c, sp, sppr, inherit, &f);
    dml_paint_fill(c, &f, &g, &m, w, h);
    shape_line(c, sp, sppr, inherit, &l);
    dml_paint_line(c, &l, &g, &m);
    if (c->text != NULL) {
        (void)c->text(c->text_ctx, sp, g.text, &m);
    }
    dml_geom_free(&g);
    return 0;
}

/** @brief A picture: its image in its geometry, then its outline. */
static int
pic(dml_ctx_t *c, const tiku_xml_node_t *p, const dml_mat_t *pm,
    const tiku_xml_node_t *inherit)
{
    const tiku_xml_node_t *sppr = local_child(p, "spPr");
    const tiku_xml_node_t *xfrm = tiku_xml_child(sppr, "a:xfrm");
    dml_geom_t g;
    dml_fill_t f;
    dml_line_t l;
    dml_mat_t m;
    float w, h;

    if (!place(c, xfrm != NULL ? xfrm : tiku_xml_child(inherit, "a:xfrm"),
               pm, &m, &w, &h)) {
        return 0;
    }
    if (dml_geometry(sppr, w, h, &g) != 0) {
        dml_geom_free(&g);
        return -1;
    }
    if (fill_of(c, local_child(p, "blipFill"), &f)) {
        picture(c, &f, &g, &m, w, h);
    }
    shape_line(c, p, sppr, inherit, &l);
    dml_paint_line(c, &l, &g, &m);
    dml_geom_free(&g);
    return 0;
}

/** @brief A group: each of its children, in its own space. */
static int
group(dml_ctx_t *c, const tiku_xml_node_t *e, const dml_mat_t *pm)
{
    const tiku_xml_node_t *pr = local_child(e, "grpSpPr"), *k;
    dml_mat_t m, child;
    float w, h;
    int err = 0;

    if (c->depth >= DEPTH) {
        return 0;
    }
    dml_xfrm(tiku_xml_child(pr, "a:xfrm"), pm, &m, &w, &h, &child);
    c->depth++;
    for (k = tiku_xml_child(e, NULL); k != NULL && err == 0;
         k = tiku_xml_next(k, NULL)) {
        if (!is_local(k, "grpSpPr") && !is_local(k, "nvGrpSpPr") &&
            !is_local(k, "cNvGrpSpPr")) {
            err = dml_draw(c, k, &child, NULL);
        }
    }
    c->depth--;
    return err;
}

int
dml_draw(dml_ctx_t *c, const tiku_xml_node_t *e, const dml_mat_t *m,
         const tiku_xml_node_t *inherit)
{
    if (e == NULL || e->name == NULL) {
        return 0;
    }
    if (is_local(e, "sp") || is_local(e, "wsp") || is_local(e, "cxnSp")) {
        return shape(c, e, m, inherit);
    }
    if (is_local(e, "pic")) {
        return pic(c, e, m, inherit);
    }
    if (is_local(e, "grpSp") || is_local(e, "wgp")) {
        return group(c, e, m);
    }
    if (is_local(e, "graphicFrame")) {
        return c->frame != NULL ? c->frame(c->frame_ctx, e, m)
                                : dml_frame(c, e, m);
    }
    if (tiku_xml_is(e, "mc:AlternateContent")) {
        const tiku_xml_node_t *k;

        for (k = tiku_xml_child(of_alternate(e), NULL); k;
             k = tiku_xml_next(k, NULL)) {
            if (dml_draw(c, k, m, inherit) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/** @brief The id, in its part's relationships, of the drawing a
 *         diagram's data part @p data says the office suite saved. */
static const char *
drawing_id(const tiku_xml_t *data)
{
    const tiku_xml_node_t *ext = tiku_xml_path(tiku_xml_root(data),
                                               "dgm:extLst/a:ext");

    for (; ext != NULL; ext = tiku_xml_next(ext, "a:ext")) {
        const char *id = tiku_xml_attr(tiku_xml_child(ext,
                                       "dsp:dataModelExt"), "relId");

        if (id != NULL) {
            return id;
        }
    }
    return NULL;
}

int
dml_diagram(dml_ctx_t *c, const tiku_xml_node_t *gd, const dml_mat_t *m)
{
    const char *part = c->part;
    const of_rel_t *dm = of_pkg_rel(c->pkg, part, tiku_xml_attr(
                             tiku_xml_child(gd, "dgm:relIds"), "r:dm"));
    tiku_xml_t *data = dm != NULL ? of_pkg_xml(c->pkg, dm->target) : NULL;
    const of_rel_t *dr = of_pkg_rel(c->pkg, part, drawing_id(data));
    tiku_xml_t *drawing;
    const tiku_xml_node_t *k;
    int err = 0;

    tiku_xml_free(data);
    dr = dr != NULL ? dr : of_pkg_rel_type(c->pkg, part, "diagramDrawing");
    drawing = dr != NULL ? of_pkg_xml(c->pkg, dr->target) : NULL;
    k = tiku_xml_child(tiku_xml_child(tiku_xml_root(drawing), "dsp:spTree"),
                       NULL);
    if (drawing == NULL) {
        return 0;
    }
    c->part = dr->target;           /* its pictures are its own */
    for (; k != NULL && err == 0; k = tiku_xml_next(k, NULL)) {
        err = dml_draw(c, k, m, NULL);
    }
    c->part = part;
    tiku_xml_free(drawing);
    return err != 0 ? -1 : 1;
}

/** @brief A chart in a frame drawn through @p m, laid out at the size it
 *         shows at: a group that shrinks its frame shrinks the chart's
 *         room, not its words, as PowerPoint lays it out again. */
static int
unscaled_chart(dml_ctx_t *c, const tiku_xml_node_t *gd, const dml_mat_t *m,
               float w, float h)
{
    double sx = hypot(m->a, m->b), sy = hypot(m->c, m->d);
    dml_mat_t u = *m;

    if (!(sx > 0) || !(sy > 0)) {
        return 0;
    }
    u.a /= sx;
    u.b /= sx;
    u.c /= sy;
    u.d /= sy;
    return dml_chart(c, gd, &u, (float)(w * sx), (float)(h * sy));
}

/* ---------------------------------------------------------------- OLE  */

/** @brief The p:oleObj under @p gd that keeps a picture of its object,
 *         looking in each branch of mc:AlternateContent; else the first
 *         found, or NULL. */
static const tiku_xml_node_t *
ole_obj(const tiku_xml_node_t *gd)
{
    const tiku_xml_node_t *k, *b, *o, *first = NULL;

    for (k = tiku_xml_child(gd, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        if (is_local(k, "oleObj")) {
            if (local_child(k, "pic") != NULL) {
                return k;
            }
            first = first != NULL ? first : k;
        }
        for (b = tiku_xml_is(k, "mc:AlternateContent") ?
                 tiku_xml_child(k, NULL) : NULL;
             b != NULL; b = tiku_xml_next(b, NULL)) {
            if ((o = local_child(b, "oleObj")) != NULL &&
                local_child(o, "pic") != NULL) {
                return o;
            }
            first = first != NULL ? first : o;
        }
    }
    return first;
}

/** @brief The picture part the legacy drawing's VML shape @p spid shows
 *         (Office 2007's way of keeping an object's picture), into @p out;
 *         "" when there is none. */
static void
vml_picture(const dml_ctx_t *c, const char *spid, char *out, size_t max)
{
    const of_rel_t *r = of_pkg_rel_type(c->pkg, c->part, "vmlDrawing");
    const tiku_xml_node_t *k;
    const char *id;
    char vml[256];
    tiku_xml_t *x;

    out[0] = '\0';
    if (spid == NULL || r == NULL || r->external) {
        return;
    }
    snprintf(vml, sizeof vml, "%s", r->target);
    if ((x = of_pkg_xml(c->pkg, vml)) == NULL) {
        return;
    }
    for (k = tiku_xml_child(tiku_xml_root(x), NULL); k != NULL;
         k = tiku_xml_next(k, NULL)) {
        id = tiku_xml_attr(k, "o:spid") != NULL ? tiku_xml_attr(k, "o:spid")
                                                : tiku_xml_attr(k, "id");
        if (tiku_xml_is(k, "v:shape") && id != NULL && strcmp(id, spid) == 0) {
            r = of_pkg_rel(c->pkg, vml, tiku_xml_attr(tiku_xml_child(k,
                                              "v:imagedata"), "o:relid"));
            if (r != NULL && !r->external) {
                snprintf(out, max, "%s", r->target);
            }
            break;
        }
    }
    tiku_xml_free(x);
}

/** @brief An embedded object's frame, @p w by @p h through @p m: the
 *         picture Office keeps of it.  @return 1 when it was drawn. */
static int
ole(dml_ctx_t *c, const tiku_xml_node_t *gd, const dml_mat_t *m, float w,
    float h)
{
    const tiku_xml_node_t *o = ole_obj(gd);
    const tiku_xml_node_t *pic = local_child(o, "pic");
    dml_fill_t f;
    dml_geom_t g;

    memset(&f, 0, sizeof f);
    if (o == NULL || (pic != NULL &&
                      !fill_of(c, local_child(pic, "blipFill"), &f))) {
        return 0;
    }
    if (pic == NULL) {
        f.kind = DML_PICTURE;
        vml_picture(c, tiku_xml_attr(o, "spid"), f.picture,
                    sizeof f.picture);
    }
    if (f.kind != DML_PICTURE || f.picture[0] == '\0' ||
        dml_geometry(NULL, w, h, &g) != 0) {
        return 0;
    }
    picture(c, &f, &g, m, w, h);
    dml_geom_free(&g);
    return 1;
}

int
dml_frame(dml_ctx_t *c, const tiku_xml_node_t *e, const dml_mat_t *pm)
{
    return dml_frame_shown(c, e, pm) < 0 ? -1 : 0;
}

int
dml_frame_shown(dml_ctx_t *c, const tiku_xml_node_t *e, const dml_mat_t *pm)
{
    const tiku_xml_node_t *gd = tiku_xml_path(e, "a:graphic/a:graphicData");
    dml_mat_t m, none;
    dml_fill_t f;
    dml_geom_t g;
    float w, h;
    int drawn = 0;

    dml_xfrm(local_child(e, "xfrm"), pm, &m, &w, &h, &none);
    if (!(w > 0) || !(h > 0)) {
        return 0;                   /* no room: nothing to show */
    }
    if (tiku_xml_child(gd, "c:chart") != NULL) {
        drawn = unscaled_chart(c, gd, &m, w, h);
    } else if (tiku_xml_child(gd, "dgm:relIds") != NULL) {
        drawn = dml_diagram(c, gd, &m);
    } else {
        drawn = ole(c, gd, &m, w, h);
    }
    if (drawn != 0) {
        return drawn < 0 ? -1 : 1;
    }
    memset(&f, 0, sizeof f);
    f.kind = DML_SOLID;
    f.color = 0xffeeeeeeu;          /* what cannot be drawn: its place */
    if (dml_geometry(NULL, w, h, &g) != 0) {
        dml_geom_free(&g);
        return -1;
    }
    dml_paint_fill(c, &f, &g, &m, w, h);
    dml_geom_free(&g);
    return 0;
}
