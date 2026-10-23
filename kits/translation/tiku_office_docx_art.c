/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_docx_art.c - Word's drawings drawn as DrawingML: shapes,
 * groups, canvases, charts, diagrams as Word saved them, pictures that
 * are more than a picture -- cut to a shape, outlined, turned, recoloured;
 * and VML WordArt, which is how Word writes a watermark.
 *
 * A drawing is kept as its XML and drawn when its page is, by the engine
 * slides are drawn with, in the document theme's colours.  The text in
 * its shapes is Word's own: each text box is read as a story with the
 * drawing and set by the pager in the room its shape leaves; a diagram's
 * is DrawingML's, set as a slide's.  The parts drawings are in stay open
 * until the document has been drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_docx.h"
#include "tiku_office_pptx.h"
#include "tiku_glyphpath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU    12700.0f             /* EMUs to a point                   */
#define DEPTH  24                   /* groups within groups              */
#define WA_MAX 256                  /* WordArt's characters drawn        */
#define WA_SIZE 1000.0f             /* and the size they are measured at */

/** @brief What a document's drawings are drawn with, kept with it. */
typedef struct {
    dml_theme_t  theme;             /* its colours and formats           */
    px_t         px;                /* drawing, pictures given to the
                                       writer, and a diagram's text, set
                                       as a slide's is                   */
    tiku_xml_t **keep;              /* the parts drawings are in         */
    int          nkeep;
} dx_art_t;

/** @brief Attribute @p name of @p e in EMUs, as points; @p dflt absent. */
static float
emu(const tiku_xml_node_t *e, const char *name, float dflt)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL && *v ? (float)(atof(v) / EMU) : dflt;
}

/* ----------------------------------------------------------- choosing  */

/** @brief Whether picture @p pic is only a picture: rectangular, upright,
 *         not mirrored or outlined, its colours as they are. */
static int
plain_pic(const tiku_xml_node_t *pic)
{
    static const char *const fx[] = {"a:duotone", "a:grayscl", "a:lum",
                                     "a:alphaModFix", "a:clrChange",
                                     "a:biLevel"};
    const tiku_xml_node_t *sp = tiku_xml_child(pic, "pic:spPr");
    const tiku_xml_node_t *xfrm = tiku_xml_child(sp, "a:xfrm");
    const tiku_xml_node_t *ln = tiku_xml_child(sp, "a:ln");
    const tiku_xml_node_t *blip = tiku_xml_path(pic, "pic:blipFill/a:blip");
    const char *prst = tiku_xml_attr(tiku_xml_child(sp, "a:prstGeom"),
                                     "prst");
    size_t i;

    if (tiku_xml_child(sp, "a:custGeom") != NULL ||
        (prst != NULL && strcmp(prst, "rect") != 0) ||
        tiku_xml_attr(xfrm, "rot") != NULL ||
        tiku_xml_attr(xfrm, "flipH") != NULL ||
        tiku_xml_attr(xfrm, "flipV") != NULL) {
        return 0;
    }
    if (ln != NULL && tiku_xml_child(ln, "a:noFill") == NULL &&
        (tiku_xml_child(ln, "a:solidFill") != NULL ||
         tiku_xml_child(ln, "a:gradFill") != NULL)) {
        return 0;
    }
    for (i = 0; i < sizeof fx / sizeof fx[0]; i++) {
        if (tiku_xml_child(blip, fx[i]) != NULL) {
            return 0;
        }
    }
    return 1;
}

int
dx_art_wants(const tiku_xml_node_t *gd)
{
    const tiku_xml_node_t *pic = tiku_xml_child(gd, "pic:pic");

    if (pic != NULL) {
        return !plain_pic(pic);
    }
    return tiku_xml_child(gd, "wps:wsp") != NULL ||
           tiku_xml_child(gd, "wpg:wgp") != NULL ||
           tiku_xml_child(gd, "wpc:wpc") != NULL ||
           tiku_xml_child(gd, "dgm:relIds") != NULL ||
           tiku_xml_child(gd, "c:chart") != NULL;
}

/* ---------------------------------------------------------- text boxes  */

/** @brief The box around rectangle @p r (left top right bottom) carried
 *         through @p m, into @p out. */
static void
bounds(const dml_mat_t *m, const float *r, float *out)
{
    int i;

    for (i = 0; i < 4; i++) {
        double px = r[i & 1 ? 2 : 0], py = r[i & 2 ? 3 : 1];
        float x = (float)(m->a * px + m->c * py + m->e);
        float y = (float)(m->b * px + m->d * py + m->f);

        out[0] = i == 0 || x < out[0] ? x : out[0];
        out[1] = i == 0 || y < out[1] ? y : out[1];
        out[2] = i == 0 || x > out[2] ? x : out[2];
        out[3] = i == 0 || y > out[3] ? y : out[3];
    }
}

/** @brief Shape @p wsp's text box, when it has one, where it falls through
 *         @p m (filling @p w by @p h when it gives no place of its own),
 *         into @p boxes.  @return 0, or -1 out of memory. */
static int
shape_box(dx_t *x, const tiku_xml_node_t *wsp, const dml_mat_t *m,
          float w, float h, of_vec_t *boxes)
{
    const tiku_xml_node_t *sp = tiku_xml_child(wsp, "wps:spPr");
    const tiku_xml_node_t *body = tiku_xml_child(wsp, "wps:bodyPr");
    const tiku_xml_node_t *txbx = tiku_xml_path(wsp, "wps:txbx/"
                                                "w:txbxContent");
    const char *anchor = tiku_xml_attr(body, "anchor");
    dml_mat_t sm = *m, none;
    dml_geom_t g;
    of_textbox_t tb, *b;
    float r[4] = {0, 0, 0, 0};

    if (txbx == NULL || x->depth >= OF_NEST_MAX) {
        return 0;
    }
    if (tiku_xml_child(sp, "a:xfrm") != NULL) {
        dml_xfrm(tiku_xml_child(sp, "a:xfrm"), m, &sm, &w, &h, &none);
    } else if (w <= 0) {
        return 0;
    }
    if (dml_geometry(sp, w, h, &g) != 0) {
        dml_geom_free(&g);
        return -1;
    }
    bounds(&sm, g.text, r);
    dml_geom_free(&g);
    tb.x = r[0] + emu(body, "lIns", 7.2f);
    tb.y = r[1] + emu(body, "tIns", 3.6f);
    tb.w = r[2] - emu(body, "rIns", 7.2f) - tb.x;
    tb.h = r[3] - emu(body, "bIns", 3.6f) - tb.y;
    tb.anchor = anchor == NULL ? 0 : strcmp(anchor, "ctr") == 0 ? 1 :
                strcmp(anchor, "b") == 0 ? 2 : 0;
    x->depth++;
    tb.text = dx_story_apart(x, txbx, NULL);
    x->depth--;
    if (tb.text == NULL || (b = of_vec_push(boxes)) == NULL) {
        return -1;
    }
    *b = tb;
    return 0;
}

/** @brief The text boxes of the shapes under @p e -- a drawing, a group,
 *         a canvas -- through @p m into @p boxes; at the top (@p depth 0)
 *         a shape with no place of its own fills @p w by @p h. */
static int
boxes_in(dx_t *x, const tiku_xml_node_t *e, const dml_mat_t *m, float w,
         float h, int depth, of_vec_t *boxes)
{
    const tiku_xml_node_t *k;
    dml_mat_t gm, child;
    float gw, gh;
    int err = 0;

    for (k = tiku_xml_child(e, NULL); k != NULL && err == 0 &&
         depth < DEPTH; k = tiku_xml_next(k, NULL)) {
        if (tiku_xml_is(k, "wps:wsp")) {
            err = shape_box(x, k, m, depth == 0 ? w : 0, h, boxes);
        } else if (tiku_xml_is(k, "wpg:wgp") ||
                   tiku_xml_is(k, "wpg:grpSp")) {
            dml_xfrm(tiku_xml_path(k, "wpg:grpSpPr/a:xfrm"), m, &gm, &gw,
                     &gh, &child);
            err = boxes_in(x, k, &child, 0, 0, depth + 1, boxes);
        } else if (tiku_xml_is(k, "wpc:wpc")) {
            err = boxes_in(x, k, m, 0, 0, depth + 1, boxes);
        } else if (tiku_xml_is(k, "mc:AlternateContent")) {
            err = boxes_in(x, of_alternate(k), m, w, h, depth, boxes);
        }
    }
    return err;
}

/* ------------------------------------------------------------- WordArt  */

/** @brief WordArt's text @p s as glyphs of face @p face at WA_SIZE, into
 *         @p g.  @return how many. */
static int
wa_glyphs(of_faces_t *fs, int face, const char *s, tiku_pdf_glyph_t *g)
{
    float pen = 0;
    int n = 0;

    while (s != NULL && *s && n < WA_MAX) {
        uint32_t cp = (unsigned char)*s++;

        if (cp >= 0xc0) {           /* the rest of a UTF-8 character */
            int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : 1;

            cp &= 0x3fu >> more;
            while (more-- > 0 && (*s & 0xc0) == 0x80) {
                cp = cp << 6 | ((unsigned char)*s++ & 0x3fu);
            }
        }
        g[n].cp = cp;
        g[n].gid = tiku_ttf_glyph(fs->face[face].ttf, cp);
        g[n].x = pen;
        pen += of_face_advance(fs, face, g[n].gid, WA_SIZE);
        n++;
    }
    return n;
}

/** @brief The box the ink of glyphs @p g (@p n, face @p face, WA_SIZE)
 *         covers: left top width height, y down from the baseline.  It
 *         is measured small -- a path's extent stops at 1024 units -- and
 *         scaled back. */
static void
wa_ink(of_faces_t *fs, int face, const tiku_pdf_glyph_t *g, int n,
       float *ink)
{
    const of_face_t *f = &fs->face[face];
    tiku_path_t *path = tiku_path_new();
    float end = n > 0 ? (float)g[n - 1].x + WA_SIZE : WA_SIZE;
    float k = 800.0f / end, m[6] = {1, 0, 0, 1, 0, 0};
    tiku_hint_t hint;
    int i, box[4] = {0, 0, 0, 0};

    memset(&hint, 0, sizeof hint);
    hint.scale = WA_SIZE * k / (float)(f->upem > 0 ? f->upem : 1000);
    for (i = 0; path != NULL && i < n; i++) {
        m[4] = (float)g[i].x * k;
        tiku_path_transform(path, m);
        (void)tiku_ttf_outline(f->ttf, g[i].gid, &hint, path);
    }
    if (path != NULL && !tiku_path_failed(path)) {
        tiku_path_bounds(path, &box[0], &box[1], &box[2], &box[3]);
    }
    tiku_path_free(path);
    for (i = 0; i < 4; i++) {
        ink[i] = (float)box[i] / k;
    }
}

/** @brief How opaque a VML fill is: ".5", or a 16.16 fraction "32768f". */
static float
opacity(const char *v)
{
    double k;

    if (v == NULL || *v == '\0') {
        return 1;
    }
    k = atof(v);
    k = v[strlen(v) - 1] == 'f' ? k / 65536.0 : k;
    return k < 0 ? 0 : k > 1 ? 1 : (float)k;
}

/** @brief The face WordArt's textpath @p tp names in its style, its
 *         weight too.  @return its index, or -1 when there are no faces. */
static int
wa_face(of_faces_t *fs, const tiku_xml_node_t *tp)
{
    const char *style = tiku_xml_attr(tp, "style"), *q;
    char family[64], v[64];

    dx_css(style != NULL ? style : "", "font-family", v, sizeof v);
    for (q = v; *q == '"' || *q == '\''; q++) {
    }
    snprintf(family, sizeof family, "%.*s", (int)strcspn(q, "\"'"), q);
    dx_css(style != NULL ? style : "", "font-weight", v, sizeof v);
    return of_face(fs, family[0] ? family : "Calibri",
                   strcmp(v, "bold") == 0, 0);
}

/** @brief The transform that stretches @p ink (a WA_SIZE run's) to fill
 *         @p box and turns it about the box's middle as shape @p s's
 *         style says. */
static dml_mat_t
wa_place(const tiku_xml_node_t *s, const float *box, const float *ink)
{
    const char *style = tiku_xml_attr(s, "style");
    char v[32];
    double turn;
    dml_mat_t sc, ro;

    dx_css(style != NULL ? style : "", "rotation", v, sizeof v);
    turn = atof(v) * M_PI / 180;
    sc = (dml_mat_t){box[2] / ink[2], 0, 0, box[3] / ink[3], 0, 0};
    ro = (dml_mat_t){cos(turn), sin(turn), -sin(turn), cos(turn), 0, 0};
    ro.e = box[0] + box[2] / 2 - (ro.a * box[2] / 2 + ro.c * box[3] / 2);
    ro.f = box[1] + box[3] / 2 - (ro.b * box[2] / 2 + ro.d * box[3] / 2);
    return dml_mul(&sc, &ro);
}

/** @brief VML WordArt @p s: its words, in its textpath's face, stretched
 *         to fill @p box and turned as its style says, in its fill. */
static void
wordart(of_faces_t *fs, const tiku_xml_node_t *s, const float *box,
        tiku_pdf_writer_t *w)
{
    const tiku_xml_node_t *tp = tiku_xml_child(s, "v:textpath");
    tiku_pdf_glyph_t g[WA_MAX];
    float ink[4], alpha;
    dml_mat_t m;
    of_frag_t run;
    uint32_t fill;
    int face = wa_face(fs, tp);

    if (face < 0 || fs->face[face].ttf == NULL) {
        return;
    }
    memset(&run, 0, sizeof run);
    run.g = g;
    run.ng = wa_glyphs(fs, face, tiku_xml_attr(tp, "string"), g);
    wa_ink(fs, face, g, run.ng, ink);
    if (run.ng == 0 || ink[2] < 1 || ink[3] < 1) {
        return;
    }
    fill = dx_vml_color(tiku_xml_attr(s, "fillcolor"), 0xffffffffu);
    alpha = opacity(tiku_xml_attr(tiku_xml_child(s, "v:fill"), "opacity"));
    run.kind = OF_FRAG_GLYPHS;
    run.face = face;
    run.bold = fs->face[face].f.bold_synth;     /* a face that has none */
    run.italic = fs->face[face].f.italic_synth;
    run.size = WA_SIZE;
    run.color = (uint32_t)(alpha * 255 + 0.5f) << 24 | (fill & 0xffffffu);
    run.x = -ink[0];
    run.y = -ink[1];
    m = wa_place(s, box, ink);
    tiku_pdf_writer_save(w);
    tiku_pdf_writer_transform(w, m.a, m.b, m.c, m.d, m.e, m.f);
    of_draw_glyphs(fs, &run, 0, 0, w);
    tiku_pdf_writer_restore(w);
}

/* ------------------------------------------------------------- drawing  */

/** @brief Fill @p f over the whole of the drawing's box, through @p m. */
static void
ground(dml_ctx_t *c, const dml_fill_t *f, const dml_mat_t *m)
{
    dml_geom_t g;

    if (dml_geometry(NULL, c->fit_w, c->fit_h, &g) == 0) {
        dml_paint_fill(c, f, &g, m, c->fit_w, c->fit_h);
    }
    dml_geom_free(&g);
}

/** @brief A drawing canvas: its ground over the drawing's box, then what
 *         is on it. */
static int
canvas(dx_art_t *ax, const tiku_xml_node_t *e, const dml_mat_t *m)
{
    dml_ctx_t *c = &ax->px.dml;
    const tiku_xml_node_t *k;
    dml_fill_t f;
    int err = 0;

    if (dml_fill(c, tiku_xml_child(e, "wpc:bg"), &f) &&
        f.kind != DML_NOFILL) {
        ground(c, &f, m);
    }
    for (k = tiku_xml_child(e, NULL); k != NULL && err == 0;
         k = tiku_xml_next(k, NULL)) {
        err = dml_draw(c, k, m, NULL);
    }
    return err;
}

/** @brief A diagram (SmartArt) from the drawing Word saved with it, or a
 *         chart from the values its part keeps; a grey box of its size
 *         when it cannot be drawn.  @return 0, or -1 out of memory. */
static int
diagram(dx_art_t *ax, const tiku_xml_node_t *gd, const dml_mat_t *m)
{
    dml_fill_t f;
    int drawn = tiku_xml_child(gd, "c:chart") != NULL
                ? dml_chart(&ax->px.dml, gd, m, ax->px.dml.fit_w,
                            ax->px.dml.fit_h)
                : dml_diagram(&ax->px.dml, gd, m);

    if (drawn == 0) {
        memset(&f, 0, sizeof f);
        f.kind = DML_SOLID;
        f.color = 0xffeeeeeeu;
        ground(&ax->px.dml, &f, m);
    }
    return drawn < 0 ? -1 : 0;
}

/** @brief A drawing drawn in @p box (x y w h, points): of_doc_t's
 *         art_draw.  Its text boxes' text is the pager's, set already; a
 *         diagram's is set here, as a slide's is. */
static int
art_draw(of_doc_t *d, of_faces_t *fs, const of_art_t *a, const float *box,
         tiku_pdf_writer_t *w)
{
    dx_art_t *ax = d->art_ctx;
    dml_mat_t m = {1, 0, 0, 1, box[0], box[1]};
    const tiku_xml_node_t *k;
    int err = 0;

    if (tiku_xml_is(a->node, "v:shape")) {
        wordart(fs, a->node, box, w);
        return 0;
    }
    ax->px.dml.w = w;
    ax->px.dml.fs = fs;
    ax->px.dml.part = a->part;
    ax->px.dml.fit_w = box[2];
    ax->px.dml.fit_h = box[3];
    ax->px.dml.depth = 0;
    for (k = tiku_xml_child(a->node, NULL); k != NULL && err == 0;
         k = tiku_xml_next(k, NULL)) {
        err = tiku_xml_is(k, "wpc:wpc") ? canvas(ax, k, &m) :
              tiku_xml_is(k, "dgm:relIds") || tiku_xml_is(k, "c:chart")
              ? diagram(ax, a->node, &m) : dml_draw(&ax->px.dml, k, &m,
                                                    NULL);
    }
    return err;
}

/** @brief What the drawings were drawn with, given back: of_doc_t's
 *         art_free. */
static void
art_free(void *ctx)
{
    dx_art_t *ax = ctx;
    int i;

    for (i = 0; i < ax->nkeep; i++) {
        tiku_xml_free(ax->keep[i]);
    }
    free(ax->keep);
    dml_done(&ax->px.dml);
    dml_theme_free(&ax->theme);
    of_doc_free(ax->px.d);
    free(ax);
}

/** @brief What the document's drawings are drawn with, made with the
 *         first of them: the theme its main part names.  NULL out of
 *         memory. */
static dx_art_t *
art_ctx(dx_t *x)
{
    const of_rel_t *m, *t;
    dx_art_t *ax = x->d->art_ctx;

    if (ax != NULL || (ax = calloc(1, sizeof *ax)) == NULL) {
        return ax;
    }
    if ((ax->px.d = of_doc_new()) == NULL) {  /* a diagram's looks */
        free(ax);
        return NULL;
    }
    m = of_pkg_rel_type(x->pkg, "", "officeDocument");
    t = of_pkg_rel_type(x->pkg, m != NULL ? m->target : "word/document.xml",
                        "theme");
    (void)dml_theme_load(x->pkg, t != NULL ? t->target : NULL, &ax->theme);
    ax->px.pkg = x->pkg;
    ax->px.dml.pkg = x->pkg;
    ax->px.dml.text_pt = 10;        /* Word's, for a chart */
    ax->px.dml.theme = &ax->theme;
    ax->px.dml.d = ax->px.d;
    ax->px.dml.text = px_text;
    ax->px.dml.text_ctx = &ax->px;
    dml_map_read(&ax->px.dml, NULL);
    x->d->art_ctx = ax;
    x->d->art_draw = art_draw;
    x->d->art_free = art_free;
    return ax;
}

int
dx_art(dx_t *x, const tiku_xml_node_t *gd, float w, float h, of_float_t *f)
{
    of_art_t *a = of_alloc(&x->d->arena, sizeof *a);
    dml_mat_t id = {1, 0, 0, 1, 0, 0};
    of_vec_t boxes;
    int err;

    if (a == NULL || art_ctx(x) == NULL) {
        return -1;
    }
    a->node = gd;
    a->part = of_strdup(&x->d->arena, x->part);
    f->art = a;
    x->arts++;
    of_vec_init(&boxes, sizeof(of_textbox_t));
    err = boxes_in(x, gd, &id, w, h, 0, &boxes);
    f->box = of_vec_freeze(&boxes, &x->d->arena, &f->nbox);
    of_vec_free(&boxes);
    return err;
}

void
dx_art_keep(dx_t *x, tiku_xml_t *xml, int since)
{
    dx_art_t *ax = x->d->art_ctx;
    tiku_xml_t **k;

    if (xml == NULL) {
        return;
    }
    if (x->arts == since || ax == NULL) {
        tiku_xml_free(xml);
        return;
    }
    k = realloc(ax->keep, (size_t)(ax->nkeep + 1) * sizeof *k);
    if (k == NULL) {
        tiku_xml_free(xml);         /* its drawings cannot be drawn: */
        x->d->arena.bad = 1;        /* the document fails to read    */
        return;
    }
    ax->keep = k;
    ax->keep[ax->nkeep++] = xml;
}
