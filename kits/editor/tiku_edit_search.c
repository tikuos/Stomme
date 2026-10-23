/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_search.c - literal search, one line at a time, through the
 * document's own runs of bytes, and Replace All as one transaction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_edit_search.h"
#include "tiku_editdoc_private.h"
#include "tiku_edit_regex.h"

#include <stdint.h>
#include <string.h>

/* Called with each match of a line, left to right; nonzero stops. */
typedef int (*take_fn)(void *ctx, tiku_edit_range_t match);

/** @brief One line being searched. */
typedef struct {
    tiku_editdoc_t *doc;
    const tiku_edit_query_t *q;
    tiku_edit_pos_t at;         /* where the run handed over starts     */
    tiku_edit_pos_t next;       /* where the next match may start       */
    tiku_edit_pos_t end;        /* the line's end                       */
    take_fn take;
    void *ctx;
    int stopped;
} scan_t;

/** @brief An ASCII letter in lower case, anything else as it is. */
static int
fold(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/** @brief Whether @p c is a byte a whole word may not touch. */
static int
word_byte(int c)
{
    return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') ||
           (fold(c) >= 'a' && fold(c) <= 'z');
}

/** @brief Whether @p a and @p b match under the query's case rule. */
static int
same(const tiku_edit_query_t *q, int a, int b)
{
    return (q->flags & TIKU_EDIT_MATCH_CASE) ? a == b : fold(a) == fold(b);
}

/** @brief The query checked: TIKU_EDIT_OK when it can be looked for. */
static tiku_edit_status_t
query_ok(const tiku_editdoc_t *doc, const tiku_edit_query_t *q)
{
    if (doc == NULL || q == NULL)
        return TIKU_EDIT_INVALID;
    if (q->text == NULL || q->length == 0 ||
        !tiku_ed_utf8_valid(q->text, q->length) ||
        memchr(q->text, '\r', q->length) != NULL ||
        memchr(q->text, '\n', q->length) != NULL)
        return TIKU_EDIT_INVALID_TEXT;
    return TIKU_EDIT_OK;
}

/** @brief Whether the query matches at byte @p i of @p run, its bytes
 *         read from the run while it holds them and from the document
 *         past its end. */
static int
match_at(const scan_t *s, const char *run, size_t n, size_t i)
{
    const unsigned char *want = (const unsigned char *)s->q->text;
    tiku_edit_pos_t pos = s->at + (tiku_edit_pos_t)i;
    size_t k, m = s->q->length;

    for (k = 0; k < m; k++) {
        int b = i + k < n ? (unsigned char)run[i + k]
                          : tiku_editdoc_byte_at(s->doc,
                                                 pos + (tiku_edit_pos_t)k);

        if (b < 0 || !same(s->q, b, want[k]))
            return 0;
    }
    if (s->q->flags & TIKU_EDIT_WHOLE_WORD)
        return !word_byte(tiku_editdoc_byte_at(s->doc, pos - 1)) &&
               !word_byte(tiku_editdoc_byte_at(s->doc,
                                               pos + (tiku_edit_pos_t)m));
    return 1;
}

/** @brief Look for matches in one run of the line's bytes. */
static int
scan_run(void *ctx, const char *bytes, size_t n)
{
    scan_t *s = ctx;
    tiku_edit_pos_t m = (tiku_edit_pos_t)s->q->length;
    size_t i = s->next > s->at ? (size_t)(s->next - s->at) : 0;
    int first = (unsigned char)s->q->text[0];

    for (; i < n; i++) {
        tiku_edit_pos_t pos = s->at + (tiku_edit_pos_t)i;

        if (pos + m > s->end)
            break;
        if (same(s->q, (unsigned char)bytes[i], first) &&
            match_at(s, bytes, n, i)) {
            tiku_edit_range_t match;

            match.start = pos;
            match.end = pos + m;
            if (s->take(s->ctx, match)) {
                s->stopped = 1;
                return 1;
            }
            s->next = match.end;
            i = (size_t)(s->next - s->at) - 1;
        }
    }
    s->at += (tiku_edit_pos_t)n;
    return 0;
}

/** @brief Hand @p line's matches to @p take; 1 when it stopped. */
static int
scan_line(tiku_editdoc_t *doc, const tiku_edit_query_t *q, int64_t line,
          take_fn take, void *ctx)
{
    tiku_edit_range_t r;
    scan_t s;

    r.start = tiku_editdoc_line_start(doc, line);
    r.end = tiku_editdoc_line_end(doc, line);
    if (r.start < 0 || r.end - r.start < (tiku_edit_pos_t)q->length)
        return 0;
    memset(&s, 0, sizeof s);
    s.doc = doc;
    s.q = q;
    s.at = r.start;
    s.next = r.start;
    s.end = r.end;
    s.take = take;
    s.ctx = ctx;
    (void)tiku_editdoc_each(doc, r, scan_run, &s);
    return s.stopped;
}

/** @brief Matches gathered inside a range, up to a capacity. */
typedef struct {
    tiku_edit_range_t scope;    /* the range they lie in                */
    tiku_edit_range_t *matches; /* where they go                        */
    size_t capacity;            /* room there                           */
    size_t count;               /* how many went                        */
    int truncated;              /* there were more                      */
} collect_t;

/** @brief Keep a match that lies inside the scope; stop when there is no
 *         room for it. */
static int
collect_match(void *ctx, tiku_edit_range_t match)
{
    collect_t *c = ctx;

    if (match.start < c->scope.start || match.end > c->scope.end)
        return 0;
    if (c->count == c->capacity) {
        c->truncated = 1;
        return 1;
    }
    c->matches[c->count++] = match;
    return 0;
}

tiku_edit_status_t
tiku_edit_collect(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                  tiku_edit_range_t scope, tiku_edit_range_t *matches,
                  size_t capacity, size_t *count, int *truncated)
{
    tiku_edit_status_t st;
    int64_t line, last;
    collect_t c;

    if (count != NULL)
        *count = 0;
    if (truncated != NULL)
        *truncated = 0;
    if (count == NULL || truncated == NULL ||
        (capacity > 0 && matches == NULL))
        return TIKU_EDIT_INVALID;
    st = query_ok(doc, q);
    if (st != TIKU_EDIT_OK)
        return st;
    if (scope.start > scope.end ||
        !tiku_editdoc_is_boundary(doc, scope.start) ||
        !tiku_editdoc_is_boundary(doc, scope.end))
        return TIKU_EDIT_INVALID_RANGE;
    if (q->flags & TIKU_EDIT_REGEX)
        return tiku_ed_regex_collect(doc, q, scope, matches, capacity,
                                     count, truncated);
    memset(&c, 0, sizeof c);
    c.scope = scope;
    c.matches = matches;
    c.capacity = capacity;
    last = tiku_editdoc_line_of(doc, scope.end);
    for (line = tiku_editdoc_line_of(doc, scope.start);
         line <= last && !c.truncated; line++)
        (void)scan_line(doc, q, line, collect_match, &c);
    *count = c.count;
    *truncated = c.truncated;
    return TIKU_EDIT_OK;
}

/** @brief A search for one match before or after a position. */
typedef struct {
    tiku_edit_pos_t limit;      /* the position searched from           */
    int before;                 /* nonzero: the last match before limit;
                                   0: the first at or after it          */
    int found;
    tiku_edit_range_t match;
} pick_t;

/** @brief Keep the first match at or after the limit, or the last one
 *         before it. */
static int
pick(void *ctx, tiku_edit_range_t match)
{
    pick_t *p = ctx;

    if (p->before) {
        if (match.start >= p->limit)
            return 1;
        p->match = match;
        p->found = 1;
        return 0;
    }
    if (match.start < p->limit)
        return 0;
    p->match = match;
    p->found = 1;
    return 1;
}

tiku_edit_status_t
tiku_edit_find(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
               tiku_edit_pos_t from, int backward, tiku_edit_range_t *match,
               int *wrapped)
{
    tiku_edit_status_t st;
    int64_t line, first, lines;
    pick_t p;

    if (q != NULL && (q->flags & TIKU_EDIT_REGEX))
        return tiku_ed_regex_find(doc, q, from, backward, match, wrapped);
    st = query_ok(doc, q);
    if (st != TIKU_EDIT_OK)
        return st;
    if (match == NULL || !tiku_editdoc_is_boundary(doc, from))
        return TIKU_EDIT_INVALID_RANGE;
    if (wrapped != NULL)
        *wrapped = 0;
    lines = tiku_editdoc_lines(doc);
    first = tiku_editdoc_line_of(doc, from);
    memset(&p, 0, sizeof p);
    p.before = backward;
    p.limit = from;
    if (!backward) {
        for (line = first; line < lines && !p.found; line++)
            (void)scan_line(doc, q, line, pick, &p);
        p.limit = 0;
        for (line = 0; line <= first && !p.found; line++) {
            (void)scan_line(doc, q, line, pick, &p);
            if (p.found && wrapped != NULL)
                *wrapped = 1;
        }
    } else {
        for (line = first; line >= 0 && !p.found; line--)
            (void)scan_line(doc, q, line, pick, &p);
        p.limit = INT64_MAX;
        for (line = lines - 1; line >= first && !p.found; line--) {
            (void)scan_line(doc, q, line, pick, &p);
            if (p.found && wrapped != NULL)
                *wrapped = 1;
        }
    }
    if (!p.found)
        return TIKU_EDIT_NOTHING;
    *match = p.match;
    return TIKU_EDIT_OK;
}

int
tiku_edit_is_match(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                   tiku_edit_range_t range)
{
    char *text = NULL;
    size_t n;
    scan_t s;

    if (q != NULL && (q->flags & TIKU_EDIT_REGEX)) {
        if (tiku_ed_regex_expand(doc, q, range, NULL, 0, &text, &n) !=
            TIKU_EDIT_OK)
            return 0;
        tiku_ed_free(text);
        return 1;
    }
    if (query_ok(doc, q) != TIKU_EDIT_OK || range.start < 0 ||
        range.end - range.start != (tiku_edit_pos_t)q->length ||
        range.end > tiku_editdoc_length(doc))
        return 0;
    memset(&s, 0, sizeof s);
    s.doc = doc;
    s.q = q;
    s.at = range.start;
    return match_at(&s, NULL, 0, 0);
}

/** @brief Every match, gathered in a growing list. */
typedef struct {
    tiku_edit_range_t *list;
    size_t n, cap;
    int failed;                 /* the list could not grow              */
} gather_t;

/** @brief Add one match to the list. */
static int
gather(void *ctx, tiku_edit_range_t match)
{
    gather_t *g = ctx;

    if (g->n == g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 64;
        tiku_edit_range_t *grown;

        if (cap > SIZE_MAX / sizeof *grown) {
            g->failed = 1;
            return 1;
        }
        grown = tiku_ed_realloc(g->list, cap * sizeof *grown);
        if (grown == NULL) {
            g->failed = 1;
            return 1;
        }
        g->list = grown;
        g->cap = cap;
    }
    g->list[g->n++] = match;
    return 0;
}

tiku_edit_status_t
tiku_edit_replace_all(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                      const char *with, size_t length,
                      tiku_edit_view_t view, size_t *count)
{
    tiku_edit_status_t st;
    tiku_edit_replace_t *edits;
    tiku_edit_transaction_t t;
    gather_t g;
    int64_t line, lines;
    size_t i;

    if (q != NULL && (q->flags & TIKU_EDIT_REGEX))
        return tiku_ed_regex_replace_all(doc, q, with, length, view, count);
    st = query_ok(doc, q);
    if (count != NULL)
        *count = 0;
    if (st != TIKU_EDIT_OK)
        return st;
    if (with == NULL && length > 0)
        return TIKU_EDIT_INVALID;
    memset(&g, 0, sizeof g);
    lines = tiku_editdoc_lines(doc);
    for (line = 0; line < lines && !g.failed; line++)
        (void)scan_line(doc, q, line, gather, &g);
    if (g.failed) {
        tiku_ed_free(g.list);
        return TIKU_EDIT_NO_MEMORY;
    }
    if (g.n == 0)
        return TIKU_EDIT_NOTHING;
    edits = g.n <= SIZE_MAX / sizeof *edits ?
            tiku_ed_alloc(g.n * sizeof *edits) : NULL;
    if (edits == NULL) {
        tiku_ed_free(g.list);
        return TIKU_EDIT_NO_MEMORY;
    }
    for (i = 0; i < g.n; i++) {
        edits[i].range = g.list[i];
        edits[i].text = length > 0 ? with : NULL;
        edits[i].length = length;
    }
    memset(&t, 0, sizeof t);
    t.edits = edits;
    t.count = g.n;
    t.view = view;
    st = tiku_editdoc_apply(doc, &t);
    tiku_ed_free(edits);
    tiku_ed_free(g.list);
    if (st == TIKU_EDIT_OK && count != NULL)
        *count = g.n;
    return st;
}
