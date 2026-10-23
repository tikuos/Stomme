/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_page.c - the pages, in order, and what each inherits.
 *
 * The page tree (ISO 32000 7.7.3) is walked once when the file opens
 * and its leaves kept in order; a page's resources, box and rotation
 * may sit on any ancestor, so they are looked up the Parent chain.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TREE_DEPTH 64

/** @brief Add @p page to the document's list. */
static int
page_add(tiku_pdf_t *doc, pdf_obj_t *page)
{
    pdf_obj_t **p;

    if (doc->npage >= PDF_PAGES_MAX) {
        return -1;
    }
    p = realloc(doc->page, (size_t)(doc->npage + 1) * sizeof *p);
    if (p == NULL) {
        return -1;
    }
    doc->page = p;
    p[doc->npage++] = page;
    return 0;
}

/**
 * @brief The leaves under @p node, in order.
 *
 * A node with Kids is a branch whatever it calls itself; anything else
 * that looks like a page is one.  @p seen marks object numbers already
 * walked, since a broken tree can point back up.
 */
static void
walk(tiku_pdf_t *doc, pdf_obj_t *node, unsigned char *seen, int depth)
{
    pdf_obj_t *kids, *dict;
    int i;

    if (node == NULL || depth > TREE_DEPTH || doc->npage >= PDF_PAGES_MAX) {
        return;
    }
    if (node->kind == PDF_REF) {
        long num = node->u.ref.num;

        if (num < 0 || num >= doc->nxref || seen[num]) {
            return;
        }
        seen[num] = 1;
        dict = pdf_resolve(doc, node);
    } else {
        dict = node;
    }
    if (dict == NULL || dict->kind != PDF_DICT) {
        return;
    }
    kids = pdf_get(doc, dict, "Kids");
    if (kids != NULL && kids->kind == PDF_ARRAY) {
        for (i = 0; i < kids->u.a.n; i++) {
            walk(doc, kids->u.a.item[i], seen, depth + 1);
        }
        return;
    }
    if (pdf_is_name(pdf_get(doc, dict, "Type"), "Page") ||
        pdf_dict_raw(dict, "Contents") != NULL ||
        pdf_dict_raw(dict, "MediaBox") != NULL) {
        (void)page_add(doc, dict);
    }
}

int
pdf_pages_build(tiku_pdf_t *doc)
{
    pdf_obj_t *root = pdf_root(doc), *pages;
    unsigned char *seen;

    free(doc->page);
    doc->page = NULL;
    doc->npage = 0;
    if (root == NULL) {
        return -1;
    }
    pages = pdf_dict_raw(root, "Pages");
    if (pages == NULL) {
        return -1;
    }
    seen = calloc((size_t)(doc->nxref > 0 ? doc->nxref : 1), 1);
    if (seen == NULL) {
        return -1;
    }
    walk(doc, pages, seen, 0);
    free(seen);
    return 0;
}

pdf_obj_t *
pdf_page_dict(tiku_pdf_t *doc, int page)
{
    if (doc == NULL || page < 0 || page >= doc->npage) {
        return NULL;
    }
    return doc->page[page];
}

pdf_obj_t *
pdf_page_inherited(tiku_pdf_t *doc, pdf_obj_t *page, const char *key)
{
    int depth;

    for (depth = 0; page != NULL && depth < TREE_DEPTH; depth++) {
        pdf_obj_t *v = pdf_get(doc, page, key);

        if (v != NULL && v->kind != PDF_NULL) {
            return v;
        }
        page = pdf_get(doc, page, "Parent");
    }
    return NULL;
}

/** @brief One stream's decoded bytes appended to @p out. */
static int
append(tiku_pdf_t *doc, pdf_obj_t *st, unsigned char **out, long *n)
{
    unsigned char *data = NULL, *p;
    long len = 0;

    if (st == NULL || st->kind != PDF_STREAM ||
        pdf_stream_data(doc, st, &data, &len, NULL) != 0) {
        return 0;                   /* a missing part is skipped */
    }
    if (*n + len + 1 > PDF_STREAM_MAX) {
        free(data);
        return -1;
    }
    p = realloc(*out, (size_t)(*n + len + 2));
    if (p == NULL) {
        free(data);
        return -1;
    }
    memcpy(p + *n, data, (size_t)len);
    p[*n + len] = '\n';
    p[*n + len + 1] = '\0';
    *out = p;
    *n += len + 1;
    free(data);
    return 0;
}

int
pdf_page_content(tiku_pdf_t *doc, pdf_obj_t *page, unsigned char **out,
                 long *n)
{
    pdf_obj_t *c = pdf_get(doc, page, "Contents");
    int i;

    *out = NULL;
    *n = 0;
    if (c == NULL) {
        *out = calloc(1, 1);
        return *out != NULL ? 0 : -1;
    }
    if (c->kind == PDF_ARRAY) {
        for (i = 0; i < c->u.a.n; i++) {
            if (append(doc, pdf_resolve(doc, c->u.a.item[i]), out, n) != 0) {
                free(*out);
                *out = NULL;
                return -1;
            }
        }
    } else if (append(doc, c, out, n) != 0) {
        free(*out);
        *out = NULL;
        return -1;
    }
    if (*out == NULL) {
        *out = calloc(1, 1);
    }
    return *out != NULL ? 0 : -1;
}

/** @brief A rectangle's normalised corners; 0, and nothing written,
 *         unless it has four numbers making a box a page could be. */
static int
rect(tiku_pdf_t *doc, pdf_obj_t *a, double *x0, double *y0, double *x1,
     double *y1)
{
    double v[4], w, h;
    int i;

    if (a == NULL || a->kind != PDF_ARRAY || a->u.a.n != 4) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        pdf_obj_t *o = pdf_resolve(doc, a->u.a.item[i]);

        if (o == NULL || (o->kind != PDF_INT && o->kind != PDF_REAL)) {
            return 0;
        }
        v[i] = pdf_number(o, 0);
    }
    w = fabs(v[2] - v[0]);
    h = fabs(v[3] - v[1]);
    if (!(w > 1 && h > 1 && w < 20000 && h < 20000)) {
        return 0;
    }
    *x0 = v[0] < v[2] ? v[0] : v[2];
    *x1 = v[0] < v[2] ? v[2] : v[0];
    *y0 = v[1] < v[3] ? v[1] : v[3];
    *y1 = v[1] < v[3] ? v[3] : v[1];
    return 1;
}

int
tiku_pdf_page_size(tiku_pdf_t *doc, int page, double *w, double *h,
                   int *rotate)
{
    pdf_obj_t *p = pdf_page_dict(doc, page);
    double x0 = 0, y0 = 0, x1 = 612, y1 = 792, cx0, cy0, cx1, cy1;
    long r;

    if (p == NULL) {
        return -1;
    }
    (void)rect(doc, pdf_page_inherited(doc, p, "MediaBox"), &x0, &y0, &x1,
               &y1);
    if (rect(doc, pdf_page_inherited(doc, p, "CropBox"), &cx0, &cy0, &cx1,
             &cy1) && cx0 < x1 && cx1 > x0 && cy0 < y1 && cy1 > y0) {
        /* The page is the crop box, within the media box. */
        if (cx0 > x0) x0 = cx0;
        if (cy0 > y0) y0 = cy0;
        if (cx1 < x1) x1 = cx1;
        if (cy1 < y1) y1 = cy1;
    }
    r = pdf_int(pdf_page_inherited(doc, p, "Rotate"), 0);
    r = ((r % 360) + 360) % 360;
    r = (r + 45) / 90 * 90 % 360;
    if (w != NULL) *w = x1 - x0;
    if (h != NULL) *h = y1 - y0;
    if (rotate != NULL) *rotate = (int)r;
    return 0;
}

void
tiku_pdf_turn_point(double w, double h, int rotate, int back, double *x,
                    double *y)
{
    int q = (rotate % 360 + 360) % 360 / 90;
    double px = *x, py = *y, t;

    if (back && q % 2) {
        /* A quarter turn is undone by three more, of the turned page. */
        q = 4 - q;
        t = w;
        w = h;
        h = t;
    }
    if (q == 1) {
        *x = h - py;
        *y = px;
    } else if (q == 2) {
        *x = w - px;
        *y = h - py;
    } else if (q == 3) {
        *x = py;
        *y = w - px;
    }
}

int
pdf_page_matrix(tiku_pdf_t *doc, int page, double scale, int turn, int x0,
                int y0, pdf_mat_t *m, int *w, int *h)
{
    double pw, ph, bx0 = 0, by0 = 0, bx1 = 612, by1 = 792;
    int rot;
    pdf_obj_t *p = pdf_page_dict(doc, page), *box;

    if (p == NULL || turn % 90 != 0 ||
        tiku_pdf_page_size(doc, page, &pw, &ph, &rot) != 0) {
        return -1;
    }
    rot = (rot + turn % 360 + 360) % 360;   /* the view's turn on the page's */
    box = pdf_page_inherited(doc, p, "CropBox");
    if (box == NULL) {
        box = pdf_page_inherited(doc, p, "MediaBox");
    }
    if (box != NULL && box->kind == PDF_ARRAY && box->u.a.n == 4) {
        double v[4];
        int i;

        for (i = 0; i < 4; i++) {
            v[i] = pdf_number(pdf_resolve(doc, box->u.a.item[i]), 0);
        }
        bx0 = v[0] < v[2] ? v[0] : v[2];
        bx1 = v[0] < v[2] ? v[2] : v[0];
        by0 = v[1] < v[3] ? v[1] : v[3];
        by1 = v[1] < v[3] ? v[3] : v[1];
    }
    *w = (int)(pw * scale + 0.5);
    *h = (int)(ph * scale + 0.5);
    /* Flip y, then turn: the page's top left lands at the origin. */
    *m = pdf_mat(scale, 0, 0, -scale, -bx0 * scale, by1 * scale);
    if (rot == 90) {
        *m = pdf_mat_mul(*m, pdf_mat(0, 1, -1, 0, ph * scale, 0));
        *w = (int)(ph * scale + 0.5);
        *h = (int)(pw * scale + 0.5);
    } else if (rot == 180) {
        *m = pdf_mat_mul(*m, pdf_mat(-1, 0, 0, -1, pw * scale, ph * scale));
    } else if (rot == 270) {
        *m = pdf_mat_mul(*m, pdf_mat(0, -1, 1, 0, 0, pw * scale));
        *w = (int)(ph * scale + 0.5);
        *h = (int)(pw * scale + 0.5);
    }
    *m = pdf_mat_mul(*m, pdf_mat(1, 0, 0, 1, -x0, -y0));
    (void)bx1;
    (void)by0;
    return 0;
}

/** @brief qsort's and bsearch's order for the page index: by address. */
static int
by_dict(const void *a, const void *b)
{
    const pdf_pageref_t *x = a, *y = b;

    return x->dict < y->dict ? -1 : x->dict > y->dict;
}

int
pdf_page_index(tiku_pdf_t *doc, pdf_obj_t *page)
{
    pdf_pageref_t key, *hit;
    int i;

    page = pdf_resolve(doc, page);
    if (page == NULL || doc->npage == 0) {
        return -1;
    }
    if (page->kind == PDF_INT) {
        i = (int)page->u.i;                 /* a page number, as GoToR has */
        return i >= 0 && i < doc->npage ? i : -1;
    }
    if (doc->pageref == NULL) {
        doc->pageref = malloc((size_t)doc->npage * sizeof *doc->pageref);
        if (doc->pageref == NULL) {
            return -1;
        }
        for (i = 0; i < doc->npage; i++) {
            doc->pageref[i].dict = doc->page[i];
            doc->pageref[i].index = i;
        }
        qsort(doc->pageref, (size_t)doc->npage, sizeof *doc->pageref,
              by_dict);
    }
    key.dict = page;
    hit = bsearch(&key, doc->pageref, (size_t)doc->npage,
                  sizeof *doc->pageref, by_dict);
    return hit != NULL ? hit->index : -1;
}
