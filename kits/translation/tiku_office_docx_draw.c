/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_docx_draw.c - what a .docx puts beside its text: pictures
 * in the text and floating over it, drawings (tiku_office_docx_art.c
 * draws them), the old VML shapes and text boxes, and tables.  A
 * picture's part is found through the relationships of the part it is
 * in.  A chart is drawn as a drawing is, from the values its part
 * keeps; one of a kind not read, a grey box of its size.  A table's cells
 * take their borders, shading and text from its style, by row and
 * column, as Word's table styles say.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_docx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMU        12700.0f         /* EMUs to a point                   */
#define LOOK_ROW1  0x0020           /* w:tblLook's bits                  */
#define LOOK_ROWN  0x0040
#define LOOK_COL1  0x0080
#define LOOK_COLN  0x0100
#define LOOK_NO_H  0x0200
#define LOOK_NO_V  0x0400

/* -------------------------------------------------------------- values  */

/** @brief Attribute @p name of @p e in EMUs, as points; @p dflt absent. */
static float
emu(const tiku_xml_node_t *e, const char *name, float dflt)
{
    const char *v = tiku_xml_attr(e, name);

    return v != NULL && *v ? (float)(atof(v) / EMU) : dflt;
}

/** @brief The text of element @p e as a number: wp:posOffset's EMUs. */
static float
text_emu(const tiku_xml_node_t *e)
{
    char buf[32];

    (void)tiku_xml_text(e, buf, sizeof buf);
    return (float)(atof(buf) / EMU);
}

/* ------------------------------------------------------------ pictures  */

/** @brief The picture relationship @p rid names, drawn @p w by @p h: its
 *         part, or none, which is drawn as a grey box. */
static of_image_t *
image(dx_t *x, const char *rid, float w, float h)
{
    const of_rel_t *r = rid != NULL ? of_pkg_rel(x->pkg, x->part, rid)
                                    : NULL;
    of_image_t *im = of_alloc(&x->d->arena, sizeof *im);

    if (im == NULL) {
        return NULL;
    }
    im->w = w > 0 ? w : 1;
    im->h = h > 0 ? h : 1;
    im->pdf = -1;
    im->part = r != NULL && !r->external ? of_strdup(&x->d->arena, r->target)
                                         : NULL;
    return im;
}

/** @brief A picture's pic:blipFill: its blip, and how much is cut away. */
static of_image_t *
blip_fill(dx_t *x, const tiku_xml_node_t *fill, float w, float h)
{
    const tiku_xml_node_t *crop = tiku_xml_child(fill, "a:srcRect");
    of_image_t *im = image(x, tiku_xml_attr(tiku_xml_child(fill, "a:blip"),
                                            "r:embed"), w, h);
    static const char *const side[4] = {"l", "t", "r", "b"};
    int k;

    for (k = 0; im != NULL && crop != NULL && k < 4; k++) {
        const char *v = tiku_xml_attr(crop, side[k]);
        float f = v != NULL ? (float)atof(v) / 100000.0f : 0;

        im->crop[k] = f > 0 && f < 0.95f ? f : 0;
    }
    return im;
}

/** @brief What a:graphicData holds: a plain picture into @p im; shapes,
 *         groups, a canvas, a diagram, a picture with more to it, a
 *         drawing in @p f; anything else (a chart of a newer kind) a
 *         grey box in @p im.
 *  @return 0, or -1 out of memory. */
static int
graphic(dx_t *x, const tiku_xml_node_t *gd, float w, float h,
        of_image_t **im, of_float_t *f)
{
    const tiku_xml_node_t *pic = tiku_xml_child(gd, "pic:pic");

    if (dx_art_wants(gd)) {
        return dx_art(x, gd, w, h, f);
    }
    *im = pic != NULL ? blip_fill(x, tiku_xml_child(pic, "pic:blipFill"), w,
                                  h)
                      : image(x, NULL, w, h);
    return *im != NULL ? 0 : -1;
}

/** @brief What a position counts from, horizontally (@p across) or down:
 *         the page, the margins' box, the strip before or after them. */
static unsigned char
relative(const char *v, int across)
{
    static const struct { const char *name; unsigned char rel; } r[] = {
        {"page", OF_REL_PAGE}, {"margin", OF_REL_MARGIN},
        {"leftMargin", OF_REL_BEFORE}, {"topMargin", OF_REL_BEFORE},
        {"insideMargin", OF_REL_BEFORE}, {"rightMargin", OF_REL_AFTER},
        {"bottomMargin", OF_REL_AFTER}, {"outsideMargin", OF_REL_AFTER},
        {"left-margin-area", OF_REL_BEFORE},    /* and VML's names */
        {"top-margin-area", OF_REL_BEFORE},
        {"inner-margin-area", OF_REL_BEFORE},
        {"right-margin-area", OF_REL_AFTER},
        {"bottom-margin-area", OF_REL_AFTER},
        {"outer-margin-area", OF_REL_AFTER}};
    size_t i;

    for (i = 0; v != NULL && i < sizeof r / sizeof r[0]; i++) {
        if (strcmp(v, r[i].name) == 0) {
            return r[i].rel;
        }
    }
    return across ? OF_REL_COLUMN : OF_REL_PARA;
}

/** @brief A wp:positionH or wp:positionV: aligned, offset in points, or
 *         (@p pct set) offset by a fraction of what it counts from. */
static void
position(const tiku_xml_node_t *p, int across, unsigned char *rel,
         unsigned char *align, float *at, unsigned char *pct)
{
    const tiku_xml_node_t *a = tiku_xml_child(p, "wp:align");
    const tiku_xml_node_t *by = tiku_xml_child(p, across
                                               ? "wp14:pctPosHOffset"
                                               : "wp14:pctPosVOffset");
    char buf[16] = "";

    *rel = relative(tiku_xml_attr(p, "relativeFrom"), across);
    *align = 0;
    *at = 0;
    *pct = 0;
    if (a != NULL) {
        (void)tiku_xml_text(a, buf, sizeof buf);
        *align = strcmp(buf, "center") == 0 ? 2 :
                 strcmp(buf, "right") == 0 || strcmp(buf, "bottom") == 0 ||
                 strcmp(buf, "outside") == 0 ? 3 : 1;
    } else if (by != NULL) {
        (void)tiku_xml_text(by, buf, sizeof buf);
        *at = (float)(atof(buf) / 100000.0);
        *pct = 1;
    } else {
        *at = text_emu(tiku_xml_child(p, "wp:posOffset"));
    }
}

/** @brief wp:anchor @p a's child @p name, or the one an
 *         mc:AlternateContent in it chooses: Word 2010's, else the older. */
static const tiku_xml_node_t *
anchor_child(const tiku_xml_node_t *a, const char *name)
{
    const tiku_xml_node_t *k = tiku_xml_child(a, name), *ac;

    for (ac = tiku_xml_child(a, "mc:AlternateContent");
         k == NULL && ac != NULL; ac = tiku_xml_next(ac,
                                                    "mc:AlternateContent")) {
        k = tiku_xml_child(tiku_xml_child(ac, "mc:Choice"), name);
        k = k != NULL ? k : tiku_xml_child(tiku_xml_child(ac, "mc:Fallback"),
                                           name);
    }
    return k;
}

/** @brief A wp:anchor's place, wrap and gap into @p f. */
static void
anchor(const tiku_xml_node_t *a, of_float_t *f)
{
    const char *simple = tiku_xml_attr(a, "simplePos");
    const tiku_xml_node_t *sp = tiku_xml_child(a, "wp:simplePos");

    position(anchor_child(a, "wp:positionH"), 1, &f->rel_x, &f->align_x,
             &f->x, &f->pct_x);
    position(anchor_child(a, "wp:positionV"), 0, &f->rel_y, &f->align_y,
             &f->y, &f->pct_y);
    if (simple != NULL && strcmp(simple, "1") == 0 && sp != NULL) {
        f->rel_x = f->rel_y = OF_REL_PAGE;
        f->align_x = f->align_y = 0;
        f->x = emu(sp, "x", 0);
        f->y = emu(sp, "y", 0);
    }
    f->behind = (unsigned char)(tiku_xml_attr(a, "behindDoc") != NULL &&
                                strcmp(tiku_xml_attr(a, "behindDoc"), "1")
                                == 0);
    f->wrap = tiku_xml_child(a, "wp:wrapSquare") ||
              tiku_xml_child(a, "wp:wrapTight") ||
              tiku_xml_child(a, "wp:wrapThrough") ? OF_WRAP_SQUARE :
              tiku_xml_child(a, "wp:wrapTopAndBottom") ? OF_WRAP_BLOCK
                                                       : OF_WRAP_NONE;
    f->gap = f->wrap == OF_WRAP_BLOCK ? emu(a, "distB", 0)
                                      : emu(a, "distR", 9);
}

/** @brief A w:drawing: a picture in the text, or a float. */
static int
drawing(dx_t *x, const tiku_xml_node_t *e, of_run_t *run)
{
    const tiku_xml_node_t *in = tiku_xml_child(e, "wp:inline");
    const tiku_xml_node_t *an = tiku_xml_child(e, "wp:anchor");
    const tiku_xml_node_t *k = in != NULL ? in : an, *ext;
    of_float_t *f;
    of_image_t *im = NULL;
    float w, h;

    if (k == NULL || (f = of_alloc(&x->d->arena, sizeof *f)) == NULL) {
        return -1;
    }
    ext = tiku_xml_child(k, "wp:extent");
    w = emu(ext, "cx", 0);
    h = emu(ext, "cy", 0);
    if (graphic(x, tiku_xml_path(k, "a:graphic/a:graphicData"), w, h, &im,
                f) != 0) {
        return -1;
    }
    if (in != NULL && f->nbox == 0) {
        if (f->art != NULL && (im = image(x, NULL, w, h)) != NULL) {
            im->art = f->art;       /* in the text, drawn as a drawing */
        }
        run->kind = OR_IMAGE;
        run->image = im;
        return im != NULL ? 0 : -1;
    }
    f->image = im;
    f->w = w > 0 ? w : 1;
    f->h = h > 0 ? h : 1;
    if (an != NULL) {
        anchor(an, f);
    } else {
        f->rel_x = OF_REL_COLUMN;
        f->rel_y = OF_REL_PARA;
        f->wrap = OF_WRAP_BLOCK;
    }
    run->kind = OR_FLOAT;
    run->flt = f;
    return 0;
}

/* ----------------------------------------------------------------- VML  */

/** @brief A CSS length in a VML style as points: pt, in, cm, mm, px. */
static float
css_length(const char *v)
{
    char *end;
    double n = strtod(v, &end);

    return (float)(strncmp(end, "pt", 2) == 0 ? n :
                   strncmp(end, "in", 2) == 0 ? n * 72 :
                   strncmp(end, "cm", 2) == 0 ? n * 72 / 2.54 :
                   strncmp(end, "mm", 2) == 0 ? n * 72 / 25.4 : n * 0.75);
}

void
dx_css(const char *style, const char *key, char *out, size_t max)
{
    size_t k = strlen(key);
    const char *p = style;

    out[0] = '\0';
    while (p != NULL && *p) {
        while (*p == ';' || *p == ' ') {
            p++;
        }
        if (strncmp(p, key, k) == 0 && p[k] == ':') {
            const char *end = strchr(p + k + 1, ';');

            snprintf(out, max, "%.*s", end ? (int)(end - p - k - 1)
                     : (int)strlen(p + k + 1), p + k + 1);
            return;
        }
        p = strchr(p, ';');
    }
}

uint32_t
dx_vml_color(const char *v, uint32_t dflt)
{
    static const struct { const char *name; uint32_t rgb; } named[] = {
        {"black", 0x000000}, {"silver", 0xc0c0c0}, {"gray", 0x808080},
        {"white", 0xffffff}, {"maroon", 0x800000}, {"red", 0xff0000},
        {"purple", 0x800080}, {"fuchsia", 0xff00ff}, {"green", 0x008000},
        {"lime", 0x00ff00}, {"olive", 0x808000}, {"yellow", 0xffff00},
        {"navy", 0x000080}, {"blue", 0x0000ff}, {"teal", 0x008080},
        {"aqua", 0x00ffff}};
    char hex[7];
    size_t i;

    if (v != NULL && v[0] == '#' && strlen(v) >= 7) {
        memcpy(hex, v + 1, 6);
        hex[6] = '\0';
        return dx_color(hex) ? dx_color(hex) : dflt;
    }
    for (i = 0; v != NULL && i < sizeof named / sizeof named[0]; i++) {
        if (strncmp(v, named[i].name, strlen(named[i].name)) == 0) {
            return 0xff000000u | named[i].rgb;
        }
    }
    return dflt;
}

/** @brief Where a CSS alignment puts a float: 1 start, 2 middle, 3 end,
 *         0 by its offset. */
static unsigned char
css_align(const char *v)
{
    return strcmp(v, "center") == 0 ? 2 : strcmp(v, "right") == 0 ||
           strcmp(v, "bottom") == 0 || strcmp(v, "outside") == 0 ? 3 :
           strcmp(v, "left") == 0 || strcmp(v, "top") == 0 ||
           strcmp(v, "inside") == 0 ? 1 : 0;
}

/** @brief VML shape @p s's place from its @p style, when it floats, and
 *         how text keeps clear of it: as its w10:wrap says, else not. */
static void
vml_place(const tiku_xml_node_t *s, const char *style, of_float_t *f)
{
    const char *wrap = tiku_xml_attr(tiku_xml_child(s, "w10:wrap"), "type");
    char v[64];

    dx_css(style, "margin-left", v, sizeof v);
    f->x = v[0] ? css_length(v) : 0;
    dx_css(style, "margin-top", v, sizeof v);
    f->y = v[0] ? css_length(v) : 0;
    dx_css(style, "mso-position-horizontal-relative", v, sizeof v);
    f->rel_x = relative(v[0] ? v : NULL, 1);
    dx_css(style, "mso-position-vertical-relative", v, sizeof v);
    f->rel_y = relative(v[0] ? v : NULL, 0);
    dx_css(style, "mso-position-horizontal", v, sizeof v);
    f->align_x = css_align(v);
    dx_css(style, "mso-position-vertical", v, sizeof v);
    f->align_y = css_align(v);
    dx_css(style, "z-index", v, sizeof v);
    f->behind = v[0] == '-';
    f->wrap = wrap == NULL ? OF_WRAP_NONE :
              strcmp(wrap, "topAndBottom") == 0 ? OF_WRAP_BLOCK :
              strcmp(wrap, "none") == 0 ? OF_WRAP_NONE : OF_WRAP_SQUARE;
    f->gap = 9;
}

/** @brief The first VML shape under @p e, whatever its kind. */
static const tiku_xml_node_t *
vml_shape(const tiku_xml_node_t *e)
{
    static const char *const kinds[] = {"v:shape", "v:rect", "v:roundrect",
                                        "v:oval", "v:image"};
    size_t i;

    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        const tiku_xml_node_t *s = tiku_xml_child(e, kinds[i]);

        if (s != NULL) {
            return s;
        }
    }
    return NULL;
}

/** @brief A VML shape's size from its @p style, into @p w and @p h.
 *  @return whether it floats: placed absolutely, not in the text. */
static int
vml_size(const char *style, float *w, float *h)
{
    char v[64];

    dx_css(style, "width", v, sizeof v);
    *w = v[0] ? css_length(v) : 72;
    dx_css(style, "height", v, sizeof v);
    *h = v[0] ? css_length(v) : 72;
    dx_css(style, "position", v, sizeof v);
    return strcmp(v, "absolute") == 0;
}

/** @brief A w:pict or w:object: its VML picture, text box or WordArt --
 *         a watermark's words, which its drawing draws. */
static int
vml(dx_t *x, const tiku_xml_node_t *e, of_run_t *run)
{
    const tiku_xml_node_t *s = vml_shape(e);
    const tiku_xml_node_t *img = tiku_xml_child(s, "v:imagedata");
    const tiku_xml_node_t *tb = tiku_xml_path(s, "v:textbox/w:txbxContent");
    const tiku_xml_node_t *art = tiku_xml_attr(tiku_xml_child(s,
                                 "v:textpath"), "string") != NULL ? s : NULL;
    const char *style = tiku_xml_attr(s, "style");
    float w, h;
    int floats;
    of_float_t *f;

    if (s == NULL || (img == NULL && tb == NULL && art == NULL)) {
        return -1;
    }
    style = style != NULL ? style : "";
    floats = vml_size(style, &w, &h);
    if (tb == NULL && art == NULL && !floats) {
        run->kind = OR_IMAGE;
        run->image = image(x, tiku_xml_attr(img, "r:id"), w, h);
        return run->image != NULL ? 0 : -1;
    }
    if ((f = of_alloc(&x->d->arena, sizeof *f)) == NULL) {
        return -1;
    }
    f->w = w;
    f->h = h;
    vml_place(s, style, f);
    if (art != NULL) {
        run->kind = OR_FLOAT;
        run->flt = f;
        return dx_art(x, art, w, h, f);
    }
    f->image = img != NULL ? image(x, tiku_xml_attr(img, "r:id"), w, h)
                           : NULL;
    f->fill = dx_vml_color(tiku_xml_attr(s, "fillcolor"), 0);
    f->inset[0] = f->inset[2] = 3.6f;
    f->inset[1] = f->inset[3] = 7.2f;
    if (tb != NULL && img == NULL && x->depth < OF_NEST_MAX) {
        x->depth++;
        f->text = dx_story_apart(x, tb, NULL);
        x->depth--;
    }
    run->kind = OR_FLOAT;
    run->flt = f;
    return 0;
}

int
dx_drawing(dx_t *x, const tiku_xml_node_t *e, of_run_t *run)
{
    if (tiku_xml_is(e, "w:drawing")) {
        return drawing(x, e, run);
    }
    if (tiku_xml_is(e, "w:pict") || tiku_xml_is(e, "w:object")) {
        return vml(x, e, run);
    }
    return -1;
}

/* -------------------------------------------------------------- tables  */

/** @brief The next child of @p e after @p at called @p name, reaching
 *         into content controls and custom XML, which wrap rows and cells
 *         without changing them.  NULL at the end. */
static const tiku_xml_node_t *
next_in(const tiku_xml_node_t *e, const tiku_xml_node_t *at,
        const char *name)
{
    const tiku_xml_node_t *c = at != NULL ? at->next : e->child;

    while (c != NULL || (at != NULL && at->parent != e)) {
        if (c == NULL) {
            at = at->parent;            /* out of a wrapper, onwards */
            c = at->next;
            continue;
        }
        if (tiku_xml_is(c, name)) {
            return c;
        }
        if (tiku_xml_is(c, "w:sdt") || tiku_xml_is(c, "w:customXml")) {
            const tiku_xml_node_t *in = tiku_xml_is(c, "w:sdt")
                ? tiku_xml_child(c, "w:sdtContent") : c;

            if (in != NULL && in->child != NULL) {
                at = in->child;
                c = in->child;
                continue;
            }
        }
        at = c;
        c = c->next;
    }
    return NULL;
}

/** @brief w:tblLook's bits, from its attributes or its hex value; Word's
 *         own default when a table has none. */
static int
look(const tiku_xml_node_t *e)
{
    static const struct { const char *name; int bit; } a[] = {
        {"w:firstRow", LOOK_ROW1}, {"w:lastRow", LOOK_ROWN},
        {"w:firstColumn", LOOK_COL1}, {"w:lastColumn", LOOK_COLN},
        {"w:noHBand", LOOK_NO_H}, {"w:noVBand", LOOK_NO_V}};
    const char *v = tiku_xml_attr(e, "w:val");
    int bits = 0, any = 0;
    size_t i;

    if (e == NULL) {
        return LOOK_ROW1 | LOOK_COL1 | LOOK_NO_V;
    }
    for (i = 0; i < sizeof a / sizeof a[0]; i++) {
        const char *b = tiku_xml_attr(e, a[i].name);

        if (b != NULL) {
            any = 1;
            bits |= strcmp(b, "1") == 0 || strcmp(b, "true") == 0 ? a[i].bit
                                                                   : 0;
        }
    }
    return any ? bits : v != NULL ? (int)strtol(v, NULL, 16)
                                  : LOOK_ROW1 | LOOK_COL1 | LOOK_NO_V;
}

/** @brief Which part of table style @p ts formats cell (@p r, @p c) of
 *         @p nr rows and @p nc columns, as @p lk says the table uses it. */
static int
cell_part(const dx_style_t *ts, int lk, int r, int nr, int c, int nc)
{
    int k;

    if (ts == NULL) {
        return TS_WHOLE;
    }
    if ((lk & LOOK_ROW1) && r == 0 && ts->has[TS_FIRST_ROW]) {
        return TS_FIRST_ROW;
    }
    if ((lk & LOOK_ROWN) && r == nr - 1 && ts->has[TS_LAST_ROW]) {
        return TS_LAST_ROW;
    }
    if ((lk & LOOK_COL1) && c == 0 && ts->has[TS_FIRST_COL]) {
        return TS_FIRST_COL;
    }
    if ((lk & LOOK_COLN) && c == nc - 1 && ts->has[TS_LAST_COL]) {
        return TS_LAST_COL;
    }
    k = ((r - ((lk & LOOK_ROW1) ? 1 : 0)) % 2) ? TS_BAND2_H : TS_BAND1_H;
    if (!(lk & LOOK_NO_H) && ts->has[k]) {
        return k;
    }
    k = ((c - ((lk & LOOK_COL1) ? 1 : 0)) % 2) ? TS_BAND2_V : TS_BAND1_V;
    return !(lk & LOOK_NO_V) && ts->has[k] ? k : TS_WHOLE;
}

/** @brief A cell's own properties, then its table style part's, into
 *         @p cell: span, merge, alignment, shading, borders, margins. */
static void
cell_props(const tiku_xml_node_t *tcpr, const dx_style_t *ts, int part,
           of_cell_t *cell)
{
    dx_tpr_t own;
    const char *v;
    const tiku_xml_node_t *vm = tiku_xml_child(tcpr, "w:vMerge");
    int k;

    memset(&own, 0, sizeof own);
    dx_tpr_read(tcpr, &own);
    v = tiku_xml_attr(tiku_xml_child(tcpr, "w:gridSpan"), "w:val");
    cell->span = v != NULL && atoi(v) > 0 ? atoi(v) : 1;
    v = tiku_xml_attr(vm, "w:val");
    cell->vmerge = vm == NULL ? 0 : v != NULL && strcmp(v, "restart") == 0
                   ? 1 : 2;
    v = tiku_xml_attr(tiku_xml_child(tcpr, "w:vAlign"), "w:val");
    cell->valign = v == NULL ? 0 : strcmp(v, "center") == 0 ? 1 :
                   strcmp(v, "bottom") == 0 ? 2 : 0;
    cell->fill = own.fill ? own.fill : ts != NULL && part != TS_WHOLE &&
                 ts->tpr[part].fill ? ts->tpr[part].fill : ts != NULL
                 ? ts->tpr[TS_WHOLE].fill : 0;
    for (k = 0; k < 4; k++) {
        cell->border[k].width = -1;
        cell->margin[k] = own.has_margin[k] ? own.margin[k] : -1;
        if (own.has_border[k]) {
            cell->border[k] = own.border[k];
        } else if (ts != NULL && part != TS_WHOLE &&
                   ts->tpr[part].has_border[k]) {
            cell->border[k] = ts->tpr[part].border[k];
        }
    }
}

/** @brief One w:tc: its properties and its contents, read in its style's
 *         part so its text looks as the style says. */
static int
cell(dx_t *x, const tiku_xml_node_t *tc, const dx_style_t *ts, int part,
     of_cell_t *out)
{
    const dx_style_t *table = x->table;
    int saved = x->table_part, deep = x->depth + 1 >= OF_NEST_MAX;

    cell_props(tiku_xml_child(tc, "w:tcPr"), ts, part, out);
    if (deep) {
        return 0;                   /* too deep: the cell shown empty */
    }
    x->table = ts;
    x->table_part = part;
    x->depth++;
    out->body = dx_story(x, tc);
    x->depth--;
    x->table = table;
    x->table_part = saved;
    return out->body == NULL ? -1 : 0;
}

/** @brief One w:tr: its height and whether it heads each page, and its
 *         cells, counted along the grid for the style's parts. */
static int
row(dx_t *x, const tiku_xml_node_t *tr, const dx_style_t *ts, int lk,
    int r, int nr, int ngrid, of_row_t *out)
{
    const tiku_xml_node_t *pr = tiku_xml_child(tr, "w:trPr"), *tc;
    const tiku_xml_node_t *ht = tiku_xml_child(pr, "w:trHeight");
    const char *rule = tiku_xml_attr(ht, "w:hRule");
    of_vec_t cells;
    int col = 0;

    out->height = dx_twips(tiku_xml_attr(ht, "w:val"), 0);
    out->exact = rule != NULL && strcmp(rule, "exact") == 0;
    out->height = rule != NULL && strcmp(rule, "auto") == 0 ? 0
                                                            : out->height;
    out->header = (unsigned char)dx_on(tiku_xml_child(pr, "w:tblHeader"));
    of_vec_init(&cells, sizeof(of_cell_t));
    for (tc = next_in(tr, NULL, "w:tc"); tc != NULL;
         tc = next_in(tr, tc, "w:tc")) {
        of_cell_t *c = of_vec_push(&cells);

        if (c == NULL || cell(x, tc, ts, cell_part(ts, lk, r, nr, col,
                                                   ngrid), c) != 0) {
            of_vec_free(&cells);
            return -1;
        }
        col += c->span;
    }
    out->cell = of_vec_freeze(&cells, &x->d->arena, &out->ncell);
    of_vec_free(&cells);
    return 0;
}

/** @brief The table's own properties over its style's: borders, cells'
 *         margins, alignment and indent; its grid. */
static void
table_props(dx_t *x, const tiku_xml_node_t *e, const dx_style_t *ts,
            of_table_t *t)
{
    const tiku_xml_node_t *pr = tiku_xml_child(e, "w:tblPr"), *g;
    dx_tpr_t whole, own;
    const char *jc = tiku_xml_attr(tiku_xml_child(pr, "w:jc"), "w:val");
    int k, n = 0;

    memset(&own, 0, sizeof own);
    memset(&whole, 0, sizeof whole);
    if (ts != NULL) {
        whole = ts->tpr[TS_WHOLE];
    }
    dx_tpr_read(pr, &own);
    for (k = 0; k < 6; k++) {
        t->border[k] = own.has_border[k] ? own.border[k] : whole.border[k];
    }
    for (k = 0; k < 4; k++) {
        t->margin[k] = own.has_margin[k] ? own.margin[k] :
                       whole.has_margin[k] ? whole.margin[k] :
                       k % 2 ? 5.4f : 0;
    }
    t->align = jc == NULL ? OF_LEFT : strcmp(jc, "center") == 0 ? OF_CENTER
               : strcmp(jc, "right") == 0 || strcmp(jc, "end") == 0
               ? OF_RIGHT : OF_LEFT;
    t->indent = dx_twips(tiku_xml_attr(tiku_xml_child(pr, "w:tblInd"),
                                       "w:w"), 0);
    for (g = tiku_xml_path(e, "w:tblGrid/w:gridCol"); g;
         g = tiku_xml_next(g, "w:gridCol")) {
        n++;
    }
    t->grid = n > 0 ? of_alloc(&x->d->arena, (size_t)n * sizeof *t->grid)
                    : NULL;
    for (g = tiku_xml_path(e, "w:tblGrid/w:gridCol"); g && t->grid;
         g = tiku_xml_next(g, "w:gridCol")) {
        t->grid[t->ngrid++] = dx_twips(tiku_xml_attr(g, "w:w"), 0);
    }
}

int
dx_table(dx_t *x, const tiku_xml_node_t *e, of_table_t *t)
{
    const tiku_xml_node_t *pr = tiku_xml_child(e, "w:tblPr"), *tr;
    const char *id = tiku_xml_attr(tiku_xml_child(pr, "w:tblStyle"),
                                   "w:val");
    const dx_style_t *ts = dx_style(x, id, 't');
    int lk = look(tiku_xml_child(pr, "w:tblLook")), nr = 0, r = 0;
    of_vec_t rows;

    table_props(x, e, ts, t);
    for (tr = next_in(e, NULL, "w:tr"); tr; tr = next_in(e, tr, "w:tr")) {
        nr++;
    }
    of_vec_init(&rows, sizeof(of_row_t));
    for (tr = next_in(e, NULL, "w:tr"); tr; tr = next_in(e, tr, "w:tr")) {
        of_row_t *rw = of_vec_push(&rows);

        if (rw == NULL || row(x, tr, ts, lk, r++, nr, t->ngrid, rw) != 0) {
            of_vec_free(&rows);
            return -1;
        }
    }
    t->row = of_vec_freeze(&rows, &x->d->arena, &t->nrow);
    of_vec_free(&rows);
    return 0;
}
