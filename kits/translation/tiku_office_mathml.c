/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_mathml.c - MathML's presentation markup, as OpenDocument's
 * formula objects keep it, read into the tree tiku_office_math.h sets:
 * tokens (an identifier of one letter slanted, operators classed by what
 * they are) and each layout element made its node -- rows, fractions,
 * roots, scripts, limits, fences, tables, enclosures, phantoms, styles.
 * A large operator with its limits becomes one, and a row between
 * stretchy fences a fenced group.  Names are read past their prefixes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_math.h"

#include <stdlib.h>
#include <string.h>

/** @brief A formula being read. */
typedef struct {
    of_arena_t *a;                  /* where its tree is kept            */
    float       size;               /* its em, for lengths in points     */
    int         variant;            /* the mathvariant in force; -1 none */
    uint32_t    color;              /* the mathcolor in force; 0 none    */
    uint32_t    ink;                /* the formula's own colour          */
    int         depth;              /* elements within elements          */
    int         bad;                /* out of memory                     */
} mm_t;

static of_mnode_t *mm_node(mm_t *m, const tiku_xml_node_t *e);

/* ---------------------------------------------------------- the markup  */

/** @brief Element @p e's name past its prefix ("math:mrow" is "mrow"). */
static const char *
local(const tiku_xml_node_t *e)
{
    const char *c = e != NULL && e->name != NULL ? strrchr(e->name, ':')
                                                 : NULL;

    return e == NULL || e->name == NULL ? "" : c != NULL ? c + 1 : e->name;
}

/** @brief Whether @p e is the element called @p name, prefixed or not. */
static int
is(const tiku_xml_node_t *e, const char *name)
{
    return strcmp(local(e), name) == 0;
}

/** @brief Element @p e's child elements' @p i-th (0 first), or NULL. */
static const tiku_xml_node_t *
nth(const tiku_xml_node_t *e, int i)
{
    const tiku_xml_node_t *c = tiku_xml_child(e, NULL);

    while (c != NULL && i-- > 0) {
        c = tiku_xml_next(c, NULL);
    }
    return c;
}

/** @brief The character at @p s, its length in @p len; 0 at the end. */
static uint32_t
char_at(const char *s, size_t *len)
{
    const unsigned char *u = (const unsigned char *)s;
    uint32_t c = u[0];
    size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4, i;

    c = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 7);
    for (i = 1; i < n; i++) {
        if ((u[i] & 0xc0) != 0x80) {
            *len = i;
            return 0xfffd;
        }
        c = c << 6 | (u[i] & 0x3f);
    }
    *len = n;
    return c;
}

/** @brief The character MathML's named entity @p name (@p n bytes, no
 *         & or ;) stands for: Greek, and the common symbols; 0 unknown. */
static uint32_t
entity(const char *name, size_t n)
{
    static const char greek[] = "alpha beta gamma delta epsilon zeta eta "
        "theta iota kappa lambda mu nu xi omicron pi rho sigmaf sigma tau "
        "upsilon phi chi psi omega ";
    static const struct { const char *name; uint16_t cp; } sym[] = {
        {"infin", 0x221e}, {"sum", 0x2211}, {"prod", 0x220f},
        {"int", 0x222b}, {"iint", 0x222c}, {"oint", 0x222e},
        {"part", 0x2202}, {"partial", 0x2202}, {"nabla", 0x2207},
        {"plusmn", 0xb1}, {"pm", 0xb1}, {"mnplus", 0x2213},
        {"times", 0xd7}, {"divide", 0xf7}, {"minus", 0x2212},
        {"sdot", 0x22c5}, {"middot", 0xb7}, {"le", 0x2264},
        {"leq", 0x2264}, {"ge", 0x2265}, {"geq", 0x2265}, {"ne", 0x2260},
        {"equiv", 0x2261}, {"approx", 0x2248}, {"sim", 0x223c},
        {"cong", 0x2245}, {"prop", 0x221d}, {"larr", 0x2190},
        {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193},
        {"harr", 0x2194}, {"lArr", 0x21d0}, {"rArr", 0x21d2},
        {"hArr", 0x21d4}, {"forall", 0x2200}, {"exist", 0x2203},
        {"empty", 0x2205}, {"isin", 0x2208}, {"in", 0x2208},
        {"notin", 0x2209}, {"ni", 0x220b}, {"sub", 0x2282},
        {"sup", 0x2283}, {"sube", 0x2286}, {"supe", 0x2287},
        {"cup", 0x222a}, {"cap", 0x2229}, {"and", 0x2227}, {"or", 0x2228},
        {"not", 0xac}, {"perp", 0x22a5}, {"ang", 0x2220}, {"deg", 0xb0},
        {"prime", 0x2032}, {"Prime", 0x2033}, {"hellip", 0x2026},
        {"ctdot", 0x22ef}, {"vellip", 0x22ee}, {"InvisibleTimes", 0x2062},
        {"it", 0x2062}, {"ApplyFunction", 0x2061}, {"af", 0x2061},
        {"InvisibleComma", 0x2063}, {"ic", 0x2063}, {"nbsp", 0xa0},
        {"ThinSpace", 0x2009}, {"lang", 0x27e8}, {"rang", 0x27e9},
        {"lceil", 0x2308}, {"rceil", 0x2309}, {"lfloor", 0x230a},
        {"rfloor", 0x230b}, {"Vert", 0x2016}, {"mid", 0x2223},
        {"hbar", 0x210f}, {"ell", 0x2113}, {"Re", 0x211c},
        {"Im", 0x2111}, {"aleph", 0x2135}, {"weierp", 0x2118},
        {"thetav", 0x3d1}, {"phiv", 0x3d5}, {"piv", 0x3d6},
        {"epsiv", 0x3f5}};
    const char *g = greek, *sp;
    uint32_t i = 0;
    size_t k;

    for (k = 0; k < sizeof sym / sizeof sym[0]; k++) {
        if (strlen(sym[k].name) == n && memcmp(sym[k].name, name, n) == 0) {
            return sym[k].cp;
        }
    }
    for (; (sp = strchr(g, ' ')) != NULL; g = sp + 1, i++) {
        if ((size_t)(sp - g) == n && memcmp(g, name, n) == 0) {
            return 0x3b1 + i;       /* in order, final sigma at its place */
        }
        if ((size_t)(sp - g) == n && name[0] == g[0] - 32 &&
            memcmp(g + 1, name + 1, n - 1) == 0 && i != 17) {
            return 0x391 + i;       /* a capital: no capital final sigma */
        }
    }
    return 0;
}

/** @brief Named entities in @p s, as an XML reader leaves them, made the
 *         characters they stand for, in place. */
static void
entities(char *s)
{
    char *r = s, *w = s, *semi;
    uint32_t c;

    while (*r != '\0') {
        semi = *r == '&' ? strchr(r, ';') : NULL;
        c = semi != NULL && semi - r > 2 && semi - r < 32
            ? entity(r + 1, (size_t)(semi - r - 1)) : 0;
        if (c == 0) {
            *w++ = *r++;
            continue;
        }
        if (c < 0x80) {             /* never longer than what it was */
            *w++ = (char)c;
        } else if (c < 0x800) {
            *w++ = (char)(0xc0 | c >> 6);
            *w++ = (char)(0x80 | (c & 0x3f));
        } else {
            *w++ = (char)(0xe0 | c >> 12);
            *w++ = (char)(0x80 | (c >> 6 & 0x3f));
            *w++ = (char)(0x80 | (c & 0x3f));
        }
        r = semi + 1;
    }
    *w = '\0';
}

/** @brief Token @p e's text, its ends trimmed and each run of white space
 *         within made one space, its named entities read, into @p out
 *         (@p max bytes).  @return its length. */
static size_t
token_text(const tiku_xml_node_t *e, char *out, size_t max)
{
    char raw[1024];
    size_t n = tiku_xml_text(e, raw, sizeof raw), i, k = 0;
    int sp = 0;

    entities(raw);
    n = strlen(raw);
    for (i = 0; i < n && k + 1 < max; i++) {
        int white = raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' ||
                    raw[i] == '\r';

        if (white) {
            sp = k > 0;
            continue;
        }
        if (sp && k + 2 < max) {
            out[k++] = ' ';
        }
        sp = 0;
        out[k++] = raw[i];
    }
    out[k] = '\0';
    return k;
}

/** @brief The variant MathML's mathvariant @p v names, @p dflt for none. */
static int
variant_of(const char *v, int dflt)
{
    static const struct { const char *name; int v; } map[] = {
        {"normal", OV_NORMAL}, {"bold", OV_BOLD}, {"italic", OV_ITALIC},
        {"bold-italic", OV_BOLD_ITALIC}, {"double-struck", OV_DOUBLE},
        {"script", OV_SCRIPT}, {"bold-script", OV_SCRIPT},
        {"fraktur", OV_FRAKTUR}, {"bold-fraktur", OV_FRAKTUR},
        {"sans-serif", OV_SANS}, {"bold-sans-serif", OV_SANS},
        {"sans-serif-italic", OV_SANS}, {"sans-serif-bold-italic", OV_SANS},
        {"monospace", OV_MONO}};
    size_t i;

    for (i = 0; v != NULL && i < sizeof map / sizeof map[0]; i++) {
        if (strcmp(v, map[i].name) == 0) {
            return map[i].v;
        }
    }
    return dflt;
}

/** @brief A colour MathML names: "#rgb", "#rrggbb", or one of HTML's
 *         sixteen; 0 for none. */
static uint32_t
color_of(const char *v)
{
    static const struct { const char *name; uint32_t argb; } names[] = {
        {"black", 0xff000000u}, {"silver", 0xffc0c0c0u},
        {"gray", 0xff808080u}, {"white", 0xffffffffu},
        {"maroon", 0xff800000u}, {"red", 0xffff0000u},
        {"purple", 0xff800080u}, {"fuchsia", 0xffff00ffu},
        {"green", 0xff008000u}, {"lime", 0xff00ff00u},
        {"olive", 0xff808000u}, {"yellow", 0xffffff00u},
        {"navy", 0xff000080u}, {"blue", 0xff0000ffu},
        {"teal", 0xff008080u}, {"aqua", 0xff00ffffu}};
    unsigned long x;
    char *end;
    size_t i;

    if (v == NULL) {
        return 0;
    }
    if (v[0] == '#') {
        x = strtoul(v + 1, &end, 16);
        if (end - (v + 1) == 3) {
            return 0xff000000u | (uint32_t)((x >> 8 & 15) * 0x110000 +
                                            (x >> 4 & 15) * 0x1100 +
                                            (x & 15) * 0x11);
        }
        return end - (v + 1) == 6 ? 0xff000000u | (uint32_t)x : 0;
    }
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(v, names[i].name) == 0) {
            return names[i].argb;
        }
    }
    return 0;
}

/** @brief A MathML length @p v in ems at size @p size: em, ex, pt, px,
 *         %, or a named math space.  @p dflt when it says none. */
static float
length_of(const char *v, float size, float dflt)
{
    static const struct { const char *name; float em; } named[] = {
        {"veryverythinmathspace", 1}, {"verythinmathspace", 2},
        {"thinmathspace", 3}, {"mediummathspace", 4},
        {"thickmathspace", 5}, {"verythickmathspace", 6},
        {"veryverythickmathspace", 7}};
    char *end;
    float x;
    size_t i;

    if (v == NULL) {
        return dflt;
    }
    for (i = 0; i < sizeof named / sizeof named[0]; i++) {
        if (strcmp(v + (strncmp(v, "negative", 8) == 0 ? 8 : 0),
                   named[i].name) == 0) {
            return (v[0] == 'n' ? -named[i].em : named[i].em) / 18;
        }
    }
    x = strtof(v, &end);
    if (end == v) {
        return dflt;
    }
    return strcmp(end, "ex") == 0 ? x * 0.43f :
           strcmp(end, "pt") == 0 ? x / size :
           strcmp(end, "px") == 0 ? x * 0.75f / size :
           strcmp(end, "%") == 0 ? x / 100 : x;   /* em, or a number */
}

/* ------------------------------------------------------------- nodes  */

/** @brief A node of @p kind for @p n parts; noted when memory ran out. */
static of_mnode_t *
fresh(mm_t *m, int kind, int n)
{
    of_mnode_t *nd = om_node(m->a, kind, n);

    m->bad |= nd == NULL;
    return nd;
}

/** @brief Text @p s kept with the tree; noted when memory ran out. */
static const char *
keep(mm_t *m, const char *s)
{
    char *k = of_strdup(m->a, s);

    m->bad |= k == NULL;
    return k;
}

/** @brief Whether @p n is a large operator's token: a sum, a product, an
 *         integral, a big union ... */
static int
large(const of_mnode_t *n)
{
    size_t len;

    return n != NULL && n->kind == OM_TOKEN && n->text != NULL &&
           om_class(char_at(n->text, &len)) == OC_OP &&
           n->text[len] == '\0';
}

/** @brief A token element (mi, mn, mo, mtext, ms) as a token: slanted
 *         when an identifier of one letter, classed by what it is; NULL
 *         for one with nothing to show. */
static of_mnode_t *
token(mm_t *m, const tiku_xml_node_t *e)
{
    char text[512];
    size_t n = token_text(e, text, sizeof text), len;
    uint32_t c = n > 0 ? char_at(text, &len) : 0;
    int one = n > 0 && text[len] == '\0', mi = is(e, "mi");
    of_mnode_t *t;

    if (n == 0 || (one && c >= 0x2061 && c <= 0x2064)) {
        return NULL;                /* invisible operators */
    }
    if (is(e, "ms") && n + 2 < sizeof text) {   /* in its quotes */
        memmove(text + 1, text, n);
        text[0] = text[n + 1] = '"';
        text[n + 2] = '\0';
    }
    if ((t = fresh(m, OM_TOKEN, 0)) == NULL) {
        return NULL;
    }
    t->text = keep(m, text);
    t->variant = (unsigned char)variant_of(tiku_xml_attr(e, "mathvariant"),
                                           m->variant >= 0 ? m->variant :
                                           mi && one ? OV_ITALIC
                                                     : OV_NORMAL);
    t->cls = (unsigned char)(is(e, "mo") ? (one ? om_class(c) : OC_OP) :
                             mi && !one ? OC_OP : OC_ORD);
    return t;
}

/** @brief Element @p e's child elements from the @p from-th on as nodes,
 *         in the formula's memory, their count in @p n; those showing
 *         nothing left out. */
static of_mnode_t **
kids(mm_t *m, const tiku_xml_node_t *e, int from, int *n)
{
    const tiku_xml_node_t *c;
    of_mnode_t **out, *k;
    int total = 0;

    for (c = nth(e, from); c != NULL; c = tiku_xml_next(c, NULL)) {
        total++;
    }
    *n = 0;
    if (total == 0 || (out = of_alloc(m->a, (size_t)total * sizeof *out))
                      == NULL) {
        m->bad |= total > 0;
        return NULL;
    }
    for (c = nth(e, from); c != NULL && *n < total;
         c = tiku_xml_next(c, NULL)) {
        if ((k = mm_node(m, c)) != NULL) {
            out[(*n)++] = k;
        }
    }
    return out;
}

/** @brief Whether node @p k is a fence a row is set between: an
 *         operator token that opens (@p open) or closes, or says so. */
static int
fence_tok(const of_mnode_t *k, const tiku_xml_node_t *e, int open)
{
    const char *st = tiku_xml_attr(e, "stretchy");
    const char *fe = tiku_xml_attr(e, "fence");

    return k != NULL && k->kind == OM_TOKEN && is(e, "mo") &&
           (st == NULL || strcmp(st, "false") != 0) &&
           (k->cls == (open ? OC_OPEN : OC_CLOSE) ||
            (fe != NULL && strcmp(fe, "true") == 0 && st != NULL));
}

/** @brief Element @p e's children as one node: a row, a lone one as
 *         itself, and a row between stretchy fences a fenced group. */
static of_mnode_t *
row(mm_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *first = nth(e, 0), *last = first;
    of_mnode_t **k, *r;
    int n, all = first != NULL;

    k = kids(m, e, 0, &n);
    if (n <= 1) {
        return n == 1 ? k[0] : NULL;
    }
    while (tiku_xml_next(last, NULL) != NULL) {
        last = tiku_xml_next(last, NULL);
        all++;
    }
    if ((r = fresh(m, OM_ROW, 0)) == NULL) {
        return NULL;
    }
    r->kid = k;
    r->nkid = n;
    if (all == n && fence_tok(k[0], first, 1) &&
        fence_tok(k[n - 1], last, 0) && (r = fresh(m, OM_FENCE, 1))) {
        r->open = k[0]->text;       /* the parts between, one row */
        r->close = k[n - 1]->text;
        r->sep = "";
        r->flags = OMF_GROW;
        if ((r->kid[0] = fresh(m, OM_ROW, 0)) != NULL) {
            r->kid[0]->kid = k + 1;
            r->kid[0]->nkid = n - 2;
        }
    }
    return r;
}

/* ----------------------------------------------------- layout elements  */

/** @brief An mfrac: stacked, with no rule when its thickness is nought,
 *         skewed when bevelled. */
static of_mnode_t *
frac(mm_t *m, const tiku_xml_node_t *e)
{
    const char *lt = tiku_xml_attr(e, "linethickness");
    const char *bev = tiku_xml_attr(e, "bevelled");
    of_mnode_t *n = fresh(m, OM_FRAC, 2);

    if (n != NULL) {
        n->flags = lt != NULL && strtof(lt, NULL) == 0 &&
                   strchr("0.", lt[0]) != NULL ? OMF_NOBAR : 0;
        n->flags |= bev != NULL && strcmp(bev, "true") == 0 ? OMF_SKEW : 0;
        n->kid[0] = mm_node(m, nth(e, 0));
        n->kid[1] = mm_node(m, nth(e, 1));
    }
    return n;
}

/** @brief An msqrt (its children the radicand) or an mroot. */
static of_mnode_t *
root(mm_t *m, const tiku_xml_node_t *e)
{
    of_mnode_t *n = fresh(m, OM_ROOT, 2);

    if (n != NULL) {
        n->kid[0] = is(e, "msqrt") ? row(m, e) : mm_node(m, nth(e, 0));
        n->kid[1] = is(e, "mroot") ? mm_node(m, nth(e, 1)) : NULL;
    }
    return n;
}

/** @brief Base @p base with scripts @p lo and @p hi as a large operator
 *         when it is one (limits @p over it, as display style puts
 *         them), else as scripts beside it (or under and over). */
static of_mnode_t *
scripted(mm_t *m, of_mnode_t *base, of_mnode_t *lo, of_mnode_t *hi,
         int over)
{
    of_mnode_t *n;

    if (large(base) && (n = fresh(m, OM_NARY, 3)) != NULL) {
        n->text = base->text;
        n->color = base->color;     /* the operator's own */
        n->flags = over ? OMF_LIMITS : OMF_BESIDE;
        n->kid[0] = lo;
        n->kid[1] = hi;
        return n;
    }
    if ((n = fresh(m, over ? OM_UNDEROVER : OM_SCRIPTS, over ? 3 : 5))
        != NULL) {
        n->kid[0] = base;
        n->kid[1] = lo;
        n->kid[2] = hi;
    }
    return n;
}

/** @brief Whether @p n is an accent's character over a base: a hat, a
 *         tilde, a dot, an arrow, a bar ... */
static int
accent_tok(const of_mnode_t *n)
{
    static const char *const marks = "^~`\xc2\xb4\xc2\xa8\xc2\xaf";
    size_t len;
    uint32_t c;

    if (n == NULL || n->kind != OM_TOKEN || n->text == NULL) {
        return 0;
    }
    c = char_at(n->text, &len);
    return n->text[len] == '\0' &&
           ((c < 0x80 && strchr(marks, (int)c) != NULL) || c == 0xb4 ||
            c == 0xa8 || c == 0xaf || (c >= 0x2c6 && c <= 0x2dd) ||
            (c >= 0x300 && c <= 0x36f) || (c >= 0x20d0 && c <= 0x20ef) ||
            c == 0x2192 || c == 0x2190 || c == 0x2194 || c == 0x203e);
}

/** @brief An mover, munder or munderover: an accent or a bar over or
 *         under its base, else its parts as a large operator's limits or
 *         as what stands under and over the base. */
static of_mnode_t *
over(mm_t *m, const tiku_xml_node_t *e)
{
    int both = is(e, "munderover"), under = both || is(e, "munder");
    of_mnode_t *b = mm_node(m, nth(e, 0)), *s = mm_node(m, nth(e, 1));
    of_mnode_t *t = both ? mm_node(m, nth(e, 2)) : NULL, *n;
    const char *acc = tiku_xml_attr(e, under ? "accentunder" : "accent");
    size_t len;
    uint32_t c = s != NULL && s->text != NULL ? char_at(s->text, &len) : 0;

    if (!both && accent_tok(s) && (acc == NULL || strcmp(acc, "false"))) {
        if ((c == 0xaf || c == 0x203e || c == 0x332 || c == '_') &&
            (n = fresh(m, OM_BAR, 1)) != NULL) {
            n->flags = under ? OMF_BOTTOM : OMF_TOP;    /* a rule */
            n->kid[0] = b;
            return n;
        }
        if (!under && (n = fresh(m, OM_ACCENT, 1)) != NULL) {
            n->text = s->text;
            n->kid[0] = b;
            return n;
        }
    }
    return scripted(m, b, under ? s : NULL, both ? t : under ? NULL : s, 1);
}

/** @brief An msub, msup or msubsup: a base and its scripts beside it, or
 *         a large operator's limits. */
static of_mnode_t *
scripts(mm_t *m, const tiku_xml_node_t *e)
{
    int sup = is(e, "msup"), both = is(e, "msubsup");
    of_mnode_t *b = mm_node(m, nth(e, 0)), *s = mm_node(m, nth(e, 1));
    of_mnode_t *t = both ? mm_node(m, nth(e, 2)) : NULL;

    return scripted(m, b, sup ? NULL : s, sup ? s : t, 0);
}

/** @brief An mmultiscripts: its base, the first pair of scripts after it
 *         and the first before it (after its mprescripts). */
static of_mnode_t *
multi(mm_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *c;
    of_mnode_t *n = fresh(m, OM_SCRIPTS, 5);
    int k = 1, end = 3;

    if (n == NULL) {
        return NULL;
    }
    n->kid[0] = mm_node(m, nth(e, 0));
    for (c = nth(e, 1); c != NULL; c = tiku_xml_next(c, NULL)) {
        if (is(c, "mprescripts")) {
            k = 3;                  /* the pairs before the base */
            end = 5;
        } else if (k < end) {       /* the first pair, each side */
            n->kid[k++] = mm_node(m, c);
        }
    }
    return n;
}

/** @brief An mfenced: its children between its fences, separated. */
static of_mnode_t *
fenced(mm_t *m, const tiku_xml_node_t *e)
{
    const char *o = tiku_xml_attr(e, "open"), *c = tiku_xml_attr(e, "close");
    const char *s = tiku_xml_attr(e, "separators");
    of_mnode_t **k, *n;
    int count;

    k = kids(m, e, 0, &count);
    if ((n = fresh(m, OM_FENCE, 0)) == NULL) {
        return NULL;
    }
    n->kid = k;
    n->nkid = count;
    n->open = keep(m, o != NULL ? o : "(");
    n->close = keep(m, c != NULL ? c : ")");
    n->sep = keep(m, s != NULL ? s : ",");
    n->flags = OMF_GROW;
    return n;
}

/** @brief An menclose: a box round its children, a bar over or under
 *         them, or a root, as its notation says; else its children. */
static of_mnode_t *
enclose(mm_t *m, const tiku_xml_node_t *e)
{
    const char *how = tiku_xml_attr(e, "notation");
    int box = how != NULL && (strstr(how, "box") || strstr(how, "circle"));
    int top = how != NULL && strstr(how, "top") != NULL;
    int bottom = how != NULL && strstr(how, "bottom") != NULL;
    of_mnode_t *n = row(m, e), *w;

    if (how != NULL && strstr(how, "radical") != NULL &&
        (w = fresh(m, OM_ROOT, 2)) != NULL) {
        w->kid[0] = n;
        return w;
    }
    if (!(box || top || bottom) || (w = fresh(m, box ? OM_BOX : OM_BAR, 1))
                                   == NULL) {
        return n;
    }
    w->flags = box ? OMF_BORDER : top ? OMF_TOP : OMF_BOTTOM;
    w->kid[0] = n;
    return w;
}

/** @brief An mtable's columns' alignments, @p cols of them, from its
 *         columnalign (the last repeated); centred where it says none. */
static const unsigned char *
columns(mm_t *m, const tiku_xml_node_t *e, int cols)
{
    const char *v = tiku_xml_attr(e, "columnalign");
    unsigned char *al = of_alloc(m->a, (size_t)cols), last = 'c';
    int c;

    if (al == NULL) {
        m->bad = 1;
        return NULL;
    }
    for (c = 0; c < cols; c++) {
        while (v != NULL && *v == ' ') {
            v++;
        }
        if (v != NULL && *v != '\0') {
            last = *v == 'l' ? 'l' : *v == 'r' ? 'r' : 'c';
            v = strchr(v, ' ');
        }
        al[c] = last;
    }
    return al;
}

/** @brief An mtable: its rows' cells, as many columns as its widest row;
 *         a labelled row's label left out. */
static of_mnode_t *
table(mm_t *m, const tiku_xml_node_t *e)
{
    const tiku_xml_node_t *r, *c;
    int rows = 0, cols = 0, k, i = 0;
    of_mnode_t *n;

    for (r = tiku_xml_child(e, NULL); r; r = tiku_xml_next(r, NULL)) {
        for (k = 0, c = nth(r, is(r, "mlabeledtr")); c != NULL;
             c = tiku_xml_next(c, NULL)) {
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
    for (r = tiku_xml_child(e, NULL); r; r = tiku_xml_next(r, NULL)) {
        for (k = 0, c = nth(r, is(r, "mlabeledtr")); c != NULL;
             c = tiku_xml_next(c, NULL)) {
            n->kid[i * cols + k++] = is(c, "mtd") ? row(m, c)
                                                  : mm_node(m, c);
        }
        i++;
    }
    return n;
}

/** @brief An mstyle: its children read with its variant, in display or
 *         text style, or a script's, when it says. */
static of_mnode_t *
style(mm_t *m, const tiku_xml_node_t *e)
{
    const char *ds = tiku_xml_attr(e, "displaystyle");
    const char *sl = tiku_xml_attr(e, "scriptlevel");
    int was = m->variant;
    of_mnode_t *n, *s;

    m->variant = variant_of(tiku_xml_attr(e, "mathvariant"), m->variant);
    n = row(m, e);
    m->variant = was;
    if (n == NULL || (ds == NULL && (sl == NULL || atoi(sl) <= 0 ||
                                     sl[0] == '-'))) {
        return n;
    }
    if ((s = fresh(m, OM_STYLE, 1)) != NULL) {
        s->flags = ds != NULL && strcmp(ds, "true") == 0 ? OMF_DISPLAY :
                   ds != NULL ? OMF_INLINE : 0;
        s->flags |= sl != NULL && atoi(sl) > 0 ? OMF_SCRIPT : 0;
        s->kid[0] = n;
    }
    return s;
}

/** @brief An mspace: a space as wide as it says. */
static of_mnode_t *
space(mm_t *m, const tiku_xml_node_t *e)
{
    of_mnode_t *n = fresh(m, OM_SPACE, 0);

    if (n != NULL) {
        n->em = length_of(tiku_xml_attr(e, "width"), m->size, 0);
    }
    return n;
}

/** @brief An mphantom: its children taking room, unseen. */
static of_mnode_t *
phantom(mm_t *m, const tiku_xml_node_t *e)
{
    of_mnode_t *n = fresh(m, OM_PHANTOM, 1);

    if (n != NULL) {
        n->kid[0] = row(m, e);
    }
    return n;
}

/** @brief One element of a formula made its node; a row of its children
 *         when it is one this does not know.  NULL for nothing. */
static of_mnode_t *
mm_node(mm_t *m, const tiku_xml_node_t *e)
{
    static const struct {
        const char *name;
        of_mnode_t *(*read)(mm_t *, const tiku_xml_node_t *);
    } kinds[] = {
        {"mi", token}, {"mn", token}, {"mo", token}, {"mtext", token},
        {"ms", token}, {"mfrac", frac}, {"msqrt", root}, {"mroot", root},
        {"msub", scripts}, {"msup", scripts}, {"msubsup", scripts},
        {"munder", over}, {"mover", over}, {"munderover", over},
        {"mmultiscripts", multi}, {"mfenced", fenced},
        {"menclose", enclose}, {"mtable", table}, {"mstyle", style},
        {"mspace", space}, {"mphantom", phantom}};
    uint32_t was = m->color, c;
    of_mnode_t *n;
    size_t i;

    if (e == NULL || e->name == NULL || m->depth >= OM_DEPTH || m->bad ||
        is(e, "annotation") || is(e, "annotation-xml") || is(e, "none") ||
        is(e, "mprescripts") || is(e, "maligngroup") ||
        is(e, "malignmark")) {
        return NULL;
    }
    c = color_of(tiku_xml_attr(e, "mathcolor"));
    m->color = c != 0 ? c : m->color;   /* it and all within it */
    m->depth++;
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        if (is(e, kinds[i].name)) {
            break;
        }
    }
    n = i < sizeof kinds / sizeof kinds[0] ? kinds[i].read(m, e) :
        is(e, "maction") ? mm_node(m, nth(e, tiku_xml_attr(e, "selection")
                                       ? atoi(tiku_xml_attr(e, "selection"))
                                         - 1 : 0)) : row(m, e);
    if (n != NULL && n->color == 0) {   /* every node carries its colour */
        n->color = m->color != 0 ? m->color : m->ink;
    }
    m->color = was;
    m->depth--;
    return n;
}

int
of_mathml(of_doc_t *d, const tiku_xml_node_t *math, int display,
          float size, uint32_t color, of_math_t **out)
{
    of_mnode_t *root;
    mm_t m;

    memset(&m, 0, sizeof m);
    m.a = &d->arena;
    m.size = size > 0 ? size : 12;
    m.variant = -1;
    m.ink = color != 0 ? color : 0xff000000u;
    *out = NULL;
    root = mm_node(&m, math);
    if (m.bad) {
        return -1;
    }
    if (root != NULL && (*out = of_math_new(d, root, display, size,
                                            color)) == NULL) {
        return -1;
    }
    return 0;
}
