/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_odt_style.c - an OpenDocument's styles resolved: how text
 * looks and paragraphs lie, each property from the nearest style in the
 * chain that states it, then the family's default; list labels counted
 * and set as their list style's level says; pages from master pages.
 *
 * A text's size may be a share of what it would otherwise be ("150%"),
 * so looks are built from the root of a chain down.  A page's margins in
 * OpenDocument run to its header; here they run to the text, as in Word,
 * so a header's height and spacing are added to them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_odt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHAIN 16                    /* styles based on styles, at most   */

/* ------------------------------------------------------------- values  */

float
od_length(const char *v, float dflt)
{
    static const struct { const char *unit; double pt; } u[] = {
        {"pt", 1}, {"in", 72}, {"cm", 72 / 2.54}, {"mm", 72 / 25.4},
        {"pc", 12}, {"px", 0.75}};
    char *end;
    double n;
    size_t i;

    if (v == NULL || *v == '\0') {
        return dflt;
    }
    n = strtod(v, &end);
    if (end == v) {
        return dflt;
    }
    for (i = 0; i < sizeof u / sizeof u[0]; i++) {
        if (strncmp(end, u[i].unit, 2) == 0) {
            return (float)(n * u[i].pt);
        }
    }
    return (float)n;                /* no unit: points */
}

uint32_t
od_color(const char *v)
{
    char *end;
    unsigned long c;

    if (v == NULL || v[0] != '#' || strlen(v) != 7) {
        return 0;
    }
    c = strtoul(v + 1, &end, 16);
    return *end == '\0' ? 0xff000000u | (uint32_t)c : 0;
}

void
od_border(const char *v, of_border_t *b)
{
    char width[32] = "", kind[32] = "", color[16] = "";

    memset(b, 0, sizeof *b);
    if (v == NULL || strcmp(v, "none") == 0 || strcmp(v, "hidden") == 0 ||
        sscanf(v, "%31s %31s %15s", width, kind, color) < 2) {
        return;
    }
    b->width = od_length(width, 0.5f);
    b->color = od_color(color);
    b->style = strcmp(kind, "double") == 0 ? OF_LS_DOUBLE :
               strcmp(kind, "dotted") == 0 ? OF_LS_DOTTED :
               strcmp(kind, "dashed") == 0 ? OF_LS_DASHED : OF_LS_SINGLE;
    if (b->width <= 0) {
        b->width = 0.5f;            /* zero: a hairline, drawn */
    }
}

int
od_count(const char *v, int dflt)
{
    long n;

    if (v == NULL || *v == '\0') {
        return dflt;
    }
    n = strtol(v, NULL, 10);
    return n < 0 ? 0 : n > 32767 ? 32767 : (int)n;
}

/** @brief Whether attribute @p v says yes. */
static int
yes(const char *v)
{
    return v != NULL && (strcmp(v, "true") == 0 || strcmp(v, "always") == 0);
}

/* ------------------------------------------------------------- finding  */

/** @brief Styles in order of name, then family, for looking up. */
static int
by_name(const void *a, const void *b)
{
    const od_style_t *p = a, *q = b;
    int c = strcmp(p->name, q->name);

    return c != 0 ? c : p->family - q->family;
}

/** @brief A style:family's value as OD_*, or -1 for one not read. */
static int
family_of(const char *v)
{
    static const char *const f[OD_FAMILIES] = {"paragraph", "text", "table",
        "table-column", "table-row", "table-cell", "graphic"};
    int i;

    for (i = 0; v != NULL && i < OD_FAMILIES; i++) {
        if (strcmp(v, f[i]) == 0) {
            return i;
        }
    }
    return -1;
}

/** @brief The styles in container @p c (office:styles, automatic ones)
 *         added to @p v; defaults and the outline style noted.  @return 0,
 *         or -1 out of memory. */
static int
gather(od_t *x, const tiku_xml_node_t *c, int automatic, of_vec_t *v)
{
    const tiku_xml_node_t *e;
    od_style_t *s;
    int f;

    for (e = tiku_xml_child(c, NULL); e != NULL; e = tiku_xml_next(e, NULL)) {
        f = family_of(tiku_xml_attr(e, "style:family"));
        if (tiku_xml_is(e, "style:default-style") && f >= 0) {
            x->dflt[f] = e;
        } else if (tiku_xml_is(e, "text:outline-style")) {
            x->outline = e;
        } else if (tiku_xml_is(e, "style:style") && f >= 0 &&
                   tiku_xml_attr(e, "style:name") != NULL) {
            if ((s = of_vec_push(v)) == NULL) {
                return -1;
            }
            s->name = tiku_xml_attr(e, "style:name");
            s->parent = tiku_xml_attr(e, "style:parent-style-name");
            s->family = f;
            s->automatic = automatic;
            s->e = e;
        }
    }
    return 0;
}

int
od_styles_load(od_t *x)
{
    const tiku_xml_node_t *sr = tiku_xml_root(x->styles);
    const tiku_xml_node_t *cr = tiku_xml_root(x->content);
    of_vec_t v;
    int err;

    of_vec_init(&v, sizeof(od_style_t));
    x->faces[0] = tiku_xml_child(sr, "office:font-face-decls");
    x->faces[1] = tiku_xml_child(cr, "office:font-face-decls");
    x->lstyles[0] = tiku_xml_child(sr, "office:styles");
    x->lstyles[1] = tiku_xml_child(sr, "office:automatic-styles");
    x->lstyles[2] = tiku_xml_child(cr, "office:automatic-styles");
    x->layouts = x->lstyles[1];
    x->masters = tiku_xml_child(sr, "office:master-styles");
    err = gather(x, x->lstyles[0], 0, &v) != 0 ||
          gather(x, x->lstyles[1], 0, &v) != 0 ||
          gather(x, x->lstyles[2], 1, &v) != 0;
    if (v.n > 1) {
        qsort(v.p, (size_t)v.n, sizeof(od_style_t), by_name);
    }
    x->style = of_vec_freeze(&v, &x->d->arena, &x->nstyle);
    of_vec_free(&v);
    return err || x->d->arena.bad ? -1 : 0;
}

const od_style_t *
od_style(const od_t *x, int family, const char *name)
{
    od_style_t key;

    if (name == NULL || x->nstyle == 0) {
        return NULL;
    }
    key.name = name;
    key.family = family;
    return bsearch(&key, x->style, (size_t)x->nstyle, sizeof key, by_name);
}

const char *
od_attr(const od_t *x, int family, const char *name, const char *props,
        const char *attr)
{
    const od_style_t *s = od_style(x, family, name);
    const char *v;
    int depth;

    for (depth = 0; s != NULL && depth < CHAIN; depth++) {
        v = tiku_xml_attr(tiku_xml_child(s->e, props), attr);
        if (v != NULL) {
            return v;
        }
        s = od_style(x, family, s->parent);
    }
    return tiku_xml_attr(tiku_xml_child(x->dflt[family], props), attr);
}

/* --------------------------------------------------------------- looks  */

/** @brief A font face declaration's family, or @p name itself: quotes
 *         taken off either way, into @p out. */
static void
face_family(const od_t *x, const char *name, char *out, size_t max)
{
    const tiku_xml_node_t *f;
    const char *fam = name;
    int i;

    for (i = 0; i < 2; i++) {
        for (f = tiku_xml_child(x->faces[i], "style:font-face"); f != NULL;
             f = tiku_xml_next(f, "style:font-face")) {
            const char *n = tiku_xml_attr(f, "style:name");

            if (n != NULL && strcmp(n, name) == 0 &&
                tiku_xml_attr(f, "svg:font-family") != NULL) {
                fam = tiku_xml_attr(f, "svg:font-family");
            }
        }
    }
    while (*fam == '\'' || *fam == '"') {
        fam++;
    }
    snprintf(out, max, "%.*s", (int)strcspn(fam, "'\","), fam);
}

/** @brief A text's font, from @p tp's style:font-name or fo:font-family
 *         (@p asian: their East Asian ones), kept in the document.  NULL
 *         when it names none. */
static const char *
font_of(od_t *x, const tiku_xml_node_t *tp, int asian)
{
    const char *n = tiku_xml_attr(tp, asian ? "style:font-name-asian"
                                            : "style:font-name");
    const char *f = tiku_xml_attr(tp, asian ? "style:font-family-asian"
                                            : "fo:font-family");
    char buf[128];

    if (n == NULL && f == NULL) {
        return NULL;
    }
    face_family(x, n != NULL ? n : f, buf, sizeof buf);
    return buf[0] ? of_strdup(&x->d->arena, buf) : NULL;
}

/** @brief A line's kind -- an underline or a line through -- from its
 *         style, type and width: OF_UL_*. */
static unsigned char
line_kind(const char *style, const char *type, const char *width)
{
    if (style == NULL || strcmp(style, "none") == 0) {
        return OF_UL_NONE;
    }
    if (type != NULL && strcmp(type, "double") == 0) {
        return OF_UL_DOUBLE;
    }
    if (strcmp(style, "wave") == 0) {
        return OF_UL_WAVE;
    }
    if (strcmp(style, "dotted") == 0) {
        return OF_UL_DOTTED;
    }
    if (strstr(style, "dash") != NULL) {
        return OF_UL_DASHED;
    }
    return width != NULL && strcmp(width, "bold") == 0 ? OF_UL_THICK
                                                       : OF_UL_SINGLE;
}

/** @brief The fonts, size, weight, slant and colours @p tp states, over
 *         @p st. */
static void
text_face(od_t *x, const tiku_xml_node_t *tp, of_style_t *st)
{
    const char *v = tiku_xml_attr(tp, "fo:font-size"), *font;
    float k;

    if ((font = font_of(x, tp, 0)) != NULL) {
        st->font = font;
    }
    if ((font = font_of(x, tp, 1)) != NULL) {
        st->font_ea = font;
    }
    if (v != NULL && v[0] && v[strlen(v) - 1] == '%') {
        k = st->size * (float)atof(v) / 100.0f;
        st->size = k >= 1 && k <= 1638 ? k : st->size;
    } else if (v != NULL) {
        k = od_length(v, st->size);
        st->size = k >= 1 && k <= 1638 ? k : st->size;
    }
    if ((v = tiku_xml_attr(tp, "fo:font-weight")) != NULL) {
        st->bold = strcmp(v, "bold") == 0 || atoi(v) >= 600;
    }
    if ((v = tiku_xml_attr(tp, "fo:font-style")) != NULL) {
        st->italic = strcmp(v, "normal") != 0;
    }
    if ((v = tiku_xml_attr(tp, "fo:color")) != NULL) {
        st->color = od_color(v);
    }
    if (yes(tiku_xml_attr(tp, "style:use-window-font-color"))) {
        st->color = 0;
    }
    if ((v = tiku_xml_attr(tp, "fo:background-color")) != NULL) {
        st->back = od_color(v);
    }
}

/** @brief Text properties @p tp (a style:text-properties) over @p st. */
static void
apply_text(od_t *x, const tiku_xml_node_t *tp, of_style_t *st)
{
    const char *v;

    if (tp == NULL) {
        return;
    }
    text_face(x, tp, st);
    if ((v = tiku_xml_attr(tp, "style:text-underline-style")) != NULL) {
        st->underline = line_kind(v, tiku_xml_attr(tp,
                                  "style:text-underline-type"),
                                  tiku_xml_attr(tp,
                                  "style:text-underline-width"));
    }
    if ((v = tiku_xml_attr(tp, "style:text-line-through-style")) != NULL) {
        st->strike = strcmp(v, "none") == 0 ? 0 : line_kind(v,
                     tiku_xml_attr(tp, "style:text-line-through-type"),
                     NULL) == OF_UL_DOUBLE ? 2 : 1;
    }
    if ((v = tiku_xml_attr(tp, "style:text-position")) != NULL) {
        st->vert = strncmp(v, "super", 5) == 0 ? 1 :
                   strncmp(v, "sub", 3) == 0 ? -1 : atof(v) > 0 ? 1 :
                   atof(v) < 0 ? -1 : 0;
    }
    if ((v = tiku_xml_attr(tp, "fo:text-transform")) != NULL) {
        st->caps = strcmp(v, "uppercase") == 0;
    }
    if ((v = tiku_xml_attr(tp, "fo:font-variant")) != NULL) {
        st->smallcaps = strcmp(v, "small-caps") == 0;
    }
    if ((v = tiku_xml_attr(tp, "text:display")) != NULL) {
        st->hidden = strcmp(v, "none") == 0;
    }
    if ((v = tiku_xml_attr(tp, "fo:letter-spacing")) != NULL) {
        st->spacing = strcmp(v, "normal") == 0 ? 0 : od_length(v, 0);
    }
    if ((v = tiku_xml_attr(tp, "style:text-scale")) != NULL &&
        atof(v) > 0) {
        st->scale = (float)atof(v) / 100.0f;
    }
}

/** @brief Style @p name's text properties over @p st, its parents' first:
 *         a share of a size is a share of theirs. */
static void
chain_text(od_t *x, int family, const char *name, of_style_t *st,
           int depth)
{
    const od_style_t *s = od_style(x, family, name);

    if (s == NULL || depth >= CHAIN) {
        return;
    }
    chain_text(x, family, s->parent, st, depth + 1);
    apply_text(x, tiku_xml_child(s->e, "style:text-properties"), st);
}

void
od_para_look(od_t *x, const char *pstyle, of_style_t *st)
{
    of_style_default(st);
    st->font = "Liberation Serif";  /* what OpenDocument's writers assume */
    apply_text(x, tiku_xml_child(x->dflt[OD_PARA], "style:text-properties"),
               st);
    chain_text(x, OD_PARA, pstyle, st, 0);
}

void
od_span_look(od_t *x, const char *tstyle, of_style_t *st)
{
    chain_text(x, OD_TEXT, tstyle, st, 0);
}

/* --------------------------------------------------------- paragraphs  */

/** @brief The paragraph property @p attr of @p pstyle, through its chain. */
static const char *
pp(const od_t *x, const char *pstyle, const char *attr)
{
    return od_attr(x, OD_PARA, pstyle, "style:paragraph-properties", attr);
}

/** @brief A leader's character from a tab stop's text or style. */
static unsigned char
leader(const tiku_xml_node_t *t)
{
    const char *text = tiku_xml_attr(t, "style:leader-text");
    const char *style = tiku_xml_attr(t, "style:leader-style");

    if (text != NULL && text[0] != '\0' && text[0] != ' ') {
        return (unsigned char)(text[0] == '.' || text[0] == '-' ||
                               text[0] == '_' ? text[0] : '.');
    }
    if (style == NULL || strcmp(style, "none") == 0) {
        return 0;
    }
    return strcmp(style, "solid") == 0 ? '_' : strstr(style, "dash") != NULL
           ? '-' : '.';
}

/** @brief The tab stops of the nearest style in @p pstyle's chain that
 *         sets any, into @p p.  @return 0, or -1 out of memory. */
static int
tabs(od_t *x, const char *pstyle, of_pstyle_t *p)
{
    const od_style_t *s = od_style(x, OD_PARA, pstyle);
    const tiku_xml_node_t *list = NULL, *t;
    of_tab_t *out;
    int depth, n = 0;

    for (depth = 0; s != NULL && list == NULL && depth < CHAIN; depth++) {
        list = tiku_xml_path(s->e, "style:paragraph-properties/"
                                   "style:tab-stops");
        s = od_style(x, OD_PARA, s->parent);
    }
    for (t = tiku_xml_child(list, "style:tab-stop"); t != NULL;
         t = tiku_xml_next(t, "style:tab-stop")) {
        n++;
    }
    if (n == 0 || (out = of_alloc(&x->d->arena, (size_t)n * sizeof *out))
        == NULL) {
        return n == 0 ? 0 : -1;
    }
    for (t = tiku_xml_child(list, "style:tab-stop"); t != NULL;
         t = tiku_xml_next(t, "style:tab-stop")) {
        const char *type = tiku_xml_attr(t, "style:type");
        of_tab_t *k = &out[p->ntab++];

        k->pos = od_length(tiku_xml_attr(t, "style:position"), 0);
        k->align = type == NULL ? OF_TAB_LEFT : strcmp(type, "center") == 0
                   ? OF_TAB_CENTER : strcmp(type, "right") == 0
                   ? OF_TAB_RIGHT : strcmp(type, "char") == 0
                   ? OF_TAB_DECIMAL : OF_TAB_LEFT;
        k->leader = leader(t);
    }
    p->tab = out;
    return 0;
}

/** @brief The borders and their spacing @p pstyle gives, into @p p. */
static void
borders(od_t *x, const char *pstyle, of_pstyle_t *p)
{
    static const char *const side[4] = {"top", "left", "bottom", "right"};
    const char *all = pp(x, pstyle, "fo:border");
    const char *pad = pp(x, pstyle, "fo:padding");
    char name[32];
    int k;

    for (k = 0; k < 4; k++) {
        const char *v;

        snprintf(name, sizeof name, "fo:border-%s", side[k]);
        v = pp(x, pstyle, name);
        od_border(v != NULL ? v : all, &p->border[k]);
        snprintf(name, sizeof name, "fo:padding-%s", side[k]);
        v = pp(x, pstyle, name);
        p->border[k].space = od_length(v != NULL ? v : pad, 0);
    }
}

/** @brief The line spacing @p pstyle gives, into @p p. */
static void
spacing(od_t *x, const char *pstyle, of_pstyle_t *p)
{
    const char *v = pp(x, pstyle, "fo:line-height");
    const char *least = pp(x, pstyle, "style:line-height-at-least");

    if (v != NULL && v[0] && v[strlen(v) - 1] == '%') {
        p->line_rule = OF_LINE_AUTO;
        p->line = (float)atof(v) / 100.0f;
    } else if (v != NULL && strcmp(v, "normal") != 0) {
        p->line_rule = OF_LINE_EXACT;
        p->line = od_length(v, 12);
    } else if (least != NULL) {
        p->line_rule = OF_LINE_LEAST;
        p->line = od_length(least, 12);
    }
    if (p->line <= 0) {
        p->line_rule = OF_LINE_AUTO;
        p->line = 1;
    }
}

int
od_layout(od_t *x, const char *pstyle, of_pstyle_t *p)
{
    const char *v;

    of_pstyle_default(p);
    v = pp(x, pstyle, "fo:text-align");
    p->align = v == NULL ? OF_LEFT : strcmp(v, "center") == 0 ? OF_CENTER
               : strcmp(v, "end") == 0 || strcmp(v, "right") == 0 ? OF_RIGHT
               : strcmp(v, "justify") == 0 ? OF_JUSTIFY : OF_LEFT;
    p->left = od_length(pp(x, pstyle, "fo:margin-left"), 0);
    p->right = od_length(pp(x, pstyle, "fo:margin-right"), 0);
    p->first = od_length(pp(x, pstyle, "fo:text-indent"), 0);
    p->before = od_length(pp(x, pstyle, "fo:margin-top"), 0);
    p->after = od_length(pp(x, pstyle, "fo:margin-bottom"), 0);
    spacing(x, pstyle, p);
    p->keep_next = (unsigned char)yes(pp(x, pstyle, "fo:keep-with-next"));
    p->keep_lines = (unsigned char)yes(pp(x, pstyle, "fo:keep-together"));
    v = pp(x, pstyle, "fo:widows");
    p->widows = (unsigned char)(v == NULL || atoi(v) >= 2);
    v = pp(x, pstyle, "fo:break-before");
    p->break_before = (unsigned char)(v != NULL && strcmp(v, "page") == 0);
    p->contextual = (unsigned char)(yes(pp(x, pstyle,
                                           "style:contextual-spacing")) ||
                                    yes(pp(x, pstyle,
                                           "loext:contextual-spacing")));
    p->fill = od_color(pp(x, pstyle, "fo:background-color"));
    borders(x, pstyle, p);
    p->style = pstyle != NULL ? of_strdup(&x->d->arena, pstyle) : NULL;
    return tabs(x, pstyle, p);
}

/* --------------------------------------------------------------- lists  */

const tiku_xml_node_t *
od_list_style(const od_t *x, const char *name)
{
    const tiku_xml_node_t *e;
    int i;

    for (i = 0; name != NULL && i < 3; i++) {
        for (e = tiku_xml_child(x->lstyles[i], "text:list-style");
             e != NULL; e = tiku_xml_next(e, "text:list-style")) {
            const char *n = tiku_xml_attr(e, "style:name");

            if (n != NULL && strcmp(n, name) == 0) {
                return e;
            }
        }
    }
    return NULL;
}

/** @brief Level @p lvl (0 first) of list or outline style @p ls. */
static const tiku_xml_node_t *
level_of(const tiku_xml_node_t *ls, int lvl)
{
    const tiku_xml_node_t *e;

    for (e = tiku_xml_child(ls, NULL); e != NULL; e = tiku_xml_next(e, NULL)) {
        const char *v = tiku_xml_attr(e, "text:level");

        if (v != NULL && atoi(v) == lvl + 1) {
            return e;
        }
    }
    return NULL;
}

/** @brief The numbers @p list counts at level @p lvl and at the levels
 *         above it that @p lv displays, outermost first, each as its
 *         format says, into @p out. */
static void
number(const tiku_xml_node_t *ls, const od_list_t *list, int lvl,
       const tiku_xml_node_t *lv, char *out, size_t max)
{
    const char *d = tiku_xml_attr(lv, "text:display-levels");
    int shown = d != NULL && atoi(d) > 1 ? atoi(d) : 1, k;
    size_t used = 0;

    out[0] = '\0';
    for (k = lvl - shown + 1; k <= lvl; k++) {
        const tiku_xml_node_t *at = level_of(ls, k < 0 ? 0 : k);
        const char *f = tiku_xml_attr(at, "style:num-format");
        int fmt = f == NULL || f[0] == '\0' ? -1 : f[0] == 'i' ? OF_NUM_LROMAN
                  : f[0] == 'I' ? OF_NUM_UROMAN : f[0] == 'a' ? OF_NUM_LLETTER
                  : f[0] == 'A' ? OF_NUM_ULETTER : OF_NUM_DECIMAL;
        char one[32];

        if (k < 0 || fmt < 0) {
            continue;
        }
        of_number(list->count[k] > 0 ? list->count[k] : 1, fmt, one,
                  sizeof one);
        used += (size_t)snprintf(out + used, used < max ? max - used : 0,
                                 "%s%s", used > 0 ? "." : "", one);
        if (used >= max) {
            return;
        }
    }
}

/** @brief The level's indents and what follows its label, into @p p.
 *         Aligned labels (OpenDocument 1.2) set the indents, unless the
 *         paragraph sets its own; older lists add theirs to the
 *         paragraph's: the label's box after its space before. */
static void
indents(const tiku_xml_node_t *lv, int own_indent, of_para_t *p)
{
    const tiku_xml_node_t *pr = tiku_xml_child(lv,
                                               "style:list-level-properties");
    const tiku_xml_node_t *al = tiku_xml_child(pr,
                                    "style:list-level-label-alignment");
    const char *follow = tiku_xml_attr(al, "text:label-followed-by");
    float box = od_length(tiku_xml_attr(pr, "text:min-label-width"), 0);

    p->label_follow = follow == NULL || strcmp(follow, "listtab") == 0 ? 0 :
                      strcmp(follow, "space") == 0 ? 1 : 2;
    if (al == NULL) {
        p->ps.left += od_length(tiku_xml_attr(pr, "text:space-before"), 0) +
                      box;
        p->ps.first = -box;
    } else if (!own_indent) {
        p->ps.left = od_length(tiku_xml_attr(al, "fo:margin-left"), 0);
        p->ps.first = od_length(tiku_xml_attr(al, "fo:text-indent"), 0);
    }
}

int
od_label(od_t *x, const tiku_xml_node_t *ls, od_list_t *list, int lvl,
         int start, const of_style_t *base, int own_indent, of_para_t *p)
{
    const tiku_xml_node_t *lv = level_of(ls, lvl);
    const char *v;
    char num[96], text[160];
    of_style_t st = *base;
    int k;

    if (lv == NULL || lvl < 0 || lvl >= OD_LEVELS) {
        return 0;
    }
    if (list->style != ls) {
        memset(list, 0, sizeof *list);
        list->style = ls;
    }
    for (k = lvl + 1; k < OD_LEVELS; k++) {
        list->count[k] = 0;         /* a level begins again under it */
    }
    v = tiku_xml_attr(lv, "text:start-value");
    list->count[lvl] = start >= 0 ? start : list->count[lvl] > 0
                       ? list->count[lvl] + 1 : od_count(v, 1);
    if (tiku_xml_is(lv, "text:list-level-style-bullet")) {
        v = tiku_xml_attr(lv, "text:bullet-char");
        snprintf(text, sizeof text, "%s", v != NULL && v[0] ? v
                                                           : "\xe2\x80\xa2");
    } else if (tiku_xml_is(lv, "text:list-level-style-image")) {
        snprintf(text, sizeof text, "\xe2\x80\xa2");
    } else {
        number(ls, list, lvl, lv, num, sizeof num);
        v = tiku_xml_attr(lv, "style:num-prefix");
        snprintf(text, sizeof text, "%s%s%s", v != NULL ? v : "", num,
                 tiku_xml_attr(lv, "style:num-suffix") != NULL
                 ? tiku_xml_attr(lv, "style:num-suffix") : "");
    }
    indents(lv, own_indent, p);
    if (text[0] == '\0') {
        return 0;                   /* counted, not shown */
    }
    st.underline = OF_UL_NONE;
    st.back = 0;
    apply_text(x, tiku_xml_child(lv, "style:text-properties"), &st);
    od_span_look(x, tiku_xml_attr(lv, "text:style-name"), &st);
    p->label = of_strdup(&x->d->arena, text);
    p->label_st = of_style(x->d, &st);
    return p->label != NULL && p->label_st != NULL ? 0 : -1;
}

/* --------------------------------------------------------------- pages  */

/** @brief The master page called @p name, or the first. */
static const tiku_xml_node_t *
master(const od_t *x, const char *name)
{
    const tiku_xml_node_t *m;

    for (m = tiku_xml_child(x->masters, "style:master-page"); m != NULL &&
         name != NULL; m = tiku_xml_next(m, "style:master-page")) {
        const char *n = tiku_xml_attr(m, "style:name");

        if (n != NULL && strcmp(n, name) == 0) {
            return m;
        }
    }
    return tiku_xml_child(x->masters, "style:master-page");
}

/** @brief The page layout called @p name, or NULL. */
static const tiku_xml_node_t *
layout(const od_t *x, const char *name)
{
    const tiku_xml_node_t *l;

    for (l = tiku_xml_child(x->layouts, "style:page-layout"); l != NULL &&
         name != NULL; l = tiku_xml_next(l, "style:page-layout")) {
        const char *n = tiku_xml_attr(l, "style:name");

        if (n != NULL && strcmp(n, name) == 0) {
            return l;
        }
    }
    return NULL;
}

/** @brief The room a header or footer (@p hf, its layout's
 *         header-footer-properties) takes from the text: its height and
 *         the space between; 0 when the master page has none (@p e). */
static float
band(const tiku_xml_node_t *e, const tiku_xml_node_t *hf, int head)
{
    const char *d = tiku_xml_attr(e, "style:display");
    float h = od_length(tiku_xml_attr(hf, "fo:min-height"), 0);

    if (e == NULL || (d != NULL && strcmp(d, "false") == 0)) {
        return 0;
    }
    h = h > 0 ? h : od_length(tiku_xml_attr(hf, "svg:height"), 0);
    return h + od_length(tiku_xml_attr(hf, head ? "fo:margin-bottom"
                                               : "fo:margin-top"), 0);
}

/** @brief A header's or footer's stories read from the master page: its
 *         own, the left pages' and the first page's. */
static void
bands(od_t *x, const tiku_xml_node_t *m, int head, of_section_t *s,
      of_story_t *(*read)(od_t *x, const tiku_xml_node_t *e))
{
    static const char *const kind[2][3] = {
        {"style:footer", "style:footer-first", "style:footer-left"},
        {"style:header", "style:header-first", "style:header-left"}};
    of_story_t **out = head ? s->header : s->footer;
    int k;

    for (k = 0; k < 3; k++) {
        const tiku_xml_node_t *e = tiku_xml_child(m, kind[head][k]);
        const char *d = tiku_xml_attr(e, "style:display");

        if (e == NULL || (d != NULL && strcmp(d, "false") == 0)) {
            continue;
        }
        out[k] = read(x, e);
        s->title_page |= (unsigned char)(k == 1);
        x->d->even_odd |= (unsigned char)(k == 2);
    }
}

void
od_page(od_t *x, const char *name, of_section_t *s,
        of_story_t *(*read)(od_t *x, const tiku_xml_node_t *e))
{
    const tiku_xml_node_t *m = master(x, name);
    const tiku_xml_node_t *l = layout(x, tiku_xml_attr(m,
                                      "style:page-layout-name"));
    const tiku_xml_node_t *pl = tiku_xml_child(l,
                                               "style:page-layout-properties");
    const tiku_xml_node_t *cols = tiku_xml_child(pl, "style:columns");
    const char *fmt = tiku_xml_attr(pl, "style:num-format");

    s->w = od_length(tiku_xml_attr(pl, "fo:page-width"), 612);
    s->h = od_length(tiku_xml_attr(pl, "fo:page-height"), 792);
    s->head = od_length(tiku_xml_attr(pl, "fo:margin-top"), 56.7f);
    s->foot = od_length(tiku_xml_attr(pl, "fo:margin-bottom"), 56.7f);
    s->left = od_length(tiku_xml_attr(pl, "fo:margin-left"), 56.7f);
    s->right = od_length(tiku_xml_attr(pl, "fo:margin-right"), 56.7f);
    s->top = s->head + band(tiku_xml_child(m, "style:header"),
                            tiku_xml_path(l, "style:header-style/"
                                          "style:header-footer-properties"),
                            1);
    s->bottom = s->foot + band(tiku_xml_child(m, "style:footer"),
                               tiku_xml_path(l, "style:footer-style/"
                                         "style:header-footer-properties"),
                               0);
    s->cols = tiku_xml_attr(cols, "fo:column-count") != NULL
              ? atoi(tiku_xml_attr(cols, "fo:column-count")) : 1;
    s->cols = s->cols >= 1 && s->cols <= 16 ? s->cols : 1;
    s->gap = od_length(tiku_xml_attr(cols, "fo:column-gap"), 18);
    s->num = fmt == NULL ? OF_NUM_DECIMAL : fmt[0] == 'i' ? OF_NUM_LROMAN :
             fmt[0] == 'I' ? OF_NUM_UROMAN : fmt[0] == 'a' ? OF_NUM_LLETTER
             : fmt[0] == 'A' ? OF_NUM_ULETTER : OF_NUM_DECIMAL;
    s->start = -1;
    if (m != NULL && x->depth < OF_NEST_MAX) {
        x->depth++;
        bands(x, m, 1, s, read);
        bands(x, m, 0, s, read);
        x->depth--;
    }
}
