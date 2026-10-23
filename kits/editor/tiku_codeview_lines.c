/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_lines.c - edits made a line at a time: a new line keeping
 * its indentation, indenting and unindenting, comments, and copying,
 * moving and deleting whole lines.  Each is one transaction of the
 * document's; what a language's comments and levels look like comes from
 * the view's rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_codeview_private.h"
#include "tiku_editdoc_private.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *const basic_blocks[] = {
    "FOR", "NEXT", "WHILE", "WEND", "REPEAT", "UNTIL", "SUB", "ENDSUB",
    "THEN", "ENDIF", NULL
};
static const tiku_codeview_rules_t c_rules = {
    "//", NULL, 0, "{", "}", "()[]{}", NULL
};
static const tiku_codeview_rules_t basic_rules = {
    "REM", "'", 1, NULL, NULL, "()[]{}", basic_blocks
};
static const tiku_codeview_rules_t prose_rules = {
    NULL, NULL, 0, NULL, NULL, "()[]{}", NULL
};

/* A level of indentation made of spaces, at most TIKU_CODEVIEW_TAB_MAX. */
static const char spaces[] = "                                ";

/** @brief Replacements gathered for one transaction, ascending; each one's
 *         text is the batch's own copy or a string that outlives it. */
typedef struct {
    tiku_edit_replace_t *edits;
    char **owned;               /* each edit's copy to free, or NULL    */
    size_t count, cap;
    int failed;                 /* an allocation failed                 */
} batch_t;

const tiku_codeview_rules_t *
tiku_codeview_rules_for(tiku_syntax_lang_t lang)
{
    if (lang == TIKU_SYNTAX_C)
        return &c_rules;
    if (lang == TIKU_SYNTAX_BASIC)
        return &basic_rules;
    return &prose_rules;
}

const tiku_codeview_rules_t *
tiku_cv_rules(const tiku_codeview_t *v)
{
    return v->own_rules ? &v->rules : tiku_codeview_rules_for(v->lang);
}

void
tiku_codeview_set_rules(tiku_codeview_t *view,
                        const tiku_codeview_rules_t *rules)
{
    if (view == NULL)
        return;
    view->own_rules = rules != NULL;
    if (rules != NULL)
        view->rules = *rules;
    view->brace_gen = 0;
}

const tiku_codeview_rules_t *
tiku_codeview_rules(const tiku_codeview_t *view)
{
    return view != NULL ? tiku_cv_rules(view) : NULL;
}

tiku_edit_status_t
tiku_codeview_set_indent(tiku_codeview_t *view, int spaces_per_level)
{
    if (view == NULL)
        return TIKU_EDIT_INVALID;
    if (spaces_per_level < 0 || spaces_per_level > TIKU_CODEVIEW_TAB_MAX)
        return TIKU_EDIT_LIMIT;
    view->indent_spaces = spaces_per_level;
    return TIKU_EDIT_OK;
}

int
tiku_codeview_indent_spaces(const tiku_codeview_t *view)
{
    return view != NULL ? view->indent_spaces : 0;
}

/** @brief Room for one more replacement; 0 when there is none. */
static int
batch_room(batch_t *b)
{
    size_t cap;
    tiku_edit_replace_t *edits;
    char **owned;

    if (b->count < b->cap)
        return 1;
    cap = b->cap ? b->cap * 2 : 16;
    if (cap > SIZE_MAX / sizeof *edits)
        return 0;
    edits = tiku_ed_realloc(b->edits, cap * sizeof *edits);
    if (edits == NULL)
        return 0;
    b->edits = edits;
    owned = tiku_ed_realloc(b->owned, cap * sizeof *owned);
    if (owned == NULL)
        return 0;
    b->owned = owned;
    b->cap = cap;
    return 1;
}

/** @brief Add the replacement of [@p start, @p end) by @p n bytes of
 *         @p text, copied when @p copy, else kept by pointer. */
static void
batch_add(batch_t *b, tiku_edit_pos_t start, tiku_edit_pos_t end,
          const char *text, size_t n, int copy)
{
    char *own = NULL;

    if (b->failed)
        return;
    if (!batch_room(b)) {
        b->failed = 1;
        return;
    }
    if (n > 0 && copy) {
        own = tiku_ed_alloc(n);
        if (own == NULL) {
            b->failed = 1;
            return;
        }
        memcpy(own, text, n);
        text = own;
    }
    b->edits[b->count].range.start = start;
    b->edits[b->count].range.end = end;
    b->edits[b->count].text = n > 0 ? text : NULL;
    b->edits[b->count].length = n;
    b->owned[b->count] = own;
    b->count++;
}

/** @brief Free what the batch holds. */
static void
batch_free(batch_t *b)
{
    size_t i;

    for (i = 0; i < b->count; i++)
        tiku_ed_free(b->owned[i]);
    tiku_ed_free(b->edits);
    tiku_ed_free(b->owned);
    memset(b, 0, sizeof *b);
}

/**
 * @brief Where @p p lands once the batch is applied.
 *
 * Text inserted exactly at @p p goes before it when @p after, after it
 * otherwise; a position inside a replaced range goes to the replacement's
 * end when @p after, its start otherwise.
 */
static tiku_edit_pos_t
batch_map(const batch_t *b, tiku_edit_pos_t p, int after)
{
    tiku_edit_pos_t shift = 0;
    size_t i;

    for (i = 0; i < b->count; i++) {
        const tiku_edit_replace_t *e = &b->edits[i];
        tiku_edit_pos_t s = e->range.start, end = e->range.end;
        tiku_edit_pos_t n = (tiku_edit_pos_t)e->length;

        if (p < s || (p == s && end == s && !after))
            break;
        if (p < end || (p == s && end == s))
            return s + shift + (after ? n : 0);
        shift += n - (end - s);
    }
    return p + shift;
}

/** @brief Apply the batch as one transaction, the selection then set to
 *         @p anchor and @p caret, and free it.  TIKU_EDIT_NOTHING when it
 *         holds no replacement. */
static tiku_edit_status_t
batch_apply(tiku_codeview_t *v, batch_t *b, tiku_edit_pos_t anchor,
            tiku_edit_pos_t caret, unsigned flags)
{
    tiku_edit_transaction_t t;
    tiku_edit_status_t st;

    if (b->failed) {
        st = TIKU_EDIT_NO_MEMORY;
    } else if (b->count == 0) {
        st = TIKU_EDIT_NOTHING;
    } else {
        memset(&t, 0, sizeof t);
        t.edits = b->edits;
        t.count = b->count;
        t.view = v->view;
        t.select = 1;
        t.anchor = anchor;
        t.caret = caret;
        t.flags = flags;
        st = tiku_editdoc_apply(v->doc, &t);
        if (st == TIKU_EDIT_OK)
            tiku_codeview_reveal(v);
    }
    batch_free(b);
    return st;
}

/** @brief Whether @p b is a blank: a space or a tab. */
static int
blank(int b)
{
    return b == ' ' || b == '\t';
}

/** @brief The bytes of blanks that start @p line. */
static tiku_edit_pos_t
blanks(const tiku_codeview_t *v, int64_t line)
{
    tiku_edit_pos_t s = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line), p = s;

    while (p < e && blank(tiku_editdoc_byte_at(v->doc, p)))
        p++;
    return p - s;
}

/** @brief Where the line after @p line starts, or the document's end
 *         after the last line. */
static tiku_edit_pos_t
next_start(const tiku_codeview_t *v, int64_t line)
{
    return tiku_editdoc_line_start(v->doc, line + 1);
}

/** @brief A level of indentation, in @p text. */
static size_t
unit(const tiku_codeview_t *v, const char **text)
{
    if (v->indent_spaces > 0) {
        *text = spaces;
        return (size_t)v->indent_spaces;
    }
    *text = "\t";
    return 1;
}

/** @brief The lines the selection touches, and the selection; a selection
 *         ending at the start of a later line leaves that line out. */
static tiku_edit_status_t
touched(const tiku_codeview_t *v, int64_t *first, int64_t *last,
        tiku_edit_pos_t *anchor, tiku_edit_pos_t *caret)
{
    tiku_edit_status_t st = tiku_codeview_selection(v, anchor, caret);
    tiku_edit_pos_t lo, hi;

    if (st != TIKU_EDIT_OK)
        return st;
    lo = *anchor < *caret ? *anchor : *caret;
    hi = *anchor < *caret ? *caret : *anchor;
    *first = tiku_editdoc_line_of(v->doc, lo);
    *last = tiku_editdoc_line_of(v->doc, hi);
    if (hi > lo && *last > *first &&
        hi == tiku_editdoc_line_start(v->doc, *last))
        (*last)--;
    return TIKU_EDIT_OK;
}

/** @brief Copy [@p start, @p end) into a new buffer with room for
 *         @p extra more bytes; NULL when there is no memory. */
static char *
copy_range(const tiku_codeview_t *v, tiku_edit_pos_t start,
           tiku_edit_pos_t end, size_t extra)
{
    tiku_edit_range_t r;
    size_t n = (size_t)(end - start), got = 0;
    char *out;

    if ((uint64_t)(end - start) > (uint64_t)SIZE_MAX - extra - 1)
        return NULL;
    out = tiku_ed_alloc(n + extra + 1);
    if (out == NULL)
        return NULL;
    r.start = start;
    r.end = end;
    if (tiku_editdoc_read(v->doc, r, out, n + extra + 1, &got) !=
        TIKU_EDIT_OK) {
        tiku_ed_free(out);
        return NULL;
    }
    return out;
}

/** @brief Apply one replacement of [@p s, @p e) by @p n bytes of @p text,
 *         copied, as one transaction, the selection then @p anchor to
 *         @p caret. */
static tiku_edit_status_t
apply_one(tiku_codeview_t *v, tiku_edit_pos_t s, tiku_edit_pos_t e,
          const char *text, size_t n, tiku_edit_pos_t anchor,
          tiku_edit_pos_t caret, unsigned flags)
{
    batch_t b;

    memset(&b, 0, sizeof b);
    batch_add(&b, s, e, text, n, 1);
    return batch_apply(v, &b, anchor, caret, flags);
}

/** @brief The selection, in order; TIKU_EDIT_INVALID without one. */
static tiku_edit_status_t
ordered(const tiku_codeview_t *v, tiku_edit_pos_t *lo, tiku_edit_pos_t *hi)
{
    tiku_edit_pos_t anchor, caret;

    if (v == NULL || tiku_codeview_selection(v, &anchor, &caret) !=
                         TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    *lo = anchor < caret ? anchor : caret;
    *hi = anchor < caret ? caret : anchor;
    return TIKU_EDIT_OK;
}

/** @brief The ink of byte @p at of a line's colouring @p runs, @p n of
 *         them; past them, plain. */
static tiku_ink_t
ink_at(const tiku_span_t *runs, size_t n, int64_t at)
{
    int64_t off = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        if (at < off + runs[i].len)
            return runs[i].ink;
        off += runs[i].len;
    }
    return TIKU_INK_PLAIN;
}

/** @brief Whether the code before @p pos on @p line ends with one of the
 *         rules' openers, after blanks and a remark are passed over. */
static int
opens_level(tiku_codeview_t *v, const tiku_codeview_rules_t *r,
            int64_t line, tiku_edit_pos_t pos)
{
    tiku_edit_pos_t start = tiku_editdoc_line_start(v->doc, line), p;
    const tiku_span_t *runs;
    size_t n = 0;

    if (r->opens == NULL || pos <= start)
        return 0;
    runs = tiku_cv_style_line(v, line, pos, &n);
    for (p = pos; p > start; p--) {
        int b = tiku_editdoc_byte_at(v->doc, p - 1);
        tiku_ink_t ink = tiku_cv_style_role(v,
                                            ink_at(runs, n, p - 1 - start));

        if (blank(b) || ink == TIKU_INK_REMARK)
            continue;
        return ink != TIKU_INK_STR && b != 0 && strchr(r->opens, b) != NULL;
    }
    return 0;
}

/** @brief Where the indentation of @p pos's line ends, no further than
 *         @p pos; the line's start in @p start. */
static tiku_edit_pos_t
indent_end(const tiku_codeview_t *v, tiku_edit_pos_t pos,
           tiku_edit_pos_t *start)
{
    tiku_edit_pos_t p = tiku_editdoc_line_start(v->doc,
                                                tiku_editdoc_line_of(v->doc,
                                                                     pos));

    *start = p;
    while (p < pos && blank(tiku_editdoc_byte_at(v->doc, p)))
        p++;
    return p;
}

/** @brief The closer of the rules that follows @p hi past blanks on its
 *         line, or -1. */
static tiku_edit_pos_t
closer_after(const tiku_codeview_t *v, const tiku_codeview_rules_t *r,
             tiku_edit_pos_t hi)
{
    tiku_edit_pos_t end = tiku_editdoc_line_end(v->doc,
                                                tiku_editdoc_line_of(v->doc,
                                                                     hi));
    int b;

    while (hi < end && blank(tiku_editdoc_byte_at(v->doc, hi)))
        hi++;
    b = tiku_editdoc_byte_at(v->doc, hi);
    if (r->closes == NULL || hi >= end || b <= 0 ||
        strchr(r->closes, b) == NULL)
        return -1;
    return hi;
}

/**
 * @brief What Return puts in: the view's line break, the indentation in
 *        [@p from, @p to), @p depth bytes of @p level, and with @p split
 *        the break and the indentation again.
 *
 * @return the text, which the caller frees, @p n bytes long; NULL when
 *         there is no memory.
 */
static char *
newline_text(const tiku_codeview_t *v, tiku_edit_pos_t from,
             tiku_edit_pos_t to, const char *level, size_t depth,
             int split, size_t *n)
{
    size_t nl = strlen(v->newline), ind = (size_t)(to - from);
    size_t total = nl + ind + depth;
    char *text = copy_range(v, from, to, 2 * nl + ind + depth);

    if (text == NULL)
        return NULL;
    /* The indentation, read to the front of the buffer, moves behind the
     * line break and is copied again after the closer's break. */
    memmove(text + nl, text, ind);
    memcpy(text, v->newline, nl);
    if (depth > 0)
        memcpy(text + nl + ind, level, depth);
    if (split) {
        memcpy(text + total, v->newline, nl);
        memcpy(text + total + nl, text + nl, ind);
        total += nl + ind;
    }
    *n = total;
    return text;
}

/** @brief Return's piece for selection @p s: the view's line break and
 *         the indentation of its line up to it, a level more after an
 *         opener, a closer after it moved to a line of its own when it
 *         stands before @p limit; in text the caller frees, 0 when there
 *         is no memory. */
static int
newline_piece(tiku_codeview_t *v, const tiku_edit_selection_t *s,
              tiku_edit_pos_t limit, tiku_cv_piece_t *p)
{
    tiku_edit_pos_t lo = s->anchor < s->caret ? s->anchor : s->caret;
    tiku_edit_pos_t hi = s->anchor < s->caret ? s->caret : s->anchor;
    tiku_edit_pos_t start, ind = indent_end(v, lo, &start), closer = -1;
    const char *level = NULL;
    size_t depth = 0, len = 0;

    if (opens_level(v, tiku_cv_rules(v), tiku_editdoc_line_of(v->doc, lo),
                    lo)) {
        depth = unit(v, &level);
        closer = closer_after(v, tiku_cv_rules(v), hi);
        if (closer >= limit)
            closer = -1;
    }
    p->text = newline_text(v, start, ind, level, depth, closer >= 0, &len);
    p->start = lo;
    p->end = closer >= 0 ? closer : hi;
    p->length = len;
    p->caret = strlen(v->newline) + (size_t)(ind - start) + depth;
    return p->text != NULL;
}

/** @brief Return at every selection, each with its own line's
 *         indentation; a closer reaching the next selection stays. */
static tiku_edit_status_t
newline_each(tiku_codeview_t *v)
{
    size_t n, main, i;
    const tiku_edit_selection_t *s = tiku_cv_selections(v, &n, &main);
    tiku_edit_status_t st = TIKU_EDIT_OK;
    tiku_edit_pos_t limit;
    tiku_cv_piece_t *p;

    if (s == NULL)
        return TIKU_EDIT_NO_MEMORY;
    if (n > SIZE_MAX / sizeof *p)
        return TIKU_EDIT_LIMIT;
    p = tiku_ed_alloc(n * sizeof *p);
    if (p == NULL)
        return TIKU_EDIT_NO_MEMORY;
    memset(p, 0, n * sizeof *p);
    for (i = 0; i < n && st == TIKU_EDIT_OK; i++) {
        limit = INT64_MAX;
        if (i + 1 < n)
            limit = s[i + 1].anchor < s[i + 1].caret ? s[i + 1].anchor
                                                     : s[i + 1].caret;
        if (!newline_piece(v, &s[i], limit, &p[i]))
            st = TIKU_EDIT_NO_MEMORY;
    }
    if (st == TIKU_EDIT_OK)
        st = tiku_cv_apply_pieces(v, p, n, main, 1);
    for (i = 0; i < n; i++)
        tiku_ed_free((char *)p[i].text);
    tiku_ed_free(p);
    return st;
}

tiku_edit_status_t
tiku_codeview_newline(tiku_codeview_t *view)
{
    tiku_edit_pos_t lo, hi, start, ind, closer = -1;
    const char *level = NULL;
    size_t depth = 0, n = 0;
    char *text;
    tiku_edit_status_t st;

    if (ordered(view, &lo, &hi) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    if (tiku_cv_many(view))
        return newline_each(view);
    ind = indent_end(view, lo, &start);
    if (opens_level(view, tiku_cv_rules(view),
                    tiku_editdoc_line_of(view->doc, lo), lo)) {
        depth = unit(view, &level);
        closer = closer_after(view, tiku_cv_rules(view), hi);
        if (closer >= 0)
            hi = closer;
    }
    text = newline_text(view, start, ind, level, depth, closer >= 0, &n);
    if (text == NULL)
        return TIKU_EDIT_NO_MEMORY;
    depth += strlen(view->newline) + (size_t)(ind - start);
    st = apply_one(view, lo, hi, text, n, lo + (tiku_edit_pos_t)depth,
                   lo + (tiku_edit_pos_t)depth, TIKU_EDIT_TYPING);
    tiku_ed_free(text);
    return st;
}

/** @brief Whether [@p a, @p a + @p n) and [@p b, @p b + @p n) hold the same
 *         bytes. */
static int
same_bytes(const tiku_codeview_t *v, tiku_edit_pos_t a, tiku_edit_pos_t b,
           tiku_edit_pos_t n)
{
    tiku_edit_pos_t i;

    for (i = 0; i < n; i++)
        if (tiku_editdoc_byte_at(v->doc, a + i) !=
            tiku_editdoc_byte_at(v->doc, b + i))
            return 0;
    return 1;
}

/** @brief The indentation of the line holding the opener @p closer pairs
 *         with that is still open before @p pos, in [@p from, @p to);
 *         0 when there is none. */
static int
opener_indent(tiku_codeview_t *v, tiku_edit_pos_t pos, char closer,
              tiku_edit_pos_t *from, tiku_edit_pos_t *to)
{
    tiku_edit_pos_t open = tiku_cv_open_before(v, pos, closer);
    int64_t line;

    if (open < 0)
        return 0;
    line = tiku_editdoc_line_of(v->doc, open);
    *from = tiku_editdoc_line_start(v->doc, line);
    *to = *from + blanks(v, line);
    return 1;
}

int
tiku_cv_type_closer(tiku_codeview_t *v, const char *text)
{
    const tiku_codeview_rules_t *r = tiku_cv_rules(v);
    tiku_edit_pos_t lo, hi, start, from = 0, to = 0;
    char *indent;
    size_t n;

    if (r->closes == NULL || text[0] == '\0' || text[1] != '\0' ||
        tiku_cv_many(v) || strchr(r->closes, text[0]) == NULL ||
        ordered(v, &lo, &hi) != TIKU_EDIT_OK || lo != hi ||
        indent_end(v, lo, &start) != lo ||
        !opener_indent(v, start, text[0], &from, &to) ||
        (to - from == lo - start && same_bytes(v, from, start, lo - start)))
        return 0;
    n = (size_t)(to - from);
    indent = copy_range(v, from, to, 1);
    if (indent == NULL) {
        v->status = TIKU_EDIT_NO_MEMORY;
        return 1;
    }
    indent[n] = text[0];
    v->status = apply_one(v, start, lo, indent, n + 1,
                          start + (tiku_edit_pos_t)n + 1,
                          start + (tiku_edit_pos_t)n + 1, TIKU_EDIT_TYPING);
    tiku_ed_free(indent);
    return 1;
}

/** @brief The bytes Unindent takes off @p line: one leading tab, or else
 *         up to a level of leading spaces. */
static tiku_edit_pos_t
unindent_width(const tiku_codeview_t *v, int64_t line)
{
    tiku_edit_pos_t s = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line), k = 0;
    tiku_edit_pos_t most = v->indent_spaces > 0 ? v->indent_spaces
                                                : v->tab_width;

    if (s < e && tiku_editdoc_byte_at(v->doc, s) == '\t')
        return 1;
    while (k < most && s + k < e &&
           tiku_editdoc_byte_at(v->doc, s + k) == ' ')
        k++;
    return k;
}

/** @brief Apply the batch with lines @p first to @p last selected whole,
 *         from the end back when @p backward. */
static tiku_edit_status_t
apply_lines(tiku_codeview_t *v, batch_t *b, int64_t first, int64_t last,
            int backward)
{
    tiku_edit_pos_t lo = tiku_editdoc_line_start(v->doc, first);
    tiku_edit_pos_t hi = batch_map(b, next_start(v, last), 0);

    if (backward)
        return batch_apply(v, b, hi, lo, 0);
    return batch_apply(v, b, lo, hi, 0);
}

tiku_edit_status_t
tiku_codeview_indent(tiku_codeview_t *view, int out)
{
    tiku_edit_pos_t anchor, caret, s, k;
    int64_t first, last, line;
    const char *level;
    size_t n;
    batch_t b;

    if (view == NULL ||
        touched(view, &first, &last, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    n = unit(view, &level);
    memset(&b, 0, sizeof b);
    for (line = first; line <= last; line++) {
        s = tiku_editdoc_line_start(view->doc, line);
        if (!out) {
            if (last == first || s != tiku_editdoc_line_end(view->doc, line))
                batch_add(&b, s, s, level, n, 0);
            continue;
        }
        k = unindent_width(view, line);
        if (k > 0)
            batch_add(&b, s, s + k, NULL, 0, 0);
    }
    if (last > first)
        return apply_lines(view, &b, first, last, caret < anchor);
    return batch_apply(view, &b, batch_map(&b, anchor, 1),
                       batch_map(&b, caret, 1), 0);
}

/** @brief Whether the text at @p p starts with @p marker, ASCII case
 *         aside; @return its length when it does, else 0. */
static size_t
marked(const tiku_codeview_t *v, tiku_edit_pos_t p, tiku_edit_pos_t end,
       const char *marker)
{
    size_t i, n;

    if (marker == NULL)
        return 0;
    n = strlen(marker);
    for (i = 0; i < n; i++) {
        int b = p + (tiku_edit_pos_t)i < end ?
                tiku_editdoc_byte_at(v->doc, p + (tiku_edit_pos_t)i) : -1;
        int m = (unsigned char)marker[i];

        if (b >= 'a' && b <= 'z')
            b -= 'a' - 'A';
        if (m >= 'a' && m <= 'z')
            m -= 'a' - 'A';
        if (b != m)
            return 0;
    }
    return n;
}

/** @brief Where a comment goes on @p line: after its blanks, and after a
 *         leading number and the blanks after it by numbered rules. */
static tiku_edit_pos_t
comment_point(const tiku_codeview_t *v, const tiku_codeview_rules_t *r,
              int64_t line)
{
    tiku_edit_pos_t p = tiku_editdoc_line_start(v->doc, line);
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line);
    int b;

    p += blanks(v, line);
    if (r->numbered && p < e && (b = tiku_editdoc_byte_at(v->doc, p)) >= '0'
        && b <= '9') {
        while (p < e && (b = tiku_editdoc_byte_at(v->doc, p)) >= '0' &&
               b <= '9')
            p++;
        while (p < e && blank(tiku_editdoc_byte_at(v->doc, p)))
            p++;
    }
    return p;
}

/* What comment_scan() found among the lines. */
#define SOME_TEXT   0x1     /* a line holds text                        */
#define ALL_MARKED  0x2     /* every line holding text is commented      */

/** @brief Whether any of lines @p first to @p last hold text and whether
 *         all of those are commented; the least indentation among them in
 *         @p least. */
static int
comment_scan(const tiku_codeview_t *v, const tiku_codeview_rules_t *r,
             int64_t first, int64_t last, tiku_edit_pos_t *least)
{
    tiku_edit_pos_t p, e;
    int64_t line;
    int state = ALL_MARKED;

    for (line = first; line <= last; line++) {
        p = comment_point(v, r, line);
        e = tiku_editdoc_line_end(v->doc, line);
        if (p == e)
            continue;
        state |= SOME_TEXT;
        if (!marked(v, p, e, r->comment) && !marked(v, p, e, r->comment_alt))
            state &= ~ALL_MARKED;
        if (*least < 0 || blanks(v, line) < *least)
            *least = blanks(v, line);
    }
    return state;
}

/** @brief Add @p line's comment edit to the batch: @p mark in at the
 *         least indentation, or with ALL_MARKED in @p state the comment
 *         and one space after it off. */
static void
comment_line(const tiku_codeview_t *v, const tiku_codeview_rules_t *r,
             batch_t *b, int64_t line, int state, tiku_edit_pos_t least,
             const char *mark)
{
    tiku_edit_pos_t p = comment_point(v, r, line);
    tiku_edit_pos_t e = tiku_editdoc_line_end(v->doc, line), m;

    if (p == e)
        return;
    if (!(state & ALL_MARKED)) {
        if (!r->numbered)
            p = tiku_editdoc_line_start(v->doc, line) + least;
        batch_add(b, p, p, mark, strlen(mark), 0);
        return;
    }
    m = (tiku_edit_pos_t)marked(v, p, e, r->comment);
    if (m == 0)
        m = (tiku_edit_pos_t)marked(v, p, e, r->comment_alt);
    if (p + m < e && tiku_editdoc_byte_at(v->doc, p + m) == ' ')
        m++;
    batch_add(b, p, p + m, NULL, 0, 0);
}

tiku_edit_status_t
tiku_codeview_toggle_comment(tiku_codeview_t *view)
{
    const tiku_codeview_rules_t *r;
    tiku_edit_pos_t anchor, caret, least = -1;
    int64_t first, last, line;
    int state;
    char mark[32];
    batch_t b;

    if (view == NULL ||
        touched(view, &first, &last, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    r = tiku_cv_rules(view);
    if (r->comment == NULL || r->comment[0] == '\0')
        return TIKU_EDIT_NOTHING;
    if (strlen(r->comment) >= sizeof mark - 1)
        return TIKU_EDIT_LIMIT;
    state = comment_scan(view, r, first, last, &least);
    if (!(state & SOME_TEXT))
        return TIKU_EDIT_NOTHING;
    snprintf(mark, sizeof mark, "%s ", r->comment);
    memset(&b, 0, sizeof b);
    for (line = first; line <= last; line++)
        comment_line(view, r, &b, line, state, least, mark);
    if (caret < anchor)
        return batch_apply(view, &b, batch_map(&b, anchor, 1),
                           batch_map(&b, caret, first == last), 0);
    return batch_apply(view, &b, batch_map(&b, anchor, first == last),
                       batch_map(&b, caret, 1), 0);
}

tiku_edit_status_t
tiku_codeview_duplicate_lines(tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor, caret, bs, be;
    int64_t first, last;
    size_t nl = 0, len;
    char *text;
    tiku_edit_status_t st;

    if (view == NULL ||
        touched(view, &first, &last, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    bs = tiku_editdoc_line_start(view->doc, first);
    be = next_start(view, last);
    if (last == tiku_editdoc_lines(view->doc) - 1)
        nl = strlen(view->newline);
    len = (size_t)(be - bs);
    text = copy_range(view, bs, be, nl);
    if (text == NULL)
        return TIKU_EDIT_NO_MEMORY;
    memmove(text + nl, text, len);
    memcpy(text, view->newline, nl);
    st = apply_one(view, be, be, text, len + nl,
                   anchor + (tiku_edit_pos_t)(len + nl),
                   caret + (tiku_edit_pos_t)(len + nl), 0);
    tiku_ed_free(text);
    return st;
}

/**
 * @brief Text whose first @p a bytes and next @p z bytes have changed
 *        places, the first part's last @p brk bytes, its line break,
 *        staying between them when @p keep is 0.
 *
 * With @p keep the parts swap whole.  A copy of [@p s, @p s + a + z),
 * which the caller frees; NULL when there is no memory.
 */
static char *
swapped(const tiku_codeview_t *v, tiku_edit_pos_t s, size_t a, size_t z,
        size_t brk, int keep)
{
    char *text = copy_range(v, s, s + (tiku_edit_pos_t)(a + z), 0);
    char *out = text != NULL ? tiku_ed_alloc(a + z + 1) : NULL;

    if (out == NULL) {
        tiku_ed_free(text);
        return NULL;
    }
    memcpy(out, text + a, z);
    if (keep) {
        memcpy(out + z, text, a);
    } else {
        memcpy(out + z, text + a - brk, brk);
        memcpy(out + z + brk, text, a - brk);
    }
    tiku_ed_free(text);
    return out;
}

/** @brief Move lines @p first to @p last above the line before them;
 *         when they end the document, that line's break goes between the
 *         two. */
static tiku_edit_status_t
move_up(tiku_codeview_t *v, int64_t first, int64_t last,
        tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t ns = tiku_editdoc_line_start(v->doc, first - 1);
    tiku_edit_pos_t bs = tiku_editdoc_line_start(v->doc, first);
    tiku_edit_pos_t be = next_start(v, last);
    size_t brk = (size_t)(bs - tiku_editdoc_line_end(v->doc, first - 1));
    int final = last == tiku_editdoc_lines(v->doc) - 1;
    char *text = swapped(v, ns, (size_t)(bs - ns), (size_t)(be - bs), brk,
                         !final);
    tiku_edit_status_t st;

    if (text == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = apply_one(v, ns, be, text, (size_t)(be - ns), anchor - (bs - ns),
                   caret - (bs - ns), 0);
    tiku_ed_free(text);
    return st;
}

/** @brief Move lines @p first to @p last below the line after them; when
 *         that line is the last, the block's own break goes between the
 *         two and a place in or after it goes to the block's new end. */
static tiku_edit_status_t
move_down(tiku_codeview_t *v, int64_t first, int64_t last,
          tiku_edit_pos_t anchor, tiku_edit_pos_t caret)
{
    tiku_edit_pos_t bs = tiku_editdoc_line_start(v->doc, first);
    tiku_edit_pos_t be = next_start(v, last);
    tiku_edit_pos_t ne = next_start(v, last + 1);
    tiku_edit_pos_t brk = tiku_editdoc_line_end(v->doc, last);
    int final = last + 1 == tiku_editdoc_lines(v->doc) - 1;
    tiku_edit_pos_t limit = final ? brk : be;
    tiku_edit_pos_t moved = (ne - be) + (final ? be - brk : 0);
    char *text = swapped(v, bs, (size_t)(be - bs), (size_t)(ne - be),
                         (size_t)(be - brk), !final);
    tiku_edit_status_t st;

    if (text == NULL)
        return TIKU_EDIT_NO_MEMORY;
    st = apply_one(v, bs, ne, text, (size_t)(ne - bs),
                   (anchor < limit ? anchor : limit) + moved,
                   (caret < limit ? caret : limit) + moved, 0);
    tiku_ed_free(text);
    return st;
}

tiku_edit_status_t
tiku_codeview_move_lines(tiku_codeview_t *view, int down)
{
    tiku_edit_pos_t anchor, caret;
    int64_t first, last;

    if (view == NULL ||
        touched(view, &first, &last, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    if (down ? last == tiku_editdoc_lines(view->doc) - 1 : first == 0)
        return TIKU_EDIT_NOTHING;
    if (down)
        return move_down(view, first, last, anchor, caret);
    return move_up(view, first, last, anchor, caret);
}

tiku_edit_status_t
tiku_codeview_delete_lines(tiku_codeview_t *view)
{
    tiku_edit_pos_t anchor, caret, rs, re;
    int64_t first, last;

    if (view == NULL ||
        touched(view, &first, &last, &anchor, &caret) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    rs = tiku_editdoc_line_start(view->doc, first);
    re = next_start(view, last);
    if (last == tiku_editdoc_lines(view->doc) - 1 && first > 0)
        rs = tiku_editdoc_line_end(view->doc, first - 1);
    if (rs == re)
        return TIKU_EDIT_NOTHING;
    return apply_one(view, rs, re, NULL, 0, rs, rs, 0);
}

tiku_edit_status_t
tiku_codeview_delete_word(tiku_codeview_t *view, int forward)
{
    tiku_edit_pos_t lo, hi;

    if (ordered(view, &lo, &hi) != TIKU_EDIT_OK)
        return TIKU_EDIT_INVALID;
    if (tiku_cv_many(view))
        return tiku_cv_delete_each(view, forward, 1);
    if (lo < hi)
        return tiku_cv_replace(view, lo, hi, NULL, 0, 0);
    if (forward)
        hi = tiku_cv_word_right(view, lo);
    else
        lo = tiku_cv_word_left(view, hi);
    if (lo == hi)
        return TIKU_EDIT_NOTHING;
    return tiku_cv_replace(view, lo, hi, NULL, 0, 0);
}
