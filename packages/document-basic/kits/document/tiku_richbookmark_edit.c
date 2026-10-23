/*
 * TikuDesktop Document Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_richbookmark_edit.c - copy-on-write named locations and edit mapping.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

/** @brief Compare all logical fields without depending on padding. */
static int
same(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    return a.body == b.body && a.paragraph == b.paragraph &&
           a.byte == b.byte && a.affinity == b.affinity;
}

/** @brief Allocate a candidate array with copied existing entries. */
static tiku_rd_bookmarks_t *
copy(tiku_richdoc_t *doc, const tiku_rd_bookmarks_t *old, size_t count,
     tiku_rich_status_t *status)
{
    tiku_rd_bookmarks_t *marks = tiku_rd_alloc(doc->memory,
        sizeof *marks + count * sizeof *marks->items, status);
    size_t n = old ? old->count : 0;

    if (!marks) return NULL;
    atomic_init(&marks->refs, 1);
    marks->count = count;
    if (n > count) n = count;
    if (n) memcpy(marks->items, old->items, n * sizeof *marks->items);
    return marks;
}

tiku_rich_status_t
tiku_rd_bookmarks_follow(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rd_map_t *maps, size_t count)
{
    tiku_rd_bookmarks_t *marks = NULL, *old = root->bookmarks;
    tiku_richsnapshot_t snapshot = {.root = root};
    tiku_rich_pos_t position;
    tiku_rich_status_t status = TIKU_RICH_OK;
    size_t i;

    if (!old || !count) return status;
    for (i = 0; i < old->count; i++) {
        position = tiku_rd_map_position(old->items[i].position,
            maps, count, 1, doc->root, root);
        status = tiku_richsnapshot_position(&snapshot, position);
        if (status != TIKU_RICH_OK) break;
        if (same(position, old->items[i].position)) continue;
        if (!marks) marks = copy(doc, old, old->count, &status);
        if (!marks) break;
        marks->items[i].position = position;
    }
    if (status != TIKU_RICH_OK) tiku_rd_bookmarks_drop(marks);
    else if (marks) {
        tiku_rd_bookmarks_drop(root->bookmarks);
        root->bookmarks = marks;
    }
    return status;
}

/** @brief Build, capture history and publish one checked bookmark change. */
static tiku_rich_status_t
publish(tiku_richdoc_t *doc, size_t index, const char *name,
        const tiku_rich_pos_t *position)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_rd_root_t *root = tiku_rd_clone(doc, &status);
    tiku_rd_bookmarks_t *marks, *old;
    tiku_rd_history_t *entry;
    size_t count;

    if (!root) return status;
    old = root->bookmarks;
    count = old ? old->count : 0;
    if (position && index == count) count++;
    marks = copy(doc, old, count, &status);
    if (!marks) { tiku_rd_root_drop(root); return status; }
    if (position) {
        strcpy(marks->items[index].name, name);
        marks->items[index].position = *position;
    } else {
        memmove(marks->items + index, marks->items + index + 1,
            (count - index - 1) * sizeof *marks->items);
        marks->count--;
    }
    tiku_rd_bookmarks_drop(old);
    root->bookmarks = marks;
    if (!marks->count) {
        tiku_rd_bookmarks_drop(marks);
        root->bookmarks = NULL;
    }
    entry = tiku_rd_history_new(doc, root, NULL, 0, &status);
    if (!entry) tiku_rd_root_drop(root);
    else tiku_rd_publish(doc, root, entry);
    return status;
}

/** @brief Validate the mutation boundary and locate an existing exact name. */
static tiku_rich_status_t
check(tiku_richdoc_t *doc, uint64_t generation, const char *name, size_t *index)
{
    tiku_rd_bookmarks_t *marks;

    if (!doc || !name || !tiku_rd_name(name)) return TIKU_RICH_INVALID;
    if (doc->notifying) return TIKU_RICH_BUSY;
    if (doc->read_only) return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation) return TIKU_RICH_STALE;
    if (doc->generation == UINT64_MAX) return TIKU_RICH_LIMIT;
    marks = doc->root->bookmarks;
    *index = 0;
    while (marks && *index < marks->count &&
           strcmp(name, marks->items[*index].name)) (*index)++;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richdoc_bookmark_set(tiku_richdoc_t *doc, uint64_t generation,
    const char *name, tiku_rich_pos_t position)
{
    size_t index;
    tiku_rich_status_t status = check(doc, generation, name, &index);
    tiku_richsnapshot_t snapshot = {.root = doc ? doc->root : NULL};

    if (status != TIKU_RICH_OK) return status;
    status = tiku_richsnapshot_position(&snapshot, position);
    if (status != TIKU_RICH_OK) return status;
    if (index == TIKU_RICH_BOOKMARKS_MAX) return TIKU_RICH_LIMIT;
    if (index < tiku_richsnapshot_bookmarks(&snapshot) &&
        same(position, doc->root->bookmarks->items[index].position))
        return TIKU_RICH_NO_CHANGE;
    return publish(doc, index, name, &position);
}

tiku_rich_status_t
tiku_richdoc_bookmark_remove(tiku_richdoc_t *doc, uint64_t generation,
    const char *name)
{
    size_t index;
    tiku_rich_status_t status = check(doc, generation, name, &index);

    if (status != TIKU_RICH_OK) return status;
    if (!doc->root->bookmarks || index == doc->root->bookmarks->count)
        return TIKU_RICH_NOT_FOUND;
    return publish(doc, index, name, NULL);
}
