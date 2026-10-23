/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_docx.c - a .docx read into a document.
 *
 * The body is walked in order: paragraphs and their runs, tables, and
 * the section properties that end each section.  What Word shows is what
 * is kept: inserted text shown and deleted text not, a field's result
 * but not its code -- save page numbers, which are counted afresh --
 * content controls and custom XML seen through.  Notes are numbered as
 * they are referred to; headers are carried from section to section.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_docx.h"
#include "tiku_office_math.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A paragraph's runs being gathered, text held until its look or
 *         link changes. */
typedef struct {
    of_vec_t          runs;         /* of_run_t                          */
    const char       *pstyle;       /* the paragraph's style             */
    char             *text;         /* text not yet a run                */
    size_t            ntext, cap;
    const of_style_t *st;           /* its look                          */
    const char       *link;         /* and its link                      */
} dx_para_t;

static int inline_of(dx_t *x, const tiku_xml_node_t *e, dx_para_t *pp,
                     const char *link);

/* --------------------------------------------------------------- runs  */

/** @brief The text held made a run.  @return 0, or -1 out of memory. */
static int
flush(dx_t *x, dx_para_t *pp)
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

/** @brief Text @p s (@p n bytes) in look @p st and link @p link, held on
 *         to the text before it when both are the same. */
static int
text(dx_t *x, dx_para_t *pp, const char *s, size_t n, const of_style_t *st,
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

/** @brief A run that is not text, after the text before it. */
static of_run_t *
object(dx_t *x, dx_para_t *pp, int kind, const of_style_t *st,
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

/** @brief @p cp as UTF-8 into @p out.  @return its length. */
static size_t
utf8(uint32_t cp, char *out)
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
    out[0] = (char)(0xe0 | cp >> 12);
    out[1] = (char)(0x80 | (cp >> 6 & 0x3f));
    out[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
}

/* ------------------------------------------------------------- fields  */

/** @brief Whether what is read now is shown: not inside a field's code,
 *         nor a result shown some other way. */
static int
shown(const dx_t *x)
{
    int i;

    for (i = 0; i < x->nfield; i++) {
        if (x->fstate[i] != 2) {
            return 0;
        }
    }
    return 1;
}

/** @brief The link the innermost HYPERLINK field gives, else @p link. */
static const char *
field_link(const dx_t *x, const char *link)
{
    int i;

    for (i = x->nfield - 1; i >= 0; i--) {
        if (x->flink[i] != NULL) {
            return x->flink[i];
        }
    }
    return link;
}

/**
 * @brief A field's code read; a HYPERLINK's target goes into @p link
 *        ("#name" for one within the document).
 * @return OF_FIELD_PAGE for PAGE, OF_FIELD_PAGES for NUMPAGES and
 *         SECTIONPAGES, which the layout counts; 0 for anything else.
 */
static int
field_code(dx_t *x, const char *code, const char **link)
{
    char word[16];
    const char *q;
    size_t k = 0;

    *link = NULL;
    while (*code == ' ') {
        code++;
    }
    while (k + 1 < sizeof word && isalpha((unsigned char)code[k])) {
        word[k] = (char)toupper((unsigned char)code[k]);
        k++;
    }
    word[k] = '\0';
    if (strcmp(word, "PAGE") == 0) {
        return OF_FIELD_PAGE;
    }
    if (strcmp(word, "NUMPAGES") == 0 || strcmp(word, "SECTIONPAGES") == 0)
        return OF_FIELD_PAGES;
    if (strcmp(word, "HYPERLINK") == 0 && (q = strchr(code, '"')) != NULL) {
        const char *end = strchr(q + 1, '"');
        int local = strstr(code, "\\l") != NULL;
        char buf[512];

        if (end != NULL) {
            snprintf(buf, sizeof buf, "%s%.*s", local ? "#" : "",
                     (int)(end - q - 1), q + 1);
            *link = of_strdup(&x->d->arena, buf);
        }
    }
    return 0;
}

/** @brief A w:fldChar: a field begun, its code ended, or the field ended.
 *  @return 0, or -1 out of memory. */
static int
fld_char(dx_t *x, const tiku_xml_node_t *c, dx_para_t *pp,
         const of_style_t *st)
{
    const char *type = tiku_xml_attr(c, "w:fldCharType"), *link;
    int kind;

    if (type == NULL) {
        return 0;
    }
    if (strcmp(type, "begin") == 0) {
        if (x->nfield < DX_FIELDS) {
            x->fstate[x->nfield] = 1;
            x->flink[x->nfield] = NULL;
        }
        x->nfield++;
        x->code[0] = '\0';
    } else if (strcmp(type, "separate") == 0 && x->nfield > 0 &&
               x->nfield <= DX_FIELDS) {
        int at = x->nfield - 1;

        kind = field_code(x, x->code, &link);
        x->fstate[at] = 2;
        x->flink[at] = link;
        if (kind != 0) {
            int outer = shown(x);           /* this one's result shows */

            x->fstate[at] = 3;              /* counted here, not shown */
            if (outer) {
                of_run_t *r = object(x, pp, OR_FIELD, st, NULL);

                if (r == NULL) {
                    return -1;
                }
                r->n = kind;
            }
        }
    } else if (strcmp(type, "end") == 0 && x->nfield > 0) {
        x->nfield--;
    }
    return 0;
}

/* -------------------------------------------------------------- notes  */

/** @brief The note @p id in @p notes (w:footnotes or w:endnotes), not one
 *         of the separators Word keeps there. */
static const tiku_xml_node_t *
find_note(const tiku_xml_t *notes, const char *id)
{
    const tiku_xml_node_t *n;

    for (n = tiku_xml_child(tiku_xml_root(notes), NULL); n != NULL && id;
         n = tiku_xml_next(n, NULL)) {
        const char *nid = tiku_xml_attr(n, "w:id");
        const char *type = tiku_xml_attr(n, "w:type");

        if (nid != NULL && strcmp(nid, id) == 0 &&
            (type == NULL || strcmp(type, "normal") == 0)) {
            return n;
        }
    }
    return NULL;
}

/** @brief A note referred to: read with its mark reading @p mark, and
 *         kept in @p list.  @return 0, or -1 when there is no such note. */
static int
take_note(dx_t *x, const tiku_xml_t *notes, const char *part, const char *id,
          const char *mark, of_vec_t *list)
{
    const tiku_xml_node_t *n = find_note(notes, id);
    const char *saved = x->mark_text;
    of_story_t **slot;
    of_story_t *s;

    if (n == NULL || x->depth >= OF_NEST_MAX) {
        return -1;
    }
    x->mark_text = mark;
    x->depth++;
    s = dx_story_apart(x, n, part);
    x->depth--;
    x->mark_text = saved;
    if (s == NULL || (slot = of_vec_push(list)) == NULL) {
        return -1;
    }
    *slot = s;
    return 0;
}

/** @brief A footnote or endnote reference: a footnote's number, its note
 *         set at the foot of the page the reference lands on; an endnote's
 *         small numeral, its text kept for the document's end. */
static int
note_ref(dx_t *x, const tiku_xml_node_t *c, dx_para_t *pp,
         const of_style_t *st, int foot)
{
    char mark[16];
    const char *id = tiku_xml_attr(c, "w:id");
    of_run_t *r;

    if (foot) {
        snprintf(mark, sizeof mark, "%d", x->notes_v.n + 1);
    } else {
        of_number(x->endnote.n + 1, OF_NUM_LROMAN, mark, sizeof mark);
    }
    if (take_note(x, foot ? x->notes : x->endnotes, foot ? x->note_part
                  : x->endnote_part, id, of_strdup(&x->d->arena, mark),
                  foot ? &x->notes_v : &x->endnote) != 0) {
        return 0;                   /* no such note: nothing to show */
    }
    if (!foot) {
        return text(x, pp, mark, strlen(mark), st, NULL);
    }
    if ((r = object(x, pp, OR_NOTE, st, NULL)) == NULL) {
        return -1;
    }
    r->n = x->notes_v.n - 1;
    r->text = of_strdup(&x->d->arena, mark);
    return 0;
}

/* --------------------------------------------------------------- runs  */

/** @brief A w:ptab as an OR_TAB run's n: where it goes, from what, with
 *         what leader; 0 for an ordinary w:tab. */
static int
absolute_tab(const tiku_xml_node_t *c)
{
    const char *al = tiku_xml_attr(c, "w:alignment");
    const char *rel = tiku_xml_attr(c, "w:relativeTo");
    const char *l = tiku_xml_attr(c, "w:leader");

    if (!tiku_xml_is(c, "w:ptab") || al == NULL) {
        return 0;
    }
    return OF_PTAB(strcmp(al, "center") == 0 ? OF_PTAB_CENTER :
                   strcmp(al, "right") == 0 ? OF_PTAB_RIGHT : OF_PTAB_LEFT,
                   rel != NULL && strcmp(rel, "indent") == 0,
                   l == NULL ? 0 : strcmp(l, "dot") == 0 ? '.' :
                   strcmp(l, "hyphen") == 0 ? '-' : strcmp(l, "underscore")
                   == 0 ? '_' : strcmp(l, "middleDot") == 0 ? 0xb7 : 0);
}

/** @brief One child of a w:r: text, a tab, a break, a symbol, a drawing,
 *         a note, a field's part. */
static int
run_child(dx_t *x, const tiku_xml_node_t *c, dx_para_t *pp,
          const of_style_t *st, const char *link)
{
    char buf[8];
    const char *v;
    of_run_t *r;

    if (tiku_xml_is(c, "w:fldChar")) {
        return fld_char(x, c, pp, st);
    }
    if (tiku_xml_is(c, "w:instrText")) {
        size_t k = strlen(x->code);

        (void)tiku_xml_text(c, x->code + k, sizeof x->code - k);
        return 0;
    }
    if (!shown(x)) {
        return 0;
    }
    link = field_link(x, link);
    if (tiku_xml_is(c, "w:t")) {
        char body[4096];
        size_t n = tiku_xml_text(c, body, sizeof body);

        if (n >= sizeof body) {
            const tiku_xml_node_t *t;

            for (t = c->child; t != NULL; t = t->next) {
                if (t->name == NULL && text(x, pp, t->text, strlen(t->text),
                                            st, link) != 0) {
                    return -1;
                }
            }
            return 0;
        }
        return text(x, pp, body, n, st, link);
    }
    if (tiku_xml_is(c, "w:tab") || tiku_xml_is(c, "w:ptab")) {
        return (r = object(x, pp, OR_TAB, st, link)) != NULL ?
               (r->n = absolute_tab(c), 0) : -1;
    }
    if (tiku_xml_is(c, "w:br") || tiku_xml_is(c, "w:cr")) {
        v = tiku_xml_attr(c, "w:type");
        return object(x, pp, v == NULL ? OR_BREAK : strcmp(v, "page") == 0
                      ? OR_PAGE : strcmp(v, "column") == 0 ? OR_COLUMN
                      : OR_BREAK, st, link) ? 0 : -1;
    }
    if (tiku_xml_is(c, "w:noBreakHyphen") || tiku_xml_is(c, "w:softHyphen")) {
        return text(x, pp, buf, utf8(c->name[2] == 'n' ? 0x2011 : 0xad, buf),
                    st, link);
    }
    if (tiku_xml_is(c, "w:sym")) {
        v = tiku_xml_attr(c, "w:char");
        return text(x, pp, buf, utf8(of_symbol(tiku_xml_attr(c, "w:font"),
                    v ? (uint32_t)strtoul(v, NULL, 16) : 0x2022), buf), st,
                    link);
    }
    if (tiku_xml_is(c, "w:footnoteReference") ||
        tiku_xml_is(c, "w:endnoteReference")) {
        return note_ref(x, c, pp, st, c->name[2] == 'f');
    }
    if ((tiku_xml_is(c, "w:footnoteRef") || tiku_xml_is(c, "w:endnoteRef"))
        && x->mark_text != NULL) {
        return text(x, pp, x->mark_text, strlen(x->mark_text), st, link);
    }
    if (tiku_xml_is(c, "mc:AlternateContent")) {
        const tiku_xml_node_t *k, *alt = of_alternate(c);

        for (k = tiku_xml_child(alt, NULL); k; k = tiku_xml_next(k, NULL)) {
            if (run_child(x, k, pp, st, link) != 0) {
                return -1;
            }
        }
        return 0;
    }
    {
        of_run_t got;

        memset(&got, 0, sizeof got);
        if (dx_drawing(x, c, &got) != 0) {
            return 0;               /* nothing drawn here */
        }
        if ((r = object(x, pp, got.kind, st, link)) == NULL) {
            return -1;
        }
        got.st = st;
        got.link = link;
        *r = got;
    }
    return 0;
}

/** @brief A w:r: its look, then each thing in it. */
static int
run(dx_t *x, const tiku_xml_node_t *r, dx_para_t *pp, const char *link)
{
    const tiku_xml_node_t *rpr = tiku_xml_child(r, "w:rPr"), *c;
    const char *rstyle = tiku_xml_attr(tiku_xml_child(rpr, "w:rStyle"),
                                       "w:val");
    dx_rpr_t own;
    const of_style_t *st;

    memset(&own, 0, sizeof own);
    dx_rpr_read(x, rpr, &own);
    if ((st = dx_look(x, pp->pstyle, rstyle, &own)) == NULL) {
        return -1;
    }
    for (c = tiku_xml_child(r, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        if (!tiku_xml_is(c, "w:rPr") && run_child(x, c, pp, st, link) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief What an equation's runs' looks are found with. */
typedef struct {
    dx_t       *x;
    const char *pstyle;             /* the paragraph's style             */
} dx_tint_t;

/** @brief Math run @p r's look: paragraph style @p pstyle's, with its
 *         own w:rPr.  NULL out of memory. */
static const of_style_t *
math_look(dx_t *x, const char *pstyle, const tiku_xml_node_t *r)
{
    const tiku_xml_node_t *rpr = tiku_xml_child(r, "w:rPr");
    dx_rpr_t own;

    memset(&own, 0, sizeof own);
    dx_rpr_read(x, rpr, &own);
    return dx_look(x, pstyle, tiku_xml_attr(tiku_xml_child(rpr,
                   "w:rStyle"), "w:val"), &own);
}

/** @brief Math run @p r's colour: of_omml()'s tint for Word. */
static uint32_t
math_tint(void *ctx, const tiku_xml_node_t *r)
{
    const dx_tint_t *t = ctx;
    const of_style_t *st = math_look(t->x, t->pstyle, r);

    return st != NULL ? st->color : 0;
}

/** @brief Equation @p e as one run of the paragraph, in display style
 *         when set apart, sized as its first run is, in the paragraph's
 *         colour where it gives none.  @return 0, or -1 out of memory. */
static int
equation(dx_t *x, const tiku_xml_node_t *e, int display, dx_para_t *pp,
         const char *link)
{
    const tiku_xml_node_t *first = of_omml_first(e);
    dx_tint_t t = { x, pp->pstyle };
    const of_style_t *base = dx_look(x, pp->pstyle, NULL, NULL);
    const of_style_t *st = first != NULL ? math_look(x, pp->pstyle, first)
                                         : base;
    of_math_t *m;
    of_run_t *r;

    if (st == NULL || base == NULL ||
        of_omml(x->d, e, display, st->size, base->color, math_tint, &t,
                &m) != 0) {
        return -1;
    }
    if (m == NULL) {
        return 0;                   /* an equation saying nothing */
    }
    if ((r = object(x, pp, OR_MATH, st, link)) == NULL) {
        return -1;
    }
    r->math = m;
    return 0;
}

/** @brief An m:oMath in the text, or an m:oMathPara's equations set
 *         apart, each on a line of its own.  @return 0, or -1. */
static int
math(dx_t *x, const tiku_xml_node_t *e, dx_para_t *pp, const char *link)
{
    const tiku_xml_node_t *c;
    const of_style_t *st;
    int n = 0;

    if (!tiku_xml_is(e, "m:oMathPara")) {
        return equation(x, e, 0, pp, link);
    }
    for (c = tiku_xml_child(e, "m:oMath"); c != NULL;
         c = tiku_xml_next(c, "m:oMath")) {
        st = pp->st != NULL ? pp->st : dx_look(x, pp->pstyle, NULL, NULL);
        if (n++ > 0 && (st == NULL || object(x, pp, OR_BREAK, st, link)
                                      == NULL)) {
            return -1;
        }
        if (equation(x, c, 1, pp, link) != 0) {
            return -1;
        }
    }
    return 0;
}

/** @brief A hyperlink's target: an address its relationship names, or a
 *         bookmark in the document. */
static const char *
hyperlink(dx_t *x, const tiku_xml_node_t *h)
{
    const char *id = tiku_xml_attr(h, "r:id");
    const char *anchor = tiku_xml_attr(h, "w:anchor");
    const of_rel_t *r = id != NULL ? of_pkg_rel(x->pkg, x->part, id) : NULL;
    char buf[512];

    if (r != NULL && r->external) {
        return of_strdup(&x->d->arena, r->target);
    }
    if (anchor != NULL) {
        snprintf(buf, sizeof buf, "#%s", anchor);
        return of_strdup(&x->d->arena, buf);
    }
    return NULL;
}

/** @brief A bookmark's start, as a run where it is. */
static int
bookmark(dx_t *x, const tiku_xml_node_t *c, dx_para_t *pp)
{
    const char *name = tiku_xml_attr(c, "w:name");
    of_run_t *r;
    of_style_t st;

    if (name == NULL || strcmp(name, "_GoBack") == 0) {
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

/** @brief A w:fldSimple: a page number counted here, or its result. */
static int
simple_field(dx_t *x, const tiku_xml_node_t *c, dx_para_t *pp,
             const char *link)
{
    const char *flink;
    int kind = field_code(x, tiku_xml_attr(c, "w:instr") ?
                          tiku_xml_attr(c, "w:instr") : "", &flink);
    const tiku_xml_node_t *r = tiku_xml_child(c, "w:r");
    of_run_t *out;
    dx_rpr_t own;

    if (kind == 0) {
        return inline_of(x, c, pp, flink ? flink : link);
    }
    memset(&own, 0, sizeof own);
    dx_rpr_read(x, tiku_xml_child(r, "w:rPr"), &own);
    out = object(x, pp, OR_FIELD, dx_look(x, pp->pstyle, NULL, &own), link);
    if (out == NULL || out->st == NULL) {
        return -1;
    }
    out->n = kind;
    return 0;
}

/** @brief The inline content of @p e: runs, and what wraps them. */
static int
inline_of(dx_t *x, const tiku_xml_node_t *e, dx_para_t *pp, const char *link)
{
    const tiku_xml_node_t *c;
    int err = 0;

    for (c = tiku_xml_child(e, NULL); c != NULL && err == 0;
         c = tiku_xml_next(c, NULL)) {
        if (tiku_xml_is(c, "w:r")) {
            err = run(x, c, pp, link);
        } else if (tiku_xml_is(c, "w:hyperlink")) {
            const char *to = hyperlink(x, c);

            err = inline_of(x, c, pp, to ? to : link);
        } else if (tiku_xml_is(c, "w:fldSimple")) {
            err = simple_field(x, c, pp, link);
        } else if (tiku_xml_is(c, "w:bookmarkStart")) {
            err = bookmark(x, c, pp);
        } else if (tiku_xml_is(c, "m:oMath") || tiku_xml_is(c,
                                                            "m:oMathPara")) {
            err = shown(x) ? math(x, c, pp, link) : 0;
        } else if (tiku_xml_is(c, "w:sdt")) {
            err = inline_of(x, tiku_xml_child(c, "w:sdtContent"), pp, link);
        } else if (tiku_xml_is(c, "mc:AlternateContent")) {
            err = inline_of(x, of_alternate(c), pp, link);
        } else if (tiku_xml_is(c, "w:ins") || tiku_xml_is(c, "w:moveTo") ||
                   tiku_xml_is(c, "w:smartTag") ||
                   tiku_xml_is(c, "w:customXml") || tiku_xml_is(c, "w:dir")
                   || tiku_xml_is(c, "w:bdo")) {
            err = inline_of(x, c, pp, link);
        }
    }
    return err;
}

/* ---------------------------------------------------------- paragraphs  */

/** @brief A paragraph holding a display equation set as the equation says:
 *         centred, unless its m:jc puts it left or right. */
static void
display_math(const tiku_xml_node_t *p, of_para_t *out)
{
    const tiku_xml_node_t *m = tiku_xml_child(p, "m:oMathPara");
    const char *jc = tiku_xml_attr(tiku_xml_path(m, "m:oMathParaPr/m:jc"),
                                   "m:val");

    if (m != NULL) {
        out->ps.align = jc != NULL && strcmp(jc, "left") == 0 ? OF_LEFT :
                        jc != NULL && strcmp(jc, "right") == 0 ? OF_RIGHT
                                                               : OF_CENTER;
    }
}

/** @brief A w:p read into a paragraph: its layout, its mark's look, its
 *         runs.  NULL out of memory. */
static of_para_t *
paragraph(dx_t *x, const tiku_xml_node_t *p)
{
    const tiku_xml_node_t *ppr = tiku_xml_child(p, "w:pPr");
    const tiku_xml_node_t *mpr = tiku_xml_child(ppr, "w:rPr");
    of_para_t *out = of_alloc(&x->d->arena, sizeof *out);
    dx_para_t pp;
    dx_ppr_t own;
    dx_rpr_t mark;
    int i, err;

    if (out == NULL) {
        return NULL;
    }
    memset(&pp, 0, sizeof pp);
    memset(&own, 0, sizeof own);
    memset(&mark, 0, sizeof mark);
    of_vec_init(&pp.runs, sizeof(of_run_t));
    pp.pstyle = tiku_xml_attr(tiku_xml_child(ppr, "w:pStyle"), "w:val");
    dx_ppr_read(x, ppr, &own);
    dx_rpr_read(x, mpr, &mark);
    dx_layout(x, pp.pstyle, &own, &mark, out);
    out->mark = dx_look(x, pp.pstyle, tiku_xml_attr(tiku_xml_child(mpr,
                        "w:rStyle"), "w:val"), &mark);
    err = out->mark == NULL;
    for (i = 0; i < x->marks.n && err == 0; i++) {
        tiku_xml_node_t fake;
        const char *pair[2] = {"w:name", ((const char **)x->marks.p)[i]};

        memset(&fake, 0, sizeof fake);
        fake.attr = pair;
        fake.nattr = 1;
        err = bookmark(x, &fake, &pp);
    }
    x->marks.n = 0;
    display_math(p, out);
    err = err || inline_of(x, p, &pp, NULL) != 0 || flush(x, &pp) != 0;
    out->run = of_vec_freeze(&pp.runs, &x->d->arena, &out->nrun);
    of_vec_free(&pp.runs);
    free(pp.text);
    return err ? NULL : out;
}

/* -------------------------------------------------------------- blocks  */

/** @brief Section properties read, the headers and footers they name
 *         read too, or carried from the section before. */
static void
section_read(dx_t *x, const tiku_xml_node_t *e, of_section_t *s)
{
    const tiku_xml_node_t *sz = tiku_xml_child(e, "w:pgSz"), *c;
    const tiku_xml_node_t *mar = tiku_xml_child(e, "w:pgMar");
    const tiku_xml_node_t *cols = tiku_xml_child(e, "w:cols");
    const tiku_xml_node_t *pn = tiku_xml_child(e, "w:pgNumType");
    const char *fmt = tiku_xml_attr(pn, "w:fmt"), *type;
    int k;

    s->w = dx_twips(tiku_xml_attr(sz, "w:w"), 612);
    s->h = dx_twips(tiku_xml_attr(sz, "w:h"), 792);
    s->top = dx_twips(tiku_xml_attr(mar, "w:top"), 72);
    s->top = s->top < 0 ? -s->top : s->top;
    s->bottom = dx_twips(tiku_xml_attr(mar, "w:bottom"), 72);
    s->bottom = s->bottom < 0 ? -s->bottom : s->bottom;
    s->left = dx_twips(tiku_xml_attr(mar, "w:left"), 72) +
              dx_twips(tiku_xml_attr(mar, "w:gutter"), 0);
    s->right = dx_twips(tiku_xml_attr(mar, "w:right"), 72);
    s->head = dx_twips(tiku_xml_attr(mar, "w:header"), 36);
    s->foot = dx_twips(tiku_xml_attr(mar, "w:footer"), 36);
    s->cols = tiku_xml_attr(cols, "w:num") ? atoi(tiku_xml_attr(cols,
                                                                "w:num")) : 1;
    s->gap = dx_twips(tiku_xml_attr(cols, "w:space"), 36);
    s->title_page = (unsigned char)dx_on(tiku_xml_child(e, "w:titlePg"));
    s->start = tiku_xml_attr(pn, "w:start") ? atoi(tiku_xml_attr(pn,
                                                   "w:start")) : -1;
    s->num = fmt == NULL ? OF_NUM_DECIMAL : strcmp(fmt, "upperRoman") == 0
             ? OF_NUM_UROMAN : strcmp(fmt, "lowerRoman") == 0
             ? OF_NUM_LROMAN : strcmp(fmt, "upperLetter") == 0
             ? OF_NUM_ULETTER : strcmp(fmt, "lowerLetter") == 0
             ? OF_NUM_LLETTER : OF_NUM_DECIMAL;
    type = tiku_xml_attr(tiku_xml_child(e, "w:type"), "w:val");
    s->kind = type != NULL && (strcmp(type, "continuous") == 0 ||
                               strcmp(type, "nextColumn") == 0);
    for (c = tiku_xml_child(e, NULL); c != NULL; c = tiku_xml_next(c, NULL)) {
        int head = tiku_xml_is(c, "w:headerReference");
        const char *t = tiku_xml_attr(c, "w:type");
        const of_rel_t *r = of_pkg_rel(x->pkg, x->part,
                                       tiku_xml_attr(c, "r:id"));
        tiku_xml_t *part;
        int since = x->arts;

        if ((!head && !tiku_xml_is(c, "w:footerReference")) || r == NULL) {
            continue;
        }
        k = t != NULL && strcmp(t, "first") == 0 ? 1 :
            t != NULL && strcmp(t, "even") == 0 ? 2 : 0;
        part = of_pkg_xml(x->pkg, r->target);
        (head ? x->header : x->footer)[k] =
            dx_story_apart(x, tiku_xml_root(part), r->target);
        dx_art_keep(x, part, since);
    }
    memcpy(s->header, x->header, sizeof s->header);
    memcpy(s->footer, x->footer, sizeof s->footer);
}

/** @brief The blocks gathered so far made a section, ended by @p sp. */
static int
section_end(dx_t *x, const tiku_xml_node_t *sp, of_vec_t *blocks,
            of_vec_t *sects)
{
    of_section_t *s = of_vec_push(sects);

    if (s == NULL) {
        return -1;
    }
    section_read(x, sp, s);
    s->body.block = of_vec_freeze(blocks, &x->d->arena, &s->body.nblock);
    return x->d->arena.bad ? -1 : 0;
}

/** @brief The blocks of @p e into @p blocks; with @p sects, a paragraph
 *         carrying section properties ends a section there. */
static int
blocks_of(dx_t *x, const tiku_xml_node_t *e, of_vec_t *blocks,
          of_vec_t *sects)
{
    const tiku_xml_node_t *c, *sp;
    of_block_t *b;
    const char **m;
    int err = 0;

    for (c = tiku_xml_child(e, NULL); c != NULL && err == 0;
         c = tiku_xml_next(c, NULL)) {
        if (tiku_xml_is(c, "w:p") || tiku_xml_is(c, "w:tbl")) {
            if ((b = of_vec_push(blocks)) == NULL) {
                return -1;
            }
            b->kind = c->name[2] == 'p' ? OB_PARA : OB_TABLE;
            if (b->kind == OB_PARA) {
                err = (b->para = paragraph(x, c)) == NULL;
            } else {
                b->table = of_alloc(&x->d->arena, sizeof *b->table);
                err = b->table == NULL || dx_table(x, c, b->table) != 0;
            }
            sp = tiku_xml_path(c, "w:pPr/w:sectPr");
            if (err == 0 && sects != NULL && sp != NULL) {
                err = section_end(x, sp, blocks, sects);
            }
        } else if (tiku_xml_is(c, "w:sdt")) {
            err = blocks_of(x, tiku_xml_child(c, "w:sdtContent"), blocks,
                            sects);
        } else if (tiku_xml_is(c, "w:customXml") || tiku_xml_is(c, "w:ins")
                   || tiku_xml_is(c, "w:moveTo")) {
            err = blocks_of(x, c, blocks, sects);
        } else if (tiku_xml_is(c, "mc:AlternateContent")) {
            err = blocks_of(x, of_alternate(c), blocks, sects);
        } else if (tiku_xml_is(c, "w:bookmarkStart") &&
                   tiku_xml_attr(c, "w:name") != NULL &&
                   (m = of_vec_push(&x->marks)) != NULL) {
            *m = of_strdup(&x->d->arena, tiku_xml_attr(c, "w:name"));
        }
    }
    return err ? -1 : 0;
}

of_story_t *
dx_story(dx_t *x, const tiku_xml_node_t *e)
{
    of_vec_t blocks;
    of_story_t *s = of_alloc(&x->d->arena, sizeof *s);
    int err;

    if (s == NULL) {
        return NULL;
    }
    of_vec_init(&blocks, sizeof(of_block_t));
    err = blocks_of(x, e, &blocks, NULL);
    s->block = of_vec_freeze(&blocks, &x->d->arena, &s->nblock);
    of_vec_free(&blocks);
    return err ? NULL : s;
}

of_story_t *
dx_story_apart(dx_t *x, const tiku_xml_node_t *e, const char *part)
{
    const char *was = x->part;
    const dx_style_t *table = x->table;
    int nfield = x->nfield, tpart = x->table_part;
    of_story_t *s;

    if (part != NULL) {
        x->part = part;
    }
    x->table = NULL;
    x->table_part = TS_WHOLE;
    x->nfield = 0;
    s = dx_story(x, e);
    x->part = was;
    x->table = table;
    x->table_part = tpart;
    x->nfield = nfield;
    return s;
}

/* ---------------------------------------------------------- the whole  */

/** @brief The document's settings: its default tab stops and whether even
 *         pages have headers of their own. */
static void
settings(dx_t *x, const char *main)
{
    const of_rel_t *r = of_pkg_rel_type(x->pkg, main, "settings");
    tiku_xml_t *s = r ? of_pkg_xml(x->pkg, r->target) : NULL;
    const tiku_xml_node_t *root = tiku_xml_root(s);
    float tab = dx_twips(tiku_xml_attr(tiku_xml_child(root,
                         "w:defaultTabStop"), "w:val"), 36);

    x->d->tab = tab >= 1 ? tab : 36;
    x->d->even_odd = (unsigned char)dx_on(tiku_xml_child(root,
                                                  "w:evenAndOddHeaders"));
    tiku_xml_free(s);
}

/** @brief The title and author the package's core properties give. */
static void
properties(dx_t *x)
{
    const of_rel_t *r = of_pkg_rel_type(x->pkg, "", "core-properties");
    tiku_xml_t *c = r ? of_pkg_xml(x->pkg, r->target) : NULL;
    char buf[256];

    if (tiku_xml_text(tiku_xml_child(tiku_xml_root(c), "dc:title"), buf,
                      sizeof buf) > 0) {
        x->d->title = of_strdup(&x->d->arena, buf);
    }
    if (tiku_xml_text(tiku_xml_child(tiku_xml_root(c), "dc:creator"), buf,
                      sizeof buf) > 0) {
        x->d->author = of_strdup(&x->d->arena, buf);
    }
    tiku_xml_free(c);
}

/** @brief The notes parts, kept open for notes to be taken from. */
static void
notes_load(dx_t *x, const char *main)
{
    const of_rel_t *f = of_pkg_rel_type(x->pkg, main, "footnotes");
    const of_rel_t *e = of_pkg_rel_type(x->pkg, main, "endnotes");

    x->notes = f ? of_pkg_xml(x->pkg, f->target) : NULL;
    x->endnotes = e ? of_pkg_xml(x->pkg, e->target) : NULL;
    x->note_part = f ? of_strdup(&x->d->arena, f->target) : NULL;
    x->endnote_part = e ? of_strdup(&x->d->arena, e->target) : NULL;
}

/** @brief The endnotes, in the order referred to, after the last
 *         section's text: each its paragraphs. */
static void
endnotes_place(dx_t *x)
{
    of_section_t *s = &x->d->sect[x->d->nsect - 1];
    of_story_t **n = x->endnote.p;
    of_block_t *all;
    int total = s->body.nblock, i, k = 0;

    for (i = 0; i < x->endnote.n; i++) {
        total += n[i]->nblock;
    }
    if (x->endnote.n == 0 || (all = of_alloc(&x->d->arena, (size_t)total *
                                             sizeof *all)) == NULL) {
        return;
    }
    memcpy(all, s->body.block, (size_t)s->body.nblock * sizeof *all);
    k = s->body.nblock;
    for (i = 0; i < x->endnote.n; i++) {
        memcpy(all + k, n[i]->block, (size_t)n[i]->nblock * sizeof *all);
        k += n[i]->nblock;
    }
    s->body.block = all;
    s->body.nblock = k;
}

/** @brief The body read into sections: each ended by a paragraph's
 *         properties, the last by the body's own.  @return 0, or -1. */
static int
body(dx_t *x, const tiku_xml_node_t *b)
{
    of_vec_t blocks, sects;
    const tiku_xml_node_t *last = tiku_xml_child(b, "w:sectPr");
    tiku_xml_node_t none;
    int err;

    memset(&none, 0, sizeof none);
    of_vec_init(&blocks, sizeof(of_block_t));
    of_vec_init(&sects, sizeof(of_section_t));
    err = blocks_of(x, b, &blocks, &sects);
    if (err == 0 && (blocks.n > 0 || sects.n == 0)) {
        err = section_end(x, last != NULL ? last : &none, &blocks, &sects);
    }
    x->d->sect = of_vec_freeze(&sects, &x->d->arena, &x->d->nsect);
    of_vec_free(&blocks);
    of_vec_free(&sects);
    return err != 0 || x->d->nsect == 0 ? -1 : 0;
}

/** @brief The reader's own memory given back; the document stays. */
static void
reader_done(dx_t *x)
{
    free(x->style);
    free(x->list);
    dx_art_keep(x, x->notes, 0);
    dx_art_keep(x, x->endnotes, 0);
    of_vec_free(&x->marks);
    of_vec_free(&x->notes_v);
    of_vec_free(&x->endnote);
}

of_doc_t *
of_docx_read(of_pkg_t *pkg, char *why, size_t max)
{
    const of_rel_t *m = of_pkg_rel_type(pkg, "", "officeDocument");
    const char *main = m != NULL ? m->target : "word/document.xml";
    tiku_xml_t *doc = of_pkg_xml(pkg, main);
    const tiku_xml_node_t *b = tiku_xml_path(tiku_xml_root(doc), "w:body");
    dx_t x;
    int err;

    memset(&x, 0, sizeof x);
    if (b == NULL || (x.d = of_doc_new()) == NULL) {
        snprintf(why, max, "%s", b == NULL ? "That document's text could not"
                 " be read: the file is damaged."
                 : "There is not enough memory to open it.");
        tiku_xml_free(doc);
        return NULL;
    }
    x.pkg = pkg;
    x.part = of_strdup(&x.d->arena, main);
    of_vec_init(&x.marks, sizeof(char *));
    of_vec_init(&x.notes_v, sizeof(of_story_t *));
    of_vec_init(&x.endnote, sizeof(of_story_t *));
    dx_styles_load(&x, main);
    settings(&x, main);
    properties(&x);
    notes_load(&x, main);
    err = body(&x, b);
    if (err == 0) {
        endnotes_place(&x);
        x.d->note = of_vec_freeze(&x.notes_v, &x.d->arena, &x.d->nnote);
    }
    dx_art_keep(&x, doc, 0);
    reader_done(&x);
    if (err != 0 || x.d->arena.bad) {
        snprintf(why, max, "There is not enough memory to read it.");
        of_doc_free(x.d);
        return NULL;
    }
    return x.d;
}
