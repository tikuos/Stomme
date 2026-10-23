/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_pptx.c - a deck drawn, a slide a page.
 *
 * A slide is drawn over its layout, and the layout over its master: the
 * first background any of them has, the master's own shapes unless the
 * layout or the slide hides them, the layout's unless the slide hides
 * them, then the slide's.  A placeholder on a slide takes its place and
 * its look from the layout's placeholder of the same index or type, and
 * that one from the master's; an empty one is not drawn.  Each slide's
 * title is marked in the PDF's outline.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_pptx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU 12700.0f

const tiku_xml_node_t *
px_ph(const tiku_xml_node_t *shape)
{
    const tiku_xml_node_t *k;

    for (k = tiku_xml_child(shape, NULL); k != NULL; k = tiku_xml_next(k,
                                                                     NULL)) {
        if (strncmp(k->name, "p:nv", 4) == 0) {
            return tiku_xml_path(k, "p:nvPr/p:ph");
        }
    }
    return NULL;
}

/** @brief A placeholder's kind as inheritance sees it: title, body, or
 *         its own type (dt, ftr, sldNum, pic ...). */
static const char *
ph_kind(const tiku_xml_node_t *ph)
{
    const char *t = tiku_xml_attr(ph, "type");

    if (t == NULL || strcmp(t, "obj") == 0 || strcmp(t, "subTitle") == 0) {
        return "body";
    }
    return strcmp(t, "ctrTitle") == 0 ? "title" : t;
}

const tiku_xml_node_t *
px_placeholder(const tiku_xml_node_t *tree, const tiku_xml_node_t *ph,
               int by_type)
{
    const char *idx = tiku_xml_attr(ph, "idx"), *kind = ph_kind(ph);
    const tiku_xml_node_t *k, *by_kind = NULL;

    for (k = tiku_xml_child(tree, NULL); k != NULL && ph != NULL;
         k = tiku_xml_next(k, NULL)) {
        const tiku_xml_node_t *p = px_ph(k);
        const char *pi = tiku_xml_attr(p, "idx");

        if (p == NULL) {
            continue;
        }
        if (!by_type && idx != NULL && pi != NULL && strcmp(idx, pi) == 0 &&
            strcmp(idx, "0") != 0) {
            return k;
        }
        if (by_kind == NULL && strcmp(ph_kind(p), kind) == 0) {
            by_kind = k;
        }
    }
    return by_kind;
}

/** @brief The properties a placeholder inherits: the layout's, else the
 *         master's, whichever places it. */
static const tiku_xml_node_t *
inherited(const px_t *x, const tiku_xml_node_t *ph)
{
    const tiku_xml_node_t *l = px_placeholder(x->layout, ph, 0);
    const tiku_xml_node_t *m = px_placeholder(x->master, l ? px_ph(l) : ph,
                                              1);
    const tiku_xml_node_t *lp = tiku_xml_child(l, "p:spPr");

    if (lp != NULL && tiku_xml_child(lp, "a:xfrm") != NULL) {
        return lp;
    }
    return m != NULL ? tiku_xml_child(m, "p:spPr") : lp;
}

/** @brief A slide's, layout's or master's background: its own fill, or
 *         the theme's it refers to.  @return 1 when it had one. */
static int
background(px_t *x, const tiku_xml_node_t *csld, const char *part)
{
    const tiku_xml_node_t *bg = tiku_xml_child(csld, "p:bg");
    const tiku_xml_node_t *pr = tiku_xml_child(bg, "p:bgPr");
    const dml_mat_t id = {1, 0, 0, 1, 0, 0};
    dml_fill_t f;
    dml_geom_t g;
    int got;

    if (bg == NULL) {
        return 0;
    }
    x->dml.part = part;
    got = pr != NULL ? dml_fill(&x->dml, pr, &f)
                     : dml_ref_fill(&x->dml, tiku_xml_child(bg, "p:bgRef"), 1,
                                    &f);
    if (!got) {
        return 0;                   /* no fill, or one not read here */
    }
    if (dml_geometry(NULL, x->w, x->h, &g) == 0) {
        dml_paint_fill(&x->dml, &f, &g, &id, x->w, x->h);
    }
    dml_geom_free(&g);
    return 1;
}

/** @brief A graphic frame met in a group, as DrawingML hands it back:
 *         px_frame(). */
static int
frame(void *ctx, const tiku_xml_node_t *e, const dml_mat_t *m)
{
    return px_frame(ctx, e, m);
}

/** @brief One element of a shape tree: a graphic frame, or anything
 *         DrawingML draws, a placeholder with what it inherits. */
static int
element(px_t *x, const tiku_xml_node_t *e, int slide)
{
    const dml_mat_t id = {1, 0, 0, 1, 0, 0};
    const tiku_xml_node_t *ph = px_ph(e);

    if (ph != NULL && !slide) {
        return 0;                   /* a layout's or master's placeholder */
    }
    if (tiku_xml_is(e, "p:graphicFrame")) {
        return px_frame(x, e, &id);
    }
    if (ph != NULL && !tiku_xml_is(e, "p:pic") && !px_has_text(e)) {
        const tiku_xml_node_t *sp = tiku_xml_child(e, "p:spPr");

        if (tiku_xml_child(sp, "a:solidFill") == NULL &&
            tiku_xml_child(sp, "a:blipFill") == NULL) {
            return 0;               /* empty: a slide show draws nothing */
        }
    }
    return dml_draw(&x->dml, e, &id, ph != NULL ? inherited(x, ph) : NULL);
}

/** @brief A shape tree's elements, in order, from part @p part. */
static int
tree(px_t *x, const tiku_xml_node_t *t, const char *part, int slide)
{
    const tiku_xml_node_t *k;
    int err = 0;

    x->dml.part = part;
    for (k = tiku_xml_child(t, NULL); k != NULL && err == 0;
         k = tiku_xml_next(k, NULL)) {
        if (!tiku_xml_is(k, "p:nvGrpSpPr") && !tiku_xml_is(k, "p:grpSpPr"))
            err = element(x, k, slide);
    }
    return err;
}

/** @brief Whether a slide or layout lets its master's shapes show. */
static int
shows_master(const tiku_xml_node_t *root)
{
    const char *v = tiku_xml_attr(root, "showMasterSp");

    return v == NULL || (strcmp(v, "0") != 0 && strcmp(v, "false") != 0);
}

/** @brief The colour map: the master's, overridden by a layout or slide
 *         that says so. */
static void
colour_map(px_t *x, const tiku_xml_node_t *master, const tiku_xml_node_t *l,
           const tiku_xml_node_t *s)
{
    const tiku_xml_node_t *o;

    dml_map_read(&x->dml, tiku_xml_child(master, "p:clrMap"));
    o = tiku_xml_path(l, "p:clrMapOvr/a:overrideClrMapping");
    if (o != NULL) {
        dml_map_read(&x->dml, o);
    }
    o = tiku_xml_path(s, "p:clrMapOvr/a:overrideClrMapping");
    if (o != NULL) {
        dml_map_read(&x->dml, o);
    }
}

/** @brief The master's theme, read unless it is the one already read. */
static void
theme(px_t *x, const char *master)
{
    const of_rel_t *r = of_pkg_rel_type(x->pkg, master, "theme");

    if (r == NULL || strcmp(r->target, x->theme_part) == 0) {
        return;
    }
    dml_theme_free(&x->theme);
    (void)dml_theme_load(x->pkg, r->target, &x->theme);
    snprintf(x->theme_part, sizeof x->theme_part, "%s", r->target);
    x->dml.theme = &x->theme;
}

/** @brief A slide's title, for the outline: its title placeholder's
 *         text, or "Slide n". */
static void
title_of(const tiku_xml_node_t *t, int n, char *out, size_t max)
{
    const tiku_xml_node_t *k;

    snprintf(out, max, "Slide %d", n);
    for (k = tiku_xml_child(t, NULL); k != NULL; k = tiku_xml_next(k, NULL)) {
        const char *type = tiku_xml_attr(px_ph(k), "type");
        char text[160];

        if (type != NULL && (strcmp(type, "title") == 0 ||
                             strcmp(type, "ctrTitle") == 0) &&
            tiku_xml_text(tiku_xml_child(k, "p:txBody"), text, sizeof text)
            > 0) {
            snprintf(out, max, "%s", text);
            return;
        }
    }
}

/** @brief The three trees of slide @p part drawn: backgrounds first, then
 *         the master's shapes, the layout's, the slide's. */
static int
draw_slide(px_t *x, const char *part, tiku_xml_t *sx, tiku_xml_t *lx,
           tiku_xml_t *mx)
{
    const tiku_xml_node_t *s = tiku_xml_root(sx), *l = tiku_xml_root(lx);
    const tiku_xml_node_t *m = tiku_xml_root(mx);
    const tiku_xml_node_t *scs = tiku_xml_child(s, "p:cSld");
    const tiku_xml_node_t *lcs = tiku_xml_child(l, "p:cSld");
    const tiku_xml_node_t *mcs = tiku_xml_child(m, "p:cSld");
    int err = 0;

    x->layout = tiku_xml_child(lcs, "p:spTree");
    x->master = tiku_xml_child(mcs, "p:spTree");
    x->styles = tiku_xml_child(m, "p:txStyles");
    colour_map(x, m, l, s);
    if (!background(x, scs, part) && !background(x, lcs, x->layout_part)) {
        (void)background(x, mcs, x->master_part);
    }
    if (shows_master(l) && shows_master(s)) {
        err = tree(x, x->master, x->master_part, 0);
    }
    if (err == 0 && shows_master(s)) {
        err = tree(x, x->layout, x->layout_part, 0);
    }
    x->slide_part = part;
    return err == 0 ? tree(x, tiku_xml_child(scs, "p:spTree"), part, 1) : err;
}

/** @brief Slide @p part drawn on a page of its own, its title marked in
 *         the outline; a hidden one is left out.  @return 0, or -1 when
 *         there was no memory. */
static int
slide(px_t *x, const char *part)
{
    const of_rel_t *lr = of_pkg_rel_type(x->pkg, part, "slideLayout");
    const of_rel_t *mr = lr ? of_pkg_rel_type(x->pkg, lr->target,
                                              "slideMaster") : NULL;
    tiku_xml_t *sx = of_pkg_xml(x->pkg, part);
    const char *show = tiku_xml_attr(tiku_xml_root(sx), "show");
    tiku_xml_t *lx, *mx;
    char title[160];
    int page, err = 0;

    if (show != NULL && (strcmp(show, "0") == 0 ||
                         strcmp(show, "false") == 0)) {
        tiku_xml_free(sx);          /* hidden: counted, not shown */
        return 0;
    }
    lx = lr ? of_pkg_xml(x->pkg, lr->target) : NULL;
    mx = mr ? of_pkg_xml(x->pkg, mr->target) : NULL;
    page = tiku_pdf_writer_page(x->dml.w, x->w, x->h);
    x->drawn += page >= 0;
    if (page < 0) {
        err = -1;
    } else if (sx != NULL) {
        x->layout_part = lr ? lr->target : part;
        x->master_part = mr ? mr->target : part;
        theme(x, x->master_part);
        err = draw_slide(x, part, sx, lx, mx);
        title_of(tiku_xml_path(tiku_xml_root(sx), "p:cSld/p:spTree"),
                 x->number, title, sizeof title);
        tiku_pdf_writer_mark(x->dml.w, 0, title, page, 0);
    }
    tiku_xml_free(sx);
    tiku_xml_free(lx);
    tiku_xml_free(mx);
    return err;
}

/** @brief The deck's title and author from its core properties, for the
 *         file's Info. */
static void
info(px_t *x)
{
    const of_rel_t *r = of_pkg_rel_type(x->pkg, "", "core-properties");
    tiku_xml_t *c = r ? of_pkg_xml(x->pkg, r->target) : NULL;
    char title[256] = "", who[256] = "";

    (void)tiku_xml_text(tiku_xml_child(tiku_xml_root(c), "dc:title"), title,
                        sizeof title);
    (void)tiku_xml_text(tiku_xml_child(tiku_xml_root(c), "dc:creator"), who,
                        sizeof who);
    tiku_pdf_writer_info(x->dml.w, title[0] ? title : NULL,
                         who[0] ? who : NULL);
    tiku_xml_free(c);
}

int
of_pptx_draw(of_pkg_t *pkg, of_faces_t *fs, tiku_pdf_writer_t *w, char *why,
             size_t max)
{
    const of_rel_t *m = of_pkg_rel_type(pkg, "", "officeDocument"), *r;
    const char *main = m != NULL ? m->target : "ppt/presentation.xml";
    tiku_xml_t *pres = of_pkg_xml(pkg, main);
    const tiku_xml_node_t *root = tiku_xml_root(pres), *id;
    const tiku_xml_node_t *sz = tiku_xml_child(root, "p:sldSz");
    px_t *x = calloc(1, sizeof *x);
    int err = 0;

    if (root == NULL || x == NULL || (x->d = of_doc_new()) == NULL) {
        snprintf(why, max, "%s", root == NULL ? "That presentation is "
                 "damaged: its slides could not be read."
                 : "There is not enough memory to open it.");
        tiku_xml_free(pres);
        free(x);
        return -1;
    }
    x->pkg = pkg;
    x->w = tiku_xml_attr(sz, "cx") ? (float)atof(tiku_xml_attr(sz, "cx")) /
           EMU : 720;
    x->h = tiku_xml_attr(sz, "cy") ? (float)atof(tiku_xml_attr(sz, "cy")) /
           EMU : 540;
    x->deftext = tiku_xml_child(root, "p:defaultTextStyle");
    x->dml.pkg = pkg;
    x->dml.w = w;
    x->dml.fs = fs;
    x->dml.d = x->d;
    x->dml.text = px_text;
    x->dml.text_ctx = x;
    x->dml.frame = frame;
    x->dml.frame_ctx = x;
    x->dml.text_pt = 18;            /* a chart's default text size */
    dml_map_read(&x->dml, NULL);
    info(x);
    for (id = tiku_xml_path(root, "p:sldIdLst/p:sldId"); id != NULL &&
         err == 0; id = tiku_xml_next(id, "p:sldId")) {
        r = of_pkg_rel(pkg, main, tiku_xml_attr(id, "r:id"));
        if (r != NULL && !r->external) {
            x->number++;
            err = slide(x, r->target);
        }
    }
    if (err == 0 && x->drawn == 0) {
        snprintf(why, max, "%s", x->number == 0 ? "That presentation has "
                 "no slides." : "Every slide of that presentation is "
                 "hidden.");
        err = -1;
    } else if (err != 0) {
        snprintf(why, max, "There is not enough memory to draw it.");
    }
    dml_done(&x->dml);
    dml_theme_free(&x->theme);
    of_doc_free(x->d);
    tiku_xml_free(pres);
    free(x);
    return err;
}

/** @brief The root of part @p xml, or NULL. */
static const tiku_xml_node_t *
root_of(const tiku_xml_t *xml)
{
    return xml != NULL ? tiku_xml_root(xml) : NULL;
}

/** @brief The inverse of @p m, which turns, flips and moves only. */
static dml_mat_t
undone(const dml_mat_t *m)
{
    double det = m->a * m->d - m->b * m->c;
    dml_mat_t r;

    r.a = m->d / det;
    r.b = -m->b / det;
    r.c = -m->c / det;
    r.d = m->a / det;
    r.e = -(m->e * r.a + m->f * r.c);
    r.f = -(m->e * r.b + m->f * r.d);
    return r;
}

/** @brief A page for frame @p gf shown at @p scale: matrix @p pm takes
 *         the frame's parent space to the page, where the frame stands
 *         unturned in its own box, sized as it shows.  @return 0, 1 for
 *         a frame with no room, or -1 when no page could be added. */
static int
frame_page(tiku_pdf_writer_t *w, const tiku_xml_node_t *gf,
           const double scale[2], dml_mat_t *pm)
{
    dml_mat_t one = {1, 0, 0, 1, 0, 0}, l, at;
    dml_mat_t by = {scale[0], 0, 0, scale[1], 0, 0};
    double sx, sy;
    float fw, fh;

    dml_xfrm(tiku_xml_child(gf, "p:xfrm"), &one, &l, &fw, &fh, NULL);
    sx = scale[0];
    sy = scale[1];
    if (!(fw > 0) || !(fh > 0) || !(sx > 0) || !(sy > 0)) {
        return 1;
    }
    if (tiku_pdf_writer_page(w, (float)(fw * sx), (float)(fh * sy)) < 0) {
        return -1;
    }
    at = undone(&l);
    by.a = sx;
    by.b = by.c = 0;
    by.d = sy;
    *pm = dml_mul(&at, &by);
    return 0;
}

int
of_pptx_frame(of_pkg_t *pkg, of_faces_t *fs, tiku_pdf_writer_t *w,
              const char *part, const tiku_xml_node_t *gf,
              const double scale[2])
{
    tiku_xml_t *sx = of_pkg_xml(pkg, part), *lx = NULL, *mx = NULL;
    const of_rel_t *lr = tiku_xml_is(root_of(sx), "p:sld")
                         ? of_pkg_rel_type(pkg, part, "slideLayout") : NULL;
    const char *layout = lr != NULL ? lr->target : part;
    const of_rel_t *mr = of_pkg_rel_type(pkg, layout, "slideMaster");
    const char *master = mr != NULL ? mr->target : layout;
    dml_mat_t m;
    px_t *x;
    int got = -1;

    if ((x = calloc(1, sizeof *x)) == NULL ||
        (x->d = of_doc_new()) == NULL) {
        free(x);
        tiku_xml_free(sx);
        return -1;
    }
    of_faces_unwritten(fs);
    x->pkg = pkg;
    x->dml.pkg = pkg;
    x->dml.w = w;
    x->dml.fs = fs;
    x->dml.d = x->d;
    x->dml.text = px_text;
    x->dml.text_ctx = x;
    x->dml.frame = frame;
    x->dml.frame_ctx = x;
    x->dml.text_pt = 18;            /* a chart's default text size */
    lx = layout != part ? of_pkg_xml(pkg, layout) : NULL;
    mx = master != layout ? of_pkg_xml(pkg, master) : NULL;
    x->slide_part = part;
    x->layout_part = layout;
    x->master_part = master;
    x->dml.part = part;             /* the frame's relationships */
    theme(x, master);
    colour_map(x, mx != NULL ? root_of(mx) : lx != NULL ? root_of(lx)
                                                        : root_of(sx),
               lx != NULL ? root_of(lx) : lr == NULL && mr != NULL
                                          ? root_of(sx) : NULL,
               lr != NULL ? root_of(sx) : NULL);
    got = frame_page(w, gf, scale, &m);
    got = got == 0 ? dml_frame_shown(&x->dml, gf, &m) : got > 0 ? 0 : -1;
    dml_done(&x->dml);
    dml_theme_free(&x->theme);
    of_doc_free(x->d);
    tiku_xml_free(sx);
    tiku_xml_free(lx);
    tiku_xml_free(mx);
    free(x);
    return got;
}
