/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_regex.c - POSIX extended regular expressions over an editor
 * document: each line matched from its start in a UTF-8 locale of the
 * kit's own, matches kept to character boundaries, and every replacement
 * made ready before the document changes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_edit_regex.h"
#include "tiku_editdoc_private.h"
#include "tiku_sys.h"

#include <limits.h>
#include <regex.h>
#include <string.h>

/* The groups a match reports, the whole match the first of them. */
#define CAPTURES 32

/** @brief A compiled query and the line it is matched against. */
typedef struct {
    regex_t rx;                 /* the pattern compiled                 */
    void *locale;               /* the UTF-8 locale it runs in          */
    int compiled;               /* rx holds a pattern                   */
    char *line;                 /* the line read, and a NUL             */
    size_t capacity;            /* room in it                           */
    const tiku_edit_query_t *query;
    tiku_editdoc_t *doc;
} regex_scan_t;

/* Called with each match of a line: @p m its groups, counted from the
 * line's first byte, which is @p text and document position @p base.
 * Setting @p stop ends the line's matches. */
typedef tiku_edit_status_t (*match_fn)(void *ctx, const char *text,
                                       const regmatch_t *m,
                                       tiku_edit_pos_t base, int *stop);

/** @brief Free what @p r holds. */
static void
close_regex(regex_scan_t *r)
{
    if (r->compiled)
        regfree(&r->rx);
    tiku_sys_locale_free(r->locale);
    tiku_ed_free(r->line);
}

/** @brief Compile @p q into @p r for @p doc in the UTF-8 locale.  A query
 *         the literal search in tiku_edit_search.c would refuse is refused
 *         the same way, a pattern that does not compile with
 *         TIKU_EDIT_BAD_PATTERN, and on a host with no UTF-8 locale any
 *         with TIKU_EDIT_INVALID. */
static tiku_edit_status_t
open_regex(regex_scan_t *r, tiku_editdoc_t *doc, const tiku_edit_query_t *q)
{
    int flags = REG_EXTENDED, error;
    void *previous;
    char *pattern;

    memset(r, 0, sizeof *r);
    if (doc == NULL || q == NULL)
        return TIKU_EDIT_INVALID;
    if (q->text == NULL || q->length == 0 || q->length >= INT_MAX ||
        !tiku_ed_utf8_valid(q->text, q->length) ||
        memchr(q->text, '\r', q->length) != NULL ||
        memchr(q->text, '\n', q->length) != NULL)
        return TIKU_EDIT_INVALID_TEXT;
    pattern = tiku_ed_alloc(q->length + 1);
    if (pattern == NULL)
        return TIKU_EDIT_NO_MEMORY;
    memcpy(pattern, q->text, q->length);
    pattern[q->length] = '\0';
    r->locale = tiku_sys_locale_utf8();
    if (r->locale == NULL) {
        tiku_ed_free(pattern);
        return TIKU_EDIT_INVALID;
    }
    if (!(q->flags & TIKU_EDIT_MATCH_CASE))
        flags |= REG_ICASE;
    previous = tiku_sys_locale_use(r->locale);
    error = regcomp(&r->rx, pattern, flags);
    (void)tiku_sys_locale_use(previous);
    tiku_ed_free(pattern);
    if (error != 0) {
        close_regex(r);
        memset(r, 0, sizeof *r);
        return error == REG_ESPACE ? TIKU_EDIT_NO_MEMORY
                                   : TIKU_EDIT_BAD_PATTERN;
    }
    r->compiled = 1;
    r->doc = doc;
    r->query = q;
    return TIKU_EDIT_OK;
}

/** @brief Whether byte @p c is one a whole word may not touch: an ASCII
 *         letter, a digit, '_' or any byte past ASCII. */
static int
word_byte(int c)
{
    return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief Read the bytes @p range of the document, one line, into @p r's
 *         line with a NUL after them. */
static tiku_edit_status_t
read_line(regex_scan_t *r, tiku_edit_range_t range)
{
    size_t length = (size_t)(range.end - range.start), got;

    if (length >= INT_MAX)
        return TIKU_EDIT_LIMIT;
    if (length + 1 > r->capacity) {
        char *grown = tiku_ed_realloc(r->line, length + 1);

        if (grown == NULL)
            return TIKU_EDIT_NO_MEMORY;
        r->line = grown;
        r->capacity = length + 1;
    }
    return tiku_editdoc_read(r->doc, range, r->line, r->capacity, &got);
}

/** @brief Whether match @p m of the line at @p start, @p length bytes,
 *         may be taken: inside the line, every group on character
 *         boundaries, and a whole word when the query asks for one. */
static int
match_ok(const regex_scan_t *r, const regmatch_t *m, tiku_edit_pos_t start,
         size_t length)
{
    size_t i;

    if ((size_t)m[0].rm_eo > length || m[0].rm_so > m[0].rm_eo)
        return 0;
    for (i = 0; i < CAPTURES; i++)
        if (m[i].rm_so >= 0 &&
            (!tiku_editdoc_is_boundary(r->doc, start + m[i].rm_so) ||
             !tiku_editdoc_is_boundary(r->doc, start + m[i].rm_eo)))
            return 0;
    if (!(r->query->flags & TIKU_EDIT_WHOLE_WORD))
        return 1;
    return !word_byte(tiku_editdoc_byte_at(r->doc, start + m[0].rm_so - 1)) &&
           !word_byte(tiku_editdoc_byte_at(r->doc, start + m[0].rm_eo));
}

/** @brief Where in the line of @p length bytes the next match may start
 *         after match @p m: its end when it was @p taken and holds bytes,
 *         else the character after its start; SIZE_MAX past the line. */
static size_t
next_offset(const regex_scan_t *r, const regmatch_t *m, size_t length,
            int taken)
{
    size_t a = (size_t)m[0].rm_so, b = (size_t)m[0].rm_eo;

    if (taken && b > a)
        return b;
    if (a >= length)
        return SIZE_MAX;
    for (a++; a < length && ((unsigned char)r->line[a] & 0xc0) == 0x80; a++)
        ;
    return a;
}

/** @brief Hand the matches of @p line to @p take in order, @p stopped set
 *         when it stops them.  Each line is matched from its start, so the
 *         same matches are found whichever way a search goes. */
static tiku_edit_status_t
scan_line(regex_scan_t *r, int64_t line, match_fn take, void *ctx,
          int *stopped)
{
    tiku_edit_range_t range;
    tiku_edit_status_t st;
    size_t length, offset = 0, i;

    range.start = tiku_editdoc_line_start(r->doc, line);
    range.end = tiku_editdoc_line_end(r->doc, line);
    length = (size_t)(range.end - range.start);
    st = read_line(r, range);
    if (st != TIKU_EDIT_OK)
        return st;
    while (offset <= length) {
        regmatch_t m[CAPTURES];
        void *previous = tiku_sys_locale_use(r->locale);
        int error = regexec(&r->rx, r->line + offset, CAPTURES, m,
                            offset > 0 ? REG_NOTBOL : 0);
        int taken;

        (void)tiku_sys_locale_use(previous);
        if (error == REG_NOMATCH)
            return TIKU_EDIT_OK;
        if (error != 0)
            return error == REG_ESPACE ? TIKU_EDIT_NO_MEMORY
                                       : TIKU_EDIT_BAD_PATTERN;
        for (i = 0; i < CAPTURES; i++) {
            if (m[i].rm_so >= 0) {
                m[i].rm_so += (regoff_t)offset;
                m[i].rm_eo += (regoff_t)offset;
            }
        }
        taken = match_ok(r, m, range.start, length);
        if (taken) {
            st = take(ctx, r->line, m, range.start, stopped);
            if (st != TIKU_EDIT_OK || *stopped)
                return st;
        }
        offset = next_offset(r, m, length, taken);
    }
    return TIKU_EDIT_OK;
}

/** @brief Matches gathered inside a range, up to a capacity. */
typedef struct {
    tiku_edit_range_t scope;    /* the range they lie in                */
    tiku_edit_range_t *matches; /* where they go                        */
    size_t capacity;            /* room there                           */
    size_t count;               /* how many went                        */
    int truncated;              /* there were more                      */
} collect_t;

/** @brief Keep match @p m when it lies inside the scope, stopping when
 *         there is no room for it. */
static tiku_edit_status_t
collect_match(void *ctx, const char *text, const regmatch_t *m,
              tiku_edit_pos_t base, int *stop)
{
    collect_t *c = ctx;
    tiku_edit_range_t r;

    (void)text;
    r.start = base + m[0].rm_so;
    r.end = base + m[0].rm_eo;
    if (r.start < c->scope.start || r.end > c->scope.end)
        return TIKU_EDIT_OK;
    if (c->count == c->capacity) {
        c->truncated = 1;
        *stop = 1;
    } else {
        c->matches[c->count++] = r;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_ed_regex_collect(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                      tiku_edit_range_t scope, tiku_edit_range_t *matches,
                      size_t capacity, size_t *count, int *truncated)
{
    regex_scan_t r;
    collect_t c;
    int64_t line, last;
    int stop = 0;
    tiku_edit_status_t st = open_regex(&r, doc, q);

    if (st != TIKU_EDIT_OK)
        return st;
    memset(&c, 0, sizeof c);
    c.scope = scope;
    c.matches = matches;
    c.capacity = capacity;
    last = tiku_editdoc_line_of(doc, scope.end);
    for (line = tiku_editdoc_line_of(doc, scope.start);
         line <= last && !stop && st == TIKU_EDIT_OK; line++)
        st = scan_line(&r, line, collect_match, &c, &stop);
    close_regex(&r);
    if (st == TIKU_EDIT_OK) {
        *count = c.count;
        *truncated = c.truncated;
    }
    return st;
}

/** @brief A search for one match before or after a position. */
typedef struct {
    tiku_edit_pos_t from;       /* the position searched from           */
    int backward;               /* nonzero: the last match before from;
                                   0: the first at or after it          */
    int found;                  /* a match is kept                      */
    tiku_edit_range_t match;    /* the match kept                       */
} pick_t;

/** @brief Keep the first match starting at or after the position, or the
 *         last one starting before it. */
static tiku_edit_status_t
pick(void *ctx, const char *text, const regmatch_t *m, tiku_edit_pos_t base,
     int *stop)
{
    pick_t *p = ctx;
    tiku_edit_range_t range;

    (void)text;
    range.start = base + m[0].rm_so;
    range.end = base + m[0].rm_eo;
    if (p->backward ? range.start < p->from : range.start >= p->from) {
        p->match = range;
        p->found = 1;
        *stop = !p->backward;
    } else if (p->backward) {
        *stop = 1;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_ed_regex_find(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                   tiku_edit_pos_t from, int backward,
                   tiku_edit_range_t *match, int *wrapped)
{
    regex_scan_t r;
    pick_t p;
    int64_t first, lines, line;
    int pass, stop;
    tiku_edit_status_t st = open_regex(&r, doc, q);

    if (st != TIKU_EDIT_OK)
        return st;
    if (wrapped != NULL)
        *wrapped = 0;
    if (match == NULL || !tiku_editdoc_is_boundary(doc, from)) {
        close_regex(&r);
        return TIKU_EDIT_INVALID_RANGE;
    }
    memset(&p, 0, sizeof p);
    p.from = from;
    p.backward = backward;
    first = tiku_editdoc_line_of(doc, from);
    lines = tiku_editdoc_lines(doc);
    for (pass = 0; pass < 2 && !p.found && st == TIKU_EDIT_OK; pass++) {
        line = pass == 0 ? first : backward ? lines - 1 : 0;
        if (pass > 0)
            p.from = backward ? INT64_MAX : 0;
        for (; line >= 0 && line < lines &&
               (pass == 0 || (backward ? line >= first : line <= first));
             line += backward ? -1 : 1) {
            stop = 0;
            st = scan_line(&r, line, pick, &p, &stop);
            if (st != TIKU_EDIT_OK || p.found)
                break;
        }
        if (p.found && pass > 0 && wrapped != NULL)
            *wrapped = 1;
    }
    close_regex(&r);
    if (st != TIKU_EDIT_OK)
        return st;
    if (!p.found)
        return TIKU_EDIT_NOTHING;
    *match = p.match;
    return TIKU_EDIT_OK;
}

/** @brief Read the group named after a '$' at byte @p *i of @p n bytes of
 *         @p with, moving @p *i past it: one digit, or digits in braces,
 *         naming a group below CAPTURES. */
static tiku_edit_status_t
group_at(const char *with, size_t n, size_t *i, size_t *group)
{
    int braced = *i < n && with[*i] == '{';

    *group = 0;
    if (braced)
        (*i)++;
    if (*i >= n || with[*i] < '0' || with[*i] > '9')
        return TIKU_EDIT_BAD_REPLACEMENT;
    do {
        *group = *group * 10 + (size_t)(with[(*i)++] - '0');
        if (*group >= CAPTURES)
            return TIKU_EDIT_BAD_REPLACEMENT;
    } while (braced && *i < n && with[*i] >= '0' && with[*i] <= '9');
    if (braced && (*i >= n || with[(*i)++] != '}'))
        return TIKU_EDIT_BAD_REPLACEMENT;
    return TIKU_EDIT_OK;
}

/** @brief The byte a backslash and @p c stand for in a replacement: a
 *         line feed, a CR, a tab or a backslash for n, r, t and a
 *         backslash; -1 for any other. */
static int
escape_of(char c)
{
    switch (c) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    default:
        return -1;
    }
}

/** @brief The replacement for match @p m of the line @p text: @p n bytes
 *         of @p with, its groups put in, written into @p dest when that
 *         is not NULL, and its length in @p length.  With no match only
 *         @p with is checked.  A group the match left out puts in
 *         nothing; one the pattern does not have is refused with
 *         TIKU_EDIT_BAD_REPLACEMENT, as is an escape there is not. */
static tiku_edit_status_t
expand(const regex_scan_t *r, const char *text, const regmatch_t *m,
       const char *with, size_t n, char *dest, size_t *length)
{
    size_t used = 0, i = 0, group, bytes;
    const char *part;
    char escaped;
    int c;

    while (i < n) {
        part = with + i;
        bytes = 1;
        if (with[i] == '$' && i + 1 < n && with[i + 1] == '$') {
            i += 2;
        } else if (with[i] == '$') {
            i++;
            if (group_at(with, n, &i, &group) != TIKU_EDIT_OK ||
                group > r->rx.re_nsub)
                return TIKU_EDIT_BAD_REPLACEMENT;
            bytes = m != NULL && m[group].rm_so >= 0 ?
                    (size_t)(m[group].rm_eo - m[group].rm_so) : 0;
            part = bytes > 0 ? text + m[group].rm_so : "";
        } else if (with[i] == '\\') {
            i++;
            c = i < n ? escape_of(with[i]) : -1;
            if (c < 0)
                return TIKU_EDIT_BAD_REPLACEMENT;
            i++;
            escaped = (char)c;
            part = &escaped;
        } else {
            i++;
        }
        if (bytes > SIZE_MAX - used - 1)
            return TIKU_EDIT_LIMIT;
        if (dest != NULL && bytes > 0)
            memcpy(dest + used, part, bytes);
        used += bytes;
    }
    if (dest != NULL)
        dest[used] = '\0';
    *length = used;
    return TIKU_EDIT_OK;
}

/** @brief The replacements of every match, or of the one match that is
 *         exactly a range. */
typedef struct {
    regex_scan_t *scan;         /* the query compiled                   */
    const char *with;           /* the replacement asked for            */
    size_t length;              /* bytes in with                        */
    tiku_edit_replace_t *edits; /* each match's replacement             */
    size_t count;               /* replacements in edits                */
    size_t capacity;            /* room in edits                        */
    int only_one;               /* only the match wanted is replaced    */
    tiku_edit_range_t wanted;   /* the match wanted, with only_one      */
} replacements_t;

/** @brief Room in @p g for one replacement more. */
static tiku_edit_status_t
edits_room(replacements_t *g)
{
    size_t cap;
    tiku_edit_replace_t *grown;

    if (g->count < g->capacity)
        return TIKU_EDIT_OK;
    cap = g->capacity > 0 ? g->capacity * 2 : 16;
    if (cap > SIZE_MAX / sizeof *grown)
        return TIKU_EDIT_LIMIT;
    grown = tiku_ed_realloc(g->edits, cap * sizeof *grown);
    if (grown == NULL)
        return TIKU_EDIT_NO_MEMORY;
    g->edits = grown;
    g->capacity = cap;
    return TIKU_EDIT_OK;
}

/** @brief Keep the replacement of match @p m, unless another match is
 *         the one wanted; the wanted one, once kept, stops the line. */
static tiku_edit_status_t
replacement(void *ctx, const char *text, const regmatch_t *m,
            tiku_edit_pos_t base, int *stop)
{
    replacements_t *g = ctx;
    tiku_edit_range_t range;
    tiku_edit_status_t st;
    size_t n;
    char *copy;

    range.start = base + m[0].rm_so;
    range.end = base + m[0].rm_eo;
    if (g->only_one && (range.start != g->wanted.start ||
                        range.end != g->wanted.end))
        return TIKU_EDIT_OK;
    st = expand(g->scan, text, m, g->with, g->length, NULL, &n);
    if (st == TIKU_EDIT_OK)
        st = edits_room(g);
    if (st != TIKU_EDIT_OK)
        return st;
    copy = tiku_ed_alloc(n + 1);
    if (copy == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = expand(g->scan, text, m, g->with, g->length, copy, &n);
    if (st != TIKU_EDIT_OK) {
        tiku_ed_free(copy);
        return st;
    }
    g->edits[g->count].range = range;
    g->edits[g->count].text = copy;
    g->edits[g->count].length = n;
    g->count++;
    *stop = g->only_one;
    return TIKU_EDIT_OK;
}

/** @brief Gather into @p g the replacements of every match of @p q, or
 *         with @p only of the one match that is exactly it: @p length
 *         bytes of @p with, each match's groups put in.  TIKU_EDIT_NOTHING
 *         when there is no such match. */
static tiku_edit_status_t
gather(tiku_editdoc_t *doc, const tiku_edit_query_t *q, const char *with,
       size_t length, const tiku_edit_range_t *only, replacements_t *g)
{
    regex_scan_t r;
    int64_t line, end;
    size_t checked;
    int stop;
    tiku_edit_status_t st = open_regex(&r, doc, q);

    memset(g, 0, sizeof *g);
    if (st != TIKU_EDIT_OK)
        return st;
    g->scan = &r;
    g->with = with;
    g->length = length;
    if ((with == NULL && length > 0) || !tiku_ed_utf8_valid(with, length))
        st = TIKU_EDIT_INVALID_TEXT;
    if (st == TIKU_EDIT_OK)
        st = expand(&r, NULL, NULL, with, length, NULL, &checked);
    line = only != NULL ? tiku_editdoc_line_of(doc, only->start) : 0;
    end = only != NULL ? line + 1 : tiku_editdoc_lines(doc);
    if (only != NULL) {
        g->only_one = 1;
        g->wanted = *only;
    }
    for (; line >= 0 && line < end && st == TIKU_EDIT_OK; line++) {
        stop = 0;
        st = scan_line(&r, line, replacement, g, &stop);
    }
    close_regex(&r);
    g->scan = NULL;
    if (st == TIKU_EDIT_OK && g->count == 0)
        st = TIKU_EDIT_NOTHING;
    return st;
}

/** @brief Free the replacements gathered in @p g. */
static void
replacements_free(replacements_t *g)
{
    size_t i;

    for (i = 0; i < g->count; i++)
        tiku_ed_free((void *)g->edits[i].text);
    tiku_ed_free(g->edits);
}

tiku_edit_status_t
tiku_ed_regex_expand(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                     tiku_edit_range_t range, const char *with,
                     size_t length, char **out, size_t *n)
{
    replacements_t g;
    tiku_edit_status_t st;

    if (doc == NULL || range.start > range.end ||
        !tiku_editdoc_is_boundary(doc, range.start) ||
        !tiku_editdoc_is_boundary(doc, range.end))
        return TIKU_EDIT_INVALID_RANGE;
    st = gather(doc, q, with, length, &range, &g);
    if (st == TIKU_EDIT_OK) {
        *out = (char *)g.edits[0].text;
        *n = g.edits[0].length;
        g.edits[0].text = NULL;
    }
    replacements_free(&g);
    return st;
}

tiku_edit_status_t
tiku_ed_regex_replace_all(tiku_editdoc_t *doc, const tiku_edit_query_t *q,
                          const char *with, size_t length,
                          tiku_edit_view_t view, size_t *count)
{
    tiku_edit_transaction_t t;
    replacements_t g;
    tiku_edit_status_t st;

    if (count != NULL)
        *count = 0;
    st = gather(doc, q, with, length, NULL, &g);
    if (st == TIKU_EDIT_OK) {
        memset(&t, 0, sizeof t);
        t.edits = g.edits;
        t.count = g.count;
        t.view = view;
        st = tiku_editdoc_apply(doc, &t);
        if (st == TIKU_EDIT_OK && count != NULL)
            *count = g.count;
    }
    replacements_free(&g);
    return st;
}
