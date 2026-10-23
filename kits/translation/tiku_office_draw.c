/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_draw.c - laid-out pages written as PDF.
 *
 * Each fragment becomes what the writer draws: glyphs in the face's font,
 * registered the first time it is used; pictures read from the package
 * once and cut as the document cuts them -- a Windows metafile drawn as
 * its records say, wherever it is shown; boxes, rules and the text
 * boxes' grounds; a drawing is its reader's to draw.  A picture this
 * cannot read is a grey box where it would be.  Links go out only to the
 * web, FTP, mail and telephone numbers; a bookmark's link leads to the
 * page it landed on.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_private.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

int
of_draw_image(of_doc_t *d, of_image_t *im, tiku_pdf_writer_t *w)
{
    unsigned char *bytes;
    size_t len;

    if (im == NULL || im->pdf == -2) {
        return -1;
    }
    if (im->pdf >= 0) {
        return im->pdf;
    }
    bytes = d->pkg ? of_pkg_read(d->pkg, im->part, OF_PART_MAX, &len) : NULL;
    im->pdf = bytes ? tiku_pdf_writer_image(w, bytes, len, NULL, NULL) : -1;
    if (im->pdf < 0 && bytes != NULL && of_metafile_is(bytes, len) &&
        (im->meta = of_alloc(&d->arena, len)) != NULL) {
        memcpy(im->meta, bytes, len);   /* drawn again wherever it is */
        im->nmeta = len;
    }
    free(bytes);
    if (im->pdf < 0) {
        im->pdf = -2;
        return -1;
    }
    return im->pdf;
}

void
of_draw_glyphs(of_faces_t *fs, const of_frag_t *g, float dx, float dy,
               tiku_pdf_writer_t *w)
{
    of_face_t *f = &fs->face[g->face];
    tiku_pdf_run_t run = {0};

    if (f->pdf == -1) {
        f->pdf = tiku_pdf_writer_font(w, f->f.path, f->f.index);
        if (f->pdf < 0) {
            f->pdf = -2;            /* never asked again */
        }
    }
    if (f->pdf < 0 || g->ng <= 0) {
        return;
    }
    run.font = f->pdf;
    run.size = g->size;
    run.argb = g->color ? g->color : 0xff000000u;
    run.bold = g->bold;
    run.italic = g->italic;
    run.x = g->x + dx;
    run.y = g->y + dy;
    tiku_pdf_writer_text(w, &run, g->g, g->ng);
}

#define PLACEHOLDER 0xffe4e4e4u     /* where a picture cannot be shown   */

/** @brief A picture drawn over the fragment's box, cut as its image says:
 *         what is left of it fills the box.  A grey box when it cannot be
 *         read. */
static void
picture(of_doc_t *d, of_faces_t *fs, const of_frag_t *g,
        tiku_pdf_writer_t *w)
{
    const of_image_t *im = g->image;
    int n = of_draw_image(d, g->image, w);
    float cw = 1 - im->crop[0] - im->crop[2], ch = 1 - im->crop[1] -
               im->crop[3];
    tiku_pdf_paint_t paint;

    if (cw <= 0.01f || ch <= 0.01f || (n < 0 && im->meta == NULL)) {
        n = -1;
    } else {
        tiku_pdf_writer_save(w);
        tiku_pdf_writer_rect(w, g->x, g->y, g->w, g->h);
        tiku_pdf_writer_clip(w, 0);
        if (n >= 0) {
            tiku_pdf_writer_draw(w, n, g->x - im->crop[0] * g->w / cw,
                                 g->y - im->crop[1] * g->h / ch, g->w / cw,
                                 g->h / ch);
        } else {
            n = of_metafile_draw(im->meta, im->nmeta, fs, w,
                                 g->x - im->crop[0] * g->w / cw,
                                 g->y - im->crop[1] * g->h / ch, g->w / cw,
                                 g->h / ch);
        }
        tiku_pdf_writer_restore(w);
    }
    if (n < 0) {
        memset(&paint, 0, sizeof paint);
        paint.fill = PLACEHOLDER;
        tiku_pdf_writer_rect(w, g->x, g->y, g->w, g->h);
        tiku_pdf_writer_paint(w, &paint);
    }
}

/** @brief A rule from the fragment's start, across and down its extent,
 *         dotted or dashed as it says. */
static void
rule(const of_frag_t *g, tiku_pdf_writer_t *w)
{
    tiku_pdf_paint_t p;
    double dash[2];

    memset(&p, 0, sizeof p);
    p.stroke = g->color ? g->color : 0xff000000u;
    p.width = g->size;
    if (g->dash == OF_LS_DOTTED || g->dash == OF_LS_DASHED) {
        dash[0] = g->dash == OF_LS_DOTTED ? g->size : g->size * 4;
        dash[1] = g->dash == OF_LS_DOTTED ? g->size * 1.5 : g->size * 2;
        p.dash = dash;
        p.ndash = 2;
    }
    tiku_pdf_writer_move(w, g->x, g->y);
    tiku_pdf_writer_line(w, g->x + g->w, g->y + g->h);
    tiku_pdf_writer_paint(w, &p);
}

/** @brief A text box's ground and outline. */
static void
box(const of_frag_t *g, tiku_pdf_writer_t *w)
{
    const of_float_t *fl = g->flt;
    tiku_pdf_paint_t p;

    if (fl == NULL) {
        return;
    }
    memset(&p, 0, sizeof p);
    p.fill = fl->fill;
    p.stroke = fl->line.color ? fl->line.color : 0xff000000u;
    p.width = fl->line.width > 0 ? fl->line.width : 0;
    tiku_pdf_writer_rect(w, g->x, g->y, g->w, g->h);
    tiku_pdf_writer_paint(w, &p);
}

/** @brief Whether @p uri may be followed from a document: http, https,
 *         ftp, mailto or tel, never a file or a script. */
static int
safe_link(const char *uri)
{
    static const char *const ok[] = {"http://", "https://", "mailto:",
                                     "ftp://", "tel:"};
    size_t i;

    for (i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        if (strncasecmp(uri, ok[i], strlen(ok[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief The fragment's link, if it has one: out of the document, or to
 *         the place its bookmark landed. */
static void
link(const of_layout_t *l, const of_frag_t *g, tiku_pdf_writer_t *w)
{
    float top = g->kind == OF_FRAG_GLYPHS ? g->y - g->size * 0.8f :
                g->kind == OF_FRAG_MATH ? g->y - g->math->asc : g->y;
    float h = g->kind == OF_FRAG_GLYPHS ? g->size : g->h;
    int i;

    if (g->link == NULL || g->w <= 0) {
        return;
    }
    if (g->clipped) {
        float bottom = top + h;
        if (top < g->y + g->clip_top) top = g->y + g->clip_top;
        if (bottom > g->y + g->clip_bottom) bottom = g->y + g->clip_bottom;
        h = bottom - top;
        if (h <= 0) return;
    }
    if (g->link[0] != '#') {
        if (safe_link(g->link)) {
            tiku_pdf_writer_link(w, g->x, top, g->w, h, g->link);
        }
        return;
    }
    for (i = 0; i < l->nplace; i++) {
        if (l->place[i].level < 0 && strcmp(l->place[i].name,
                                            g->link + 1) == 0) {
            tiku_pdf_writer_jump(w, g->x, top, g->w, h, l->place[i].page,
                                 l->place[i].y);
            return;
        }
    }
}

/** @brief A drawing its reader draws, over the fragment's box; a
 *         formula set in it, centred. */
static void
art(of_doc_t *d, of_faces_t *fs, const of_art_t *a, const of_frag_t *g,
    tiku_pdf_writer_t *w)
{
    const of_math_t *m = a->math;
    float box[4];

    if (m != NULL) {
        of_math_draw(fs, m, g->x + (g->w - m->w) / 2,
                     g->y + (g->h - m->asc - m->desc) / 2 + m->asc, w);
        return;
    }
    box[0] = g->x;
    box[1] = g->y;
    box[2] = g->w;
    box[3] = g->h;
    if (d->art_draw != NULL) {
        (void)d->art_draw(d, fs, a, box, w);
    }
}

void
of_draw_frag(of_doc_t *d, of_faces_t *fs, const of_frag_t *g, float dx,
             float dy, tiku_pdf_writer_t *w)
{
    of_frag_t at = *g;
    tiku_pdf_paint_t fill;

    at.x += dx;
    at.y += dy;
    if (g->clipped) {
        if (g->clip_bottom <= g->clip_top) return;
        tiku_pdf_writer_save(w);
        tiku_pdf_writer_rect(w, -1000000, at.y + g->clip_top, 2000000,
                              g->clip_bottom - g->clip_top);
        tiku_pdf_writer_clip(w, 0);
    }
    memset(&fill, 0, sizeof fill);
    if (g->kind == OF_FRAG_GLYPHS) {
        of_draw_glyphs(fs, g, dx, dy, w);
    } else if (g->kind == OF_FRAG_IMAGE && g->image != NULL &&
               g->image->art != NULL) {
        art(d, fs, g->image->art, &at, w);
    } else if (g->kind == OF_FRAG_ART && g->flt != NULL) {
        art(d, fs, g->flt->art, &at, w);
    } else if (g->kind == OF_FRAG_IMAGE && g->image != NULL) {
        picture(d, fs, &at, w);
    } else if (g->kind == OF_FRAG_RECT) {
        fill.fill = g->color;
        tiku_pdf_writer_rect(w, at.x, at.y, g->w, g->h);
        tiku_pdf_writer_paint(w, &fill);
    } else if (g->kind == OF_FRAG_RULE) {
        rule(&at, w);
    } else if (g->kind == OF_FRAG_BOX) {
        box(&at, w);
    } else if (g->kind == OF_FRAG_MATH && g->math != NULL) {
        of_math_draw(fs, g->math, at.x, at.y, w);
    }
    if (g->clipped) tiku_pdf_writer_restore(w);
}

int
of_draw(of_doc_t *d, of_faces_t *fs, const of_layout_t *l,
        tiku_pdf_writer_t *w)
{
    int p, i;

    for (p = 0; p < l->npage; p++) {
        const of_page_t *pg = &l->page[p];

        if (tiku_pdf_writer_page(w, pg->w, pg->h) < 0) {
            return -1;
        }
        for (i = 0; i < pg->nfrag; i++) {
            of_draw_frag(d, fs, &pg->frag[i], 0, 0, w);
            link(l, &pg->frag[i], w);
        }
    }
    for (i = 0; i < l->nplace; i++) {
        if (l->place[i].level >= 0) {
            tiku_pdf_writer_mark(w, l->place[i].level, l->place[i].name,
                                 l->place[i].page, l->place[i].y);
        }
    }
    return 0;
}
