/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_gdi_text.c - a metafile's text: ExtTextOut's string in the
 * font selected, found among the document's faces, set from its
 * reference point as the text alignment says, each character where the
 * record's advances put it.
 *
 * Glyphs keep their own proportions, as GDI draws a font at the height
 * asked however the picture is stretched; only their places follow the
 * stretch.  Text stays upright whichever way the mapping turns y, and
 * turns with the escapement and the world.  A symbol font's codes
 * become the characters they draw.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_gdi.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_MAX 32768              /* characters one string draws       */

/** @brief Where a string is set: its baseline's start and axes in the
 *         box, and the font's measures, all in points. */
typedef struct {
    double ox, oy;                  /* the reference point               */
    double ax, ay;                  /* a glyph unit along the baseline   */
    double bx, by;                  /* and down the glyphs: one point    */
    double su;                      /* glyph units a logical unit is     */
    double em, asc, desc;           /* the size; above, below the line   */
    double stretch;                 /* the width asked, over the natural */
    int    face;
} gdi_set_t;

static const uint16_t cp1252[32] = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
    0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};

static const uint16_t cp1251[64] = {
    0x0402, 0x0403, 0x201a, 0x0453, 0x201e, 0x2026, 0x2020, 0x2021,
    0x20ac, 0x2030, 0x0409, 0x2039, 0x040a, 0x040c, 0x040b, 0x040f,
    0x0452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0459, 0x203a, 0x045a, 0x045c, 0x045b, 0x045f,
    0x00a0, 0x040e, 0x045e, 0x0408, 0x00a4, 0x0490, 0x00a6, 0x00a7,
    0x0401, 0x00a9, 0x0404, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x0407,
    0x00b0, 0x00b1, 0x0406, 0x0456, 0x0491, 0x00b5, 0x00b6, 0x00b7,
    0x0451, 0x2116, 0x0454, 0x00bb, 0x0458, 0x0405, 0x0455, 0x0457};

/** @brief Whether @p s holds @p word, whatever its case. */
static int
has(const char *s, const char *word)
{
    size_t n = strlen(word), i;

    for (; *s; s++) {
        for (i = 0; i < n && tolower((unsigned char)s[i]) == word[i]; i++) {
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether font @p f is Symbol or Wingdings, whose codes
 *         of_symbol() knows. */
static int
known(const gdi_font_t *f)
{
    return has(f->face, "symbol") || has(f->face, "wingdings");
}

/** @brief Whether font @p f draws its own codes, not a character set's. */
static int
symbolic(const gdi_font_t *f)
{
    return f->charset == 2 || known(f);
}

uint32_t
gdi_char(const gdi_font_t *f, unsigned c)
{
    c &= 0xff;
    if (c < 0x80 || symbolic(f)) {
        return c;
    }
    if (f->charset == 204) {        /* RUSSIAN_CHARSET: Windows-1251 */
        return c >= 0xc0 ? 0x410 + c - 0xc0 : cp1251[c - 0x80];
    }
    if (f->charset == 161 && c >= 0xb8 && c != 0xbb && c != 0xbd &&
        c != 0xd2) {                /* GREEK_CHARSET: Windows-1253 */
        return 0x380 + c - 0xb0;
    }
    return c < 0xa0 ? cp1252[c - 0x80] : c;
}

/** @brief The character @p c stands for in font @p f. */
static uint32_t
character(const gdi_font_t *f, uint32_t c)
{
    uint32_t k = c >= 0xf000 && c <= 0xf0ff ? c - 0xf000 : c;

    if (!symbolic(f) || k > 0xff) {
        return c;
    }
    if (known(f)) {
        return of_symbol(f->face, k);
    }
    return 0xf000 + k;              /* the font's own, if it is here */
}

/** @brief The advance of an average character in face @p face, in font
 *         units: lower case weighted as English uses it, as the OS/2
 *         table's first version measured it. */
static double
average(of_faces_t *fs, int face)
{
    static const unsigned char w[27] = {64, 14, 27, 35, 100, 20, 14, 42,
                                        63, 3, 6, 35, 20, 56, 56, 17, 4,
                                        49, 56, 71, 31, 10, 18, 3, 18, 2,
                                        166};
    double sum = 0;
    unsigned gid;
    int i, f;

    for (i = 0; i < 27; i++) {
        f = of_face_for(fs, face, i < 26 ? 'a' + (unsigned)i : ' ', &gid);
        sum += w[i] * of_face_advance(fs, f, gid,
                                      (float)fs->face[f].upem);
    }
    return sum / 1000;
}

/** @brief The face the font selected asks for.  @return its index, or
 *         -1 when there are no faces. */
static int
face_of(gdi_t *g)
{
    const gdi_font_t *f = &g->dc.font;

    if (g->fs == NULL) {
        return -1;
    }
    return of_face(g->fs, f->face[0] ? f->face : "Arial", f->weight >= 600,
                   f->italic);
}

/** @brief How string @p t is set, into @p s: in glyph units, a point
 *         each down the glyphs.  @return 0, or -1 when it cannot be. */
static int
set_up(gdi_t *g, const gdi_text_t *t, gdi_set_t *s)
{
    const gdi_font_t *f = &g->dc.font;
    const of_face_t *fc;
    double a[2], b[2], k[2], nb, sv, avg;

    if ((s->face = face_of(g)) < 0) {
        return -1;
    }
    avg = f->width > 0 ? average(g->fs, s->face) : 0;
    fc = &g->fs->face[s->face];     /* after: fallbacks move the faces */
    gdi_axes(g, f->escape * 3.14159265358979323846 / 1800, a, b, k);
    nb = hypot(b[0], b[1]);
    if (!(nb > 1e-9) || !(hypot(a[0], a[1]) > 1e-9) || !(k[0] > 1e-12) ||
        !(k[1] > 1e-12) || fc->upem <= 0) {
        return -1;
    }
    s->ax = a[0] / nb;
    s->ay = a[1] / nb;
    s->bx = b[0] / nb;
    s->by = b[1] / nb;
    s->su = k[0] * nb;
    sv = k[1] * nb;
    s->em = f->height < 0 ? -f->height * sv :
            f->height > 0 ? f->height * sv * fc->upem / fmax(fc->height, 1)
                          : 10;
    s->em = fmin(fmax(s->em, 0.05), 5000);
    s->asc = fc->ascent * s->em / fc->upem;
    s->desc = fmax(fc->height - fc->ascent, 0) * s->em / fc->upem;
    s->stretch = avg > 0 ? f->width * s->su / (avg * s->em / fc->upem)
                         : 1;
    s->stretch = s->stretch == s->stretch ?
                 fmin(fmax(s->stretch, 0.2), 5) : 1;
    gdi_map(g, t->x, t->y, &s->ox, &s->oy);
    return 0;
}

/** @brief The glyphs of @p t, their faces in @p face and places from the
 *         reference point (points, along the baseline) in @p gl.
 *  @return the advance of them all. */
static double
glyphs(gdi_t *g, const gdi_text_t *t, const gdi_set_t *s,
       tiku_pdf_glyph_t *gl, int *face)
{
    const gdi_font_t *f = &g->dc.font;
    const of_face_t *fc;
    double x = 0, adv;
    int i;

    for (i = 0; i < t->n; i++) {
        gl[i].x = x;
        if (t->opts & GDI_ETO_GLYPHS) {
            fc = &g->fs->face[s->face];
            face[i] = fc->f.exact ? s->face : -1;
            gl[i].gid = t->ch[i];
            gl[i].cp = 0;
        } else {
            gl[i].cp = character(f, t->ch[i]);
            face[i] = of_face_for(g->fs, s->face, gl[i].cp, &gl[i].gid);
        }
        adv = face[i] >= 0 ? of_face_advance(g->fs, face[i], gl[i].gid,
                                             (float)s->em) * s->stretch : 0;
        x += t->dx != NULL ? t->dx[i] * s->su
                           : adv + g->dc.extra * s->su;
    }
    return x;
}

/** @brief The string's ground: the box ETO_OPAQUE names, in the ground's
 *         colour. */
static void
opaque(gdi_t *g, const gdi_text_t *t)
{
    gdi_brush_t b;

    memset(&b, 0, sizeof b);
    b.color = g->dc.bk;
    gdi_boxes(g, t->rect, 1, &b, 0, 0);
}

/** @brief The clip ETO_CLIPPED names, the writer's until restored. */
static void
clipped(gdi_t *g, const double *r)
{
    static const int corner[4][2] = {{0, 1}, {2, 1}, {2, 3}, {0, 3}};
    double x, y;
    int i;

    for (i = 0; i < 4; i++) {
        gdi_map(g, r[corner[i][0]], r[corner[i][1]], &x, &y);
        if (i == 0) {
            tiku_pdf_writer_move(g->w, x, y);
        } else {
            tiku_pdf_writer_line(g->w, x, y);
        }
    }
    tiku_pdf_writer_close(g->w);
    tiku_pdf_writer_clip(g->w, 0);
}

/** @brief A band along the baseline from 0 to @p w, @p y down from it
 *         and @p h thick, in colour @p c: a ground, a rule. */
static void
band(gdi_t *g, double w, double y, double h, uint32_t c)
{
    tiku_pdf_paint_t p;

    memset(&p, 0, sizeof p);
    p.fill = c;
    tiku_pdf_writer_rect(g->w, fmin(0, w), y, fabs(w), h);
    tiku_pdf_writer_paint(g->w, &p);
}

/** @brief The glyphs drawn, a run for each face in turn; one at a time
 *         when ETO_PDY moves them up and down. */
static void
runs(gdi_t *g, const gdi_text_t *t, const gdi_set_t *s,
     tiku_pdf_glyph_t *gl, const int *face)
{
    of_frag_t fr;
    int i, j, k;
    double rise = 0;

    memset(&fr, 0, sizeof fr);
    fr.size = (float)s->em;
    fr.color = g->dc.text;
    for (i = 0; i < t->n; i = j) {
        for (j = i + 1; j < t->n && face[j] == face[i] && t->dy == NULL;
             j++) {
        }
        if (face[i] < 0) {
            continue;
        }
        for (k = i; k < j; k++) {
            gl[k].x /= s->stretch;
        }
        fr.face = face[i];
        fr.bold = (unsigned char)g->fs->face[face[i]].f.bold_synth;
        fr.italic = (unsigned char)g->fs->face[face[i]].f.italic_synth;
        fr.y = (float)rise;
        fr.g = gl + i;
        fr.ng = j - i;
        of_draw_glyphs(g->fs, &fr, 0, 0, g->w);
        if (t->dy != NULL) {
            rise -= t->dy[i] * s->su;
        }
    }
}

/** @brief The string's rules: an underline, a strikeout. */
static void
rules(gdi_t *g, const gdi_set_t *s, double w)
{
    const of_face_t *fc = &g->fs->face[s->face];
    double k = s->em / fc->upem, h;

    if (g->dc.font.underline) {
        h = fmax(fc->under_size * k, s->em / 20);
        band(g, w / s->stretch, (fc->under ? -fc->under * k : s->em / 10) -
             h / 2, h, g->dc.text);
    }
    if (g->dc.font.strike) {
        h = fmax(fc->strike_size * k, s->em / 20);
        band(g, w / s->stretch, (fc->strike ? -fc->strike * k :
                                 -s->em * 0.3) - h / 2, h, g->dc.text);
    }
}

/** @brief The reference point moved to where the string starts, as the
 *         alignment asks, for a string @p w wide; the current position
 *         moved past it when the alignment says to. */
static void
align(gdi_t *g, gdi_set_t *s, double w)
{
    int a = g->dc.align;
    double dx = (a & 6) == 6 ? -w / 2 : (a & 6) == 2 ? -w : 0;
    double dy = (a & 24) == 24 ? 0 : (a & 8) ? -s->desc : s->asc;
    double end = (a & 6) == 2 ? -w : w;

    if ((a & GDI_TA_UPDATECP) && (a & 6) != 6) {
        gdi_unmap(g, s->ox + s->ax * end, s->oy + s->ay * end, &g->dc.x,
                  &g->dc.y);
    }
    s->ox += s->ax * dx + s->bx * dy;
    s->oy += s->ay * dx + s->by * dy;
}

void
gdi_text(gdi_t *g, const gdi_text_t *t)
{
    tiku_pdf_glyph_t *gl;
    gdi_set_t s;
    int *face, n = t->n < TEXT_MAX ? t->n : TEXT_MAX;
    gdi_text_t tt = *t;
    double w;

    if (g->inpath || (g->budget -= n) <= 0) {
        return;
    }
    if (tt.opts & GDI_ETO_OPAQUE) {     /* even with no text: a fill */
        opaque(g, &tt);
    }
    if (n <= 0 || g->fs == NULL) {
        return;
    }
    tt.n = n;
    if (g->dc.align & GDI_TA_UPDATECP) {
        tt.x = g->dc.x;
        tt.y = g->dc.y;
    }
    gl = malloc((size_t)n * sizeof *gl);
    face = malloc((size_t)n * sizeof *face);
    if (gl == NULL || face == NULL || set_up(g, &tt, &s) != 0) {
        free(gl);
        free(face);
        return;
    }
    w = glyphs(g, &tt, &s, gl, face);
    gdi_clip_apply(g);
    align(g, &s, w);
    tiku_pdf_writer_save(g->w);
    if (tt.opts & GDI_ETO_CLIPPED) {
        clipped(g, tt.rect);
    }
    tiku_pdf_writer_transform(g->w, s.ax * s.stretch, s.ay * s.stretch,
                              s.bx, s.by, s.ox, s.oy);
    if (g->dc.bkmode == 2 && !(tt.opts & GDI_ETO_OPAQUE)) {
        band(g, w / s.stretch, -s.asc, s.asc + s.desc, g->dc.bk);
    }
    runs(g, &tt, &s, gl, face);
    rules(g, &s, w);
    tiku_pdf_writer_restore(g->w);
    g->drawn = 1;
    free(gl);
    free(face);
}
