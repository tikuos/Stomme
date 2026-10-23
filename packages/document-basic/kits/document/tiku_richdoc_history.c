/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_history.c - bounded root history and saved-state tracking.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Release a complete stack and its retained allocations. */
static void
drop_stack(tiku_rd_history_t *entry)
{
    tiku_rd_history_t *next;

    while (entry) {
        next = entry->next;
        tiku_rd_root_drop(entry->root);
        tiku_rd_free(entry->maps);
        tiku_rd_free(entry->bindings);
        tiku_rd_free(entry);
        entry = next;
    }
}

void
tiku_rd_clear_history(tiku_richdoc_t *doc)
{
    if (!doc)
        return;
    drop_stack(doc->undo);
    drop_stack(doc->redo);
    doc->undo = NULL;
    doc->redo = NULL;
    doc->history_bytes = 0;
    doc->history_total = 0;
}

void
tiku_richdoc_clear_history(tiku_richdoc_t *doc)
{
    if (!doc || doc->notifying || (!doc->undo && !doc->redo))
        return;
    tiku_rd_clear_history(doc);
    tiku_rd_notify(doc, TIKU_RICH_CHANGE_HISTORY, 0);
}

tiku_rd_history_t *
tiku_rd_history_new(tiku_richdoc_t *doc, tiku_rd_root_t *after,
                     tiku_rd_map_t *maps, size_t map_count,
                     tiku_rich_status_t *status)
{
    tiku_rd_history_t *entry;

    *status = tiku_rd_bookmarks_follow(doc, after, maps, map_count);
    if (*status != TIKU_RICH_OK) return NULL;
    entry = tiku_rd_alloc(doc->memory, sizeof *entry, status);
    if (!entry)
        return NULL;
    entry->maps = maps;
    entry->map_count = map_count;
    *status = tiku_rd_bindings(doc, after, entry);
    if (*status != TIKU_RICH_OK)
        goto fail;
    if (doc->initiator) {
        entry->origin = tiku_richsession_id(doc->initiator);
        entry->pending_before = doc->initiator->pending;
        if (doc->initiator->typing)
            entry->pending_after = doc->initiator->pending;
    }
    tiku_rd_coalesce(doc, after, entry);
    *status = tiku_rd_history_plan(doc, after, entry);
    if (*status != TIKU_RICH_OK) goto fail;
    return entry;
fail:
    tiku_rd_free(entry->bindings);
    tiku_rd_free(entry);
    return NULL;
}

void
tiku_rd_publish(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                 tiku_rd_history_t *entry)
{
    tiku_rd_history_t **keep;

    if (entry->replace_top)
        tiku_rd_coalesce_publish(doc, entry);
    else {
        entry->root = doc->root;
        tiku_rd_anchors_move(doc, entry, 1, root);
    }
    keep = &doc->undo;
    drop_stack(doc->redo);
    doc->redo = NULL;
    while (*keep != entry->discard)
        keep = &(*keep)->next;
    drop_stack(*keep);
    *keep = NULL;
    entry->next = doc->undo;
    doc->undo = entry;
    doc->history_bytes = entry->planned_bytes;
    doc->history_total = entry->planned_total;
    entry->discard = NULL;
    root->state = ++doc->generation;
    doc->root = root;
    tiku_rs_history(doc, entry, 1);
    tiku_rd_notify(doc, TIKU_RICH_CHANGE_EDIT, entry->origin);
}

/** @brief Exchange live and retained roots without allocating memory. */
static tiku_rich_status_t
exchange(tiku_richdoc_t *doc, int redo)
{
    tiku_rd_history_t **from, **to, *entry;
    tiku_rd_root_t *root;

    if (!doc)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    if (doc->read_only)
        return TIKU_RICH_READ_ONLY;
    if (doc->generation == UINT64_MAX)
        return TIKU_RICH_LIMIT;
    from = redo ? &doc->redo : &doc->undo;
    to = redo ? &doc->undo : &doc->redo;
    entry = *from;
    if (!entry)
        return TIKU_RICH_NO_CHANGE;
    entry->groupable = 0;
    tiku_rd_anchors_move(doc, entry, redo, entry->root);
    *from = entry->next;
    root = doc->root;
    doc->root = entry->root;
    entry->root = root;
    entry->next = *to;
    *to = entry;
    doc->generation++;
    tiku_rs_history(doc, entry, redo);
    tiku_rd_notify(doc, redo ? TIKU_RICH_CHANGE_REDO : TIKU_RICH_CHANGE_UNDO,
                     entry->origin);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richdoc_undo(tiku_richdoc_t *doc)
{
    return exchange(doc, 0);
}

tiku_rich_status_t
tiku_richdoc_redo(tiku_richdoc_t *doc)
{
    return exchange(doc, 1);
}

int
tiku_richdoc_can_undo(const tiku_richdoc_t *doc)
{
    return doc && doc->undo && !doc->read_only && !doc->notifying &&
           doc->generation < UINT64_MAX;
}

int
tiku_richdoc_can_redo(const tiku_richdoc_t *doc)
{
    return doc && doc->redo && !doc->read_only && !doc->notifying &&
           doc->generation < UINT64_MAX;
}

size_t
tiku_richdoc_history_memory(const tiku_richdoc_t *doc)
{
    return doc ? doc->history_bytes : 0;
}

size_t
tiku_richdoc_history_retained_memory(const tiku_richdoc_t *doc)
{
    return doc && doc->history_total ?
           doc->history_total - doc->root->footprint : 0;
}

tiku_rich_status_t
tiku_richdoc_mark_saved(tiku_richdoc_t *doc,
                         const tiku_richsnapshot_t *snapshot)
{
    if (!doc || !snapshot || snapshot->fragment ||
        snapshot->memory != doc->memory)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    if (doc->undo)
        doc->undo->groupable = 0;
    if (doc->saved_state == snapshot->root->state)
        return TIKU_RICH_OK;
    doc->saved_state = snapshot->root->state;
    tiku_rd_notify(doc, TIKU_RICH_CHANGE_SAVED, 0);
    return TIKU_RICH_OK;
}

int
tiku_richdoc_dirty(const tiku_richdoc_t *doc)
{
    return doc && doc->root->state != doc->saved_state;
}
