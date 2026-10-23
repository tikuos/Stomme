/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_chart_ink.c - the pen a chart is drawn with: words
 * measured and set in the document's faces, a face found for what one
 * lacks; paths of lines and arcs, filled and outlined through the
 * frame's place on the page; and the palette series take when their
 * file names no colour.
 *
 * Everything is measured in the frame's own points, its top left the
 * origin, and carried to the page through the frame's transform.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define INK_GLYPHS 256              /* characters one label draws        */
#define INK_TEXT   0xff595959u      /* words when the file names no ink  */

/* ---------------------------------------------------------------- words  */

/** @brief The face @p f names, the theme's body face when it names
 *         none.  @return its index, or -1 with no faces at all. */
static int
face_of(ch_chart_t *k, const ch_font_t *f)
{
    const char *name = f->font[0] ? dml_font(k->c, f->font) : NULL;

    if (name == NULL || name[0] == '\0') {
        name = k->c->theme != NULL && k->c->theme->minor[0]
               ? k->c->theme->minor : "Calibri";
    }
    return k->c->fs != NULL ? of_face(k->c->fs, name, f->bold, f->italic)
                            : -1;
}

/** @brief The next character of UTF-8 text @p s, @p s moved past it. */
static uint32_t
next_char(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp = *p++;
    int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;

    cp &= more ? 0x3fu >> more : 0xffu;
    while (more-- > 0 && (*p & 0xc0) == 0x80) {
        cp = cp << 6 | (*p++ & 0x3fu);
    }
    *s = (const char *)p;
    return cp;
}

/**
 * @brief Text @p s in face @p face at @p size laid along a baseline from
 *        @p x, @p y: each run of one face drawn when @p w is given, the
 *        width either way.
 */
static float
runs(ch_chart_t *k, int face, float size, uint32_t color, const char *s,
     float x, float y, tiku_pdf_writer_t *w)
{
    of_faces_t *fs = k->c->fs;
    tiku_pdf_glyph_t g[INK_GLYPHS];
    of_frag_t run;
    float pen = 0;

    memset(&run, 0, sizeof run);
    run.kind = OF_FRAG_GLYPHS;
    run.g = g;
    run.size = size;
    run.color = color;
    run.face = -1;
    while (s != NULL && *s && run.ng < INK_GLYPHS) {
        uint32_t cp = next_char(&s);
        unsigned gid;
        int f = of_face_for(fs, face, cp, &gid);

        if (f != run.face && run.ng > 0 && w != NULL) {
            of_draw_glyphs(fs, &run, 0, 0, w);
            run.ng = 0;
        }
        if (run.ng == 0) {
            run.face = f;
            run.x = x + pen;
            run.y = y;
            run.bold = fs->face[f].f.bold_synth;
            run.italic = fs->face[f].f.italic_synth;
        }
        g[run.ng].gid = gid;
        g[run.ng].cp = cp;
        g[run.ng++].x = x + pen - run.x;
        pen += of_face_advance(fs, f, gid, size);
    }
    if (run.ng > 0 && w != NULL) {
        of_draw_glyphs(fs, &run, 0, 0, w);
    }
    return pen;
}

float
ch_width(ch_chart_t *k, const ch_font_t *f, const char *s)
{
    int face = face_of(k, f);

    return face < 0 || s == NULL ? 0 : runs(k, face, f->size, 0, s, 0, 0,
                                            NULL);
}

float
ch_line_h(const ch_font_t *f)
{
    return f->size * 1.2f;
}

void
ch_text(ch_chart_t *k, const ch_font_t *f, const char *s, float x,
        float y, int align, int valign, float turn)
{
    int face = face_of(k, f);
    float wide, top;
    dml_mat_t at = {1, 0, 0, 1, 0, 0}, m;
    double r = turn * M_PI / 180;

    if (face < 0 || s == NULL || s[0] == '\0') {
        return;
    }
    wide = runs(k, face, f->size, 0, s, 0, 0, NULL);
    top = valign == 0 ? 0 : valign == 1 ? -ch_line_h(f) / 2
                                        : -ch_line_h(f);
    at.a = cos(r);
    at.b = sin(r);
    at.c = -sin(r);
    at.d = cos(r);
    at.e = x;
    at.f = y;
    m = dml_mul(&at, k->m);
    tiku_pdf_writer_save(k->c->w);
    tiku_pdf_writer_transform(k->c->w, m.a, m.b, m.c, m.d, m.e, m.f);
    (void)runs(k, face, f->size, f->color ? f->color : INK_TEXT, s,
               align == 0 ? 0 : align == 1 ? -wide / 2 : -wide,
               top + f->size * 0.93f, k->c->w);
    tiku_pdf_writer_restore(k->c->w);
}

/** @brief The next line of @p s into @p out: up to its newline, which
 *         @p s is moved past.  @return whether there was one. */
static int
next_line(const char **s, char *out, size_t max)
{
    size_t n = strcspn(*s, "\n");

    if (**s == '\0') {
        return 0;
    }
    n = n < max ? n : max - 1;
    memcpy(out, *s, n);
    out[n] = '\0';
    *s += strcspn(*s, "\n");
    *s += **s == '\n';
    return 1;
}

void
ch_block_size(ch_chart_t *k, const ch_font_t *f, const char *s, float *w,
              float *h)
{
    char line[512];
    float one;

    *w = *h = 0;
    while (s != NULL && next_line(&s, line, sizeof line)) {
        one = ch_width(k, f, line);
        *w = one > *w ? one : *w;
        *h += ch_line_h(f);
    }
}

void
ch_text_block(ch_chart_t *k, const ch_font_t *f, const char *s, float x,
              float y, int align, int valign)
{
    char line[512];
    float w, h;

    ch_block_size(k, f, s, &w, &h);
    y -= valign == 0 ? 0 : valign == 1 ? h / 2 : h;
    while (s != NULL && next_line(&s, line, sizeof line)) {
        ch_text(k, f, line, x, y, align, 0, 0);
        y += ch_line_h(f);
    }
}

/* ---------------------------------------------------------------- paths  */

/** @brief Room for @p n more floats in the path.  @return where they go,
 *         or NULL out of memory, the path then let go of and marked
 *         bad. */
static float *
room(ch_path_t *p, int n)
{
    dml_sub_t *s = &p->g.sub[0];

    if (s->n + n > s->cap) {
        int cap = s->cap ? s->cap * 2 : 64;
        float *pt;

        while (cap < s->n + n) {
            cap *= 2;
        }
        if ((pt = realloc(s->pt, (size_t)cap * sizeof *pt)) == NULL) {
            free(s->pt);
            s->pt = NULL;
            s->n = s->cap = 0;
            p->g.nsub = 0;          /* drawn as nothing */
            p->bad = 1;
            return NULL;
        }
        s->pt = pt;
        s->cap = cap;
    }
    s->n += n;
    return s->pt + s->n - n;
}

void
ch_path(ch_path_t *p, float ox, float oy, int fill, int stroke)
{
    memset(p, 0, sizeof *p);
    p->g.nsub = 1;
    p->g.sub[0].fill = fill;
    p->g.sub[0].stroke = stroke;
    p->ox = ox;
    p->oy = oy;
}

/** @brief A path op and its one point. */
static void
op(ch_path_t *p, char what, float x, float y)
{
    float *f = p->g.nsub ? room(p, 3) : NULL;

    if (f != NULL) {
        f[0] = what;
        f[1] = x - p->ox;
        f[2] = y - p->oy;
    }
}

void
ch_move(ch_path_t *p, float x, float y)
{
    op(p, 'M', x, y);
}

void
ch_line(ch_path_t *p, float x, float y)
{
    op(p, 'L', x, y);
}

void
ch_curve(ch_path_t *p, float x1, float y1, float x2, float y2, float x3,
         float y3)
{
    float *f = p->g.nsub ? room(p, 7) : NULL;

    if (f != NULL) {
        f[0] = 'C';
        f[1] = x1 - p->ox;
        f[2] = y1 - p->oy;
        f[3] = x2 - p->ox;
        f[4] = y2 - p->oy;
        f[5] = x3 - p->ox;
        f[6] = y3 - p->oy;
    }
}

void
ch_close(ch_path_t *p)
{
    float *f = p->g.nsub ? room(p, 1) : NULL;

    if (f != NULL) {
        f[0] = 'Z';
    }
}

void
ch_arc(ch_path_t *p, float cx, float cy, float r, double a0, double a1)
{
    double turn = fabs(a1 - a0), step, k;
    int n = isfinite(turn) ? (int)ceil(fmin(turn, 16) / (M_PI / 2)) : 0, i;
    float *f;

    if (n == 0 && !isfinite(turn)) {
        return;                     /* angles of no number: no arc */
    }
    n = n < 1 ? 1 : n > 8 ? 8 : n;
    step = (a1 - a0) / n;
    k = 4.0 / 3.0 * tan(step / 4) * r;
    for (i = 0; i < n; i++) {
        double a = a0 + step * i, b = a + step;

        if ((f = p->g.nsub ? room(p, 7) : NULL) == NULL) {
            return;
        }
        f[0] = 'C';
        f[1] = (float)(cx + r * sin(a) + k * cos(a)) - p->ox;
        f[2] = (float)(cy - r * cos(a) + k * sin(a)) - p->oy;
        f[3] = (float)(cx + r * sin(b) - k * cos(b)) - p->ox;
        f[4] = (float)(cy - r * cos(b) - k * sin(b)) - p->oy;
        f[5] = (float)(cx + r * sin(b)) - p->ox;
        f[6] = (float)(cy - r * cos(b)) - p->oy;
    }
}

void
ch_paint(ch_chart_t *k, ch_path_t *p, const dml_fill_t *f,
         const dml_line_t *l, float w, float h)
{
    dml_mat_t at = {1, 0, 0, 1, p->ox, p->oy}, m = dml_mul(&at, k->m);

    if (f != NULL && f->kind != DML_NOFILL) {
        dml_paint_fill(k->c, f, &p->g, &m, w > 0 ? w : 1, h > 0 ? h : 1);
    }
    if (l != NULL) {
        dml_paint_line(k->c, l, &p->g, &m);
    }
    k->mem.bad |= p->bad;           /* the chart read as short of memory */
    dml_geom_free(&p->g);
}

void
ch_rect(ch_chart_t *k, const ch_box_t *b, const dml_fill_t *f,
        const dml_line_t *l)
{
    ch_path_t p;

    ch_path(&p, b->x, b->y, 1, 1);
    ch_move(&p, b->x, b->y);
    ch_line(&p, b->x + b->w, b->y);
    ch_line(&p, b->x + b->w, b->y + b->h);
    ch_line(&p, b->x, b->y + b->h);
    ch_close(&p);
    ch_paint(k, &p, f, l, b->w, b->h);
}

void
ch_segment(ch_chart_t *k, float x0, float y0, float x1, float y1,
           const dml_line_t *l)
{
    ch_path_t p;

    ch_path(&p, 0, 0, 0, 1);
    ch_move(&p, x0, y0);
    ch_line(&p, x1, y1);
    ch_paint(k, &p, NULL, l, 0, 0);
}

/* -------------------------------------------------------------- palette  */

uint32_t
ch_palette(ch_chart_t *k, int i)
{
    static const double mod[4][2] = {{1, 0}, {0.6, 0}, {0.6, 0.4},
                                     {0.8, 0}};
    const dml_theme_t *t = k->c->theme;
    uint32_t base = t != NULL ? t->clr[DML_ACCENT1 + (i < 0 ? 0 : i % 6)]
                              : 0xff4472c4u;
    int round = (i < 0 ? 0 : i / 6) % 4;

    if (k->palette != NULL && k->npalette > 0) {    /* the file's own */
        return k->palette[(i < 0 ? 0 : i) % k->npalette];
    }
    return round == 0 ? base : dml_lum(base, mod[round][0], mod[round][1]);
}
