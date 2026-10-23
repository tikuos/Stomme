/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_diff.c - Myers' difference in linear space, over numbered lines.
 *
 * E. W. Myers, "An O(ND) Difference Algorithm and Its Variations",
 * Algorithmica 1 (1986), section 4b: a forward and a backward search meet
 * on a middle snake, each half is solved the same way, and no more than
 * two vectors of diagonals are ever held.  Written from the paper.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_diff.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIFF_DEPTH 256          /* splits deep before a range is settled */

/* ------------------------------------------------------------ lines  */

long
tiku_diff_split(const char *text, size_t len, tiku_diff_line_t **lines)
{
    size_t i, start = 0;
    long n = 0, k = 0;

    for (i = 0; i < len; i++)
        if (text[i] == '\n') n++;
    if (len > 0 && text[len - 1] != '\n') n++;
    *lines = malloc((size_t)(n > 0 ? n : 1) * sizeof **lines);
    if (*lines == NULL) return -1;
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            (*lines)[k].text = text + start;
            (*lines)[k++].len = i + 1 - start;
            start = i + 1;
        }
    }
    if (start < len) {
        (*lines)[k].text = text + start;
        (*lines)[k++].len = len - start;
    }
    return k;
}

/** @brief Whether @p c is a blank that TIKU_DIFF_SPACE passes over. */
static int
blank(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/** @brief A line read as the flags see it, a byte at a time. */
typedef struct {
    const unsigned char *p;
    size_t i, end;              /* the next byte, and where the text
                                   compared ends                     */
    int lf, flags;              /* an LF still to come; the flags    */
} reader_t;

/** @brief @p r set to read @p l: the LF kept for last, a CR before it
 *         dropped when the flags say. */
static void
reader_start(reader_t *r, const tiku_diff_line_t *l, int flags)
{
    r->p = (const unsigned char *)l->text;
    r->i = 0;
    r->end = l->len;
    r->lf = r->end > 0 && r->p[r->end - 1] == '\n';
    if (r->lf) r->end--;
    if ((flags & TIKU_DIFF_CR) && r->end > 0 && r->p[r->end - 1] == '\r')
        r->end--;
    r->flags = flags;
}

/** @brief The next byte that counts: under TIKU_DIFF_SPACE a run of blanks
 *         is one space and blanks at the end are none; the LF comes last.
 *  @return it, or -1 at the end. */
static int
reader_next(reader_t *r)
{
    if (r->i < r->end) {
        int c = r->p[r->i];

        if (!(r->flags & TIKU_DIFF_SPACE) || !blank(c)) {
            r->i++;
            return c;
        }
        while (r->i < r->end && blank(r->p[r->i])) r->i++;
        if (r->i < r->end) return ' ';
    }
    if (r->lf) {
        r->lf = 0;
        return '\n';
    }
    return -1;
}

/** @brief FNV-1a over what counts of @p l. */
static uint32_t
line_hash(const tiku_diff_line_t *l, int flags)
{
    reader_t r;
    uint32_t h = 2166136261u;
    int c;

    reader_start(&r, l, flags);
    while ((c = reader_next(&r)) >= 0) h = (h ^ (uint32_t)c) * 16777619u;
    return h;
}

/** @brief Whether @p x and @p y are the same line as the flags see them. */
static int
line_same(const tiku_diff_line_t *x, const tiku_diff_line_t *y, int flags)
{
    reader_t a, b;
    int c;

    if (!(flags & (TIKU_DIFF_SPACE | TIKU_DIFF_CR)))
        return x->len == y->len && memcmp(x->text, y->text, x->len) == 0;
    reader_start(&a, x, flags);
    reader_start(&b, y, flags);
    do {
        c = reader_next(&a);
        if (c != reader_next(&b)) return 0;
    } while (c >= 0);
    return 1;
}

/** @brief Whether @p l holds nothing but blanks and its LF. */
static int
line_blank(const tiku_diff_line_t *l)
{
    size_t i;

    for (i = 0; i < l->len; i++)
        if (!blank((unsigned char)l->text[i]) && l->text[i] != '\n') return 0;
    return 1;
}

/**
 * @brief Every line of both sides numbered by its content, a's then b's
 *        into @p id, equal lines alike, in one open-addressed table.
 * @return how many different lines there are, or -1 out of memory.
 */
static long
number_lines(const tiku_diff_line_t *a, long na, const tiku_diff_line_t *b,
             long nb, int flags, uint32_t *id)
{
    size_t cap = 16, s;
    long *slot, k, classes = 0;
    uint32_t *hash;

    while (cap < 2u * (size_t)(na + nb)) cap <<= 1;
    slot = calloc(cap, sizeof *slot);       /* 1 + a line of the class */
    hash = malloc((size_t)(na + nb + 1) * sizeof *hash);
    if (slot == NULL || hash == NULL) {
        free(slot);
        free(hash);
        return -1;
    }
    for (k = 0; k < na + nb; k++) {
        const tiku_diff_line_t *l = k < na ? &a[k] : &b[k - na];

        hash[k] = line_hash(l, flags);
        for (s = hash[k] & (cap - 1);; s = (s + 1) & (cap - 1)) {
            long f = slot[s] - 1;

            if (f < 0) {
                slot[s] = k + 1;
                id[k] = (uint32_t)classes++;
                break;
            }
            if (hash[f] == hash[k] &&
                line_same(l, f < na ? &a[f] : &b[f - na], flags)) {
                id[k] = id[f];
                break;
            }
        }
    }
    free(slot);
    free(hash);
    return classes;
}

/* ------------------------------------------------------------ Myers  */

/** @brief The search over two numbered sides. */
typedef struct {
    const uint32_t *x, *y;
    char *del, *ins;            /* what it marks changed, per side    */
    long *fd, *bd;              /* furthest x on each diagonal, both ways */
    long bound;                 /* steps before a split settles       */
    int  cut;                   /* one did, or a range was settled    */
} myers_t;

/** @brief The box being split, and the diagonals each search holds. */
typedef struct {
    long xl, xh, yl, yh;
    long fmin, fmax, bmin, bmax;
} box_t;

/** @brief One more edit forwards on every diagonal the search holds.
 *  @return 1 when it met the backward search, the place in @p xm, @p ym. */
static int
forward(myers_t *m, box_t *s, long dmin, long dmax, int odd, long *xm,
        long *ym)
{
    long *fd = m->fd, d;

    if (s->fmin > dmin) fd[--s->fmin - 1] = -1; else s->fmin++;
    if (s->fmax < dmax) fd[++s->fmax + 1] = -1; else s->fmax--;
    for (d = s->fmax; d >= s->fmin; d -= 2) {
        long x = fd[d - 1] >= fd[d + 1] ? fd[d - 1] + 1 : fd[d + 1];
        long y = x - d;

        while (x < s->xh && y < s->yh && m->x[x] == m->y[y]) x++, y++;
        fd[d] = x;
        if (odd && s->bmin <= d && d <= s->bmax && m->bd[d] <= x) {
            *xm = x;
            *ym = y;
            return 1;
        }
    }
    return 0;
}

/** @brief One more edit backwards, as forward() is the other way.
 *  @return 1 when it met the forward search. */
static int
backward(myers_t *m, box_t *s, long dmin, long dmax, int odd, long *xm,
         long *ym)
{
    long *bd = m->bd, d;

    if (s->bmin > dmin) bd[--s->bmin - 1] = LONG_MAX; else s->bmin++;
    if (s->bmax < dmax) bd[++s->bmax + 1] = LONG_MAX; else s->bmax--;
    for (d = s->bmax; d >= s->bmin; d -= 2) {
        long x = bd[d - 1] < bd[d + 1] ? bd[d - 1] : bd[d + 1] - 1;
        long y = x - d;

        while (x > s->xl && y > s->yl && m->x[x - 1] == m->y[y - 1]) x--, y--;
        bd[d] = x;
        if (!odd && s->fmin <= d && d <= s->fmax && x <= m->fd[d]) {
            *xm = x;
            *ym = y;
            return 1;
        }
    }
    return 0;
}

/** @brief Past the step bound: split where either search got furthest,
 *         a correct place if not the one the shortest script needs. */
static void
settle(const myers_t *m, const box_t *s, long *xm, long *ym)
{
    long d, x, y, best = -1;

    for (d = s->fmax; d >= s->fmin; d -= 2) {
        x = m->fd[d] < s->xh ? m->fd[d] : s->xh;
        y = x - d;
        if (y > s->yh) { y = s->yh; x = y + d; }
        if ((x + y) - (s->xl + s->yl) > best) {
            best = (x + y) - (s->xl + s->yl);
            *xm = x;
            *ym = y;
        }
    }
    for (d = s->bmax; d >= s->bmin; d -= 2) {
        x = m->bd[d] > s->xl ? m->bd[d] : s->xl;
        y = x - d;
        if (y < s->yl) { y = s->yl; x = y + d; }
        if ((s->xh + s->yh) - (x + y) > best) {
            best = (s->xh + s->yh) - (x + y);
            *xm = x;
            *ym = y;
        }
    }
}

/** @brief Where a shortest path from (xl, yl) to (xh, yh) crosses its
 *         middle, or past the bound where a search got furthest. */
static void
split(myers_t *m, long xl, long xh, long yl, long yh, long *xm, long *ym)
{
    box_t s = { xl, xh, yl, yh, xl - yl, xl - yl, xh - yh, xh - yh };
    long dmin = xl - yh, dmax = xh - yl, c;
    int odd = ((s.fmin - s.bmin) & 1) != 0;

    m->fd[s.fmin] = xl;
    m->bd[s.bmin] = xh;
    for (c = 1;; c++) {
        if (forward(m, &s, dmin, dmax, odd, xm, ym) ||
            backward(m, &s, dmin, dmax, odd, xm, ym))
            return;
        if (c >= m->bound) {
            m->cut = 1;
            settle(m, &s, xm, ym);
            return;
        }
    }
}

/** @brief Mark what changed between x[xl..xh) and y[yl..yh): the common
 *         ends taken off, then a split at the middle snake and each half
 *         done alike.  A range DIFF_DEPTH splits deep, or whose split
 *         point falls outside it or on (xl, yl) or (xh, yh), is all
 *         changed. */
static void
compare(myers_t *m, long xl, long xh, long yl, long yh, int depth)
{
    long xm = 0, ym = 0;

    while (xl < xh && yl < yh && m->x[xl] == m->y[yl]) xl++, yl++;
    while (xl < xh && yl < yh && m->x[xh - 1] == m->y[yh - 1]) xh--, yh--;
    if (xl < xh && yl < yh && depth < DIFF_DEPTH) {
        split(m, xl, xh, yl, yh, &xm, &ym);
        if (xm >= xl && xm <= xh && ym >= yl && ym <= yh &&
            !(xm == xl && ym == yl) && !(xm == xh && ym == yh)) {
            compare(m, xl, xm, yl, ym, depth + 1);
            compare(m, xm, xh, ym, yh, depth + 1);
            return;
        }
    }
    if (xl < xh && yl < yh) m->cut = 1;
    while (xl < xh) m->del[xl++] = 1;
    while (yl < yh) m->ins[yl++] = 1;
}

/** @brief The square root of @p n, rounded down. */
static long
isqrt(long n)
{
    long r = 0;

    while ((r + 1) * (r + 1) <= n) r++;
    return r;
}

/** @brief What changed between @p x and @p y, into @p del and @p ins.
 *  @return 1 when that is the shortest script, 0 when a bound cut it,
 *          -1 out of memory. */
static int
core(const uint32_t *x, long nx, const uint32_t *y, long ny, int flags,
     char *del, char *ins)
{
    size_t span = (size_t)(nx + ny + 3);
    long *v = malloc(2 * span * sizeof *v);
    myers_t m;

    if (v == NULL) return -1;
    m.x = x;
    m.y = y;
    m.del = del;
    m.ins = ins;
    m.fd = v + ny + 1;              /* diagonals -ny-1 .. nx+1 */
    m.bd = v + span + ny + 1;
    m.bound = (flags & TIKU_DIFF_EXACT) ? LONG_MAX
                                        : 256 + 2 * isqrt(nx + ny);
    m.cut = 0;
    compare(&m, 0, nx, 0, ny, 0);
    free(v);
    return !m.cut;
}

/* ------------------------------------------------------------- runs  */

/** @brief The runs @p del (of @p na) and @p ins (of @p nb) describe:
 *         removals before additions where a change has both.
 *  @return 0, or -1 out of memory. */
static int
make_runs(const char *del, long na, const char *ins, long nb,
          tiku_diff_t *out)
{
    long i = 0, j = 0;

    memset(out, 0, sizeof *out);
    out->run = malloc((size_t)(na + nb + 1) * sizeof *out->run);
    if (out->run == NULL) return -1;
    while (i < na || j < nb) {
        tiku_diff_run_t *r = &out->run[out->nrun++];

        r->a = i;
        r->b = j;
        r->n = 0;
        if (i < na && (del[i] || j >= nb)) {        /* removed */
            r->kind = TIKU_DIFF_DEL;
            while (i < na && (del[i] || j >= nb)) i++, r->n++;
            out->removed += r->n;
        } else if (j < nb && (ins[j] || i >= na)) { /* added */
            r->kind = TIKU_DIFF_ADD;
            while (j < nb && (ins[j] || i >= na)) j++, r->n++;
            out->added += r->n;
        } else {                                    /* kept */
            r->kind = TIKU_DIFF_SAME;
            while (i < na && j < nb && !del[i] && !ins[j]) i++, j++, r->n++;
        }
    }
    return 0;
}

/** @brief Run @p k, a removal or an addition between two kept runs,
 *         moved one line down (@p dir 1) or up (-1) its own side. */
static void
shift(tiku_diff_t *d, long k, int dir)
{
    tiku_diff_run_t *r = &d->run[k], *pre = &d->run[k - 1];
    tiku_diff_run_t *post = &d->run[k + 1];

    r->a += dir;
    r->b += dir;
    pre->n += dir;
    post->a += dir;
    post->b += dir;
    post->n -= dir;
}

/**
 * @brief A lone removal or addition slid along the lines equal at its two
 *        ends, where any place is as correct, to end on a blank line
 *        where it can; else as low as it goes.
 */
static void
slide(tiku_diff_t *d, const uint32_t *id, long na,
      const tiku_diff_line_t *a, const tiku_diff_line_t *b)
{
    long k;

    for (k = 1; k + 1 < d->nrun; k++) {
        tiku_diff_run_t *r = &d->run[k];
        int add = r->kind == TIKU_DIFF_ADD, found = 0;
        const uint32_t *side = add ? id + na : id;
        const tiku_diff_line_t *text = add ? b : a;
        long moved = 0, best = 0;       /* lines below the highest place */

        if (r->kind == TIKU_DIFF_SAME ||
            d->run[k - 1].kind != TIKU_DIFF_SAME ||
            d->run[k + 1].kind != TIKU_DIFF_SAME)
            continue;
        while (d->run[k - 1].n > 1 &&            /* as high as it goes */
               side[(add ? r->b : r->a) - 1] ==
               side[(add ? r->b : r->a) + r->n - 1])
            shift(d, k, -1);
        for (;;) {                               /* then down, noting */
            long at = add ? r->b : r->a;

            if (line_blank(&text[at + r->n - 1])) {
                best = moved;
                found = 1;
            }
            if (d->run[k + 1].n <= 1 || side[at] != side[at + r->n]) break;
            shift(d, k, 1);
            moved++;
        }
        while (found && moved > best) {          /* back to a blank end */
            shift(d, k, -1);
            moved--;
        }
    }
}

int
tiku_diff_ints(const uint32_t *a, long na, const uint32_t *b, long nb,
               int flags, tiku_diff_t *out)
{
    char *del = calloc((size_t)na + 1, 1), *ins = calloc((size_t)nb + 1, 1);
    int best = del && ins ? core(a, na, b, nb, flags, del, ins) : -1;

    memset(out, 0, sizeof *out);
    if (best < 0 || make_runs(del, na, ins, nb, out) != 0) {
        free(del);
        free(ins);
        tiku_diff_free(out);
        return -1;
    }
    out->shortest = best;
    free(del);
    free(ins);
    return 0;
}

/** @brief What tiku_diff_lines() holds while it works. */
typedef struct {
    uint32_t *id;               /* each line's class, a's then b's    */
    char *del, *ins;            /* what changed, per side             */
    uint32_t *kx, *ky;          /* the lines both sides have some of  */
    long *ix, *iy;              /* where each of those came from      */
    long nkx, nky;
} lines_work_t;

/** @brief Give back what @p w holds. */
static void
work_free(lines_work_t *w)
{
    free(w->id);
    free(w->del);
    free(w->ins);
    free(w->kx);
    free(w->ky);
    free(w->ix);
    free(w->iy);
}

/** @brief A line found on one side only cannot be kept: marked changed
 *         here, and only the rest handed to the search.
 *  @return 0, or -1 out of memory. */
static int
set_aside(lines_work_t *w, long na, long nb, long classes)
{
    long *count = calloc((size_t)(2 * classes + 1), sizeof *count), i;

    if (count == NULL) return -1;
    for (i = 0; i < na; i++) count[2 * w->id[i]]++;
    for (i = 0; i < nb; i++) count[2 * w->id[na + i] + 1]++;
    for (i = 0; i < na; i++) {
        if (count[2 * w->id[i] + 1] == 0) {
            w->del[i] = 1;
        } else {
            w->kx[w->nkx] = w->id[i];
            w->ix[w->nkx++] = i;
        }
    }
    for (i = 0; i < nb; i++) {
        if (count[2 * w->id[na + i]] == 0) {
            w->ins[i] = 1;
        } else {
            w->ky[w->nky] = w->id[na + i];
            w->iy[w->nky++] = i;
        }
    }
    free(count);
    return 0;
}

/** @brief The search over what set_aside() left, marked back onto both
 *         sides.  @return as core() does. */
static int
search(lines_work_t *w, int flags)
{
    char *kd = calloc((size_t)w->nkx + 1, 1);
    char *ki = calloc((size_t)w->nky + 1, 1);
    int best = kd && ki ? core(w->kx, w->nkx, w->ky, w->nky, flags, kd, ki)
                        : -1;
    long k;

    for (k = 0; best >= 0 && k < w->nkx; k++)
        if (kd[k]) w->del[w->ix[k]] = 1;
    for (k = 0; best >= 0 && k < w->nky; k++)
        if (ki[k]) w->ins[w->iy[k]] = 1;
    free(kd);
    free(ki);
    return best;
}

int
tiku_diff_lines(const tiku_diff_line_t *a, long na,
                const tiku_diff_line_t *b, long nb, int flags,
                tiku_diff_t *out)
{
    lines_work_t w;
    long classes;
    int best = -1;

    memset(&w, 0, sizeof w);
    memset(out, 0, sizeof *out);
    w.id = malloc((size_t)(na + nb + 1) * sizeof *w.id);
    w.del = calloc((size_t)na + 1, 1);
    w.ins = calloc((size_t)nb + 1, 1);
    w.kx = malloc((size_t)na * sizeof *w.kx + 1);
    w.ky = malloc((size_t)nb * sizeof *w.ky + 1);
    w.ix = malloc((size_t)na * sizeof *w.ix + 1);
    w.iy = malloc((size_t)nb * sizeof *w.iy + 1);
    if (w.id && w.del && w.ins && w.kx && w.ky && w.ix && w.iy &&
        (classes = number_lines(a, na, b, nb, flags, w.id)) >= 0 &&
        set_aside(&w, na, nb, classes) == 0)
        best = search(&w, flags);
    if (best < 0 || make_runs(w.del, na, w.ins, nb, out) != 0) {
        work_free(&w);
        tiku_diff_free(out);
        return -1;
    }
    out->shortest = best;
    slide(out, w.id, na, a, b);
    work_free(&w);
    return 0;
}

void
tiku_diff_free(tiku_diff_t *d)
{
    if (d == NULL) return;
    free(d->run);
    memset(d, 0, sizeof *d);
}

/* ---------------------------------------------------------- unified  */

/** @brief Text being written: grown as it goes, @c bad once out of room. */
typedef struct {
    char  *p;
    size_t n, cap;
    int    bad;
} text_t;

/** @brief @p n bytes of @p s onto @p t. */
static void
put(text_t *t, const char *s, size_t n)
{
    if (t->bad) return;
    if (t->n + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap : 4096;
        char *p;

        while (t->n + n + 1 > cap) cap *= 2;
        if ((p = realloc(t->p, cap)) == NULL) {
            t->bad = 1;
            return;
        }
        t->p = p;
        t->cap = cap;
    }
    memcpy(t->p + t->n, s, n);
    t->n += n;
    t->p[t->n] = '\0';
}

/** @brief One line with its mark in front, and diff's note after a last
 *         line that has no LF. */
static void
put_line(text_t *t, char mark, const tiku_diff_line_t *l)
{
    put(t, &mark, 1);
    put(t, l->text, l->len);
    if (l->len == 0 || l->text[l->len - 1] != '\n')
        put(t, "\n\\ No newline at end of file\n", 29);
}

/** @brief A hunk header's range for @p n lines from @p at: "at+1,n", the
 *         count left off when it is one, and the line before when none. */
static void
put_range(text_t *t, char sign, long at, long n)
{
    char s[64];
    int k;

    if (n == 1) k = snprintf(s, sizeof s, "%c%ld", sign, at + 1);
    else if (n == 0) k = snprintf(s, sizeof s, "%c%ld,0", sign, at);
    else k = snprintf(s, sizeof s, "%c%ld,%ld", sign, at + 1, n);
    put(t, s, (size_t)k);
}

/** @brief Where run @p r ends on side a (@p side 0) or b. */
static long
run_end(const tiku_diff_run_t *r, int side)
{
    if (side == 0) return r->a + (r->kind == TIKU_DIFF_ADD ? 0 : r->n);
    return r->b + (r->kind == TIKU_DIFF_DEL ? 0 : r->n);
}

/**
 * @brief The hunk of changes runs @p first to @p last hold, with up to
 *        @p context kept lines before and after, onto @p t.
 */
static void
put_hunk(text_t *t, const tiku_diff_t *d, long first, long last,
         const tiku_diff_line_t *a, const tiku_diff_line_t *b, int context)
{
    const tiku_diff_run_t *f = &d->run[first];
    long lead = first > 0 && d->run[first - 1].n < context
                ? d->run[first - 1].n : (first > 0 ? context : 0);
    long trail = last + 1 < d->nrun
                 ? (d->run[last + 1].n < context ? d->run[last + 1].n : context)
                 : 0;
    long k, i;

    put(t, "@@ ", 3);
    put_range(t, '-', f->a - lead, run_end(&d->run[last], 0) + trail -
                                   (f->a - lead));
    put(t, " ", 1);
    put_range(t, '+', f->b - lead, run_end(&d->run[last], 1) + trail -
                                   (f->b - lead));
    put(t, " @@\n", 4);
    for (i = f->a - lead; i < f->a; i++) put_line(t, ' ', &a[i]);
    for (k = first; k <= last; k++) {
        const tiku_diff_run_t *r = &d->run[k];

        for (i = 0; i < r->n; i++)
            put_line(t, r->kind == TIKU_DIFF_SAME ? ' ' :
                        r->kind == TIKU_DIFF_DEL ? '-' : '+',
                     r->kind == TIKU_DIFF_ADD ? &b[r->b + i] : &a[r->a + i]);
    }
    for (i = 0; i < trail; i++)
        put_line(t, ' ', &a[run_end(&d->run[last], 0) + i]);
}

char *
tiku_diff_unified(const tiku_diff_t *d, const tiku_diff_line_t *a,
                  const tiku_diff_line_t *b, const char *name_a,
                  const char *name_b, int context, size_t *len)
{
    text_t t = { NULL, 0, 0, 0 };
    long k = 0, first, last;

    if (context < 0) context = 0;
    put(&t, "", 0);
    if (d->removed + d->added > 0) {
        put(&t, "--- ", 4);
        put(&t, name_a, strlen(name_a));
        put(&t, "\n+++ ", 5);
        put(&t, name_b, strlen(name_b));
        put(&t, "\n", 1);
    }
    while (k < d->nrun) {
        while (k < d->nrun && d->run[k].kind == TIKU_DIFF_SAME) k++;
        if (k >= d->nrun) break;
        first = last = k;
        /* Changes a short run of kept lines apart share a hunk. */
        while (last + 1 < d->nrun) {
            long next = last + 1;

            if (d->run[next].kind != TIKU_DIFF_SAME) last = next;
            else if (next + 1 < d->nrun && d->run[next].n <= 2L * context)
                last = next + 1;
            else break;
        }
        put_hunk(&t, d, first, last, a, b, context);
        k = last + 1;
    }
    if (t.bad) {
        free(t.p);
        return NULL;
    }
    if (len != NULL) *len = t.n;
    return t.p;
}
