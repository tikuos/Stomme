/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_lines.c - line starts with a deferred shift, and the text
 * edits that keep them current across LF, CR and CRLF.
 *
 * Adapted from Scintilla 5.6.7 src/Partitioning.h, Copyright 1998-2007,
 * and src/CellBuffer.cxx, Copyright 1998-2001, both by Neil Hodgson; see
 * LICENSE.Scintilla and UPSTREAM.md for what changed.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include "tiku_editdoc_private.h"

#include <assert.h>

/* An edit less than a tenth of the line starts before the pending step
 * moves the step back to it; one further away applies the step first. */
#define CLOSE_PORTION 10
#define LINE_GROW     256       /* the first growth step, in line starts */
#define BLOCK         128       /* line starts gathered before each add  */

/** @brief Add the pending shift to the starts after the step's partition
 *         up to @p upto, which becomes the step's partition; at the last
 *         partition the shift is used up. */
static void
apply_step(tiku_ed_lines_t *l, int64_t upto)
{
    if (l->step_length != 0)
        tiku_ed_starts_add(&l->body, (ptrdiff_t)l->step_partition + 1,
                           (ptrdiff_t)upto + 1, l->step_length);
    l->step_partition = upto;
    if (l->step_partition >= tiku_ed_lines_count(l)) {
        l->step_partition = tiku_ed_lines_count(l);
        l->step_length = 0;
    }
}

/** @brief Take the pending shift back off the starts after @p down, so
 *         it is pending from there. */
static void
back_step(tiku_ed_lines_t *l, int64_t down)
{
    if (l->step_length != 0)
        tiku_ed_starts_add(&l->body, (ptrdiff_t)down + 1,
                           (ptrdiff_t)l->step_partition + 1,
                           -l->step_length);
    l->step_partition = down;
}

int
tiku_ed_lines_init(tiku_ed_lines_t *l)
{
    tiku_ed_starts_init(&l->body);
    l->body.grow = LINE_GROW;
    l->step_partition = 0;
    l->step_length = 0;
    if (!tiku_ed_starts_reserve(&l->body, 2))
        return 0;
    /* The first start stays 0; the second is the end of the one line. */
    tiku_ed_starts_insert(&l->body, 0, 0);
    tiku_ed_starts_insert(&l->body, 1, 0);
    return 1;
}

void
tiku_ed_lines_free(tiku_ed_lines_t *l)
{
    tiku_ed_starts_free(&l->body);
    l->step_partition = 0;
    l->step_length = 0;
}

void
tiku_ed_lines_reset(tiku_ed_lines_t *l)
{
    /* The two entries go into the storage already held, so a reset
     * never allocates. */
    tiku_ed_starts_delete(&l->body, 0, l->body.length);
    tiku_ed_starts_insert(&l->body, 0, 0);
    tiku_ed_starts_insert(&l->body, 1, 0);
    l->step_partition = 0;
    l->step_length = 0;
}

int
tiku_ed_lines_reserve(tiku_ed_lines_t *l, ptrdiff_t extra)
{
    return tiku_ed_starts_reserve(&l->body, extra);
}

int64_t
tiku_ed_lines_count(const tiku_ed_lines_t *l)
{
    return (int64_t)l->body.length - 1;
}

int64_t
tiku_ed_lines_start(const tiku_ed_lines_t *l, int64_t partition)
{
    int64_t pos;

    if (partition < 0 || partition >= (int64_t)l->body.length)
        return 0;
    pos = tiku_ed_starts_at(&l->body, (ptrdiff_t)partition);
    if (partition > l->step_partition)
        pos += l->step_length;
    return pos;
}

int64_t
tiku_ed_lines_find(const tiku_ed_lines_t *l, int64_t pos)
{
    int64_t lower = 0, upper = tiku_ed_lines_count(l);

    if (l->body.length <= 1)
        return 0;
    if (pos >= tiku_ed_lines_start(l, upper))
        return upper - 1;
    do {
        int64_t middle = (upper + lower + 1) / 2;       /* round high */
        int64_t at = tiku_ed_starts_at(&l->body, (ptrdiff_t)middle);

        if (middle > l->step_partition)
            at += l->step_length;
        if (pos < at)
            upper = middle - 1;
        else
            lower = middle;
    } while (lower < upper);
    return lower;
}

void
tiku_ed_lines_insert(tiku_ed_lines_t *l, int64_t partition, int64_t pos)
{
    if (l->step_partition < partition)
        apply_step(l, partition);
    tiku_ed_starts_insert(&l->body, (ptrdiff_t)partition, pos);
    l->step_partition++;
}

void
tiku_ed_lines_insert_many(tiku_ed_lines_t *l, int64_t partition,
                          const int64_t *pos, ptrdiff_t n)
{
    if (l->step_partition < partition)
        apply_step(l, partition);
    tiku_ed_starts_insert_many(&l->body, (ptrdiff_t)partition, pos, n);
    l->step_partition += n;
}

void
tiku_ed_lines_set_start(tiku_ed_lines_t *l, int64_t partition, int64_t pos)
{
    apply_step(l, partition + 1);
    if (partition < 0 || partition >= (int64_t)l->body.length)
        return;
    tiku_ed_starts_set(&l->body, (ptrdiff_t)partition, pos);
}

void
tiku_ed_lines_shift(tiku_ed_lines_t *l, int64_t partition, int64_t delta)
{
    /* One pending step holds the shift.  With none pending, the change
     * starts it.  A change at or after the step applies the step up to
     * the change, and one within a tenth of the line starts before it
     * moves the step back to the change; delta then joins the step.  A
     * change further away applies the whole step and starts a new one. */
    if (l->step_length == 0) {
        l->step_partition = partition;
        l->step_length = delta;
    } else if (partition >= l->step_partition) {
        apply_step(l, partition);
        l->step_length += delta;
    } else if (partition >= l->step_partition -
               (int64_t)l->body.length / CLOSE_PORTION) {
        back_step(l, partition);
        l->step_length += delta;
    } else {
        apply_step(l, tiku_ed_lines_count(l));
        l->step_partition = partition;
        l->step_length = delta;
    }
}

void
tiku_ed_lines_remove(tiku_ed_lines_t *l, int64_t partition)
{
    if (partition > l->step_partition) {
        apply_step(l, partition);
        l->step_partition--;
    } else {
        l->step_partition--;
    }
    tiku_ed_starts_delete(&l->body, (ptrdiff_t)partition, 1);
}

int
tiku_ed_text_init(tiku_ed_text_t *t)
{
    tiku_ed_bytes_init(&t->bytes);
    return tiku_ed_lines_init(&t->lines);
}

void
tiku_ed_text_free(tiku_ed_text_t *t)
{
    tiku_ed_bytes_free(&t->bytes);
    tiku_ed_lines_free(&t->lines);
}

int
tiku_ed_text_reserve(tiku_ed_text_t *t, int64_t bytes, int64_t lines)
{
    if (bytes < 0 || lines < 0 || bytes > PTRDIFF_MAX || lines > PTRDIFF_MAX)
        return 0;
    return tiku_ed_bytes_reserve(&t->bytes, (ptrdiff_t)bytes) &&
           tiku_ed_lines_reserve(&t->lines, (ptrdiff_t)lines);
}

int64_t
tiku_ed_text_line_cost(const char *s, int64_t n)
{
    int64_t i, cost = 2;

    /* An upper bound on the starts an insertion of the text adds: one
     * per CR or LF, one for splitting a CRLF at the insertion point, and
     * one spare. */
    for (i = 0; i < n; i++)
        if (s[i] == '\r' || s[i] == '\n')
            cost++;
    return cost;
}

/** @brief The byte at @p pos, 0 outside. */
static unsigned char
byte_at(const tiku_ed_text_t *t, int64_t pos)
{
    return (unsigned char)tiku_ed_bytes_at(&t->bytes, (ptrdiff_t)pos);
}

void
tiku_ed_text_insert(tiku_ed_text_t *t, int64_t pos, const char *s, int64_t n)
{
    unsigned char after, prev, ch = 0;
    int64_t line, starts[BLOCK];
    ptrdiff_t gathered = 0;
    const char *ptr = s, *end;

    if (n <= 0)
        return;
    after = byte_at(t, pos);
    line = tiku_ed_lines_find(&t->lines, pos) + 1;
    tiku_ed_bytes_insert(&t->bytes, (ptrdiff_t)pos, s, (ptrdiff_t)n);
    tiku_ed_lines_shift(&t->lines, line - 1, n);
    prev = byte_at(t, pos - 1);
    if (prev == '\r' && after == '\n') {
        /* Splitting up a CRLF pair at pos. */
        tiku_ed_lines_insert(&t->lines, line, pos);
        line++;
    }
    end = s + n - 1;
    if (prev == '\r' && *ptr == '\n') {
        /* The inserted LF completes a CR already there. */
        ++ptr;
        tiku_ed_lines_set_start(&t->lines, line - 1, pos + (ptr - s));
    }
    while (ptr < end) {
        ch = (unsigned char)*ptr++;
        while (ch != '\r' && ch != '\n' && ptr < end)
            ch = (unsigned char)*ptr++;
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && *ptr == '\n')
                ++ptr;
            starts[gathered++] = pos + (ptr - s);
            if (gathered == BLOCK) {
                tiku_ed_lines_insert_many(&t->lines, line, starts, gathered);
                line += gathered;
                gathered = 0;
            }
        }
    }
    if (gathered) {
        tiku_ed_lines_insert_many(&t->lines, line, starts, gathered);
        line += gathered;
    }
    ch = (unsigned char)*end;
    if (ptr == end) {
        ++ptr;
        if (ch == '\r' || ch == '\n') {
            tiku_ed_lines_insert(&t->lines, line, pos + (ptr - s));
            line++;
        }
    }
    /* A CR inserted before an LF already there joins it: the line break
     * the CR made is the LF's, already in the index. */
    if (after == '\n' && ch == '\r')
        tiku_ed_lines_remove(&t->lines, line - 1);
}

void
tiku_ed_text_delete(tiku_ed_text_t *t, int64_t pos, int64_t n)
{
    unsigned char before, next, ch, after;
    int64_t line, i;
    int ignore_lf = 0;

    if (n <= 0)
        return;
    if (pos == 0 && n == (int64_t)t->bytes.length) {
        tiku_ed_lines_reset(&t->lines);
        tiku_ed_bytes_delete(&t->bytes, 0, (ptrdiff_t)n);
        return;
    }
    /* The index is fixed up first, while the bytes are still there to
     * say which line breaks go. */
    line = tiku_ed_lines_find(&t->lines, pos) + 1;
    tiku_ed_lines_shift(&t->lines, line - 1, -n);
    before = byte_at(t, pos - 1);
    next = byte_at(t, pos);
    if (before == '\r' && next == '\n') {
        /* Deleting from between a CR and its LF: the LF's line starts at
         * pos, and the first LF deleted removes no line. */
        tiku_ed_lines_set_start(&t->lines, line, pos);
        line++;
        ignore_lf = 1;
    }
    ch = next;
    for (i = 0; i < n; i++) {
        next = byte_at(t, pos + i + 1);
        if (ch == '\r') {
            if (next != '\n')
                tiku_ed_lines_remove(&t->lines, line);
        } else if (ch == '\n') {
            if (ignore_lf)
                ignore_lf = 0;
            else
                tiku_ed_lines_remove(&t->lines, line);
        }
        ch = next;
    }
    after = byte_at(t, pos + n);
    if (before == '\r' && after == '\n') {
        /* The deletion brings a CR next to an LF: one line break. */
        tiku_ed_lines_remove(&t->lines, line - 1);
        tiku_ed_lines_set_start(&t->lines, line - 1, pos + 1);
    }
    tiku_ed_bytes_delete(&t->bytes, (ptrdiff_t)pos, (ptrdiff_t)n);
}

int64_t
tiku_ed_text_line_end(const tiku_ed_text_t *t, int64_t line)
{
    int64_t count = tiku_ed_lines_count(&t->lines), pos;

    if (line < 0 || line >= count)
        return -1;
    if (line == count - 1)
        return (int64_t)t->bytes.length;
    pos = tiku_ed_lines_start(&t->lines, line + 1) - 1;  /* the CR or LF */
    if (pos > tiku_ed_lines_start(&t->lines, line) &&
        byte_at(t, pos) == '\n' && byte_at(t, pos - 1) == '\r')
        pos--;
    return pos;
}
