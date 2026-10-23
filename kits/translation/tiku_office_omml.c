/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_omml.c - Office Math (ECMA-376 part 1, 22.1), Word's and
 * PowerPoint's equations, read into the tree tiku_office_math.h sets:
 * each run cut into letters, numbers and operators, each structure made
 * its node -- fractions, scripts, roots, n-ary operators, delimiters,
 * equation arrays and matrices, accents, bars, group characters, limits,
 * functions, boxes and phantoms.  Letters are slanted, capital Greek
 * aside, unless a run says otherwise; function names stand upright.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_math.h"

#include <stdlib.h>
#include <string.h>

/** @brief An equation being read. */
typedef struct {
    of_arena_t    *a;               /* where its tree is kept            */
    of_omml_tint_t tint;            /* a run's own colour, by its format */
    void          *ctx;             /* handed to tint                    */
    uint32_t       color;           /* the equation's ink                */
    int         depth;              /* elements within elements          */
    int         array;              /* in an equation array: & lines up  */
    int         fname;              /* in a function's name              */
    int         bad;                /* out of memory                     */
} mr_t;

static of_mnode_t *node(mr_t *m, const tiku_xml_node_t *e);
static of_mnode_t *row(mr_t *m, const tiku_xml_node_t *e);

/* ---------------------------------------------------------- properties  */

/** @brief Whether on/off property @p e is on: present, and not "0",
 *         "off" or "false". */
static int
on(const tiku_xml_node_t *e)
{
    const char *v = tiku_xml_attr(e, "m:val");

    return e != NULL && (v == NULL || !(strcmp(v, "0") == 0 ||
                                        strcmp(v, "off") == 0 ||
                                        strcmp(v, "false") == 0));
}

/** @brief Property @p name in @p e's properties @p pr (m:fPr ...): its
 *         m:val, "" when it gives none, @p dflt when it is absent. */
static const char *
prop(const tiku_xml_node_t *e, const char *pr, const char *name,
     const char *dflt)
{
    const tiku_xml_node_t *c = tiku_xml_child(tiku_xml_child(e, pr), name);
    const char *v = tiku_xml_attr(c, "m:val");

    return c == NULL ? dflt : v != NULL ? v : "";
}

/** @brief Whether on/off property @p name in @p e's @p pr is on. */
static int
flag(const tiku_xml_node_t *e, const char *pr, const char *name)
{
    return on(tiku_xml_child(tiku_xml_child(e, pr), name));
}

/** @brief Text @p s kept with the tree; noted when memory ran out. */
static const char *
keep(mr_t *m, const char *s)
{
    char *k = of_strdup(m->a, s != NULL ? s : "");

    m->bad |= k == NULL;
    return k;
}

/** @brief A node of @p kind for @p n parts; noted when memory ran out. */
static of_mnode_t *
fresh(mr_t *m, int kind, int n)
{
    of_mnode_t *nd = om_node(m->a, kind, n);

    m->bad |= nd == NULL;
    return nd;
}

/** @brief Node @p n pushed on @p kids; noted when memory ran out. */
static void
push(mr_t *m, of_vec_t *kids, of_mnode_t *n)
{
    of_mnode_t **slot;

    if (n != NULL && (slot = of_vec_push(kids)) != NULL) {
        *slot = n;
    } else if (n != NULL) {
        m->bad = 1;
    }
}

/* --------------------------------------------------------------- runs  */

/** @brief The colour of what @p holder's run properties give (an m:r,
 *         an m:ctrlPr), automatic as black; the equation's for none. */
static uint32_t
ink(const mr_t *m, const tiku_xml_node_t *holder)
{
    uint32_t c = holder != NULL && m->tint != NULL ? m->tint(m->ctx, holder)
                                                   : m->color;

    return c != 0 ? c : 0xff000000u;
}

/** @brief The colour structure @p e's control properties give its rules,
 *         fences and operator (its m:fPr's m:ctrlPr ...). */
static uint32_t
ctrl_color(const mr_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *c, *ctrl = NULL;
    size_t k;

    for (c = tiku_xml_child(e, NULL); c != NULL && ctrl == NULL;
         c = tiku_xml_next(c, NULL)) {
        k = c->name != NULL ? strlen(c->name) : 0;
        if (k > 4 && strcmp(c->name + k - 2, "Pr") == 0) {
            ctrl = tiku_xml_child(c, "m:ctrlPr");
        }
    }
    return ink(m, ctrl);
}

/** @brief The variant a math run's properties @p pr ask for: a script
 *         (m:scr), else ordinary text (m:nor), else a style (m:sty), else
 *         OV_AUTO's slanted letters. */
static int
variant(const tiku_xml_node_t *pr)
{
    static const struct { const char *scr; int v; } scr[] = {
        {"script", OV_SCRIPT}, {"fraktur", OV_FRAKTUR},
        {"double-struck", OV_DOUBLE}, {"sans-serif", OV_SANS},
        {"monospace", OV_MONO}};
    const char *s = tiku_xml_attr(tiku_xml_child(pr, "m:scr"), "m:val");
    const char *sty = tiku_xml_attr(tiku_xml_child(pr, "m:sty"), "m:val");
    size_t i;

    for (i = 0; s != NULL && i < sizeof scr / sizeof scr[0]; i++) {
        if (strcmp(s, scr[i].scr) == 0) {
            return scr[i].v;
        }
    }
    if (on(tiku_xml_child(pr, "m:nor"))) {
        return OV_NORMAL;           /* ordinary text */
    }
    return sty == NULL ? OV_AUTO : strcmp(sty, "p") == 0 ? OV_NORMAL :
           strcmp(sty, "b") == 0 ? OV_BOLD :
           strcmp(sty, "bi") == 0 ? OV_BOLD_ITALIC : OV_ITALIC;
}

/** @brief The character at @p s and its length in @p len. */
static uint32_t
char_at(const unsigned char *s, size_t *len)
{
    uint32_t c = s[0];
    size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4, i;

    c = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 7);
    for (i = 1; i < n; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            *len = i;
            return 0xfffd;
        }
        c = c << 6 | (s[i] & 0x3f);
    }
    *len = n;
    return c;
}

/** @brief What character @p c is in a run: 1 a letter, 2 a digit, 0
 *         anything else -- an operator or a symbol, a token alone. */
static int
sort_of(uint32_t c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= 0x391 && c <= 0x3c9 && c != 0x3a2) ||
        (c >= 0x1d400 && c <= 0x1d7cb)) {
        return 1;
    }
    return c >= '0' && c <= '9' ? 2 : 0;
}

/** @brief Where the token at @p i of @p s (@p n bytes) ends: a run of
 *         letters, a number (its point or comma within), one other. */
static size_t
token_end(const char *s, size_t n, size_t i)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t len, j = i;
    int sort = sort_of(char_at(u + i, &len));

    if (sort == 0) {
        return i + len;
    }
    while (j < n) {
        uint32_t c = char_at(u + j, &len);

        if (sort_of(c) == sort || (sort == 2 && (c == '.' || c == ',') &&
                                   j + 1 < n && sort_of(u[j + 1]) == 2)) {
            j += len;
        } else {
            break;
        }
    }
    return j;
}

/** @brief Token @p s (@p n bytes) of a run in variant @p v and colour
 *         @p color pushed on @p kids: a space as a space, an alignment
 *         mark in an array, the invisible operators as nothing. */
static void
token(mr_t *m, of_vec_t *kids, const char *s, size_t n, int v,
      uint32_t color)
{
    size_t len;
    uint32_t c = char_at((const unsigned char *)s, &len);
    int sort = sort_of(c);
    of_mnode_t *t;

    if ((c >= 0x2061 && c <= 0x2064) || (c == '&' && !m->array)) {
        return;
    }
    if (c == '&' || c == ' ' || c == 0xa0) {
        if ((t = fresh(m, c == '&' ? OM_ALIGN : OM_SPACE, 0)) != NULL) {
            t->em = 0.25f;
        }
        push(m, kids, t);
        return;
    }
    if ((t = fresh(m, OM_TOKEN, 0)) == NULL ||
        (t->text = of_strndup(m->a, s, n)) == NULL) {
        m->bad = 1;
        return;
    }
    t->cls = (unsigned char)(sort == 1 && m->fname ? OC_OP :
                             sort ? OC_ORD : om_class(c));
    t->variant = (unsigned char)(m->fname && v == OV_AUTO ? OV_NORMAL : v);
    t->color = color;
    push(m, kids, t);
}

/** @brief Run @p r (m:r, or w:r: ordinary text) cut into tokens on
 *         @p kids, in its own colour. */
static void
run(mr_t *m, const tiku_xml_node_t *r, of_vec_t *kids)
{
    const tiku_xml_node_t *t = tiku_xml_child(r, "m:t");
    int v = t != NULL ? variant(tiku_xml_child(r, "m:rPr")) : OV_NORMAL;
    uint32_t color = ink(m, r);     /* each run its own, never inherited */
    char text[4096];
    size_t n, i = 0, j;

    n = tiku_xml_text(t != NULL ? t : tiku_xml_child(r, "w:t"), text,
                      sizeof text);
    n = n < sizeof text ? n : strlen(text);
    while (i < n && !m->bad) {
        j = token_end(text, n, i);
        token(m, kids, text + i, j - i, v, color);
        i = j > i ? j : n;
    }
}

/** @brief Each child of @p e, its properties passed over, read onto
 *         @p kids: a run's tokens, a structure's node. */
static void
collect(mr_t *m, const tiku_xml_node_t *e, of_vec_t *kids)
{
    const tiku_xml_node_t *c;

    for (c = tiku_xml_child(e, NULL); c != NULL && !m->bad;
         c = tiku_xml_next(c, NULL)) {
        size_t k = c->name != NULL ? strlen(c->name) : 0;

        if (k < 3 || strcmp(c->name + k - 2, "Pr") == 0 ||
            tiku_xml_is(c, "w:del") || tiku_xml_is(c, "w:moveFrom")) {
            continue;               /* m:rPr, m:ctrlPr ...; deletions */
        }
        if (tiku_xml_is(c, "m:r") || tiku_xml_is(c, "w:r")) {
            run(m, c, kids);
        } else if (tiku_xml_is(c, "w:ins") || tiku_xml_is(c, "w:moveTo") ||
                   tiku_xml_is(c, "w:smartTag") ||
                   tiku_xml_is(c, "w:customXml") ||
                   tiku_xml_is(c, "w:sdt") ||
                   tiku_xml_is(c, "w:sdtContent")) {
            collect(m, c, kids);
        } else {
            push(m, kids, node(m, c));
        }
    }
}

/** @brief Element @p e's children as one node: a row of them, the one
 *         itself when alone; NULL for none. */
static of_mnode_t *
row(mr_t *m, const tiku_xml_node_t *e)
{
    of_mnode_t **kid, *r = NULL;
    of_vec_t v;
    int n = 0;

    if (e == NULL || m->depth >= OM_DEPTH) {
        return NULL;
    }
    m->depth++;
    of_vec_init(&v, sizeof(of_mnode_t *));
    collect(m, e, &v);
    kid = of_vec_freeze(&v, m->a, &n);
    of_vec_free(&v);
    m->depth--;
    if (n > 0 && kid == NULL) {
        m->bad = 1;
    } else if (n == 1 && kid[0]->kind != OM_ALIGN) {
        r = kid[0];
    } else if (n > 0 && (r = fresh(m, OM_ROW, 0)) != NULL) {
        r->kid = kid;
        r->nkid = n;
    }
    return r;
}

/** @brief Argument @p name of @p e (m:e, m:num ...) as one node; a
 *         function's name is upright only in its base. */
static of_mnode_t *
arg(mr_t *m, const tiku_xml_node_t *e, const char *name)
{
    int was = m->fname;
    of_mnode_t *n;

    m->fname = was && strcmp(name, "m:e") == 0;
    n = row(m, tiku_xml_child(e, name));
    m->fname = was;
    return n;
}

/* --------------------------------------------------------- structures  */

/** @brief An m:f: stacked, skewed, along the line or with no bar, as its
 *         type says. */
static of_mnode_t *
frac(mr_t *m, const tiku_xml_node_t *e)
{
    const char *type = prop(e, "m:fPr", "m:type", "bar");
    of_mnode_t *n = fresh(m, OM_FRAC, 2);

    if (n != NULL) {
        n->flags = strcmp(type, "skw") == 0 ? OMF_SKEW :
                   strcmp(type, "lin") == 0 ? OMF_LINEAR :
                   strcmp(type, "noBar") == 0 ? OMF_NOBAR : 0;
        n->kid[0] = arg(m, e, "m:num");
        n->kid[1] = arg(m, e, "m:den");
    }
    return n;
}

/** @brief An m:sSub, m:sSup, m:sSubSup or m:sPre: a base, its scripts. */
static of_mnode_t *
scripts(mr_t *m, const tiku_xml_node_t *e)
{
    int pre = tiku_xml_is(e, "m:sPre");
    of_mnode_t *n = fresh(m, OM_SCRIPTS, 5);

    if (n != NULL) {
        n->kid[0] = arg(m, e, "m:e");
        n->kid[pre ? 3 : 1] = arg(m, e, "m:sub");
        n->kid[pre ? 4 : 2] = arg(m, e, "m:sup");
    }
    return n;
}

/** @brief An m:rad: a radicand, and its degree unless that is hidden. */
static of_mnode_t *
root(mr_t *m, const tiku_xml_node_t *e)
{
    of_mnode_t *n = fresh(m, OM_ROOT, 2);

    if (n != NULL) {
        n->kid[0] = arg(m, e, "m:e");
        n->kid[1] = flag(e, "m:radPr", "m:degHide") ? NULL
                                                     : arg(m, e, "m:deg");
    }
    return n;
}

/** @brief An m:nary: its operator (an integral unless it names one),
 *         its limits where they go, its operand. */
static of_mnode_t *
nary(mr_t *m, const tiku_xml_node_t *e)
{
    const char *loc = prop(e, "m:naryPr", "m:limLoc", "");
    of_mnode_t *n = fresh(m, OM_NARY, 3);

    if (n == NULL) {
        return NULL;
    }
    n->text = keep(m, prop(e, "m:naryPr", "m:chr", "\xe2\x88\xab"));
    n->flags = (unsigned char)((strcmp(loc, "undOvr") == 0 ? OMF_LIMITS :
                                strcmp(loc, "subSup") == 0 ? OMF_BESIDE
                                                           : 0) |
                               (flag(e, "m:naryPr", "m:grow") ? OMF_GROW
                                                              : 0));
    n->kid[0] = flag(e, "m:naryPr", "m:subHide") ? NULL
                                                  : arg(m, e, "m:sub");
    n->kid[1] = flag(e, "m:naryPr", "m:supHide") ? NULL
                                                  : arg(m, e, "m:sup");
    n->kid[2] = arg(m, e, "m:e");
    return n;
}

/** @brief An m:d: its parts between its fences, separated, the fences
 *         grown to them unless it says not to. */
static of_mnode_t *
fence(mr_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *c, *grow;
    of_mnode_t *n;
    int k = 0;

    for (c = tiku_xml_child(e, "m:e"); c; c = tiku_xml_next(c, "m:e")) {
        k++;
    }
    if ((n = fresh(m, OM_FENCE, k)) == NULL) {
        return NULL;
    }
    n->open = keep(m, prop(e, "m:dPr", "m:begChr", "("));
    n->close = keep(m, prop(e, "m:dPr", "m:endChr", ")"));
    n->sep = keep(m, prop(e, "m:dPr", "m:sepChr", "|"));
    grow = tiku_xml_child(tiku_xml_child(e, "m:dPr"), "m:grow");
    n->flags = grow == NULL || on(grow) ? OMF_GROW : 0;
    k = 0;
    for (c = tiku_xml_child(e, "m:e"); c; c = tiku_xml_next(c, "m:e")) {
        n->kid[k++] = row(m, c);
    }
    return n;
}

/** @brief An m:eqArr: its rows, each lined up at its & marks. */
static of_mnode_t *
array(mr_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *c;
    int k = 0, was = m->array;
    of_mnode_t *n;

    for (c = tiku_xml_child(e, "m:e"); c; c = tiku_xml_next(c, "m:e")) {
        k++;
    }
    if ((n = fresh(m, OM_ARRAY, k)) == NULL) {
        return NULL;
    }
    m->array = 1;
    k = 0;
    for (c = tiku_xml_child(e, "m:e"); c; c = tiku_xml_next(c, "m:e")) {
        n->kid[k++] = row(m, c);
    }
    m->array = was;
    return n;
}

/** @brief A matrix's columns' alignments, @p cols of them, as its m:mcs
 *         says, each 'l', 'c' or 'r'; centred where it says nothing. */
static const unsigned char *
columns(mr_t *m, const tiku_xml_node_t *e, int cols)
{
    const tiku_xml_node_t *mc = tiku_xml_child(tiku_xml_path(e,
                                               "m:mPr/m:mcs"), "m:mc");
    unsigned char *al = of_alloc(m->a, (size_t)cols);
    int c = 0, k, count;

    if (al == NULL) {
        m->bad = 1;
        return NULL;
    }
    memset(al, 'c', (size_t)cols);
    for (; mc != NULL && c < cols; mc = tiku_xml_next(mc, "m:mc")) {
        const char *jc = prop(mc, "m:mcPr", "m:mcJc", "center");

        count = atoi(prop(mc, "m:mcPr", "m:count", "1"));
        for (k = 0; k < (count > 0 ? count : 1) && c < cols; k++) {
            al[c++] = jc[0] == 'l' ? 'l' : jc[0] == 'r' ? 'r' : 'c';
        }
    }
    return al;
}

/** @brief An m:m: its cells row by row, as many columns as its widest
 *         row; NULL for none, or for more than 64 rows or columns. */
static of_mnode_t *
matrix(mr_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *r, *c;
    int rows = 0, cols = 0, k, i = 0;
    of_mnode_t *n;

    for (r = tiku_xml_child(e, "m:mr"); r; r = tiku_xml_next(r, "m:mr")) {
        for (k = 0, c = tiku_xml_child(r, "m:e"); c;
             c = tiku_xml_next(c, "m:e")) {
            k++;
        }
        cols = k > cols ? k : cols;
        rows++;
    }
    if (rows == 0 || cols == 0 || rows > 64 || cols > 64 ||
        (n = fresh(m, OM_MATRIX, rows * cols)) == NULL) {
        return NULL;
    }
    n->rows = rows;
    n->cols = cols;
    n->align = columns(m, e, cols);
    for (r = tiku_xml_child(e, "m:mr"); r; r = tiku_xml_next(r, "m:mr")) {
        for (k = 0, c = tiku_xml_child(r, "m:e"); c;
             c = tiku_xml_next(c, "m:e")) {
            n->kid[i * cols + k++] = row(m, c);
        }
        i++;
    }
    return n;
}

/** @brief An m:acc, m:bar or m:groupChr: its base with an accent, a bar
 *         or a character (a brace) over or under it. */
static of_mnode_t *
over(mr_t *m, const tiku_xml_node_t *e)
{
    int acc = tiku_xml_is(e, "m:acc"), bar = tiku_xml_is(e, "m:bar");
    const char *pr = acc ? "m:accPr" : bar ? "m:barPr" : "m:groupChrPr";
    of_mnode_t *n = fresh(m, acc ? OM_ACCENT : bar ? OM_BAR : OM_BRACE, 1);

    if (n == NULL) {
        return NULL;
    }
    n->flags = strcmp(prop(e, pr, "m:pos", "bot"), "top") == 0 ? OMF_TOP
                                                               : OMF_BOTTOM;
    if (!bar) {
        n->text = keep(m, prop(e, pr, "m:chr", acc ? "\xcc\x82"
                                                   : "\xe2\x8f\x9f"));
    }
    n->kid[0] = arg(m, e, "m:e");
    return n;
}

/** @brief An m:limLow or m:limUpp: a base, its limit under or over. */
static of_mnode_t *
limit(mr_t *m, const tiku_xml_node_t *e)
{
    int low = tiku_xml_is(e, "m:limLow");
    of_mnode_t *n = fresh(m, OM_UNDEROVER, 3);

    if (n != NULL) {
        n->kid[0] = arg(m, e, "m:e");
        n->kid[low ? 1 : 2] = arg(m, e, "m:lim");
    }
    return n;
}

/** @brief An m:func: its name, upright and spaced as an operator, then
 *         its argument. */
static of_mnode_t *
func(mr_t *m, const tiku_xml_node_t *e)
{
    of_mnode_t *n = fresh(m, OM_ROW, 2);
    int was = m->fname;

    if (n != NULL) {
        m->fname = 1;
        n->kid[0] = row(m, tiku_xml_child(e, "m:fName"));
        m->fname = was;
        n->kid[1] = arg(m, e, "m:e");
    }
    return n;
}

/** @brief An m:box, or an m:borderBox with its border; or an m:phant:
 *         its base unseen, or taking no room, as it says. */
static of_mnode_t *
box(mr_t *m, const tiku_xml_node_t *e)
{
    int ph = tiku_xml_is(e, "m:phant");
    const tiku_xml_node_t *pr = tiku_xml_child(e, "m:phantPr");
    of_mnode_t *n = fresh(m, ph ? OM_PHANTOM : OM_BOX, 1);

    if (n == NULL) {
        return NULL;
    }
    if (ph) {
        n->flags = (unsigned char)((tiku_xml_child(pr, "m:show") == NULL ||
                                    on(tiku_xml_child(pr, "m:show"))
                                    ? OMF_SHOW : 0) |
                                   (flag(e, "m:phantPr", "m:zeroWid")
                                    ? OMF_ZEROW : 0) |
                                   (flag(e, "m:phantPr", "m:zeroAsc") ||
                                    flag(e, "m:phantPr", "m:zeroDesc")
                                    ? OMF_ZEROH : 0));
    } else if (tiku_xml_is(e, "m:borderBox")) {
        n->flags = OMF_BORDER;
    }
    n->kid[0] = arg(m, e, "m:e");
    return n;
}

/** @brief One element of an equation made its node; anything else's
 *         children as a row. */
static of_mnode_t *
node(mr_t *m, const tiku_xml_node_t *e)
{
    static const struct {
        const char *name;
        of_mnode_t *(*read)(mr_t *, const tiku_xml_node_t *);
    } kinds[] = {
        {"m:f", frac}, {"m:sSub", scripts}, {"m:sSup", scripts},
        {"m:sSubSup", scripts}, {"m:sPre", scripts}, {"m:rad", root},
        {"m:nary", nary}, {"m:d", fence}, {"m:eqArr", array},
        {"m:m", matrix}, {"m:acc", over}, {"m:bar", over},
        {"m:groupChr", over}, {"m:limLow", limit}, {"m:limUpp", limit},
        {"m:func", func}, {"m:box", box}, {"m:borderBox", box},
        {"m:phant", box}};
    of_mnode_t *n = NULL;
    size_t i;

    if (e->name == NULL || m->depth >= OM_DEPTH) {
        return NULL;
    }
    m->depth++;
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        if (strcmp(e->name, kinds[i].name) == 0) {
            break;
        }
    }
    n = i < sizeof kinds / sizeof kinds[0] ? kinds[i].read(m, e)
                                           : row(m, e);
    if (n != NULL && i < sizeof kinds / sizeof kinds[0]) {
        n->color = ctrl_color(m, e);    /* its rules, fences, operator */
    }
    m->depth--;
    return n;
}

/** @brief The first math run under @p e, depth first, from @p depth. */
static const tiku_xml_node_t *
first_run(const tiku_xml_node_t *e, int depth)
{
    const tiku_xml_node_t *c, *r;

    for (c = tiku_xml_child(e, NULL); c != NULL && depth < OM_DEPTH;
         c = tiku_xml_next(c, NULL)) {
        if (tiku_xml_is(c, "m:r")) {
            return c;
        }
        if ((r = first_run(c, depth + 1)) != NULL) {
            return r;
        }
    }
    return NULL;
}

const tiku_xml_node_t *
of_omml_first(const tiku_xml_node_t *e)
{
    return first_run(e, 0);
}

int
of_omml(of_doc_t *d, const tiku_xml_node_t *e, int display, float size,
        uint32_t color, of_omml_tint_t tint, void *ctx, of_math_t **out)
{
    of_mnode_t *root;
    mr_t m;

    memset(&m, 0, sizeof m);
    m.a = &d->arena;
    m.tint = tint;
    m.ctx = ctx;
    m.color = color;
    *out = NULL;
    root = row(&m, e);
    if (m.bad) {
        return -1;
    }
    if (root != NULL &&
        (*out = of_math_new(d, root, display, size, color)) == NULL) {
        return -1;
    }
    return 0;
}
