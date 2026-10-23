/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richoutline.c - bounded semantic heading scans over immutable content.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richoutline.h"
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Source ownership, scan cursor and charged index storage. */
struct tiku_richoutline {
    tiku_rd_memory_t *memory;  /**< kept alive by charged allocations */
    tiku_rd_root_t *root;
    uint64_t generation;
    size_t cursor, count, capacity;
    tiku_rich_heading_t *entries;
    tiku_rich_status_t status;
};

tiku_rich_status_t
tiku_richoutline_create(const tiku_richsnapshot_t *snapshot,
                        tiku_richoutline_t **out)
{
    tiku_richoutline_t *outline;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!snapshot) return TIKU_RICH_INVALID;
    outline = tiku_rd_alloc(snapshot->memory, sizeof *outline, &status);
    if (!outline) return status;
    outline->memory = snapshot->memory;
    outline->root = snapshot->root;
    atomic_fetch_add_explicit(&outline->root->refs, 1, memory_order_relaxed);
    outline->generation = snapshot->generation;
    outline->status = TIKU_RICH_PENDING;
    *out = outline;
    return TIKU_RICH_OK;
}

/** @brief Append one entry, accounting for old and replacement array storage. */
static int
append(tiku_richoutline_t *outline, const tiku_rich_paragraph_t *p,
       unsigned level)
{
    if (outline->count == outline->capacity) {
        size_t capacity = outline->capacity ? outline->capacity * 2 : 16;
        tiku_rich_heading_t *entries;

        if (capacity < outline->capacity) {
            outline->status = TIKU_RICH_LIMIT;
            return 0;
        }
        if (capacity > outline->root->count) capacity = outline->root->count;
        if (capacity > SIZE_MAX / sizeof *entries) {
            outline->status = TIKU_RICH_LIMIT;
            return 0;
        }
        entries = tiku_rd_alloc(outline->memory,
            capacity * sizeof *entries, &outline->status);
        if (!entries) return 0;
        if (outline->count) memcpy(entries, outline->entries,
                                     outline->count * sizeof *entries);
        tiku_rd_free(outline->entries);
        outline->entries = entries;
        outline->capacity = capacity;
    }
    outline->entries[outline->count++] = (tiku_rich_heading_t){
        .position = {outline->root->body, p->id, 0, TIKU_RICH_AFTER},
        .paragraph_index = outline->cursor, .level = level,
        .text = p->text, .length = p->length};
    return 1;
}

tiku_rich_status_t
tiku_richoutline_step(tiku_richoutline_t *outline, size_t work)
{
    if (!outline) return TIKU_RICH_INVALID;
    if (outline->status != TIKU_RICH_PENDING) return outline->status;
    while (work-- && outline->cursor < outline->root->count) {
        const tiku_rich_paragraph_t *p =
            &outline->root->paragraphs[outline->cursor]->value;
        tiku_rich_chars_t chars;
        tiku_rich_para_t props;
        tiku_rich_status_t status = tiku_rd_resolve_value(outline->root, p,
                                                          NULL, &chars, &props);

        if (status != TIKU_RICH_OK) { outline->status = status; break; }
        if (props.outline && !append(outline, p, props.outline)) break;
        outline->cursor++;
    }
    if (outline->cursor == outline->root->count) outline->status = TIKU_RICH_OK;
    return outline->status;
}

tiku_rich_status_t
tiku_richoutline_status(const tiku_richoutline_t *outline)
{
    return outline ? outline->status : TIKU_RICH_INVALID;
}

uint64_t
tiku_richoutline_generation(const tiku_richoutline_t *outline)
{
    return outline ? outline->generation : 0;
}

size_t
tiku_richoutline_count(const tiku_richoutline_t *outline)
{
    return outline ? outline->count : 0;
}

tiku_rich_status_t
tiku_richoutline_get(const tiku_richoutline_t *outline, size_t index,
                     tiku_rich_heading_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!outline) return TIKU_RICH_INVALID;
    if (index >= outline->count) return TIKU_RICH_INVALID_RANGE;
    *out = outline->entries[index];
    return TIKU_RICH_OK;
}

void
tiku_richoutline_release(tiku_richoutline_t *outline)
{
    if (!outline) return;
    tiku_rd_root_drop(outline->root);
    tiku_rd_free(outline->entries);
    tiku_rd_free(outline);
}

tiku_rich_status_t
tiku_richoutline_neighbor(const tiku_richoutline_t *outline,
    tiku_rich_pos_t position, int forward, size_t *index)
{
    tiku_richsnapshot_t snapshot = {0};
    tiku_rich_status_t status;
    size_t paragraph, low = 0, high;

    if (!index) return TIKU_RICH_INVALID;
    *index = SIZE_MAX;
    if (!outline || (forward != 0 && forward != 1)) return TIKU_RICH_INVALID;
    if (outline->status != TIKU_RICH_OK) return outline->status;
    snapshot.root = outline->root;
    status = tiku_richsnapshot_position(&snapshot, position);
    if (status != TIKU_RICH_OK) return status;
    paragraph = tiku_rd_find(outline->root, position.paragraph);
    high = outline->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        size_t p = outline->entries[middle].paragraph_index;

        if (p < paragraph || (p == paragraph && (forward || position.byte)))
            low = middle + 1;
        else high = middle;
    }
    if (forward ? low == outline->count : !low) return TIKU_RICH_NOT_FOUND;
    *index = forward ? low : low - 1;
    return TIKU_RICH_OK;
}
