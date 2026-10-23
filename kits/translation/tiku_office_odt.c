/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_odt.c - an OpenDocument text (.odt) read into a document.
 *
 * The body is walked in order: paragraphs and headings with their spans,
 * lists and their labels, tables, sections and indexes seen through.  A
 * paragraph whose style names a master page begins pages of that master.
 * White space collapses as OpenDocument says; tracked deletions and
 * comments are not shown.  Frames become pictures in the text, or boxes
 * floating beside it; notes are numbered as they are met.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_odt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A paragraph's runs being gathered, text held until its look or
 *         link changes. */
typedef struct {
    of_vec_t          runs;         /* of_run_t                          */
    char             *text;         /* text not yet a run                */
    size_t            ntext, cap;
    const of_style_t *st;           /* its look                          */
    const char       *link;         /* and its link                      */
    int               space;        /* the last kept was a space: white
                                       space after it collapses          */
    const char       *pstyle;       /* the paragraph's style             */
} od_para_t;

/** @brief Where a paragraph is in a list: its style, level, and whether
 *         it carries the item's label. */
typedef struct {
    const tiku_xml_node_t *style;   /* text:list-style, NULL: no list    */
    int                    level;   /* 0 first                           */
    int                    start;   /* the item's own number, or -1      */
    int                    label;   /* 1 the label; 0 indented only      */
} od_item_t;

static int inline_of(od_t *x, const tiku_xml_node_t *e, od_para_t *pp,
                     const of_style_t *look, const char *link);
static int blocks_of(od_t *x, const tiku_xml_node_t *e, of_vec_t *blocks,
                     of_vec_t *sects, const od_item_t *item);

/* --------------------------------------------------------------- runs  */

/** @brief The text held made a run.  @return 0, or -1 out of memory. */
static int
flush(od_t *x, od_para_t *pp)
{
    of_run_t *r;

    if (pp->ntext == 0) {
        return 0;
    }
    if ((r = of_vec_push(&pp->runs)) == NULL) {
        return -1;
    }
    r->kind = OR_TEXT;
    r->st = pp->st;
    r->link = pp->link;
    r->text = of_strndup(&x->d->arena, pp->text, pp->ntext);
    pp->ntext = 0;
    return r->text != NULL ? 0 : -1;
}

/** @brief @p n bytes of @p s added in look @p st and link @p link, held
 *         on to the text before when both are the same. */
static int
add(od_t *x, od_para_t *pp, const char *s, size_t n, const of_style_t *st,
    const char *link)
{
    if (n == 0 || st == NULL) {
        return st == NULL ? -1 : 0;
    }
    if ((pp->st != st || pp->link != link) && flush(x, pp) != 0) {
        return -1;
    }
    if (pp->ntext + n + 1 > pp->cap) {
        size_t cap = (pp->ntext + n + 1) * 2;
        char *g = realloc(pp->text, cap);

        if (g == NULL) {
            return -1;
        }
        pp->text = g;
        pp->cap = cap;
    }
    memcpy(pp->text + pp->ntext, s, n);
    pp->ntext += n;
    pp->st = st;
    pp->link = link;
    return 0;
}

/** @brief Text as the file has it: runs of white space made one space,
 *         none kept after a space or at a paragraph's start. */
static int
words(od_t *x, od_para_t *pp, const char *s, const of_style_t *st,
      const char *link)
{
    size_t start = 0, i;
    int err = 0;

    for (i = 0; s[i] != '\0' && err == 0; i++) {
        int white = s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                    s[i] == '\r';

        if (!white) {
            pp->space = 0;
            continue;
        }
        err = add(x, pp, s + start, i - start, st, link);
        if (err == 0 && !pp->space) {
            err = add(x, pp, " ", 1, st, link);
        }
        pp->space = 1;
        start = i + 1;
    }
    return err != 0 ? -1 : add(x, pp, s + start, i - start, st, link);
}

/** @brief A run that is not text, after the text before it. */
static of_run_t *
object(od_t *x, od_para_t *pp, int kind, const of_style_t *st,
       const char *link)
{
    of_run_t *r;

    if (flush(x, pp) != 0 || (r = of_vec_push(&pp->runs)) == NULL) {
        return NULL;
    }
    r->kind = (unsigned char)kind;
    r->st = st;
    r->link = link;
    return r;
}

/* -------------------------------------------------------------- frames  */

/** @brief A frame's picture: its draw:image's part, @p w by @p h -- the
 *         drawing of a chart or formula object instead, when the frame
 *         holds one; a grey box when it has none, or one outside the
 *         package. */
static of_image_t *
picture(od_t *x, const tiku_xml_node_t *fr, float w, float h)
{
    const tiku_xml_node_t *img = tiku_xml_child(fr, "draw:image");
    const tiku_xml_node_t *obj = tiku_xml_child(fr, "draw:object");
    const char *href = tiku_xml_attr(img, "xlink:href");
    of_image_t *im = of_alloc(&x->d->arena, sizeof *im);

    if (im == NULL) {
        return NULL;
    }
    im->art = obj != NULL ? od_object_art(x, obj) : NULL;
    while (href != NULL && href[0] == '.' && href[1] == '/') {
        href += 2;
    }
    im->w = w > 0 ? w : 1;
    im->h = h > 0 ? h : 1;
    im->pdf = -1;
    im->part = href != NULL && strstr(href, "://") == NULL
               ? of_strdup(&x->d->arena, href) : NULL;
    return im;
}

/** @brief What a frame's place counts from, across (@p across) or down. */
static unsigned char
frame_rel(const char *v, int across)
{
    if (v == NULL) {
        return across ? OF_REL_COLUMN : OF_REL_PARA;
    }
    if (strcmp(v, "page") == 0) {
        return OF_REL_PAGE;
    }
    if (strcmp(v, "page-content") == 0) {
        return OF_REL_MARGIN;
    }
    if (strcmp(v, "page-start-margin") == 0) {
        return OF_REL_BEFORE;
    }
    if (strcmp(v, "page-end-margin") == 0) {
        return OF_REL_AFTER;
    }
    return across ? OF_REL_COLUMN : OF_REL_PARA;
}

/** @brief Where an alignment puts a frame: 1 start, 2 middle, 3 end, 0
 *         by its offset. */
static unsigned char
frame_align(const char *v)
{
    if (v == NULL || strncmp(v, "from-", 5) == 0) {
        return 0;
    }
    return strcmp(v, "center") == 0 || strcmp(v, "middle") == 0 ? 2 :
           strcmp(v, "right") == 0 || strcmp(v, "bottom") == 0 ||
           strcmp(v, "outside") == 0 ? 3 : 1;
}

/** @brief A floating frame's place, wrap and box from its graphic style
 *         @p gs, into @p f. */
static void
frame_place(od_t *x, const char *gs, const tiku_xml_node_t *fr,
            of_float_t *f)
{
    const char *g = "style:graphic-properties";
    const char *wrap = od_attr(x, OD_GRAPHIC, gs, g, "style:wrap");
    const char *run = od_attr(x, OD_GRAPHIC, gs, g, "style:run-through");
    const char *anchor = tiku_xml_attr(fr, "text:anchor-type");

    f->x = od_length(tiku_xml_attr(fr, "svg:x"), 0);
    f->y = od_length(tiku_xml_attr(fr, "svg:y"), 0);
    f->rel_x = frame_rel(od_attr(x, OD_GRAPHIC, gs, g,
                                 "style:horizontal-rel"), 1);
    f->rel_y = frame_rel(od_attr(x, OD_GRAPHIC, gs, g,
                                 "style:vertical-rel"), 0);
    if (anchor != NULL && strcmp(anchor, "page") == 0) {
        f->rel_x = f->rel_x == OF_REL_COLUMN ? OF_REL_PAGE : f->rel_x;
        f->rel_y = f->rel_y == OF_REL_PARA ? OF_REL_PAGE : f->rel_y;
    }
    f->align_x = frame_align(od_attr(x, OD_GRAPHIC, gs, g,
                                     "style:horizontal-pos"));
    f->align_y = frame_align(od_attr(x, OD_GRAPHIC, gs, g,
                                     "style:vertical-pos"));
    f->wrap = wrap == NULL || strcmp(wrap, "none") == 0 ? OF_WRAP_BLOCK :
              strcmp(wrap, "run-through") == 0 ? OF_WRAP_NONE
                                               : OF_WRAP_SQUARE;
    f->behind = (unsigned char)(f->wrap == OF_WRAP_NONE && run != NULL &&
                                strcmp(run, "background") == 0);
    f->gap = od_length(od_attr(x, OD_GRAPHIC, gs, g, "fo:margin-right"), 0);
    f->fill = od_color(od_attr(x, OD_GRAPHIC, gs, g, "fo:background-color"));
    od_border(od_attr(x, OD_GRAPHIC, gs, g, "fo:border"), &f->line);
    f->inset[0] = f->inset[1] = f->inset[2] = f->inset[3] =
        od_length(od_attr(x, OD_GRAPHIC, gs, g, "fo:padding"), 0);
}

/** @brief A draw:frame: a picture in the text when it sits as a
 *         character, else a float holding its picture or text box.
 *  @return 0, or -1 out of memory. */
static int
frame(od_t *x, const tiku_xml_node_t *fr, od_para_t *pp,
      const of_style_t *st, const char *link)
{
    const char *anchor = tiku_xml_attr(fr, "text:anchor-type");
    const tiku_xml_node_t *tb = tiku_xml_child(fr, "draw:text-box");
    float w = od_length(tiku_xml_attr(fr, "svg:width"), 0);
    float h = od_length(tiku_xml_attr(fr, "svg:height"), 0);
    of_float_t *f;
    of_run_t *r;

    h = h > 0 ? h : od_length(tiku_xml_attr(tb, "fo:min-height"), 0);
    if (anchor != NULL && strcmp(anchor, "as-char") == 0 && tb == NULL) {
        of_image_t *im = picture(x, fr, w, h);
        of_math_t *m = im != NULL && im->art != NULL ? im->art->math : NULL;

        if (im == NULL ||
            (r = object(x, pp, m != NULL ? OR_MATH : OR_IMAGE, st, link))
            == NULL) {
            return -1;
        }
        r->image = m != NULL ? NULL : im;   /* a formula, set in the line */
        r->math = m;
        pp->space = 0;              /* a character: a space after it kept */
        return 0;
    }
    if ((f = of_alloc(&x->d->arena, sizeof *f)) == NULL) {
        return -1;
    }
    f->w = w > 0 ? w : 72;
    f->h = h > 0 ? h : 72;
    frame_place(x, tiku_xml_attr(fr, "draw:style-name"), fr, f);
    if (tb != NULL && x->depth < OF_NEST_MAX) {
        x->depth++;
        f->text = od_story(x, tb);
        x->depth--;
    } else if (tb == NULL && (f->image = picture(x, fr, f->w, f->h)) == NULL) {
        return -1;
    }
    if ((r = object(x, pp, OR_FLOAT, st, link)) == NULL) {
        return -1;
    }
    r->flt = f;
    return 0;
}

/* --------------------------------------------------------------- notes  */

/** @brief A note's story with its citation @p mark, then a tab, before
 *         its first paragraph's text.  @return 0, or -1 out of memory. */
static int
cite(od_t *x, of_story_t *s, const char *mark)
{
    of_para_t *p;
    of_run_t *r;

    if (s->nblock == 0 || s->block[0].kind != OB_PARA) {
        return 0;
    }
    p = s->block[0].para;
    r = of_alloc(&x->d->arena, (size_t)(p->nrun + 2) * sizeof *r);
    if (r == NULL) {
        return -1;
    }
    memcpy(r + 2, p->run, (size_t)p->nrun * sizeof *r);
    r[0].kind = OR_TEXT;
    r[0].st = p->mark;
    r[0].text = mark;
    r[1].kind = OR_TAB;
    r[1].st = p->mark;
    p->run = r;
    p->nrun += 2;
    return 0;
}

/** @brief A text:note: a footnote's number, its note set at the foot of
 *         the page the citation lands on, or an endnote's, its text kept
 *         for the document's end. */
static int
note(od_t *x, const tiku_xml_node_t *n, od_para_t *pp, const of_style_t *st)
{
    const char *cls = tiku_xml_attr(n, "text:note-class");
    const tiku_xml_node_t *c = tiku_xml_child(n, "text:note-citation");
    int foot = cls == NULL || strcmp(cls, "endnote") != 0;
    of_vec_t *list = foot ? &x->notes : &x->endnotes;
    char buf[32];
    of_style_t up = *st;
    of_story_t *s, **slot;
    of_run_t *r;

    if (tiku_xml_text(c, buf, sizeof buf) == 0) {
        snprintf(buf, sizeof buf, "%d", list->n + 1);
    }
    if (x->depth >= OF_NEST_MAX) {
        return 0;
    }
    x->depth++;
    s = od_story(x, tiku_xml_child(n, "text:note-body"));
    x->depth--;
    if (s == NULL || cite(x, s, of_strdup(&x->d->arena, buf)) != 0 ||
        (slot = of_vec_push(list)) == NULL) {
        return -1;
    }
    *slot = s;
    up.vert = 1;
    if ((st = of_style(x->d, &up)) == NULL) {
        return -1;
    }
    if (!foot) {
        pp->space = 0;
        return add(x, pp, buf, strlen(buf), st, NULL);
    }
    if ((r = object(x, pp, OR_NOTE, st, NULL)) == NULL) {
        return -1;
    }
    r->n = x->notes.n - 1;
    r->text = of_strdup(&x->d->arena, buf);
    return 0;
}

/* -------------------------------------------------------------- inline  */

/** @brief A bookmark, as a run where it is. */
static int
bookmark(od_t *x, const tiku_xml_node_t *c, od_para_t *pp)
{
    const char *name = tiku_xml_attr(c, "text:name");
    of_style_t st;
    of_run_t *r;

    if (name == NULL) {
        return 0;
    }
    of_style_default(&st);
    st.hidden = 1;
    if ((r = object(x, pp, OR_MARK, of_style(x->d, &st), NULL)) == NULL) {
        return -1;
    }
    r->text = of_strdup(&x->d->arena, name);
    return 0;
}

/** @brief Spaces written as text:s, kept as they are. */
static int
spaces(od_t *x, od_para_t *pp, const tiku_xml_node_t *c,
       const of_style_t *st, const char *link)
{
    const char *v = tiku_xml_attr(c, "text:c");
    int n = v != NULL ? atoi(v) : 1, err = 0;

    n = n < 1 ? 1 : n > 256 ? 256 : n;
    while (n-- > 0 && err == 0) {
        err = add(x, pp, " ", 1, st, link);
    }
    pp->space = 1;
    return err;
}

/** @brief A mark in the text standing for something laid out: a tab, a
 *         line break, a page number or count.  -2 when @p c is not one. */
static int
mark(od_t *x, od_para_t *pp, const tiku_xml_node_t *c,
     const of_style_t *st, const char *link)
{
    of_run_t *r;
    int kind = tiku_xml_is(c, "text:tab") ? OR_TAB :
               tiku_xml_is(c, "text:line-break") ? OR_BREAK :
               tiku_xml_is(c, "text:page-number") ||
               tiku_xml_is(c, "text:page-count") ? OR_FIELD : -1;

    if (kind < 0) {
        return -2;
    }
    if ((r = object(x, pp, kind, st, link)) == NULL) {
        return -1;
    }
    r->n = tiku_xml_is(c, "text:page-count") ? OF_FIELD_PAGES :
           kind == OR_FIELD ? OF_FIELD_PAGE : 0;
    pp->space = kind == OR_BREAK;
    return 0;
}

/** @brief Whether element @p c is one never shown: comments, tracked
 *         deletions, drawings other than frames, index marks. */
static int
unshown(const tiku_xml_node_t *c)
{
    static const char *const skip[] = {"office:annotation",
        "text:tracked-changes", "text:note-citation", "draw:custom-shape",
        "draw:rect", "draw:ellipse", "draw:line", "draw:g", "draw:polygon",
        "draw:path", "draw:connector", "draw:control", "text:ruby-text",
        "text:alphabetical-index-mark", "text:toc-mark",
        "text:user-index-mark"};
    size_t i;

    for (i = 0; i < sizeof skip / sizeof skip[0]; i++) {
        if (tiku_xml_is(c, skip[i])) {
            return 1;
        }
    }
    return 0;
}

/** @brief One child of a paragraph or a span read.  @return 0, or -1
 *         out of memory. */
static int
inline_one(od_t *x, const tiku_xml_node_t *c, od_para_t *pp,
           const of_style_t *look, const of_style_t *st, const char *link)
{
    of_style_t span;
    int k;

    if (c->name == NULL) {
        return words(x, pp, c->text, st, link);
    }
    if ((k = mark(x, pp, c, st, link)) != -2) {
        return k;
    }
    if (tiku_xml_is(c, "text:span")) {
        span = *look;
        od_span_look(x, tiku_xml_attr(c, "text:style-name"), &span);
        return inline_of(x, c, pp, &span, link);
    }
    if (tiku_xml_is(c, "text:a") || tiku_xml_is(c, "draw:a")) {
        const char *to = tiku_xml_attr(c, "xlink:href");

        return inline_of(x, c, pp, look, to != NULL
                         ? of_strdup(&x->d->arena, to) : link);
    }
    if (tiku_xml_is(c, "text:s")) {
        return spaces(x, pp, c, st, link);
    }
    if (tiku_xml_is(c, "text:bookmark") ||
        tiku_xml_is(c, "text:bookmark-start")) {
        return bookmark(x, c, pp);
    }
    if (tiku_xml_is(c, "text:note")) {
        return note(x, c, pp, st);
    }
    if (tiku_xml_is(c, "draw:frame")) {
        return frame(x, c, pp, st, link);
    }
    return unshown(c) ? 0 : inline_of(x, c, pp, look, link);
}

/** @brief The inline content of @p e in look @p look: text, spans, marks,
 *         frames and notes, and what fields show. */
static int
inline_of(od_t *x, const tiku_xml_node_t *e, od_para_t *pp,
          const of_style_t *look, const char *link)
{
    const of_style_t *st = of_style(x->d, look);
    const tiku_xml_node_t *c;
    int err = st == NULL;

    for (c = e != NULL ? e->child : NULL; c != NULL && err == 0;
         c = c->next) {
        err = inline_one(x, c, pp, look, st, link);
    }
    return err ? -1 : 0;
}

/* ---------------------------------------------------------- paragraphs  */

/** @brief Whether paragraph style @p pstyle sets its own indent, so a
 *         list's does not replace it: an automatic style that says so. */
static int
own_indent(const od_t *x, const char *pstyle)
{
    const od_style_t *s = od_style(x, OD_PARA, pstyle);
    const tiku_xml_node_t *pr = s != NULL && s->automatic
        ? tiku_xml_child(s->e, "style:paragraph-properties") : NULL;

    return tiku_xml_attr(pr, "fo:margin-left") != NULL;
}

/** @brief A paragraph's label: its list item's, or a heading's number as
 *         the outline style says.  @return 0, or -1 out of memory. */
static int
label(od_t *x, const tiku_xml_node_t *e, const od_item_t *item,
      const of_style_t *look, of_para_t *out)
{
    const char *pstyle = tiku_xml_attr(e, "text:style-name");
    const char *lv = tiku_xml_attr(e, "text:outline-level");

    if (item != NULL && item->style != NULL) {
        if (item->label) {
            return od_label(x, item->style, &x->list, item->level,
                            item->start, look, own_indent(x, pstyle), out);
        }
        if (!own_indent(x, pstyle)) {
            of_para_t tmp = *out;   /* the level's indent, unlabelled */

            (void)od_label(x, item->style, &(od_list_t){0}, item->level, 1,
                           look, 0, &tmp);
            out->ps.left = tmp.ps.left;
            out->ps.first = 0;
        }
        return 0;
    }
    if (tiku_xml_is(e, "text:h") && x->outline != NULL) {
        return od_label(x, x->outline, &x->heading,
                        (lv != NULL ? atoi(lv) : 1) - 1, -1, look, 1, out);
    }
    return 0;
}

/** @brief A text:p or text:h read into a paragraph: its layout, look,
 *         label and runs, frames waiting for it first.  NULL out of
 *         memory. */
static of_para_t *
paragraph(od_t *x, const tiku_xml_node_t *e, const od_item_t *item)
{
    const char *pstyle = tiku_xml_attr(e, "text:style-name");
    const char *lv = tiku_xml_attr(e, "text:outline-level");
    of_para_t *out = of_alloc(&x->d->arena, sizeof *out);
    od_para_t pp;
    of_style_t look;
    int err;

    if (out == NULL || od_layout(x, pstyle, &out->ps) != 0) {
        return NULL;
    }
    if (tiku_xml_is(e, "text:h")) {
        out->ps.outline = (signed char)(lv != NULL && atoi(lv) >= 1 &&
                                        atoi(lv) <= 9 ? atoi(lv) - 1 : 0);
    }
    od_para_look(x, pstyle, &look);
    out->mark = of_style(x->d, &look);
    memset(&pp, 0, sizeof pp);
    of_vec_init(&pp.runs, sizeof(of_run_t));
    pp.space = 1;
    pp.pstyle = pstyle;
    err = out->mark == NULL || label(x, e, item, &look, out) != 0;
    if (err == 0 && x->waiting.n > 0) {
        of_run_t *w = x->waiting.p, *r;
        int i;

        for (i = 0; i < x->waiting.n && err == 0; i++) {
            err = (r = object(x, &pp, OR_FLOAT, out->mark, NULL)) == NULL;
            if (err == 0) {
                *r = w[i];
            }
        }
        x->waiting.n = 0;
    }
    err = err || inline_of(x, e, &pp, &look, NULL) != 0 ||
          flush(x, &pp) != 0;
    out->run = of_vec_freeze(&pp.runs, &x->d->arena, &out->nrun);
    of_vec_free(&pp.runs);
    free(pp.text);
    return err ? NULL : out;
}

/* -------------------------------------------------------------- blocks  */

/** @brief A header's or footer's contents: od_page()'s reader. */
static of_story_t *
band_story(od_t *x, const tiku_xml_node_t *e)
{
    return od_story(x, e);
}

/** @brief The blocks gathered so far made a section of master page
 *         x->master's pages.  @return 0, or -1 out of memory. */
static int
section_end(od_t *x, of_vec_t *blocks, of_vec_t *sects)
{
    of_section_t *s = of_vec_push(sects);

    if (s == NULL) {
        return -1;
    }
    od_page(x, x->master, s, band_story);
    s->body.block = of_vec_freeze(blocks, &x->d->arena, &s->body.nblock);
    return x->d->arena.bad ? -1 : 0;
}

/** @brief A paragraph as a block, beginning pages of the master page its
 *         style names first, when it names one.  @return 0, or -1. */
static int
para_block(od_t *x, const tiku_xml_node_t *e, of_vec_t *blocks,
           of_vec_t *sects, const od_item_t *item)
{
    const od_style_t *s = od_style(x, OD_PARA, tiku_xml_attr(e,
                                   "text:style-name"));
    const char *m = s != NULL ? tiku_xml_attr(s->e, "style:master-page-name")
                              : NULL;
    of_block_t *b;

    if (sects != NULL && m != NULL && m[0] != '\0') {
        if ((blocks->n > 0 || sects->n > 0) &&
            section_end(x, blocks, sects) != 0) {
            return -1;
        }
        x->master = m;
    }
    if ((b = of_vec_push(blocks)) == NULL) {
        return -1;
    }
    b->kind = OB_PARA;
    return (b->para = paragraph(x, e, item)) != NULL ? 0 : -1;
}

/** @brief A list's items: each's first paragraph labelled, the rest
 *         indented to its level; lists in them a level deeper. */
static int
list(od_t *x, const tiku_xml_node_t *e, of_vec_t *blocks, of_vec_t *sects,
     const od_item_t *outer)
{
    const char *name = tiku_xml_attr(e, "text:style-name");
    const tiku_xml_node_t *it;
    od_item_t in;
    int err = 0;

    memset(&in, 0, sizeof in);
    in.style = name != NULL ? od_list_style(x, name)
                            : outer != NULL ? outer->style : NULL;
    in.level = outer != NULL ? outer->level + 1 : 0;
    if (outer == NULL && tiku_xml_attr(e, "text:continue-numbering") == NULL
        && tiku_xml_attr(e, "text:continue-list") == NULL) {
        x->list.style = NULL;       /* a new list counts from its start */
    }
    for (it = tiku_xml_child(e, NULL); it != NULL && err == 0;
         it = tiku_xml_next(it, NULL)) {
        const char *sv = tiku_xml_attr(it, "text:start-value");

        if (!tiku_xml_is(it, "text:list-item") &&
            !tiku_xml_is(it, "text:list-header")) {
            continue;
        }
        in.label = tiku_xml_is(it, "text:list-item");
        in.start = sv != NULL ? od_count(sv, 1) : -1;
        err = blocks_of(x, it, blocks, sects, &in);
    }
    return err;
}

/** @brief Whether @p c is an index -- contents, figures, a bibliography
 *         -- whose body's paragraphs are read in its place. */
static int
is_index(const tiku_xml_node_t *c)
{
    static const char *const kinds[] = {"text:table-of-content",
        "text:illustration-index", "text:table-index", "text:object-index",
        "text:user-index", "text:alphabetical-index", "text:bibliography"};
    size_t i;

    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        if (tiku_xml_is(c, kinds[i])) {
            return 1;
        }
    }
    return 0;
}

/** @brief One block-level child: a paragraph, a list, a table, what holds
 *         them.  @p item is the list item it is in, or NULL. */
static int
block(od_t *x, const tiku_xml_node_t *c, of_vec_t *blocks, of_vec_t *sects,
      od_item_t *item)
{
    of_block_t *b;
    of_run_t *w;
    int err;

    if (tiku_xml_is(c, "text:p") || tiku_xml_is(c, "text:h")) {
        err = para_block(x, c, blocks, sects, item);
        if (item != NULL) {
            item->label = 0;        /* the item's next paragraphs: none */
        }
        return err;
    }
    if (tiku_xml_is(c, "text:list")) {
        return list(x, c, blocks, sects, item);
    }
    if (tiku_xml_is(c, "table:table")) {
        if ((b = of_vec_push(blocks)) == NULL ||
            (b->table = of_alloc(&x->d->arena, sizeof *b->table)) == NULL) {
            return -1;
        }
        b->kind = OB_TABLE;
        return od_table(x, c, b->table);
    }
    if (tiku_xml_is(c, "text:section") || tiku_xml_is(c, "text:index-title")
        || tiku_xml_is(c, "text:index-body")) {
        return blocks_of(x, c, blocks, sects, item);
    }
    if (is_index(c)) {
        return blocks_of(x, tiku_xml_child(c, "text:index-body"), blocks,
                         sects, item);
    }
    if (tiku_xml_is(c, "draw:frame") && (w = of_vec_push(&x->waiting))) {
        od_para_t pp;               /* anchored to the page: kept for the
                                       paragraph after it                */
        of_style_t st;

        memset(&pp, 0, sizeof pp);
        of_vec_init(&pp.runs, sizeof(of_run_t));
        of_style_default(&st);
        err = frame(x, c, &pp, of_style(x->d, &st), NULL) != 0 ||
              pp.runs.n != 1;
        *w = err ? *w : *(of_run_t *)pp.runs.p;
        x->waiting.n -= err;
        of_vec_free(&pp.runs);
        return 0;
    }
    return 0;
}

/** @brief The blocks of @p e into @p blocks; with @p sects, a paragraph
 *         naming a master page begins a section. */
static int
blocks_of(od_t *x, const tiku_xml_node_t *e, of_vec_t *blocks,
          of_vec_t *sects, const od_item_t *item)
{
    const tiku_xml_node_t *c;
    od_item_t here;
    int err = 0;

    if (item != NULL) {
        here = *item;
    }
    for (c = tiku_xml_child(e, NULL); c != NULL && err == 0;
         c = tiku_xml_next(c, NULL)) {
        err = block(x, c, blocks, sects, item != NULL ? &here : NULL);
    }
    return err ? -1 : 0;
}

of_story_t *
od_story(od_t *x, const tiku_xml_node_t *e)
{
    of_vec_t blocks;
    of_story_t *s = of_alloc(&x->d->arena, sizeof *s);
    of_vec_t waiting = x->waiting;
    int err;

    if (s == NULL) {
        return NULL;
    }
    of_vec_init(&x->waiting, sizeof(of_run_t));
    of_vec_init(&blocks, sizeof(of_block_t));
    err = blocks_of(x, e, &blocks, NULL, NULL);
    s->block = of_vec_freeze(&blocks, &x->d->arena, &s->nblock);
    of_vec_free(&blocks);
    of_vec_free(&x->waiting);
    x->waiting = waiting;
    return err ? NULL : s;
}

/* ---------------------------------------------------------- the whole  */

/** @brief The endnotes, in the order met, after the last section's text. */
static void
endnotes_place(od_t *x)
{
    of_section_t *s = &x->d->sect[x->d->nsect - 1];
    of_story_t **n = x->endnotes.p;
    of_block_t *all;
    int total = s->body.nblock, i, k;

    for (i = 0; i < x->endnotes.n; i++) {
        total += n[i]->nblock;
    }
    if (x->endnotes.n == 0 || (all = of_alloc(&x->d->arena, (size_t)total *
                                              sizeof *all)) == NULL) {
        return;
    }
    memcpy(all, s->body.block, (size_t)s->body.nblock * sizeof *all);
    k = s->body.nblock;
    for (i = 0; i < x->endnotes.n; i++) {
        memcpy(all + k, n[i]->block, (size_t)n[i]->nblock * sizeof *all);
        k += n[i]->nblock;
    }
    s->body.block = all;
    s->body.nblock = k;
}

/** @brief The title and author meta.xml gives. */
static void
meta(od_t *x)
{
    tiku_xml_t *m = of_pkg_xml(x->pkg, "meta.xml");
    const tiku_xml_node_t *om = tiku_xml_child(tiku_xml_root(m),
                                               "office:meta");
    char buf[256];

    if (tiku_xml_text(tiku_xml_child(om, "dc:title"), buf, sizeof buf) > 0) {
        x->d->title = of_strdup(&x->d->arena, buf);
    }
    if (tiku_xml_text(tiku_xml_child(om, "meta:initial-creator"), buf,
                      sizeof buf) > 0 ||
        tiku_xml_text(tiku_xml_child(om, "dc:creator"), buf,
                      sizeof buf) > 0) {
        x->d->author = of_strdup(&x->d->arena, buf);
    }
    tiku_xml_free(m);
}

/** @brief The body read into sections.  @return 0, or -1. */
static int
body(od_t *x, const tiku_xml_node_t *text)
{
    of_vec_t blocks, sects;
    int err;

    of_vec_init(&blocks, sizeof(of_block_t));
    of_vec_init(&sects, sizeof(of_section_t));
    err = blocks_of(x, text, &blocks, &sects, NULL);
    if (err == 0 && (blocks.n > 0 || sects.n == 0)) {
        err = section_end(x, &blocks, &sects);
    }
    x->d->sect = of_vec_freeze(&sects, &x->d->arena, &x->d->nsect);
    of_vec_free(&blocks);
    of_vec_free(&sects);
    return err != 0 || x->d->nsect == 0 ? -1 : 0;
}

of_doc_t *
of_odt_read(of_pkg_t *pkg, char *why, size_t max)
{
    od_t x;
    const tiku_xml_node_t *text;
    int err;

    memset(&x, 0, sizeof x);
    x.pkg = pkg;
    x.content = of_pkg_xml(pkg, "content.xml");
    x.styles = of_pkg_xml(pkg, "styles.xml");
    text = tiku_xml_path(tiku_xml_root(x.content), "office:body/office:text");
    if (text == NULL || (x.d = of_doc_new()) == NULL) {
        snprintf(why, max, "%s", text == NULL ? "That document's text could "
                 "not be read: the file is damaged."
                 : "There is not enough memory to open it.");
        tiku_xml_free(x.content);
        tiku_xml_free(x.styles);
        return NULL;
    }
    of_vec_init(&x.notes, sizeof(of_story_t *));
    of_vec_init(&x.endnotes, sizeof(of_story_t *));
    of_vec_init(&x.waiting, sizeof(of_run_t));
    err = od_styles_load(&x);
    x.d->tab = od_length(tiku_xml_attr(tiku_xml_child(x.dflt[OD_PARA],
                         "style:paragraph-properties"),
                         "style:tab-stop-distance"), 35.4f);
    x.d->tab = x.d->tab >= 1 ? x.d->tab : 35.4f;
    x.d->tab_indent = 1;            /* stops count from the indent */
    meta(&x);
    err = err || body(&x, text) != 0;
    if (err == 0) {
        endnotes_place(&x);
        x.d->note = of_vec_freeze(&x.notes, &x.d->arena, &x.d->nnote);
    }
    of_vec_free(&x.notes);
    of_vec_free(&x.endnotes);
    of_vec_free(&x.waiting);
    tiku_xml_free(x.content);
    tiku_xml_free(x.styles);
    if (err != 0 || x.d->arena.bad) {
        snprintf(why, max, "There is not enough memory to read it.");
        of_doc_free(x.d);
        return NULL;
    }
    return x.d;
}
