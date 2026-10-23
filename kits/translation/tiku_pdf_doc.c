/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_pdf_doc.c - the document: where its objects are, and getting one.
 *
 * The cross-reference says where each object lies, as a table at the
 * end (ISO 32000 7.5.4) or a stream of packed entries (7.5.8), with
 * older ones chained behind by Prev; objects may be packed together in
 * an object stream (7.5.7).  A table that lies about even one object is
 * dropped and the whole file read for "N G obj" instead, so a damaged
 * file still opens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREV_MAX   64               /* cross-reference sections in a chain */
#define TAIL       2048             /* where startxref is looked for       */
#define RESOLVE_MAX 32              /* references to references ...        */

static int repair(tiku_pdf_t *doc);

/** @brief Room for objects numbered below @p n. */
static int
reserve(tiku_pdf_t *doc, long n)
{
    pdf_xref_t *x;
    pdf_obj_t **c;

    if (n <= doc->nxref) {
        return 0;
    }
    if (n > PDF_OBJECTS_MAX) {
        return -1;
    }
    x = realloc(doc->xref, (size_t)n * sizeof *x);
    if (x == NULL) {
        return -1;
    }
    memset(x + doc->nxref, 0, (size_t)(n - doc->nxref) * sizeof *x);
    doc->xref = x;
    c = realloc(doc->cache, (size_t)n * sizeof *c);
    if (c == NULL) {
        return -1;
    }
    memset(c + doc->nxref, 0, (size_t)(n - doc->nxref) * sizeof *c);
    doc->cache = c;
    doc->nxref = n;
    return 0;
}

/** @brief Record where object @p num is, unless an entry was read already. */
static void
xref_put(tiku_pdf_t *doc, long num, int kind, long off, long idx, int force)
{
    if (num < 0 || num >= PDF_OBJECTS_MAX || reserve(doc, num + 1) != 0) {
        return;
    }
    if (doc->xref[num].set && !force) {
        return;
    }
    doc->xref[num].kind = (unsigned char)kind;
    doc->xref[num].off = off;
    doc->xref[num].idx = idx;
    doc->xref[num].set = 1;
}

/* ------------------------------------------------------- one object  */

/**
 * @brief "N G obj <object>" at @p off, N into @p num and G into @p gen.
 * @return the object, or NULL when the bytes there are not an object.
 */
static pdf_obj_t *
parse_at(tiku_pdf_t *doc, long off, long *num, long *gen)
{
    pdf_cur_t c = { doc->data, doc->len, off };
    pdf_obj_t *a, *b, *k, *o;

    if (off < 0 || off >= doc->len) {
        return NULL;
    }
    a = pdf_parse(&c, 0);
    b = pdf_parse(&c, 0);
    k = pdf_parse(&c, 0);
    if (a == NULL || b == NULL || k == NULL || a->kind != PDF_INT ||
        b->kind != PDF_INT || k->kind != PDF_OP || k->u.s.n != 3 ||
        memcmp(k->u.s.p, "obj", 3) != 0) {
        pdf_free(a);
        pdf_free(b);
        pdf_free(k);
        return NULL;
    }
    *num = a->u.i;
    *gen = b->u.i;
    pdf_free(a);
    pdf_free(b);
    pdf_free(k);
    o = pdf_parse(&c, 0);
    if (o != NULL && o->kind == PDF_OP) {
        /* "endobj" straight after the header: an empty object. */
        pdf_free(o);
        o = pdf_new(PDF_NULL);
    }
    return o;
}

/* --------------------------------------------------- object streams  */

/** @brief The object stream numbered @p num, decoded and kept. */
static pdf_objstm_t *
objstm_get(tiku_pdf_t *doc, long num)
{
    pdf_objstm_t *s;
    pdf_obj_t *st;
    int i;

    for (i = 0; i < PDF_OBJSTM_KEEP; i++) {
        if (doc->objstm[i].data != NULL && doc->objstm[i].num == num) {
            return &doc->objstm[i];
        }
    }
    st = pdf_object(doc, num);
    if (st == NULL || st->kind != PDF_STREAM) {
        return NULL;
    }
    s = &doc->objstm[doc->objstm_next];
    doc->objstm_next = (doc->objstm_next + 1) % PDF_OBJSTM_KEEP;
    free(s->data);
    s->data = NULL;
    if (pdf_stream_data(doc, st, &s->data, &s->len, NULL) != 0) {
        return NULL;
    }
    s->num = num;
    s->count = pdf_int(pdf_get(doc, st, "N"), 0);
    s->first = pdf_int(pdf_get(doc, st, "First"), 0);
    if (s->count < 0 || s->first < 0 || s->first > s->len) {
        free(s->data);
        s->data = NULL;
        return NULL;
    }
    return s;
}

/**
 * @brief Object @p want at index @p idx of object stream @p snum.
 *
 * The stream opens with pairs of "number offset"; the pair at @p idx
 * should name @p want, and when a table lies the pairs are searched.
 */
static pdf_obj_t *
parse_in_objstm(tiku_pdf_t *doc, long snum, long idx, long want)
{
    pdf_objstm_t *s = objstm_get(doc, snum);
    pdf_cur_t c;
    long i, num = -1, off = -1;

    if (s == NULL) {
        return NULL;
    }
    c.p = s->data;
    c.n = s->first;
    c.pos = 0;
    for (i = 0; i < s->count; i++) {
        pdf_obj_t *a = pdf_parse(&c, 0), *b = pdf_parse(&c, 0);
        int hit = a != NULL && b != NULL && a->kind == PDF_INT &&
                  b->kind == PDF_INT && a->u.i == want;

        if (hit) {
            num = a->u.i;
            off = b->u.i;
        }
        pdf_free(a);
        pdf_free(b);
        if (hit && i >= idx) {
            break;
        }
    }
    if (num != want || off < 0 || s->first + off >= s->len) {
        return NULL;
    }
    c.p = s->data;
    c.n = s->len;
    c.pos = s->first + off;
    return pdf_parse(&c, 0);
}

/* ----------------------------------------------------------- resolve  */

pdf_obj_t *
pdf_object(tiku_pdf_t *doc, long num)
{
    pdf_obj_t *o = NULL;
    pdf_xref_t *x;
    long got = -1, gen = 0;

    if (num < 0 || num >= doc->nxref) {
        return NULL;
    }
    if (doc->cache[num] != NULL) {
        return doc->cache[num];
    }
    x = &doc->xref[num];
    if (x->seen) {
        return NULL;                /* an object that needs itself */
    }
    x->seen = 1;
    if (x->kind == 1) {
        o = parse_at(doc, x->off, &got, &gen);
        if (o != NULL && got != num) {
            pdf_free(o);
            o = NULL;
        }
        if (o != NULL && o->kind == PDF_STREAM) {
            o->u.st.num = num;      /* its key, when it is encrypted */
            o->u.st.gen = gen;
        }
        if (o != NULL && doc->crypt != NULL) {
            pdf_crypt_object(doc, o, num, gen);
        }
    } else if (x->kind == 2) {
        o = parse_in_objstm(doc, x->off, x->idx, num);
    }
    x = &doc->xref[num];            /* a repair may have moved the table */
    x->seen = 0;
    if (o == NULL && x->set && !doc->repaired && x->kind != 0) {
        if (repair(doc) == 0) {
            return pdf_object(doc, num);
        }
    }
    if (o != NULL && num < doc->nxref) {
        doc->cache[num] = o;
    }
    return o;
}

pdf_obj_t *
pdf_resolve(tiku_pdf_t *doc, pdf_obj_t *o)
{
    int hops = 0;

    while (o != NULL && o->kind == PDF_REF && hops++ < RESOLVE_MAX) {
        o = pdf_object(doc, o->u.ref.num);
    }
    return (o != NULL && o->kind == PDF_REF) ? NULL : o;
}

pdf_obj_t *
pdf_get(tiku_pdf_t *doc, pdf_obj_t *dict, const char *key)
{
    return pdf_resolve(doc, pdf_dict_raw(dict, key));
}

pdf_obj_t *
pdf_root(tiku_pdf_t *doc)
{
    pdf_obj_t *r = pdf_get(doc, doc->trailer, "Root");

    return (r != NULL && r->kind == PDF_DICT) ? r : NULL;
}

int
pdf_stream_raw(tiku_pdf_t *doc, pdf_obj_t *stream, const unsigned char **p,
               long *n)
{
    long off, len, end;

    if (stream == NULL || stream->kind != PDF_STREAM) {
        return -1;
    }
    off = stream->u.st.off;
    if (off < 0 || off > doc->len) {
        return -1;
    }
    if (stream->u.st.len < 0) {
        pdf_cur_t c = { doc->data, doc->len, 0 };

        len = pdf_int(pdf_get(doc, stream, "Length"), -1);
        c.pos = (len >= 0 && off + len <= doc->len) ? off + len : -1;
        if (c.pos < 0 || !pdf_skip_ws(&c) || !pdf_keyword_at(&c, "endstream")) {
            end = pdf_find(doc->data, doc->len, off, "endstream");
            len = end < 0 ? doc->len - off : end - off;
            while (len > 0 && (doc->data[off + len - 1] == '\n' ||
                               doc->data[off + len - 1] == '\r')) {
                len--;
            }
        }
        stream->u.st.len = len;
    }
    *p = doc->data + off;
    *n = stream->u.st.len;
    return 0;
}

/* --------------------------------------------- the cross-reference  */

/** @brief A classic table at the cursor: "xref", sections, "trailer". */
static pdf_obj_t *
xref_table(tiku_pdf_t *doc, pdf_cur_t *c)
{
    c->pos += 4;
    while (pdf_skip_ws(c) && !pdf_keyword_at(c, "trailer")) {
        pdf_obj_t *a = pdf_parse(c, 0), *b = pdf_parse(c, 0);
        long start, count, i;

        if (a == NULL || b == NULL || a->kind != PDF_INT ||
            b->kind != PDF_INT) {
            pdf_free(a);
            pdf_free(b);
            return NULL;
        }
        start = a->u.i;
        count = b->u.i;
        pdf_free(a);
        pdf_free(b);
        if (start < 0 || count < 0 || start + count > PDF_OBJECTS_MAX) {
            return NULL;
        }
        for (i = 0; i < count; i++) {
            pdf_obj_t *off = pdf_parse(c, 0), *gen = pdf_parse(c, 0);
            pdf_obj_t *kind = pdf_parse(c, 0);
            int ok = off != NULL && gen != NULL && kind != NULL &&
                     off->kind == PDF_INT && gen->kind == PDF_INT &&
                     kind->kind == PDF_OP && kind->u.s.n == 1;

            if (ok && kind->u.s.p[0] == 'n') {
                xref_put(doc, start + i, 1, off->u.i, 0, 0);
            } else if (ok && kind->u.s.p[0] == 'f') {
                xref_put(doc, start + i, 0, 0, 0, 0);
            }
            pdf_free(off);
            pdf_free(gen);
            pdf_free(kind);
            if (!ok) {
                return NULL;
            }
        }
    }
    if (!pdf_keyword_at(c, "trailer")) {
        return NULL;
    }
    c->pos += 7;
    return pdf_parse(c, 0);
}

/** @brief An unsigned big-endian number of @p w bytes. */
static long
field(const unsigned char *p, int w)
{
    long v = 0;

    while (w-- > 0) {
        v = (v << 8) | *p++;
    }
    return v;
}

/** @brief A cross-reference stream: /W widths over /Index ranges. */
static int
xref_stream(tiku_pdf_t *doc, pdf_obj_t *st)
{
    pdf_obj_t *w = pdf_get(doc, st, "W"), *index = pdf_get(doc, st, "Index");
    unsigned char *data = NULL;
    long n = 0, pos = 0, size;
    int wid[3] = {0, 0, 0}, i, rows;

    if (w == NULL || w->kind != PDF_ARRAY || w->u.a.n != 3) {
        return -1;
    }
    for (i = 0; i < 3; i++) {
        wid[i] = pdf_int(pdf_resolve(doc, w->u.a.item[i]), 0);
        if (wid[i] < 0 || wid[i] > 8) {
            return -1;
        }
    }
    size = pdf_int(pdf_get(doc, st, "Size"), 0);
    if (pdf_stream_data(doc, st, &data, &n, NULL) != 0) {
        return -1;
    }
    rows = (index != NULL && index->kind == PDF_ARRAY) ? index->u.a.n / 2 : 1;
    for (i = 0; i < rows; i++) {
        long start = 0, count = size, k;

        if (index != NULL && index->kind == PDF_ARRAY) {
            start = pdf_int(pdf_resolve(doc, index->u.a.item[2 * i]), 0);
            count = pdf_int(pdf_resolve(doc, index->u.a.item[2 * i + 1]),
                            0);
        }
        for (k = 0; k < count; k++) {
            long type = 1, f2, f3;

            if (pos + wid[0] + wid[1] + wid[2] > n) {
                free(data);
                return 0;           /* a short stream: what it had counts */
            }
            if (wid[0] > 0) {
                type = field(data + pos, wid[0]);
            }
            f2 = field(data + pos + wid[0], wid[1]);
            f3 = field(data + pos + wid[0] + wid[1], wid[2]);
            pos += wid[0] + wid[1] + wid[2];
            if (type == 1) {
                xref_put(doc, start + k, 1, f2, 0, 0);
            } else if (type == 2) {
                xref_put(doc, start + k, 2, f2, f3, 0);
            } else if (type == 0) {
                xref_put(doc, start + k, 0, 0, 0, 0);
            }
        }
    }
    free(data);
    return 0;
}

/** @brief Keep the first trailer; fill its Root, Info, Encrypt and ID
 *         from later ones when it has none. */
static void
trailer_merge(tiku_pdf_t *doc, pdf_obj_t *t)
{
    static const char *const keys[] = {"Root", "Info", "Encrypt", "ID"};
    size_t i;

    if (t == NULL) {
        return;
    }
    if (t->kind == PDF_STREAM) {
        pdf_obj_t *d = t->u.st.dict;

        t->u.st.dict = NULL;
        pdf_free(t);
        t = d;
    }
    if (t->kind != PDF_DICT) {
        pdf_free(t);
        return;
    }
    if (doc->trailer == NULL) {
        doc->trailer = t;
        return;
    }
    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        pdf_obj_t *v = pdf_dict_raw(t, keys[i]);

        if (v != NULL && pdf_dict_raw(doc->trailer, keys[i]) == NULL) {
            int k;

            for (k = 0; k < t->u.d.n; k++) {
                if (t->u.d.val[k] == v) {
                    t->u.d.val[k] = NULL;
                }
            }
            (void)pdf_dict_set(doc->trailer, keys[i], v);
        }
    }
    pdf_free(t);
}

/**
 * @brief The cross-reference section at @p off, then those behind it.
 *
 * A hybrid file's table names a stream (XRefStm) that is read before
 * Prev, as the standard orders it.  @return 0 when the section read.
 */
static int
load_xref_at(tiku_pdf_t *doc, long off, long *visited, int *nvisited)
{
    pdf_cur_t c = { doc->data, doc->len, off };
    pdf_obj_t *t;
    long prev, stm;
    int i;

    if (off <= 0 || off >= doc->len || *nvisited >= PREV_MAX) {
        return -1;
    }
    for (i = 0; i < *nvisited; i++) {
        if (visited[i] == off) {
            return 0;               /* a loop of Prev: done */
        }
    }
    visited[(*nvisited)++] = off;
    if (!pdf_skip_ws(&c)) {
        return -1;
    }
    if (pdf_keyword_at(&c, "xref")) {
        t = xref_table(doc, &c);
        if (t == NULL) {
            return -1;
        }
    } else {
        long num, gen;

        t = parse_at(doc, c.pos, &num, &gen);
        if (t == NULL || t->kind != PDF_STREAM ||
            xref_stream(doc, t) != 0) {
            pdf_free(t);
            return -1;
        }
    }
    stm = pdf_int(pdf_dict_raw(t, "XRefStm"), -1);
    prev = pdf_int(pdf_dict_raw(t, "Prev"), -1);
    trailer_merge(doc, t);
    if (stm > 0) {
        (void)load_xref_at(doc, stm, visited, nvisited);
    }
    if (prev > 0) {
        (void)load_xref_at(doc, prev, visited, nvisited);
    }
    return 0;
}

/** @brief From "startxref" at the end to a Root that resolves. */
static int
load_xref(tiku_pdf_t *doc)
{
    long visited[PREV_MAX], from = doc->len - TAIL, at, off = -1;
    int nvisited = 0;

    for (;;) {
        long next = pdf_find(doc->data, doc->len, from < 0 ? 0 : from,
                             "startxref");

        if (next < 0) {
            break;
        }
        at = next;
        from = next + 9;
        {
            pdf_cur_t c = { doc->data, doc->len, at + 9 };
            pdf_obj_t *o = pdf_parse(&c, 0);

            off = (o != NULL && o->kind == PDF_INT) ? o->u.i : -1;
            pdf_free(o);
        }
    }
    if (off < 0 || load_xref_at(doc, off, visited, &nvisited) != 0) {
        return -1;
    }
    return pdf_root(doc) != NULL ? 0 : -1;
}

/* ------------------------------------------------------------ repair  */

/** @brief A number's digits, read backwards from just before @p end. */
static long
digits_before(const unsigned char *p, long end, long *start)
{
    long i = end, v = 0, mul = 1;

    while (i > 0 && p[i - 1] >= '0' && p[i - 1] <= '9') {
        if (mul > 100000000000L) {
            return -1;
        }
        v += (p[i - 1] - '0') * mul;
        mul *= 10;
        i--;
    }
    if (i == end) {
        return -1;
    }
    *start = i;
    return v;
}

/** @brief @p t as the trailer from here; the one from before the repair
 *         is kept, since a caller may be holding it. */
static void
set_trailer(tiku_pdf_t *doc, pdf_obj_t *t)
{
    if (doc->trailer != doc->old_trailer) {
        pdf_free(doc->trailer);
    }
    doc->trailer = t;
}

/**
 * @brief Forget the table and find every object by reading the file.
 *
 * "N G obj" is looked for byte by byte; the last occurrence of a number
 * wins, as it does in an updated file.  Objects packed in object
 * streams are found through the streams themselves, and the trailer's
 * Root from any trailer dictionary or cross-reference stream, else
 * from the object that calls itself the catalogue.
 */
static int
repair(tiku_pdf_t *doc)
{
    const unsigned char *p = doc->data;
    pdf_obj_t **fresh;
    long i, n = doc->len;

    if (doc->repaired) {
        return -1;
    }
    doc->repaired = 1;
    /* Whoever asked for an object may be holding it still: what was
     * parsed before is kept until the document closes, and read anew. */
    fresh = calloc((size_t)doc->nxref + 1, sizeof *fresh);
    if (fresh == NULL) {
        return -1;
    }
    doc->grave = doc->cache;
    doc->ngrave = doc->nxref;
    doc->cache = fresh;
    doc->old_trailer = doc->trailer;
    for (i = 0; i < doc->nxref; i++) {
        doc->xref[i].set = 0;
        doc->xref[i].kind = 0;
    }
    for (i = 0; i < PDF_OBJSTM_KEEP; i++) {
        free(doc->objstm[i].data);
        doc->objstm[i].data = NULL;
    }
    for (i = 3; i + 3 <= n; i++) {
        long gs, ge, ns, num;

        if (p[i] != 'o' || p[i + 1] != 'b' || p[i + 2] != 'j' ||
            (i + 3 < n && ((p[i + 3] >= 'a' && p[i + 3] <= 'z') ||
                           (p[i + 3] >= '0' && p[i + 3] <= '9')))) {
            continue;
        }
        ge = i;
        while (ge > 0 && (p[ge - 1] == ' ' || p[ge - 1] == '\r' ||
                          p[ge - 1] == '\n' || p[ge - 1] == '\t')) {
            ge--;
        }
        if (ge == i || digits_before(p, ge, &gs) < 0) {
            continue;
        }
        ge = gs;
        while (ge > 0 && (p[ge - 1] == ' ' || p[ge - 1] == '\r' ||
                          p[ge - 1] == '\n' || p[ge - 1] == '\t')) {
            ge--;
        }
        if (ge == gs) {
            continue;
        }
        num = digits_before(p, ge, &ns);
        if (num < 0 || (ns > 0 && ((p[ns - 1] >= '0' && p[ns - 1] <= '9') ||
                                   (p[ns - 1] >= 'a' && p[ns - 1] <= 'z') ||
                                   (p[ns - 1] >= 'A' && p[ns - 1] <= 'Z')))) {
            continue;
        }
        xref_put(doc, num, 1, ns, 0, 1);
    }
    /* The objects each object stream holds, where the file has not got
     * them loose; then a Root, from wherever one is written. */
    for (i = 0; i < doc->nxref; i++) {
        pdf_obj_t *o;

        if (doc->xref[i].kind != 1) {
            continue;
        }
        o = pdf_object(doc, i);
        if (o == NULL || o->kind != PDF_STREAM) {
            continue;
        }
        if (pdf_is_name(pdf_get(doc, o, "Type"), "ObjStm")) {
            pdf_objstm_t *s = objstm_get(doc, i);
            pdf_cur_t c;
            long k;

            if (s == NULL) {
                continue;
            }
            c.p = s->data;
            c.n = s->first;
            c.pos = 0;
            for (k = 0; k < s->count; k++) {
                pdf_obj_t *a = pdf_parse(&c, 0), *b = pdf_parse(&c, 0);

                if (a != NULL && a->kind == PDF_INT && b != NULL &&
                    b->kind == PDF_INT) {
                    xref_put(doc, a->u.i, 2, i, k, 0);
                }
                pdf_free(a);
                pdf_free(b);
            }
        } else if (pdf_is_name(pdf_get(doc, o, "Type"), "XRef") &&
                   pdf_dict_raw(o, "Root") != NULL) {
            pdf_obj_t *t = pdf_new(PDF_DICT), *r = pdf_dict_raw(o, "Root");
            pdf_obj_t *copy = pdf_new(PDF_REF);

            if (t != NULL && copy != NULL && r->kind == PDF_REF) {
                *copy = *r;
                (void)pdf_dict_set(t, "Root", copy);
                set_trailer(doc, t);
            } else {
                pdf_free(t);
                pdf_free(copy);
            }
        }
    }
    for (i = 0; ; ) {
        pdf_cur_t c = { doc->data, doc->len, 0 };
        long at = pdf_find(doc->data, doc->len, i, "trailer");
        pdf_obj_t *t;

        if (at < 0) {
            break;
        }
        i = at + 7;
        c.pos = i;
        t = pdf_parse(&c, 0);
        if (t != NULL && t->kind == PDF_DICT && pdf_dict_raw(t, "Root")) {
            set_trailer(doc, t);
        } else {
            pdf_free(t);
        }
    }
    if (pdf_root(doc) == NULL) {
        for (i = 0; i < doc->nxref; i++) {
            pdf_obj_t *o = doc->xref[i].set ? pdf_object(doc, i) : NULL;

            if (o != NULL && o->kind == PDF_DICT &&
                pdf_is_name(pdf_get(doc, o, "Type"), "Catalog")) {
                pdf_obj_t *t = pdf_new(PDF_DICT), *r = pdf_new(PDF_REF);

                if (t == NULL || r == NULL) {
                    pdf_free(t);
                    pdf_free(r);
                    break;
                }
                r->u.ref.num = i;
                (void)pdf_dict_set(t, "Root", r);
                set_trailer(doc, t);
                break;
            }
        }
    }
    return pdf_root(doc) != NULL ? 0 : -1;
}

/* -------------------------------------------------------- open, close  */

int
tiku_pdf_is_pdf(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char head[1024];
    size_t n;

    if (f == NULL) {
        return 0;
    }
    n = fread(head, 1, sizeof head, f);
    fclose(f);
    return pdf_find(head, (long)n, 0, "%PDF") >= 0;
}

/**
 * @brief Decryption from here on: what was read before it -- the
 *        dictionary itself aside -- is read again, through the key.
 */
static tiku_pdf_err_t
unlock(tiku_pdf_t *doc, const char *password, const tiku_key_t *key)
{
    int rc = pdf_crypt_open(doc, password, key);
    long i;

    if (rc != 0) {
        return rc == -2 ? TIKU_PDF_PASSWORD : rc == -3 ? TIKU_PDF_CERTIFICATE
                                                       : TIKU_PDF_UNSUPPORTED;
    }
    for (i = 0; i < doc->nxref; i++) {
        pdf_obj_t *raw = pdf_dict_raw(doc->trailer, "Encrypt");

        if (doc->cache[i] != NULL &&
            !(raw != NULL && raw->kind == PDF_REF && raw->u.ref.num == i)) {
            pdf_free(doc->cache[i]);
            doc->cache[i] = NULL;
        }
    }
    for (i = 0; i < PDF_OBJSTM_KEEP; i++) {
        free(doc->objstm[i].data);
        doc->objstm[i].data = NULL;
    }
    return TIKU_PDF_OK;
}

/** @brief The document over @p data, which it now owns, opened with
 *         @p password or, sealed to certificates, @p key. */
static tiku_pdf_err_t
open_data(unsigned char *data, long len, const char *password,
          const tiku_key_t *key, tiku_pdf_t **out)
{
    tiku_pdf_t *doc;

    if (pdf_find(data, len < 1024 ? len : 1024, 0, "%PDF") < 0) {
        free(data);
        return TIKU_PDF_NOT_PDF;
    }
    doc = calloc(1, sizeof *doc);
    if (doc == NULL) {
        free(data);
        return TIKU_PDF_BAD;
    }
    doc->data = data;
    doc->len = len;
    if (load_xref(doc) != 0 && repair(doc) != 0) {
        tiku_pdf_close(doc);
        return TIKU_PDF_BAD;
    }
    if (pdf_dict_raw(doc->trailer, "Encrypt") != NULL) {
        tiku_pdf_err_t rc = unlock(doc, password, key);

        doc->encrypted = 1;
        if (rc != TIKU_PDF_OK) {
            tiku_pdf_close(doc);
            return rc;
        }
    }
    if (pdf_pages_build(doc) != 0 || doc->npage == 0) {
        if (doc->repaired || repair(doc) != 0 || pdf_pages_build(doc) != 0 ||
            doc->npage == 0) {
            tiku_pdf_close(doc);
            return TIKU_PDF_BAD;
        }
    }
    *out = doc;
    return TIKU_PDF_OK;
}

/** @brief File @p path read whole, @p max bytes at most, a NUL after.
 *         @return its bytes to free(), @p len of them, or NULL. */
static unsigned char *
slurp(const char *path, long max, long *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (*len = ftell(f)) < 0 ||
        *len > max || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = malloc((size_t)*len + 1);
    if (data == NULL || fread(data, 1, (size_t)*len, f) != (size_t)*len) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[*len] = '\0';
    return data;
}

tiku_pdf_err_t
tiku_pdf_open_password(const char *path, const char *password,
                       tiku_pdf_t **out)
{
    long len;
    unsigned char *data = slurp(path, PDF_FILE_MAX, &len);

    return data != NULL ? open_data(data, len, password, NULL, out)
                        : TIKU_PDF_BAD;
}

/** @brief The document's @p len bytes at @p data, which it now owns,
 *         opened with the key in key file @p kf, unlocked by @p password. */
static tiku_pdf_err_t
open_with_key(unsigned char *data, long len, const void *kf, size_t kn,
              const char *password, tiku_pdf_t **out)
{
    tiku_key_t *key = NULL;
    tiku_pdf_err_t rc;
    int got = tiku_key_read(kf, kn, password, &key);

    if (got != TIKU_KEY_OK) {
        free(data);
        return got == TIKU_KEY_PASSWORD ? TIKU_PDF_PASSWORD
                                        : TIKU_PDF_KEY_FILE;
    }
    rc = open_data(data, len, "", key, out);
    tiku_key_free(key);
    return rc;
}

tiku_pdf_err_t
tiku_pdf_open_key(const char *path, const char *keyfile, const char *password,
                  tiku_pdf_t **out)
{
    long len, kn;
    unsigned char *data = slurp(path, PDF_FILE_MAX, &len), *kf;
    tiku_pdf_err_t rc;

    if (data == NULL) {
        return TIKU_PDF_BAD;
    }
    if ((kf = slurp(keyfile, 1L << 20, &kn)) == NULL) {
        free(data);
        return TIKU_PDF_KEY_FILE;
    }
    rc = open_with_key(data, len, kf, (size_t)kn, password, out);
    memset(kf, 0, (size_t)kn);
    free(kf);
    return rc;
}

tiku_pdf_err_t
tiku_pdf_decode_key(const void *bytes, size_t len, const void *key,
                    size_t keylen, const char *password, tiku_pdf_t **out)
{
    unsigned char *data;

    if (bytes == NULL || len > (size_t)PDF_FILE_MAX ||
        (data = malloc(len + 1)) == NULL) {
        return TIKU_PDF_BAD;
    }
    memcpy(data, bytes, len);
    data[len] = '\0';
    return open_with_key(data, (long)len, key, keylen, password, out);
}

tiku_pdf_err_t
tiku_pdf_open(const char *path, tiku_pdf_t **out)
{
    return tiku_pdf_open_password(path, "", out);
}

tiku_pdf_err_t
tiku_pdf_decode_password(const void *bytes, size_t len, const char *password,
                         tiku_pdf_t **out)
{
    unsigned char *data;

    if (bytes == NULL || len > (size_t)PDF_FILE_MAX) {
        return TIKU_PDF_BAD;
    }
    data = malloc(len + 1);
    if (data == NULL) {
        return TIKU_PDF_BAD;
    }
    memcpy(data, bytes, len);
    data[len] = '\0';
    return open_data(data, (long)len, password, NULL, out);
}

tiku_pdf_err_t
tiku_pdf_decode(const void *bytes, size_t len, tiku_pdf_t **out)
{
    return tiku_pdf_decode_password(bytes, len, "", out);
}

int
tiku_pdf_may_copy(const tiku_pdf_t *doc)
{
    return doc != NULL && pdf_crypt_may_copy(doc->crypt);
}

void
tiku_pdf_close(tiku_pdf_t *doc)
{
    long i;

    if (doc == NULL) {
        return;
    }
    pdf_fonts_free(doc);            /* they hold what the cache holds */
    pdf_free(doc->annot_res);
    for (i = 0; i < doc->nxref; i++) {
        pdf_free(doc->cache[i]);
    }
    for (i = 0; i < doc->ngrave; i++) {
        pdf_free(doc->grave[i]);
    }
    for (i = 0; i < PDF_OBJSTM_KEEP; i++) {
        free(doc->objstm[i].data);
    }
    for (i = 0; i < doc->nsubst; i++) {
        tiku_ttf_close(doc->subst[i]);
    }
    pdf_cid_unicode_free(doc);
    pdf_crypt_free(doc->crypt);
    if (doc->old_trailer != doc->trailer) {
        pdf_free(doc->old_trailer);
    }
    pdf_free(doc->trailer);
    free(doc->grave);
    free(doc->oc_off);
    free(doc->cache);
    free(doc->xref);
    free(doc->page);
    free(doc->pageref);
    free(doc->data);
    free(doc);
}

int
tiku_pdf_pages(const tiku_pdf_t *doc)
{
    return doc != NULL ? doc->npage : 0;
}

int
tiku_pdf_repaired(const tiku_pdf_t *doc)
{
    return doc != NULL && doc->repaired;
}

/* PDFDocEncoding's 0x80..0x9f as characters; 0x9f stands for none. */
static const unsigned short pdfdoc_high[32] = {
    0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044,
    0x2039, 0x203a, 0x2212, 0x2030, 0x201e, 0x201c, 0x201d, 0x2018,
    0x2019, 0x201a, 0x2122, 0xfb01, 0xfb02, 0x0141, 0x0152, 0x0160,
    0x0178, 0x017d, 0x0131, 0x0142, 0x0153, 0x0161, 0x017e, 0x0000
};

/* PDFDocEncoding's 0x18..0x1f: the spacing diacritics. */
static const unsigned short pdfdoc_marks[8] = {
    0x02d8, 0x02c7, 0x02c6, 0x02d9, 0x02dd, 0x02db, 0x02da, 0x02dc
};

/** @brief The PDFDocEncoding byte for @p cp, or -1 when there is none. */
static int
pdfdoc_byte(unsigned long cp)
{
    int i;

    if (cp < 0x80 && (cp < 0x18 || cp > 0x1f)) {
        return (int)cp;
    }
    if (cp == 0x20ac) {
        return 0xa0;                /* the euro, where Latin-1 has NBSP */
    }
    if (cp > 0xa0 && cp <= 0xff && cp != 0xad) {
        return (int)cp;
    }
    for (i = 0; cp != 0 && i < 32; i++) {
        if (pdfdoc_high[i] == cp) {
            return 0x80 + i;
        }
    }
    for (i = 0; i < 8; i++) {
        if (pdfdoc_marks[i] == cp) {
            return 0x18 + i;
        }
    }
    return -1;
}

long
pdf_docenc(const char *in, unsigned char *out, size_t cap)
{
    const unsigned char *p = (const unsigned char *)in;
    size_t n = 0;

    while (*p != 0) {
        unsigned long cp = *p;
        int k = cp < 0x80 ? 0 : cp >= 0xc2 && cp < 0xe0 ? 1 : cp >= 0xe0 &&
                cp < 0xf0 ? 2 : cp >= 0xf0 && cp < 0xf5 ? 3 : -1, i, b;

        if (k < 0) {
            return -1;
        }
        cp &= k == 0 ? 0x7fu : 0x3fu >> k;
        for (i = 1; i <= k; i++) {
            if ((p[i] & 0xc0) != 0x80) {
                return -1;
            }
            cp = cp << 6 | (p[i] & 0x3fu);
        }
        if ((b = pdfdoc_byte(cp)) < 0 || n == cap) {
            return -1;
        }
        out[n++] = (unsigned char)b;
        p += k + 1;
    }
    return (long)n;
}

/** @brief @p cp as UTF-8 at @p out + *@p n, a control made a space. */
static void
utf8_put(char *out, size_t *n, unsigned long cp)
{
    if (cp < 0x20 || cp == 0x7f) {
        cp = ' ';
    }
    if (cp < 0x80) {
        out[(*n)++] = (char)cp;
    } else if (cp < 0x800) {
        out[(*n)++] = (char)(0xc0 | (cp >> 6));
        out[(*n)++] = (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out[(*n)++] = (char)(0xe0 | (cp >> 12));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[(*n)++] = (char)(0x80 | (cp & 0x3f));
    } else {
        out[(*n)++] = (char)(0xf0 | (cp >> 18));
        out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[(*n)++] = (char)(0x80 | (cp & 0x3f));
    }
}

char *
pdf_text_string(const pdf_obj_t *s)
{
    const unsigned char *p;
    char *out;
    size_t i, n = 0, len;

    if (s == NULL || s->kind != PDF_STRING) {
        return NULL;
    }
    p = s->u.s.p;
    len = s->u.s.n;
    out = malloc(len * 3 + 1);
    if (out == NULL) {
        return NULL;
    }
    if (len >= 2 && ((p[0] == 0xfe && p[1] == 0xff) ||
                     (p[0] == 0xff && p[1] == 0xfe))) {
        int le = p[0] == 0xff;

        for (i = 2; i + 1 < len; i += 2) {
            unsigned long cp = le ? (unsigned long)p[i + 1] << 8 | p[i]
                                  : (unsigned long)p[i] << 8 | p[i + 1];

            if (cp >= 0xd800 && cp < 0xdc00 && i + 3 < len) {
                unsigned long lo = le ? (unsigned long)p[i + 3] << 8 | p[i + 2]
                                      : (unsigned long)p[i + 2] << 8 | p[i + 3];

                if (lo >= 0xdc00 && lo < 0xe000) {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    i += 2;
                }
            }
            if (cp < 0xd800 || cp >= 0xe000) {
                utf8_put(out, &n, cp);
            }
        }
    } else if (len >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) {
        for (i = 3; i < len; i++) {
            out[n++] = p[i] < 0x20 ? ' ' : (char)p[i];  /* PDF 2.0 UTF-8 */
        }
    } else {
        for (i = 0; i < len; i++) {
            unsigned cp = p[i];

            if (cp >= 0x18 && cp <= 0x1f) {
                cp = pdfdoc_marks[cp - 0x18];
            } else if (cp >= 0x80 && cp < 0xa0) {
                cp = pdfdoc_high[cp - 0x80];
            } else if (cp == 0xa0) {
                cp = 0x20ac;                /* the euro */
            }
            if (cp != 0) {
                utf8_put(out, &n, cp);
            }
        }
    }
    out[n] = '\0';
    return out;
}

char *
tiku_pdf_title(tiku_pdf_t *doc)
{
    pdf_obj_t *info = doc != NULL ? pdf_get(doc, doc->trailer, "Info") : NULL;

    return pdf_text_string(pdf_get(doc, info, "Title"));
}
