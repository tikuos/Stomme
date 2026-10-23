/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_buffer.c - gap buffers of bytes and of 64-bit positions.
 *
 * Adapted from Scintilla 5.6.7 src/SplitVector.h, with RangeAddDelta from
 * src/Partitioning.h, both Copyright 1998-2007 by Neil Hodgson; see
 * LICENSE.Scintilla and UPSTREAM.md for what changed.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include "tiku_editdoc_private.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define GROWTH_PORTION 6        /* grow by a sixth of the size at least */
#define INITIAL_GROW   8        /* the first growth step, in elements   */

/** @brief Move the gap of a buffer of @p elem-byte elements so it starts
 *         at @p pos. */
static void
gap_to(void *body, ptrdiff_t *part1, ptrdiff_t gap, ptrdiff_t pos, size_t elem)
{
    char *b = body;

    if (pos == *part1)
        return;
    if (gap > 0) {
        if (pos < *part1)
            memmove(b + (size_t)(pos + gap) * elem, b + (size_t)pos * elem,
                    (size_t)(*part1 - pos) * elem);
        else
            memmove(b + (size_t)*part1 * elem,
                    b + (size_t)(*part1 + gap) * elem,
                    (size_t)(pos - *part1) * elem);
    }
    *part1 = pos;
}

/** @brief The capacity a buffer grows to for @p extra more elements and
 *         a growth step, the step doubled while it is under a sixth of
 *         the capacity.  @return 0 when the arithmetic would overflow. */
static ptrdiff_t
grown_size(ptrdiff_t capacity, ptrdiff_t extra, ptrdiff_t *grow, size_t elem)
{
    ptrdiff_t step = *grow, limit = PTRDIFF_MAX / (ptrdiff_t)elem;

    while (step < capacity / GROWTH_PORTION && step <= limit / 2)
        step *= 2;
    if (extra > limit || capacity > limit - extra ||
        step > limit - capacity - extra)
        return 0;
    *grow = step;
    return capacity + extra + step;
}

void
tiku_ed_bytes_init(tiku_ed_bytes_t *b)
{
    memset(b, 0, sizeof *b);
    b->grow = INITIAL_GROW;
}

void
tiku_ed_bytes_free(tiku_ed_bytes_t *b)
{
    tiku_ed_free(b->body);
    tiku_ed_bytes_init(b);
}

int
tiku_ed_bytes_reserve(tiku_ed_bytes_t *b, ptrdiff_t extra)
{
    ptrdiff_t grow = b->grow, size;
    char *body;

    if (extra < 0)
        return 0;
    if (b->gap >= extra)
        return 1;
    size = grown_size(b->capacity, extra, &grow, 1);
    if (size == 0)
        return 0;
    gap_to(b->body, &b->part1, b->gap, b->length, 1);
    body = tiku_ed_realloc(b->body, (size_t)size);
    if (!body)
        return 0;
    b->body = body;
    b->gap += size - b->capacity;
    b->capacity = size;
    b->grow = grow;
    return 1;
}

void
tiku_ed_bytes_insert(tiku_ed_bytes_t *b, ptrdiff_t pos, const char *s,
                     ptrdiff_t n)
{
    if (n <= 0 || pos < 0 || pos > b->length)
        return;
    assert(b->gap >= n);
    gap_to(b->body, &b->part1, b->gap, pos, 1);
    memcpy(b->body + b->part1, s, (size_t)n);
    b->length += n;
    b->part1 += n;
    b->gap -= n;
}

void
tiku_ed_bytes_delete(tiku_ed_bytes_t *b, ptrdiff_t pos, ptrdiff_t n)
{
    if (n <= 0 || pos < 0 || n > b->length - pos)
        return;
    if (pos == 0 && n == b->length) {
        /* The storage is kept, so a commit that empties the text can
         * refill it without allocating. */
        b->length = 0;
        b->part1 = 0;
        b->gap = b->capacity;
        return;
    }
    gap_to(b->body, &b->part1, b->gap, pos, 1);
    b->length -= n;
    b->gap += n;
}

int
tiku_ed_bytes_at(const tiku_ed_bytes_t *b, ptrdiff_t pos)
{
    if (pos < 0 || pos >= b->length)
        return 0;
    if (pos < b->part1)
        return (unsigned char)b->body[pos];
    return (unsigned char)b->body[b->gap + pos];
}

void
tiku_ed_bytes_copy(const tiku_ed_bytes_t *b, char *out, ptrdiff_t pos,
                   ptrdiff_t n)
{
    ptrdiff_t first = 0;

    if (n <= 0)
        return;
    if (pos < b->part1) {
        first = b->part1 - pos;
        if (first > n)
            first = n;
        memcpy(out, b->body + pos, (size_t)first);
    }
    if (n > first)
        memcpy(out + first, b->body + pos + first + b->gap,
               (size_t)(n - first));
}

int
tiku_ed_bytes_each(const tiku_ed_bytes_t *b, ptrdiff_t pos, ptrdiff_t n,
                   tiku_edit_bytes_fn fn, void *ctx)
{
    ptrdiff_t first = 0;

    if (n <= 0)
        return 0;
    if (pos < b->part1) {
        first = b->part1 - pos;
        if (first > n)
            first = n;
        if (fn(ctx, b->body + pos, (size_t)first))
            return 1;
    }
    if (n > first)
        return fn(ctx, b->body + pos + first + b->gap, (size_t)(n - first));
    return 0;
}

void
tiku_ed_starts_init(tiku_ed_starts_t *v)
{
    memset(v, 0, sizeof *v);
    v->grow = INITIAL_GROW;
}

void
tiku_ed_starts_free(tiku_ed_starts_t *v)
{
    tiku_ed_free(v->body);
    tiku_ed_starts_init(v);
}

int
tiku_ed_starts_reserve(tiku_ed_starts_t *v, ptrdiff_t extra)
{
    ptrdiff_t grow = v->grow, size;
    int64_t *body;

    if (extra < 0)
        return 0;
    if (v->gap >= extra)
        return 1;
    size = grown_size(v->capacity, extra, &grow, sizeof *body);
    if (size == 0)
        return 0;
    gap_to(v->body, &v->part1, v->gap, v->length, sizeof *body);
    body = tiku_ed_realloc(v->body, (size_t)size * sizeof *body);
    if (!body)
        return 0;
    v->body = body;
    v->gap += size - v->capacity;
    v->capacity = size;
    v->grow = grow;
    return 1;
}

void
tiku_ed_starts_insert(tiku_ed_starts_t *v, ptrdiff_t pos, int64_t value)
{
    tiku_ed_starts_insert_many(v, pos, &value, 1);
}

void
tiku_ed_starts_insert_many(tiku_ed_starts_t *v, ptrdiff_t pos,
                           const int64_t *values, ptrdiff_t n)
{
    if (n <= 0 || pos < 0 || pos > v->length)
        return;
    assert(v->gap >= n);
    gap_to(v->body, &v->part1, v->gap, pos, sizeof *v->body);
    memcpy(v->body + v->part1, values, (size_t)n * sizeof *v->body);
    v->length += n;
    v->part1 += n;
    v->gap -= n;
}

void
tiku_ed_starts_delete(tiku_ed_starts_t *v, ptrdiff_t pos, ptrdiff_t n)
{
    if (n <= 0 || pos < 0 || n > v->length - pos)
        return;
    if (pos == 0 && n == v->length) {
        v->length = 0;
        v->part1 = 0;
        v->gap = v->capacity;
        return;
    }
    gap_to(v->body, &v->part1, v->gap, pos, sizeof *v->body);
    v->length -= n;
    v->gap += n;
}

int64_t
tiku_ed_starts_at(const tiku_ed_starts_t *v, ptrdiff_t pos)
{
    if (pos < 0 || pos >= v->length)
        return 0;
    if (pos < v->part1)
        return v->body[pos];
    return v->body[v->gap + pos];
}

void
tiku_ed_starts_set(tiku_ed_starts_t *v, ptrdiff_t pos, int64_t value)
{
    if (pos < 0 || pos >= v->length)
        return;
    if (pos < v->part1)
        v->body[pos] = value;
    else
        v->body[v->gap + pos] = value;
}

void
tiku_ed_starts_add(tiku_ed_starts_t *v, ptrdiff_t start, ptrdiff_t end,
                   int64_t delta)
{
    ptrdiff_t i = start, first;

    if (start < 0 || end > v->length || start >= end)
        return;
    /* The run before the gap, then the run after it. */
    first = v->part1 < end ? v->part1 : end;
    for (; i < first; i++)
        v->body[i] += delta;
    for (; i < end; i++)
        v->body[v->gap + i] += delta;
}

void
tiku_ed_starts_copy(const tiku_ed_starts_t *v, int64_t *out, ptrdiff_t pos,
                    ptrdiff_t n)
{
    ptrdiff_t i;

    for (i = 0; i < n; i++)
        out[i] = tiku_ed_starts_at(v, pos + i);
}
