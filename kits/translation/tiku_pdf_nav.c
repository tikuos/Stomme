/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_nav.c - where a document points: its outline, the links on
 * a page, and the places both of them lead.
 *
 * A destination (ISO 32000 12.3.2) is a page and a view of it, written
 * out as an array or named, the name looked up in the catalog's Dests
 * or its Names tree; an action (12.6) carries one (GoTo), an address
 * (URI) or a step (Named).  The outline (12.3.3) is a tree of items
 * joined by First and Next; a link (12.5.6.5), an annotation's Rect.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <stdlib.h>
#include <string.h>

#define NAME_DEPTH     32           /* levels of a name tree            */
#define DEST_DEPTH     4            /* a name that names a name ...     */
#define OUTLINE_DEPTH  24
#define OUTLINE_MAX    65536        /* items one document may give      */
#define LINKS_MAX      4096         /* links one page may give          */
#define URI_MAX        2048

/* -------------------------------------------------------- destinations  */

/** @brief Two keys in the byte order a name tree sorts by. */
static int
key_cmp(const unsigned char *a, size_t an, const unsigned char *b,
        size_t bn)
{
    size_t n = an < bn ? an : bn;
    int c = memcmp(a, b, n);

    return c != 0 ? c : (an > bn) - (an < bn);
}

/** @brief The value under @p key in the name tree at @p node, or NULL;
 *         a kid whose Limits leave the key out is not entered. */
static pdf_obj_t *
tree_find(tiku_pdf_t *doc, pdf_obj_t *node, const unsigned char *key,
          size_t n, int depth)
{
    pdf_obj_t *limits, *names, *kids;
    int i;

    node = pdf_resolve(doc, node);
    if (node == NULL || node->kind != PDF_DICT || depth > NAME_DEPTH) {
        return NULL;
    }
    limits = pdf_get(doc, node, "Limits");
    if (limits != NULL && limits->kind == PDF_ARRAY && limits->u.a.n == 2) {
        pdf_obj_t *lo = pdf_resolve(doc, limits->u.a.item[0]);
        pdf_obj_t *hi = pdf_resolve(doc, limits->u.a.item[1]);

        if (lo != NULL && hi != NULL && lo->kind == PDF_STRING &&
            hi->kind == PDF_STRING &&
            (key_cmp(key, n, lo->u.s.p, lo->u.s.n) < 0 ||
             key_cmp(key, n, hi->u.s.p, hi->u.s.n) > 0)) {
            return NULL;
        }
    }
    names = pdf_get(doc, node, "Names");
    for (i = 0; names != NULL && names->kind == PDF_ARRAY &&
                i + 1 < names->u.a.n; i += 2) {
        pdf_obj_t *k = pdf_resolve(doc, names->u.a.item[i]);

        if (k != NULL && (k->kind == PDF_STRING || k->kind == PDF_NAME) &&
            key_cmp(k->u.s.p, k->u.s.n, key, n) == 0) {
            return pdf_resolve(doc, names->u.a.item[i + 1]);
        }
    }
    kids = pdf_get(doc, node, "Kids");
    for (i = 0; kids != NULL && kids->kind == PDF_ARRAY && i < kids->u.a.n;
         i++) {
        pdf_obj_t *v = tree_find(doc, kids->u.a.item[i], key, n, depth + 1);

        if (v != NULL) {
            return v;
        }
    }
    return NULL;
}

/** @brief What the name @p name (a name or a string) stands for: the
 *         catalog's Dests (PDF 1.1), then its Names tree (1.2). */
static pdf_obj_t *
named(tiku_pdf_t *doc, const pdf_obj_t *name)
{
    pdf_obj_t *root = pdf_root(doc), *v = NULL;
    char key[256];

    if (root == NULL) {
        return NULL;
    }
    if (name->u.s.n < sizeof key) {
        memcpy(key, name->u.s.p, name->u.s.n);
        key[name->u.s.n] = '\0';
        v = pdf_get(doc, pdf_get(doc, root, "Dests"), key);
    }
    if (v == NULL) {
        v = tree_find(doc, pdf_get(doc, pdf_get(doc, root, "Names"), "Dests"),
                      name->u.s.p, name->u.s.n, 0);
    }
    return v;
}

/** @brief A number at @p i of @p a into @p v; 0 when it is not one (a
 *         null there means "as it is"). */
static int
number_at(tiku_pdf_t *doc, const pdf_obj_t *a, int i, double *v)
{
    pdf_obj_t *o = i < a->u.a.n ? pdf_resolve(doc, a->u.a.item[i]) : NULL;

    if (o == NULL || (o->kind != PDF_INT && o->kind != PDF_REAL)) {
        return 0;
    }
    *v = pdf_number(o, 0);
    return 1;
}

/** @brief The point a destination's view names, by its kind: /XYZ left
 *         top, /FitH top, /FitV left, /FitR l b r t and the B forms.
 *  @return 1 when it gives an x, 2 a y, 3 both. */
static int
view_point(tiku_pdf_t *doc, pdf_obj_t *a, double *x, double *y)
{
    pdf_obj_t *kind = a->u.a.n > 1 ? pdf_resolve(doc, a->u.a.item[1]) : NULL;

    if (pdf_is_name(kind, "XYZ")) {
        return number_at(doc, a, 2, x) | number_at(doc, a, 3, y) << 1;
    }
    if (pdf_is_name(kind, "FitH") || pdf_is_name(kind, "FitBH")) {
        return number_at(doc, a, 2, y) << 1;
    }
    if (pdf_is_name(kind, "FitV") || pdf_is_name(kind, "FitBV")) {
        return number_at(doc, a, 2, x);
    }
    if (pdf_is_name(kind, "FitR")) {
        return number_at(doc, a, 2, x) | number_at(doc, a, 5, y) << 1;
    }
    return 0;                       /* /Fit and /FitB: the whole page */
}

/**
 * @brief An explicit destination: [page view ...].
 *
 * The point its view names is taken to the page as drawn at scale 1;
 * what it leaves out is the page's own top left corner.
 */
static void
explicit_dest(tiku_pdf_t *doc, pdf_obj_t *a, tiku_pdf_dest_t *out)
{
    pdf_mat_t m;
    double x = 0, y = 0, det, cx, cy;
    int w, h, have;

    if (a->u.a.n < 1) {
        return;
    }
    out->page = pdf_page_index(doc, a->u.a.item[0]);
    if (out->page < 0 ||
        pdf_page_matrix(doc, out->page, 1.0, 0, 0, 0, &m, &w, &h) != 0) {
        return;
    }
    have = view_point(doc, a, &x, &y);
    /* The page's top left in user space: the drawn origin taken back. */
    det = m.a * m.d - m.b * m.c;
    if (det == 0) {
        return;
    }
    cx = (m.c * m.f - m.d * m.e) / det;
    cy = (m.b * m.e - m.a * m.f) / det;
    pdf_mat_apply(&m, (have & 1) ? x : cx, (have & 2) ? y : cy, &out->left,
                  &out->top);
    if (out->left < 0) out->left = 0;
    if (out->top < 0) out->top = 0;
    if (out->left > w) out->left = w;
    if (out->top > h) out->top = h;
}

/** @brief Destination @p d, followed through names and their D. */
static void
dest(tiku_pdf_t *doc, pdf_obj_t *d, tiku_pdf_dest_t *out, int depth)
{
    d = pdf_resolve(doc, d);
    if (d == NULL || depth > DEST_DEPTH) {
        return;
    }
    if (d->kind == PDF_NAME || d->kind == PDF_STRING) {
        dest(doc, named(doc, d), out, depth + 1);
    } else if (d->kind == PDF_DICT) {
        dest(doc, pdf_get(doc, d, "D"), out, depth + 1);
    } else if (d->kind == PDF_ARRAY) {
        explicit_dest(doc, d, out);
    }
}

/** @brief An action's target: GoTo's destination, a URI's address, or a
 *         Named step from page @p from (-1 when there is none to step). */
static void
action(tiku_pdf_t *doc, pdf_obj_t *a, int from, tiku_pdf_dest_t *out)
{
    pdf_obj_t *s = pdf_get(doc, a, "S"), *v;

    if (pdf_is_name(s, "GoTo")) {
        dest(doc, pdf_get(doc, a, "D"), out, 0);
    } else if (pdf_is_name(s, "URI")) {
        v = pdf_get(doc, a, "URI");
        if (v != NULL && v->kind == PDF_STRING && v->u.s.n > 0 &&
            v->u.s.n < URI_MAX) {
            out->uri = malloc(v->u.s.n + 1);
            if (out->uri != NULL) {
                memcpy(out->uri, v->u.s.p, v->u.s.n);
                out->uri[v->u.s.n] = '\0';
            }
        }
    } else if (pdf_is_name(s, "Named")) {
        v = pdf_get(doc, a, "N");
        if (pdf_is_name(v, "FirstPage")) out->page = 0;
        if (pdf_is_name(v, "LastPage")) out->page = doc->npage - 1;
        if (pdf_is_name(v, "NextPage") && from >= 0) out->page = from + 1;
        if (pdf_is_name(v, "PrevPage") && from >= 0) out->page = from - 1;
        if (out->page >= doc->npage) out->page = -1;
    }
}

/** @brief Where @p dict (an outline item or a link) leads: its Dest, or
 *         the action under A. */
static void
target(tiku_pdf_t *doc, pdf_obj_t *dict, int from, tiku_pdf_dest_t *out)
{
    pdf_obj_t *d = pdf_get(doc, dict, "Dest"), *a;

    memset(out, 0, sizeof *out);
    out->page = -1;
    if (d != NULL) {
        dest(doc, d, out, 0);
        return;
    }
    a = pdf_get(doc, dict, "A");
    if (a != NULL && a->kind == PDF_DICT) {
        action(doc, a, from, out);
    }
}

/* ------------------------------------------------------------- outline  */

/** @brief The outline as it is gathered, and the items already seen. */
typedef struct {
    tiku_pdf_outline_t *item;
    int                 n, cap;
    unsigned char      *seen;       /* by object number: a cycle guard  */
} outline_walk_t;

/** @brief Room for one more item; 0 when there is none. */
static int
outline_room(outline_walk_t *w)
{
    tiku_pdf_outline_t *p;
    int cap;

    if (w->n < w->cap) {
        return 1;
    }
    if (w->cap >= OUTLINE_MAX) {
        return 0;
    }
    cap = w->cap ? w->cap * 2 : 64;
    p = realloc(w->item, (size_t)cap * sizeof *p);
    if (p == NULL) {
        return 0;
    }
    w->item = p;
    w->cap = cap;
    return 1;
}

/** @brief The items from @p ref on along Next, each followed by those
 *         under it, depth first. */
static void
outline_run(tiku_pdf_t *doc, outline_walk_t *w, pdf_obj_t *ref, int level)
{
    while (ref != NULL && level < OUTLINE_DEPTH && outline_room(w)) {
        pdf_obj_t *it;
        tiku_pdf_outline_t *o;
        int at;

        if (ref->kind != PDF_REF || ref->u.ref.num < 0 ||
            ref->u.ref.num >= doc->nxref || w->seen[ref->u.ref.num]) {
            return;                 /* items are indirect, and seen once */
        }
        w->seen[ref->u.ref.num] = 1;
        it = pdf_resolve(doc, ref);
        if (it == NULL || it->kind != PDF_DICT) {
            return;
        }
        at = w->n++;
        o = &w->item[at];
        memset(o, 0, sizeof *o);
        o->title = pdf_text_string(pdf_get(doc, it, "Title"));
        if (o->title == NULL) {
            o->title = calloc(1, 1);
        }
        o->level = level;
        o->open = pdf_number(pdf_get(doc, it, "Count"), 0) > 0;
        target(doc, it, -1, &o->to);
        outline_run(doc, w, pdf_dict_raw(it, "First"), level + 1);
        w->item[at].below = w->n - at - 1;
        ref = pdf_dict_raw(it, "Next");
    }
}

int
tiku_pdf_outline(tiku_pdf_t *doc, tiku_pdf_outline_t **out)
{
    outline_walk_t w;
    pdf_obj_t *root, *outlines;

    *out = NULL;
    root = doc != NULL ? pdf_root(doc) : NULL;
    outlines = root != NULL ? pdf_get(doc, root, "Outlines") : NULL;
    if (outlines == NULL || outlines->kind != PDF_DICT) {
        return 0;
    }
    memset(&w, 0, sizeof w);
    w.seen = calloc((size_t)doc->nxref + 1, 1);
    if (w.seen == NULL) {
        return -1;
    }
    outline_run(doc, &w, pdf_dict_raw(outlines, "First"), 0);
    free(w.seen);
    *out = w.item;
    return w.n;
}

void
tiku_pdf_outline_free(tiku_pdf_outline_t *items, int n)
{
    int i;

    for (i = 0; items != NULL && i < n; i++) {
        free(items[i].title);
        free(items[i].to.uri);
    }
    free(items);
}

/* --------------------------------------------------------------- links  */

int
tiku_pdf_links(tiku_pdf_t *doc, int page, tiku_pdf_link_t **out)
{
    pdf_obj_t *p = pdf_page_dict(doc, page), *annots;
    tiku_pdf_link_t *links;
    pdf_mat_t m;
    int i, n = 0, w, h;

    *out = NULL;
    if (p == NULL ||
        pdf_page_matrix(doc, page, 1.0, 0, 0, 0, &m, &w, &h) != 0) {
        return -1;
    }
    annots = pdf_get(doc, p, "Annots");
    if (annots == NULL || annots->kind != PDF_ARRAY || annots->u.a.n == 0) {
        return 0;
    }
    links = calloc((size_t)(annots->u.a.n < LINKS_MAX ? annots->u.a.n
                                                      : LINKS_MAX),
                   sizeof *links);
    if (links == NULL) {
        return -1;
    }
    for (i = 0; i < annots->u.a.n && n < LINKS_MAX; i++) {
        pdf_obj_t *a = pdf_resolve(doc, annots->u.a.item[i]), *r;
        tiku_pdf_link_t *l = &links[n];
        double v[4];
        int k;

        if (a == NULL || a->kind != PDF_DICT ||
            !pdf_is_name(pdf_get(doc, a, "Subtype"), "Link")) {
            continue;
        }
        r = pdf_get(doc, a, "Rect");
        if (r == NULL || r->kind != PDF_ARRAY || r->u.a.n != 4) {
            continue;
        }
        for (k = 0; k < 4; k++) {
            v[k] = pdf_number(pdf_resolve(doc, r->u.a.item[k]), 0);
        }
        target(doc, a, page, &l->to);
        if (l->to.page < 0 && l->to.uri == NULL) {
            continue;               /* leads nowhere this can follow */
        }
        for (k = 0; k < 4; k++) {
            double x, y;

            pdf_mat_apply(&m, v[k & 1 ? 2 : 0], v[k & 2 ? 3 : 1], &x, &y);
            if (k == 0 || x < l->x0) l->x0 = (float)x;
            if (k == 0 || y < l->y0) l->y0 = (float)y;
            if (k == 0 || x > l->x1) l->x1 = (float)x;
            if (k == 0 || y > l->y1) l->y1 = (float)y;
        }
        n++;
    }
    if (n == 0) {
        free(links);
        return 0;
    }
    *out = links;
    return n;
}

void
tiku_pdf_links_free(tiku_pdf_link_t *links, int n)
{
    int i;

    for (i = 0; links != NULL && i < n; i++) {
        free(links[i].to.uri);
    }
    free(links);
}
