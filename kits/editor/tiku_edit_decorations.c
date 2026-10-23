/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_decorations.c - line markers kept at their lines' starts and
 * moved with each replacement, and the execution line pinned to a
 * history state.
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

/** @brief The live marker a handle names, or NULL. */
static tiku_ed_marker_t *
marker_get(const tiku_editdoc_t *doc, tiku_edit_marker_t marker)
{
    tiku_ed_marker_t *m;

    if (!doc || marker.slot == 0 || marker.slot > doc->nmarkers)
        return NULL;
    m = &doc->markers[marker.slot - 1];
    return m->live && m->generation == marker.generation ? m : NULL;
}

/** @brief The line a marker is on. */
static int64_t
marker_line(const tiku_editdoc_t *doc, const tiku_ed_marker_t *m)
{
    return tiku_ed_lines_find(&doc->text.lines, m->pos);
}

/**
 * @brief Where a marker at line start @p p goes when [s, e) becomes n
 *        bytes.
 *
 * Text put in at its line's start pushes it along with the line's text.
 * When the replaced range takes its line's start, the line break before
 * it is gone, and the marker goes to where the range starts.
 */
static int64_t
map_marker(int64_t p, int64_t s, int64_t e, int64_t n)
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
tiku_ed_markers_map(tiku_editdoc_t *doc, int64_t s, int64_t e, int64_t n)
{
    size_t i;

    for (i = 0; i < doc->nmarkers; i++) {
        tiku_ed_marker_t *m = &doc->markers[i];

        if (m->live)
            m->pos = map_marker(m->pos, s, e, n);
    }
}

void
tiku_ed_markers_settle(tiku_editdoc_t *doc)
{
    size_t i;

    for (i = 0; i < doc->nmarkers; i++) {
        tiku_ed_marker_t *m = &doc->markers[i];

        if (m->live)
            m->pos = tiku_ed_lines_start(&doc->text.lines,
                                         marker_line(doc, m));
    }
}

void
tiku_ed_markers_drop(tiku_editdoc_t *doc)
{
    size_t i;

    for (i = 0; i < doc->nmarkers; i++) {
        tiku_ed_marker_t *m = &doc->markers[i];

        if (m->live) {
            m->live = 0;
            m->generation = next_generation(m->generation);
        }
    }
}

void
tiku_ed_markers_free(tiku_editdoc_t *doc)
{
    tiku_ed_free(doc->markers);
    doc->markers = NULL;
    doc->nmarkers = 0;
}

/** @brief Room for one more marker; @return its slot, or -1. */
static ptrdiff_t
free_slot(tiku_editdoc_t *doc)
{
    size_t i, n;
    tiku_ed_marker_t *grown;

    for (i = 0; i < doc->nmarkers; i++)
        if (!doc->markers[i].live)
            return (ptrdiff_t)i;
    n = doc->nmarkers ? doc->nmarkers * 2 : 4;
    if (n > UINT32_MAX || n > SIZE_MAX / sizeof *grown)
        return -1;
    grown = tiku_ed_realloc(doc->markers, n * sizeof *grown);
    if (!grown)
        return -1;
    memset(grown + doc->nmarkers, 0, (n - doc->nmarkers) * sizeof *grown);
    doc->markers = grown;
    doc->nmarkers = n;
    return (ptrdiff_t)i;
}

tiku_edit_status_t
tiku_editdoc_marker_add(tiku_editdoc_t *doc, int64_t line, unsigned kind,
                        tiku_edit_marker_t *marker)
{
    ptrdiff_t i;
    tiku_ed_marker_t *m;

    if (!doc || kind >= TIKU_EDIT_MARKER_KINDS)
        return TIKU_EDIT_INVALID;
    if (line < 0 || line >= tiku_ed_lines_count(&doc->text.lines))
        return TIKU_EDIT_INVALID_RANGE;
    i = free_slot(doc);
    if (i < 0)
        return TIKU_EDIT_NO_MEMORY;
    m = &doc->markers[i];
    m->generation = next_generation(m->generation);
    m->live = 1;
    m->kind = (uint8_t)kind;
    m->pos = tiku_ed_lines_start(&doc->text.lines, line);
    if (marker) {
        marker->slot = (uint32_t)i + 1;
        marker->generation = m->generation;
    }
    return TIKU_EDIT_OK;
}

int64_t
tiku_editdoc_marker_line(const tiku_editdoc_t *doc, tiku_edit_marker_t marker)
{
    const tiku_ed_marker_t *m = marker_get(doc, marker);

    return m ? marker_line(doc, m) : -1;
}

void
tiku_editdoc_marker_remove(tiku_editdoc_t *doc, tiku_edit_marker_t marker)
{
    tiku_ed_marker_t *m = marker_get(doc, marker);

    if (m) {
        m->live = 0;
        m->generation = next_generation(m->generation);
    }
}

uint32_t
tiku_editdoc_markers_on(const tiku_editdoc_t *doc, int64_t line)
{
    uint32_t kinds = 0;
    int64_t start;
    size_t i;

    if (!doc || line < 0 || line >= tiku_ed_lines_count(&doc->text.lines))
        return 0;
    start = tiku_ed_lines_start(&doc->text.lines, line);
    for (i = 0; i < doc->nmarkers; i++)
        if (doc->markers[i].live && doc->markers[i].pos == start)
            kinds |= (uint32_t)1 << doc->markers[i].kind;
    return kinds;
}

int64_t
tiku_editdoc_markers_next(const tiku_editdoc_t *doc, int64_t from,
                          uint32_t kinds)
{
    int64_t best = -1;
    size_t i;

    if (!doc)
        return -1;
    for (i = 0; i < doc->nmarkers; i++) {
        const tiku_ed_marker_t *m = &doc->markers[i];
        int64_t line;

        if (!m->live || !(kinds & ((uint32_t)1 << m->kind)))
            continue;
        line = marker_line(doc, m);
        if (line >= from && (best < 0 || line < best))
            best = line;
    }
    return best;
}

size_t
tiku_editdoc_markers_clear(tiku_editdoc_t *doc, int64_t line, uint32_t kinds)
{
    size_t i, removed = 0;
    int64_t start = -1;

    if (!doc)
        return 0;
    if (line >= 0) {
        if (line >= tiku_ed_lines_count(&doc->text.lines))
            return 0;
        start = tiku_ed_lines_start(&doc->text.lines, line);
    }
    for (i = 0; i < doc->nmarkers; i++) {
        tiku_ed_marker_t *m = &doc->markers[i];

        if (!m->live || !(kinds & ((uint32_t)1 << m->kind)) ||
            (start >= 0 && m->pos != start))
            continue;
        m->live = 0;
        m->generation = next_generation(m->generation);
        removed++;
    }
    return removed;
}

tiku_edit_status_t
tiku_editdoc_execution_set(tiku_editdoc_t *doc, int64_t line)
{
    if (!doc)
        return TIKU_EDIT_INVALID;
    if (line < 0 || line >= tiku_ed_lines_count(&doc->text.lines))
        return TIKU_EDIT_INVALID_RANGE;
    doc->exec_line = line;
    doc->exec_state = tiku_ed_history_state(&doc->history);
    return TIKU_EDIT_OK;
}

void
tiku_editdoc_execution_clear(tiku_editdoc_t *doc)
{
    if (doc)
        doc->exec_line = -1;
}

tiku_edit_status_t
tiku_editdoc_execution(const tiku_editdoc_t *doc, int64_t *line)
{
    if (!doc || !line)
        return TIKU_EDIT_INVALID;
    *line = doc->exec_line;
    if (doc->exec_line < 0)
        return TIKU_EDIT_NOTHING;
    if (doc->exec_state != tiku_ed_history_state(&doc->history))
        return TIKU_EDIT_STALE;
    return TIKU_EDIT_OK;
}
