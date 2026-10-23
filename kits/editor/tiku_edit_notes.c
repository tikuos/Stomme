/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_notes.c - indicators, ranges of text kept through each
 * replacement, and annotations, text kept at the start of a line.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_edit_decorations.h"
#include "tiku_editdoc_private.h"

#include <string.h>

/** @brief A slot's next life: never 0, so a zeroed handle is none. */
static uint32_t
next_generation(uint32_t generation)
{
    return generation + 1 ? generation + 1 : 1;
}

/** @brief Where a range end at @p p goes when [s, e) becomes n bytes:
 *         a start (@p starts) stays after new text put in at it, an end
 *         before. */
static int64_t
map_end(int64_t p, int starts, int64_t s, int64_t e, int64_t n)
{
    if (p < s)
        return p;
    if (p <= e)
        return starts ? s + n : s;
    return p - (e - s) + n;
}

/** @brief Where an annotation at line start @p p goes when [s, e) becomes
 *         n bytes: one at @p s moves past the new text, one after @p s up
 *         to @p e moves to @p s. */
static int64_t
map_line(int64_t p, int64_t s, int64_t e, int64_t n)
{
    if (p < s)
        return p;
    if (p == s)
        return s + n;
    if (p <= e)
        return s;
    return p - (e - s) + n;
}

void
tiku_ed_notes_map(tiku_editdoc_t *doc, int64_t s, int64_t e, int64_t n)
{
    size_t i;

    for (i = 0; i < doc->nindicators; i++) {
        tiku_ed_indicator_t *d = &doc->indicators[i];

        if (d->live) {
            d->start = map_end(d->start, 1, s, e, n);
            d->end = map_end(d->end, 0, s, e, n);
        }
    }
    for (i = 0; i < doc->nannotations; i++)
        if (doc->annotations[i].live)
            doc->annotations[i].pos = map_line(doc->annotations[i].pos, s,
                                               e, n);
}

/** @brief Whether @p p sits between a CR and its LF. */
static int
inside_crlf(const tiku_editdoc_t *doc, int64_t p)
{
    const tiku_ed_bytes_t *b = &doc->text.bytes;

    return p > 0 && p < (int64_t)b->length &&
           tiku_ed_bytes_at(b, (ptrdiff_t)p - 1) == '\r' &&
           tiku_ed_bytes_at(b, (ptrdiff_t)p) == '\n';
}

/** @brief Free annotation @p a's text and its slot, and raise the
 *         annotation serial. */
static void
annotation_drop(tiku_editdoc_t *doc, tiku_ed_annotation_t *a)
{
    tiku_ed_free(a->text);
    a->text = NULL;
    a->live = 0;
    doc->annotation_serial++;
}

void
tiku_ed_notes_settle(tiku_editdoc_t *doc)
{
    size_t i;

    for (i = 0; i < doc->nindicators; i++) {
        tiku_ed_indicator_t *d = &doc->indicators[i];

        if (!d->live)
            continue;
        if (inside_crlf(doc, d->start))
            d->start++;
        if (inside_crlf(doc, d->end))
            d->end--;
        if (d->end <= d->start) {
            d->live = 0;
            d->generation = next_generation(d->generation);
        }
    }
    for (i = 0; i < doc->nannotations; i++) {
        tiku_ed_annotation_t *a = &doc->annotations[i];

        if (a->live)
            a->pos = tiku_ed_lines_start(&doc->text.lines,
                                         tiku_ed_lines_find(&doc->text.lines,
                                                            a->pos));
    }
}

void
tiku_ed_notes_drop(tiku_editdoc_t *doc)
{
    size_t i;

    for (i = 0; i < doc->nindicators; i++) {
        tiku_ed_indicator_t *d = &doc->indicators[i];

        if (d->live) {
            d->live = 0;
            d->generation = next_generation(d->generation);
        }
    }
    for (i = 0; i < doc->nannotations; i++)
        if (doc->annotations[i].live)
            annotation_drop(doc, &doc->annotations[i]);
}

void
tiku_ed_notes_free(tiku_editdoc_t *doc)
{
    size_t i;

    for (i = 0; i < doc->nannotations; i++)
        tiku_ed_free(doc->annotations[i].text);
    tiku_ed_free(doc->annotations);
    tiku_ed_free(doc->indicators);
    doc->annotations = NULL;
    doc->indicators = NULL;
    doc->nannotations = 0;
    doc->nindicators = 0;
}

/** @brief Room for one more of @p size bytes in @p table, which holds
 *         @p n slots, live ones marked by their byte at @p live; @return
 *         a free slot's index, or -1 when there is no room. */
static ptrdiff_t
free_slot(void **table, size_t *n, size_t size, size_t live)
{
    unsigned char *t = *table, *grown;
    size_t i, cap;

    for (i = 0; i < *n; i++)
        if (!t[i * size + live])
            return (ptrdiff_t)i;
    cap = *n ? *n * 2 : 4;
    if (cap > UINT32_MAX || cap > SIZE_MAX / size)
        return -1;
    grown = tiku_ed_realloc(t, cap * size);
    if (!grown)
        return -1;
    memset(grown + *n * size, 0, (cap - *n) * size);
    *table = grown;
    *n = cap;
    return (ptrdiff_t)i;
}

/** @brief The live indicator a handle names, or NULL. */
static tiku_ed_indicator_t *
indicator_get(const tiku_editdoc_t *doc, tiku_edit_indicator_t indicator)
{
    tiku_ed_indicator_t *d;

    if (!doc || indicator.slot == 0 || indicator.slot > doc->nindicators)
        return NULL;
    d = &doc->indicators[indicator.slot - 1];
    return d->live && d->generation == indicator.generation ? d : NULL;
}

tiku_edit_status_t
tiku_editdoc_indicator_add(tiku_editdoc_t *doc, tiku_edit_range_t range,
                           unsigned kind, tiku_edit_indicator_t *indicator)
{
    ptrdiff_t i;
    tiku_ed_indicator_t *d;

    if (!doc || kind >= TIKU_EDIT_INDICATOR_KINDS)
        return TIKU_EDIT_INVALID;
    if (range.start < 0 || range.end <= range.start ||
        range.end > (int64_t)doc->text.bytes.length ||
        !tiku_editdoc_is_boundary(doc, range.start) ||
        !tiku_editdoc_is_boundary(doc, range.end))
        return TIKU_EDIT_INVALID_RANGE;
    i = free_slot((void **)&doc->indicators, &doc->nindicators, sizeof *d,
                  offsetof(tiku_ed_indicator_t, live));
    if (i < 0)
        return TIKU_EDIT_NO_MEMORY;
    d = &doc->indicators[i];
    d->generation = next_generation(d->generation);
    d->live = 1;
    d->kind = (uint8_t)kind;
    d->start = range.start;
    d->end = range.end;
    if (indicator) {
        indicator->slot = (uint32_t)i + 1;
        indicator->generation = d->generation;
    }
    return TIKU_EDIT_OK;
}

tiku_edit_status_t
tiku_editdoc_indicator_range(const tiku_editdoc_t *doc,
                             tiku_edit_indicator_t indicator,
                             tiku_edit_range_t *range)
{
    const tiku_ed_indicator_t *d = indicator_get(doc, indicator);

    if (!d)
        return TIKU_EDIT_NOTHING;
    if (range) {
        range->start = d->start;
        range->end = d->end;
    }
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_indicator_remove(tiku_editdoc_t *doc,
                              tiku_edit_indicator_t indicator)
{
    tiku_ed_indicator_t *d = indicator_get(doc, indicator);

    if (d) {
        d->live = 0;
        d->generation = next_generation(d->generation);
    }
}

size_t
tiku_editdoc_indicators_clear(tiku_editdoc_t *doc, uint32_t kinds)
{
    size_t i, removed = 0;

    for (i = 0; doc && i < doc->nindicators; i++) {
        tiku_ed_indicator_t *d = &doc->indicators[i];

        if (d->live && (kinds & ((uint32_t)1 << d->kind))) {
            d->live = 0;
            d->generation = next_generation(d->generation);
            removed++;
        }
    }
    return removed;
}

/** @brief Whether @p d is taken over @p best in a search forward, or with
 *         @p backward back: the start nearer the search's position, then
 *         the wider range. */
static int
nearer(const tiku_ed_indicator_t *d, const tiku_ed_indicator_t *best,
       int backward)
{
    if (!best)
        return 1;
    if (d->start != best->start)
        return backward ? d->start > best->start : d->start < best->start;
    return d->end > best->end;
}

tiku_edit_status_t
tiku_editdoc_indicator_next(const tiku_editdoc_t *doc, tiku_edit_pos_t pos,
                            uint32_t kinds, int backward,
                            tiku_edit_range_t *range, unsigned *kind)
{
    const tiku_ed_indicator_t *best = NULL;
    size_t i;

    if (!doc)
        return TIKU_EDIT_INVALID;
    for (i = 0; i < doc->nindicators; i++) {
        const tiku_ed_indicator_t *d = &doc->indicators[i];

        if (!d->live || !(kinds & ((uint32_t)1 << d->kind)) ||
            (backward ? d->start >= pos : d->start <= pos))
            continue;
        if (nearer(d, best, backward))
            best = d;
    }
    if (!best)
        return TIKU_EDIT_NOTHING;
    if (range) {
        range->start = best->start;
        range->end = best->end;
    }
    if (kind)
        *kind = best->kind;
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_indicators_each(const tiku_editdoc_t *doc,
                             tiku_edit_range_t span, uint32_t kinds,
                             tiku_edit_indicator_fn fn, void *ctx)
{
    size_t i;

    for (i = 0; doc && fn && i < doc->nindicators; i++) {
        const tiku_ed_indicator_t *d = &doc->indicators[i];
        tiku_edit_range_t r;

        if (!d->live || !(kinds & ((uint32_t)1 << d->kind)) ||
            d->end <= span.start || d->start >= span.end)
            continue;
        r.start = d->start;
        r.end = d->end;
        if (fn(ctx, r, d->kind))
            return;
    }
}

tiku_edit_status_t
tiku_editdoc_annotation_add(tiku_editdoc_t *doc, int64_t line,
                            const char *text, unsigned kind)
{
    ptrdiff_t i;
    size_t n, last;
    char *copy;
    tiku_ed_annotation_t *a;

    if (!doc || kind >= TIKU_EDIT_INDICATOR_KINDS)
        return TIKU_EDIT_INVALID;
    if (line < 0 || line >= tiku_ed_lines_count(&doc->text.lines))
        return TIKU_EDIT_INVALID_RANGE;
    if (!text || !*text)
        return TIKU_EDIT_NOTHING;
    n = strlen(text);
    if (!tiku_ed_utf8_valid(text, n))
        return TIKU_EDIT_INVALID_TEXT;
    copy = tiku_ed_alloc(n + 1);
    if (!copy)
        return TIKU_EDIT_NO_MEMORY;
    i = free_slot((void **)&doc->annotations, &doc->nannotations, sizeof *a,
                  offsetof(tiku_ed_annotation_t, live));
    if (i < 0) {
        tiku_ed_free(copy);
        return TIKU_EDIT_NO_MEMORY;
    }
    memcpy(copy, text, n + 1);
    /* Slot order is insertion order; the free slot moves past live entries. */
    last = doc->nannotations;
    while (last > (size_t)i + 1 && !doc->annotations[last - 1].live)
        last--;
    memmove(&doc->annotations[i], &doc->annotations[i + 1],
            (last - (size_t)i - 1) * sizeof *a);
    i = (ptrdiff_t)last - 1;
    a = &doc->annotations[i];
    a->text = copy;
    a->live = 1;
    a->kind = (uint8_t)kind;
    a->pos = tiku_ed_lines_start(&doc->text.lines, line);
    doc->annotation_serial++;
    return TIKU_EDIT_OK;
}

const char *
tiku_editdoc_annotation(const tiku_editdoc_t *doc, int64_t line, size_t index,
                        unsigned *kind)
{
    int64_t start;
    size_t i;

    if (!doc || line < 0 || line >= tiku_ed_lines_count(&doc->text.lines))
        return NULL;
    start = tiku_ed_lines_start(&doc->text.lines, line);
    for (i = 0; i < doc->nannotations; i++) {
        const tiku_ed_annotation_t *a = &doc->annotations[i];

        if (!a->live || a->pos != start || index-- > 0)
            continue;
        if (kind)
            *kind = a->kind;
        return a->text;
    }
    return NULL;
}

size_t
tiku_editdoc_annotations_clear(tiku_editdoc_t *doc, int64_t line,
                               uint32_t kinds)
{
    int64_t start = -1;
    size_t i, removed = 0;

    if (!doc)
        return 0;
    if (line >= 0) {
        if (line >= tiku_ed_lines_count(&doc->text.lines))
            return 0;
        start = tiku_ed_lines_start(&doc->text.lines, line);
    }
    for (i = 0; i < doc->nannotations; i++) {
        tiku_ed_annotation_t *a = &doc->annotations[i];

        if (!a->live || !(kinds & ((uint32_t)1 << a->kind)) ||
            (start >= 0 && a->pos != start))
            continue;
        annotation_drop(doc, a);
        removed++;
    }
    return removed;
}

size_t
tiku_editdoc_annotations(const tiku_editdoc_t *doc, uint64_t *serial)
{
    size_t i, live = 0;

    for (i = 0; doc && i < doc->nannotations; i++)
        live += doc->annotations[i].live != 0;
    if (serial)
        *serial = doc ? doc->annotation_serial : 0;
    return live;
}
