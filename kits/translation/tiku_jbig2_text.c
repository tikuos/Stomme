/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_jbig2_text.c - text in JBIG2: glyphs coded once in a symbol
 * dictionary (T.88 6.5) and placed along strips by a text region (6.4),
 * each instance as it is or refined against its symbol.
 *
 * Both come arithmetically coded or Huffman coded.  A dictionary may
 * build a symbol from others, a small text region of its own; the two
 * share their decoders then, as the standard says.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_jbig2_private.h"

#include <stdlib.h>
#include <string.h>

#define SYMS_MAX   (1L << 20)       /* symbols one dictionary or region */
#define INST_MAX   (1L << 24)       /* instances one region places      */

/** @brief The standard tables, built for one dictionary or region. */
typedef struct {
    jb_table_t t[16];               /* B.1 is t[1]                       */
    jb_line_t  lines[16][24];
} std_t;

/** @brief Symbol IDs Huffman coded, decoded canonically: by length, the
 *         first code and where its symbols start in index order. */
typedef struct {
    long      count[33], start[33];
    uint32_t  first[33];
    long     *sym;
} ids_t;

/** @brief How a text region is decoded (6.4.2), and what it places. */
typedef struct {
    int               huff, refine, logstrips, corner, transposed, op;
    int               dsoffset, rtmpl, rat[4], idlen;
    long              ninst;
    jb_bm_t         **sym;
    long              nsym;
    const jb_table_t *fs, *ds, *dt, *rdw, *rdh, *rdx, *rdy, *rsize;
    const ids_t      *ids;          /* NULL: IDs of idlen bits, as read  */
} text_t;

/** @brief Every standard table into @p s. */
static void
std_build(std_t *s)
{
    int k;

    for (k = 1; k <= 15; k++) {
        jb_table_std(k, &s->t[k], s->lines[k]);
    }
}

jb_ar_t *
jb_ar_new(int idlen)
{
    jb_ar_t *ar = calloc(1, sizeof *ar);

    if (ar == NULL) {
        return NULL;
    }
    ar->id = calloc((size_t)1 << idlen, sizeof *ar->id);
    ar->gb = calloc(65536, sizeof *ar->gb);
    ar->gr = calloc(8192, sizeof *ar->gr);
    if (ar->id == NULL || ar->gb == NULL || ar->gr == NULL) {
        jb_ar_free(ar);
        return NULL;
    }
    return ar;
}

void
jb_ar_free(jb_ar_t *ar)
{
    if (ar != NULL) {
        free(ar->id);
        free(ar->gb);
        free(ar->gr);
        free(ar);
    }
}

/** @brief floor(@p v / 2), as GRREFERENCEDX asks of a signed width. */
static long
half(long v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

/* ---------------------------------------------------------- symbol IDs  */

/**
 * @brief The Huffman codes of @p nsym symbol IDs (7.4.3.1.7): their
 *        lengths coded in runs, by 35 run codes whose own lengths come
 *        first, four bits each.  @return 0, or -1.
 */
static int
ids_read(jb_bits_t *b, long nsym, ids_t *d)
{
    jb_line_t run[35];
    jb_table_t rt;
    unsigned char *len = malloc((size_t)nsym + 1);
    long i = 0, k, at[33];
    uint32_t v;
    int32_t rc;

    rt.n = 35;
    rt.line = run;
    memset(d, 0, sizeof *d);
    d->sym = malloc(((size_t)nsym + 1) * sizeof *d->sym);
    for (k = 0; k < 35 && len != NULL && d->sym != NULL; k++) {
        if (jb_bits(b, 4, &v)) {
            break;
        }
        run[k].prefl = (int)v;
        run[k].rangel = 0;
        run[k].kind = JB_LINE;
        run[k].low = (int32_t)k;
    }
    if (k < 35) {
        free(len);
        return -1;
    }
    jb_table_codes(&rt);
    while (i < nsym) {
        if (jb_huff(b, &rt, &rc) != 0) {
            free(len);
            return -1;
        }
        if (rc < 32) {
            len[i++] = (unsigned char)rc;
            continue;
        }
        if ((rc == 32 && i == 0) ||
            jb_bits(b, rc == 32 ? 2 : rc == 33 ? 3 : 7, &v)) {
            free(len);
            return -1;
        }
        for (k = 0; k < (long)v + (rc == 34 ? 11 : 3) && i < nsym; k++, i++) {
            len[i] = rc == 32 ? len[i - 1] : 0;
        }
    }
    jb_align(b);
    for (i = 0; i < nsym; i++) {
        d->count[len[i]]++;
    }
    d->count[0] = 0;
    for (k = 1, at[0] = 0; k <= 32; k++) {
        d->first[k] = (d->first[k - 1] + (uint32_t)d->count[k - 1]) << 1;
        d->start[k] = d->start[k - 1] + d->count[k - 1];
        at[k] = d->start[k];
    }
    for (i = 0; i < nsym; i++) {
        if (len[i] > 0) {
            d->sym[at[len[i]]++] = i;
        }
    }
    free(len);
    return 0;
}

/** @brief A symbol ID by @p d's codes.  @return it, or -1. */
static long
ids_get(jb_bits_t *b, const ids_t *d)
{
    uint32_t code = 0, bit;
    int k;

    for (k = 1; k <= 32; k++) {
        if (jb_bits(b, 1, &bit)) {
            return -1;
        }
        code = code << 1 | bit;
        if (d->count[k] > 0 && code >= d->first[k] &&
            code - d->first[k] < (uint32_t)d->count[k]) {
            return d->sym[d->start[k] + (long)(code - d->first[k])];
        }
    }
    return -1;
}

/* -------------------------------------------------------- text regions  */

/** @brief A value of the text region's fields: @p ia arithmetically, by
 *         table @p tab Huffman coded.  @return 0, 1 for OOB, -1. */
static int
field(const text_t *t, jb_ar_t *ar, jb_bits_t *b, jb_iax_t *ia,
      const jb_table_t *tab, int32_t *v)
{
    if (t->huff) {
        return jb_huff(b, tab, v);
    }
    return jb_int(&ar->mq, ia, v);
}

/**
 * @brief Instance refinement (6.4.11): the symbol's bitmap as its
 *        refinement codes it against the symbol, sizes and offsets
 *        first; Huffman coded, the refinement is BMSIZE bytes of its
 *        own arithmetic code.
 */
static jb_bm_t *
refine_sym(jb_dec_t *jd, const text_t *t, jb_ar_t *ar, jb_bits_t *b,
           const jb_bm_t *ibo)
{
    int32_t rdw, rdh, rdx, rdy, size = 0;
    jb_ref_t r;
    jb_bm_t *ib;
    tiku_mq_t own;

    if (field(t, ar, b, &ar->rdw, t->rdw, &rdw) ||
        field(t, ar, b, &ar->rdh, t->rdh, &rdh) ||
        field(t, ar, b, &ar->rdx, t->rdx, &rdx) ||
        field(t, ar, b, &ar->rdy, t->rdy, &rdy) ||
        (t->huff && jb_huff(b, t->rsize, &size)) ||
        (ib = jb_new(jd, (long)ibo->w + rdw, (long)ibo->h + rdh, 0)) ==
            NULL) {
        return NULL;
    }
    memset(&r, 0, sizeof r);
    r.tmpl = t->rtmpl;
    memcpy(r.at, t->rat, sizeof r.at);
    r.ref = ibo;
    r.dx = half(rdw) + rdx;
    r.dy = half(rdh) + rdy;
    if (!t->huff) {
        return jb_refine(&ar->mq, ar->gr, &r, ib) == 0 ? ib : NULL;
    }
    jb_align(b);
    if (size < 0 || size > b->n - b->bit / 8) {
        return NULL;
    }
    memset(ar->gr, 0, 8192 * sizeof *ar->gr);
    tiku_mq_init(&own, b->p + b->bit / 8, size);
    b->bit += (long)size * 8;
    return jb_refine(&own, ar->gr, &r, ib) == 0 ? ib : NULL;
}

/**
 * @brief Symbol @p ib drawn with its reference corner at S @p *curs and
 *        T @p tt, or at T and S transposed (6.4.5 c vi to x); S moves
 *        past it before or after, as the corner says.
 */
static void
place_sym(const text_t *t, jb_bm_t *reg, const jb_bm_t *ib, long *curs,
          long tt)
{
    int right = t->corner == 2 || t->corner == 3;
    int bottom = t->corner == 0 || t->corner == 2;
    long x, y;

    if (!t->transposed && right) {
        *curs += ib->w - 1;
    } else if (t->transposed && bottom) {
        *curs += ib->h - 1;
    }
    x = t->transposed ? tt : *curs;
    y = t->transposed ? *curs : tt;
    jb_compose(reg, ib, right ? x - (ib->w - 1) : x,
               bottom ? y - (ib->h - 1) : y, t->op);
    if (!t->transposed && !right) {
        *curs += ib->w - 1;
    } else if (t->transposed && !bottom) {
        *curs += ib->h - 1;
    }
}

/** @brief One instance, its S already in @p curs: T, ID, refinement,
 *         then drawn.  @return 0, or -1. */
static int
instance(jb_dec_t *jd, const text_t *t, jb_ar_t *ar, jb_bits_t *b,
         jb_bm_t *reg, long *curs, long stript)
{
    int32_t curt = 0, ri = 0;
    uint32_t bits;
    long id;
    const jb_bm_t *ib;

    if (t->logstrips > 0) {
        if (t->huff ? jb_bits(b, t->logstrips, &bits)
                    : jb_int(&ar->mq, &ar->it, &curt)) {
            return -1;
        }
        curt = t->huff ? (int32_t)bits : curt;
    }
    if (!t->huff) {
        id = jb_iaid(&ar->mq, ar->id, t->idlen);
    } else if (t->ids != NULL) {
        id = ids_get(b, t->ids);
    } else {
        id = jb_bits(b, t->idlen, &bits) ? -1 : (long)bits;
    }
    if (id < 0 || id >= t->nsym) {
        return -1;
    }
    if (t->refine && t->huff) {
        if (jb_bits(b, 1, &bits)) {
            return -1;
        }
        ri = (int32_t)bits;
    } else if (t->refine && jb_int(&ar->mq, &ar->ri, &ri)) {
        return -1;
    }
    ib = ri ? refine_sym(jd, t, ar, b, t->sym[id]) : t->sym[id];
    if (ib == NULL || jb_work(jd, (long long)ib->w * ib->h)) {
        return -1;
    }
    place_sym(t, reg, ib, curs, stript + curt);
    return 0;
}

/**
 * @brief The text region decoding procedure (6.4.5): strips down the
 *        region, each an S to start and then instances until the S
 *        step codes OOB.  @return 0, or -1 when the data gives out.
 */
static int
text_decode(jb_dec_t *jd, const text_t *t, jb_ar_t *ar, jb_bits_t *b,
            jb_bm_t *reg)
{
    long strips = 1L << t->logstrips, stript, firsts = 0, curs, done = 0;
    int32_t v;
    int r;

    if (field(t, ar, b, &ar->dt, t->dt, &v) != 0) {
        return -1;
    }
    stript = -(long)v * strips;
    while (done < t->ninst) {
        if (field(t, ar, b, &ar->dt, t->dt, &v) != 0) {
            return -1;
        }
        stript += (long)v * strips;
        if (field(t, ar, b, &ar->fs, t->fs, &v) != 0) {
            return -1;
        }
        firsts += v;
        curs = firsts;
        for (;;) {
            if (instance(jd, t, ar, b, reg, &curs, stript) != 0 ||
                ++done > INST_MAX || (!t->huff && tiku_mq_spent(&ar->mq))) {
                return -1;
            }
            r = field(t, ar, b, &ar->ds, t->ds, &v);
            if (r == 1) {
                break;              /* OOB: the strip is done */
            }
            if (r != 0) {
                return -1;
            }
            curs += (long)v + t->dsoffset;
        }
    }
    return 0;
}

/** @brief Symbols of the dictionaries @p s refers to, in their order;
 *         @return how many, or -1. */
static long
gather(jb_dec_t *jd, const jb_seg_t *s, jb_bm_t ***out)
{
    long n = 0, k;
    int i;

    *out = NULL;
    for (i = 0; i < s->nref; i++) {
        const jb_seg_t *d = jb_referred(jd, s, i);

        if (d != NULL && d->type == 0) {
            n += d->nsym;
        }
    }
    if (n > SYMS_MAX || (*out = malloc(((size_t)n + 1) * sizeof **out)) ==
                        NULL) {
        return -1;
    }
    for (i = 0, n = 0; i < s->nref; i++) {
        const jb_seg_t *d = jb_referred(jd, s, i);

        for (k = 0; d != NULL && d->type == 0 && k < d->nsym; k++) {
            (*out)[n++] = d->sym[k];
        }
    }
    return n;
}

/** @brief The next table segment @p s refers to after the @p *used
 *         taken so far, or NULL. */
static const jb_table_t *
custom(jb_dec_t *jd, const jb_seg_t *s, int *used)
{
    int i, seen = 0;

    for (i = 0; i < s->nref; i++) {
        const jb_seg_t *d = jb_referred(jd, s, i);

        if (d != NULL && d->type == 53 && seen++ == *used) {
            (*used)++;
            return d->table.n > 0 ? &d->table : NULL;
        }
    }
    return NULL;
}

/** @brief The table a two-bit selector picks: standard B.@p a, B.@p b
 *         or B.@p c for 0, 1 and 2, the next custom one for 3. */
static const jb_table_t *
pick(jb_dec_t *jd, const jb_seg_t *s, const std_t *st, int sel, int a,
     int b, int c, int *used)
{
    int k = sel == 0 ? a : sel == 1 ? b : sel == 2 ? c : 0;

    return sel == 3 ? custom(jd, s, used) : k > 0 ? &st->t[k] : NULL;
}

/** @brief A text region's Huffman tables (7.4.3.1.2).  @return 0 when
 *         each it needs is there. */
static int
text_tables(jb_dec_t *jd, const jb_seg_t *s, const std_t *st, unsigned f,
            text_t *t)
{
    int used = 0;

    t->fs = pick(jd, s, st, f & 3, 6, 7, 0, &used);
    t->ds = pick(jd, s, st, (f >> 2) & 3, 8, 9, 10, &used);
    t->dt = pick(jd, s, st, (f >> 4) & 3, 11, 12, 13, &used);
    t->rdw = pick(jd, s, st, (f >> 6) & 3, 14, 15, 0, &used);
    t->rdh = pick(jd, s, st, (f >> 8) & 3, 14, 15, 0, &used);
    t->rdx = pick(jd, s, st, (f >> 10) & 3, 14, 15, 0, &used);
    t->rdy = pick(jd, s, st, (f >> 12) & 3, 14, 15, 0, &used);
    t->rsize = (f >> 14) & 1 ? custom(jd, s, &used) : &st->t[1];
    return t->fs && t->ds && t->dt && (!t->refine || (t->rdw && t->rdh &&
           t->rdx && t->rdy && t->rsize)) ? 0 : -1;
}

/** @brief The text region's flags (7.4.3.1.1) into @p t. */
static void
text_flags(unsigned f, text_t *t)
{
    t->huff = f & 1;
    t->refine = (f >> 1) & 1;
    t->logstrips = (f >> 2) & 3;
    t->corner = (f >> 4) & 3;
    t->transposed = (f >> 6) & 1;
    t->op = (f >> 7) & 3;
    t->dsoffset = (int)((f >> 10) & 31) - ((f >> 14) & 1 ? 32 : 0);
    t->rtmpl = (f >> 15) & 1;
}

/**
 * @brief The text region segment's header after its region information
 *        (7.4.3.1): flags, Huffman flags, refinement AT pixels and the
 *        instances' count.  @return where its coded data starts, or -1.
 */
static long
text_head(const unsigned char *p, long n, text_t *t, unsigned *f,
          unsigned *hf)
{
    long at = 19;
    int i;

    *f = (unsigned)p[17] << 8 | p[18];
    text_flags(*f, t);
    if (t->huff) {
        if (at + 2 > n) {
            return -1;
        }
        *hf = (unsigned)p[at] << 8 | p[at + 1];
        at += 2;
    }
    if (t->refine && t->rtmpl == 0) {
        if (at + 4 > n) {
            return -1;
        }
        for (i = 0; i < 4; i++) {
            t->rat[i] = (signed char)p[at + i];
        }
        at += 4;
    }
    if (at + 4 > n) {
        return -1;
    }
    t->ninst = (long)jb_be32(p + at);
    return at + 4;
}

/**
 * @brief The region's instances decoded into @p reg from the data at
 *        @p at: Huffman coded by its tables and symbol codes, or
 *        arithmetically.  @return 0, -1 when the data gives out, -2 when
 *        decoding could not begin.
 */
static int
text_run(jb_dec_t *jd, const jb_seg_t *s, text_t *t, unsigned hf, long at,
         jb_bm_t *reg)
{
    std_t *st = NULL;
    ids_t ids;
    jb_ar_t *ar = NULL;
    jb_bits_t b;
    int rc = -2;

    memset(&ids, 0, sizeof ids);
    b.p = s->data;
    b.n = s->len;
    b.bit = at * 8;
    if (t->huff && (st = malloc(sizeof *st)) != NULL) {
        std_build(st);
        if (text_tables(jd, s, st, hf, t) == 0 &&
            ids_read(&b, t->nsym, &ids) == 0) {
            t->ids = &ids;
            ar = jb_ar_new(0);      /* its refinements' contexts */
        }
    } else if (!t->huff && (ar = jb_ar_new(t->idlen)) != NULL) {
        tiku_mq_init(&ar->mq, s->data + at, s->len - at);
    }
    if (ar != NULL) {
        rc = text_decode(jd, t, ar, &b, reg);
    }
    jb_ar_free(ar);
    free(ids.sym);
    free(st);
    return rc;
}

int
jb_text(jb_dec_t *jd, jb_seg_t *s)
{
    jb_rinfo_t ri;
    text_t t;
    unsigned f = 0, hf = 0;
    long at;
    jb_bm_t *reg = NULL;
    int rc = -2;

    memset(&t, 0, sizeof t);
    if (jb_rinfo(s->data, s->len, &ri) || s->len < 19 ||
        (at = text_head(s->data, s->len, &t, &f, &hf)) < 0 ||
        (t.nsym = gather(jd, s, &t.sym)) < 0) {
        free(t.sym);
        return -1;
    }
    t.idlen = jb_log2((uint32_t)t.nsym);
    if (t.ninst <= INST_MAX &&
        (reg = jb_new(jd, ri.w, ri.h, (int)((f >> 9) & 1))) != NULL) {
        rc = text_run(jd, s, &t, hf, at, reg);
    }
    if (rc != -2) {
        jb_place(jd, s, reg, &ri);  /* what decoded, even when cut short */
    }
    free(t.sym);
    return rc == 0 ? 0 : -1;
}

/* ------------------------------------------------------ symbol dicts  */

/** @brief What a symbol dictionary is coded with (7.4.2.1). */
typedef struct {
    int         huff, refagg, tmpl, rtmpl, at[8], rat[4], idlen;
    long        nin, nnew, nexp;
    jb_bm_t   **sym;                /* the input symbols, then the new  */
    const jb_table_t *dh, *dw, *bmsize, *agginst;
    std_t      *st;
} dict_t;

/** @brief A field of the dictionary: @p ia arithmetically, by @p tab
 *         Huffman coded.  @return 0, 1 for OOB, -1. */
static int
dfield(const dict_t *d, jb_ar_t *ar, jb_bits_t *b, jb_iax_t *ia,
       const jb_table_t *tab, int32_t *v)
{
    return d->huff ? jb_huff(b, tab, v) : jb_int(&ar->mq, ia, v);
}

/**
 * @brief One symbol refined against one before it (6.5.8.2.2): which,
 *        and by what offset, then the refinement into @p bm; Huffman
 *        coded, that is BMSIZE bytes of arithmetic code of its own.
 */
static jb_bm_t *
one_refined(const dict_t *d, const text_t *t, jb_ar_t *ar, jb_bits_t *b,
            jb_bm_t *bm)
{
    int32_t rdx, rdy, size = 0;
    uint32_t bits = 0;
    long id;
    jb_ref_t r;
    tiku_mq_t own;

    if (d->huff && jb_bits(b, d->idlen, &bits)) {
        return NULL;
    }
    id = d->huff ? (long)bits : jb_iaid(&ar->mq, ar->id, d->idlen);
    if (id < 0 || id >= t->nsym ||
        dfield(d, ar, b, &ar->rdx, t->rdx, &rdx) ||
        dfield(d, ar, b, &ar->rdy, t->rdy, &rdy) ||
        (d->huff && jb_huff(b, t->rsize, &size))) {
        return NULL;
    }
    memset(&r, 0, sizeof r);
    r.tmpl = d->rtmpl;
    memcpy(r.at, d->rat, sizeof r.at);
    r.ref = d->sym[id];
    r.dx = rdx;
    r.dy = rdy;
    if (!d->huff) {
        return jb_refine(&ar->mq, ar->gr, &r, bm) == 0 ? bm : NULL;
    }
    jb_align(b);
    if (size < 0 || size > b->n - b->bit / 8) {
        return NULL;
    }
    memset(ar->gr, 0, 8192 * sizeof *ar->gr);
    tiku_mq_init(&own, b->p + b->bit / 8, size);
    b->bit += (long)size * 8;
    return jb_refine(&own, ar->gr, &r, bm) == 0 ? bm : NULL;
}

/**
 * @brief A symbol built from others (6.5.8.2): one refined against a
 *        symbol before it, or several placed as a small text region
 *        that shares the dictionary's decoders.
 */
static jb_bm_t *
aggregate(jb_dec_t *jd, const dict_t *d, jb_ar_t *ar, jb_bits_t *b,
          long w, long h, long have)
{
    text_t t;
    int32_t n;
    jb_bm_t *bm;

    if (dfield(d, ar, b, &ar->ai, d->agginst, &n) != 0 || n < 1 ||
        (bm = jb_new(jd, w, h, 0)) == NULL) {
        return NULL;
    }
    memset(&t, 0, sizeof t);
    t.huff = d->huff;
    t.refine = 1;
    t.corner = 1;                   /* TOPLEFT */
    t.rtmpl = d->rtmpl;
    memcpy(t.rat, d->rat, sizeof t.rat);
    t.idlen = d->idlen;
    t.sym = d->sym;
    t.nsym = have;
    t.ninst = n;
    t.fs = &d->st->t[6];
    t.ds = &d->st->t[8];
    t.dt = &d->st->t[11];
    t.rdw = t.rdh = t.rdx = t.rdy = &d->st->t[15];
    t.rsize = &d->st->t[1];
    if (n > 1) {
        return text_decode(jd, &t, ar, b, bm) == 0 ? bm : NULL;
    }
    return one_refined(d, &t, ar, b, bm);
}

/**
 * @brief A height class's symbols Huffman coded together (6.5.9): one
 *        bitmap, stored or MMR coded, cut at their widths into
 *        @p out[@p from] to @p out[@p to - 1].
 */
static int
collective(jb_dec_t *jd, const dict_t *d, jb_bits_t *b, long h, long from,
           long to, const long *widths, jb_bm_t **out)
{
    int32_t size;
    long total = 0, i, x = 0, at, rb, row, col;
    jb_bm_t *all;

    for (i = from; i < to; i++) {
        total += widths[i];
    }
    if (jb_huff(b, d->bmsize, &size) != 0 || size < 0) {
        return -1;
    }
    jb_align(b);
    at = b->bit / 8;
    rb = (total + 7) / 8;
    if ((all = jb_new(jd, total, h, 0)) == NULL ||
        (size == 0 ? rb * h : size) > b->n - at ||
        (size > 0 && jb_generic_mmr(b->p + at, size, all) < 0)) {
        return -1;
    }
    for (row = 0; size == 0 && row < h; row++) {
        for (col = 0; col < total; col++) {
            all->px[row * total + col] = (unsigned char)
                ((b->p[at + row * rb + col / 8] >> (7 - col % 8)) & 1u);
        }
    }
    b->bit += (size == 0 ? rb * h : size) * 8;
    for (i = from; i < to; x += widths[i++]) {
        if ((out[i] = jb_new(jd, widths[i], h, 0)) == NULL) {
            return -1;
        }
        for (row = 0; row < h; row++) {
            memcpy(out[i]->px + row * widths[i], all->px + row * total + x,
                   (size_t)widths[i]);
        }
    }
    return 0;
}

/** @brief One new symbol of @p w by @p h: coded alone, or built from
 *         others.  @return it, or NULL. */
static jb_bm_t *
new_symbol(jb_dec_t *jd, const dict_t *d, jb_ar_t *ar, jb_bits_t *b,
           long w, long h, long have)
{
    jb_gen_t g;
    jb_bm_t *bm;

    if (d->refagg) {
        return aggregate(jd, d, ar, b, w, h, have);
    }
    memset(&g, 0, sizeof g);
    g.tmpl = d->tmpl;
    memcpy(g.at, d->at, sizeof g.at);
    bm = jb_new(jd, w, h, 0);
    return bm != NULL && jb_generic(&ar->mq, ar->gb, &g, bm) == 0 ? bm : NULL;
}

/**
 * @brief The new symbols (6.5.5 step 4): height classes, each a change
 *        of height and then widths until OOB, a bitmap for each or,
 *        Huffman coded without aggregation, one for the class.
 *        @return 0, or -1.
 */
static int
dict_decode(jb_dec_t *jd, dict_t *d, jb_ar_t *ar, jb_bits_t *b)
{
    long done = 0, first, classes = 0, h = 0, w, *widths;
    jb_bm_t **fresh = d->sym + d->nin;
    int32_t v;
    int r = 0;

    widths = malloc(((size_t)d->nnew + 1) * sizeof *widths);
    while (widths != NULL && r == 0 && done < d->nnew) {
        if (++classes > d->nnew + 16 ||
            dfield(d, ar, b, &ar->dh, d->dh, &v) != 0 || (h += v) < 0) {
            break;
        }
        for (w = 0, first = done;;) {
            if ((r = dfield(d, ar, b, &ar->dw, d->dw, &v)) != 0) {
                r = r == 1 ? 0 : -1;    /* OOB: the class is done */
                break;
            }
            if (done >= d->nnew || (w += v) < 0 || w > 0xFFFFFF ||
                (!d->huff && tiku_mq_spent(&ar->mq))) {
                r = -1;
                break;
            }
            widths[done] = w;
            if ((!d->huff || d->refagg) &&
                (fresh[done] = new_symbol(jd, d, ar, b, w, h,
                                          d->nin + done)) == NULL) {
                r = -1;
                break;
            }
            done++;
        }
        if (r == 0 && d->huff && !d->refagg) {
            r = collective(jd, d, b, h, first, done, widths, fresh);
        }
    }
    free(widths);
    return widths != NULL && r == 0 && done == d->nnew ? 0 : -1;
}

/**
 * @brief Which symbols the dictionary exports (6.5.10): runs, not and
 *        then so by turns, over its input symbols and then its new.
 */
static int
exports(const dict_t *d, jb_ar_t *ar, jb_bits_t *b, jb_seg_t *s)
{
    long total = d->nin + d->nnew, at = 0, k, turns = 0;
    int flag = 0;
    int32_t run;

    s->nsym = 0;
    s->sym = malloc(((size_t)total + 1) * sizeof *s->sym);
    while (s->sym != NULL && at < total) {
        if (++turns > 2 * total + 4 ||
            dfield(d, ar, b, &ar->ex, &d->st->t[1], &run) != 0 ||
            run < 0 || run > total - at) {
            return -1;
        }
        for (k = 0; k < run; k++, at++) {
            if (flag && d->sym[at] != NULL) {
                s->sym[s->nsym++] = d->sym[at];
            }
        }
        flag = !flag;
    }
    return s->sym != NULL ? 0 : -1;
}

/** @brief The dictionary's flags and fields (7.4.2.1) into @p d.
 *  @return where its coded data starts, or -1. */
static long
dict_head(const unsigned char *p, long n, dict_t *d)
{
    unsigned f = (unsigned)p[0] << 8 | p[1];
    long at = 2;
    int i, nat;

    d->huff = f & 1;
    d->refagg = (f >> 1) & 1;
    d->tmpl = (f >> 10) & 3;
    d->rtmpl = (f >> 12) & 1;
    nat = d->huff ? 0 : d->tmpl == 0 ? 8 : 2;
    for (i = 0; i < nat && at + i < n; i++) {
        d->at[i] = (signed char)p[at + i];
    }
    at += nat;
    if (d->refagg && d->rtmpl == 0) {
        for (i = 0; i < 4 && at + i < n; i++) {
            d->rat[i] = (signed char)p[at + i];
        }
        at += 4;
    }
    if (at + 8 > n) {
        return -1;
    }
    d->nexp = (long)jb_be32(p + at);
    d->nnew = (long)jb_be32(p + at + 4);
    return at + 8;
}

/** @brief The dictionary's Huffman tables (7.4.2.1.1).  @return 0 when
 *         each is there. */
static int
dict_tables(jb_dec_t *jd, const jb_seg_t *s, unsigned f, dict_t *d)
{
    int used = 0;

    d->dh = pick(jd, s, d->st, (f >> 2) & 3, 4, 5, 0, &used);
    d->dw = pick(jd, s, d->st, (f >> 4) & 3, 2, 3, 0, &used);
    d->bmsize = (f >> 6) & 1 ? custom(jd, s, &used) : &d->st->t[1];
    d->agginst = (f >> 7) & 1 ? custom(jd, s, &used) : &d->st->t[1];
    return !d->huff || (d->dh && d->dw && d->bmsize && d->agginst) ? 0 : -1;
}

/** @brief The contexts the last dictionary @p s refers to kept, into
 *         @p ar, when @p s says it goes on from them. */
static void
dict_resume(jb_dec_t *jd, const jb_seg_t *s, jb_ar_t *ar)
{
    int i;

    for (i = s->nref - 1; i >= 0; i--) {
        const jb_seg_t *d = jb_referred(jd, s, i);

        if (d != NULL && d->type == 0) {
            if (d->gbkeep != NULL) {
                memcpy(ar->gb, d->gbkeep, 65536 * sizeof *ar->gb);
            }
            if (d->grkeep != NULL) {
                memcpy(ar->gr, d->grkeep, 8192 * sizeof *ar->gr);
            }
            return;
        }
    }
}

/** @brief The contexts @p ar ends with, kept with @p s for the next. */
static void
dict_keep(jb_seg_t *s, const jb_ar_t *ar)
{
    s->gbkeep = malloc(65536 * sizeof *s->gbkeep);
    s->grkeep = malloc(8192 * sizeof *s->grkeep);
    if (s->gbkeep != NULL) {
        memcpy(s->gbkeep, ar->gb, 65536 * sizeof *s->gbkeep);
    }
    if (s->grkeep != NULL) {
        memcpy(s->grkeep, ar->gr, 8192 * sizeof *s->grkeep);
    }
}

int
jb_symbols(jb_dec_t *jd, jb_seg_t *s)
{
    const unsigned char *p = s->data;
    unsigned f = s->len >= 2 ? (unsigned)p[0] << 8 | p[1] : 0;
    long at;
    dict_t d;
    jb_bm_t **in = NULL;
    jb_ar_t *ar = NULL;
    jb_bits_t b;
    int rc = -1;

    memset(&d, 0, sizeof d);
    if (s->len < 2 || (at = dict_head(p, s->len, &d)) < 0 ||
        (d.nin = gather(jd, s, &in)) < 0 || d.nnew > SYMS_MAX ||
        d.nin + d.nnew > SYMS_MAX ||
        (d.sym = calloc((size_t)(d.nin + d.nnew) + 1, sizeof *d.sym)) ==
            NULL ||
        (d.st = malloc(sizeof *d.st)) == NULL) {
        free(in);
        free(d.sym);
        return -1;
    }
    memcpy(d.sym, in, (size_t)d.nin * sizeof *d.sym);
    d.idlen = jb_log2((uint32_t)(d.nin + d.nnew));
    std_build(d.st);
    if (dict_tables(jd, s, f, &d) == 0 && (ar = jb_ar_new(d.idlen)) != NULL) {
        if ((f >> 8) & 1) {
            dict_resume(jd, s, ar);
        }
        tiku_mq_init(&ar->mq, p + at, s->len - at);
        b.p = p;
        b.n = s->len;
        b.bit = at * 8;
        rc = dict_decode(jd, &d, ar, &b) == 0 && exports(&d, ar, &b, s) == 0
             ? 0 : -1;
        if (rc == 0 && ((f >> 9) & 1)) {
            dict_keep(s, ar);
        }
    }
    jb_ar_free(ar);
    free(d.st);
    free(d.sym);
    free(in);
    return rc;
}
