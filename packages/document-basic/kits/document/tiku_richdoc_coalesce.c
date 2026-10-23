/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_coalesce.c - adjacent insertion history with exact selections.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Whether successive captures describe the same unmoved anchor. */
static int
continuous(const tiku_rd_binding_t *old, const tiku_rd_binding_t *next)
{
    return old->id == next->id && old->revision == next->revision &&
        old->after.body == next->before.body &&
        old->after.paragraph == next->before.paragraph &&
        old->after.byte == next->before.byte &&
        old->after.affinity == next->before.affinity;
}

void
tiku_rd_coalesce(tiku_richdoc_t *doc, const tiku_rd_root_t *after,
                   tiku_rd_history_t *entry)
{
    const tiku_rd_history_t *old = doc->undo;
    tiku_rd_map_t *map;
    size_t i;

    if (!doc->initiator || !doc->initiator->coalescing ||
        entry->map_count != 1 || !entry->maps[0].flat ||
        entry->maps[0].removed || !entry->maps[0].inserted ||
        after->count != doc->root->count)
        return;
    entry->groupable = 1;
    if (!old || doc->redo || !old->groupable ||
        old->origin != entry->origin || old->map_count != 1 ||
        old->binding_count != entry->binding_count ||
        old->maps[0].at + old->maps[0].inserted != entry->maps[0].at)
        return;
    for (i = 0; i < entry->binding_count; i++)
        if (!continuous(&old->bindings[i], &entry->bindings[i]))
            return;
    map = &entry->maps[0];
    map->at = old->maps[0].at;
    map->inserted += old->maps[0].inserted;
    for (i = 0; i < entry->binding_count; i++)
        entry->bindings[i].before = old->bindings[i].before;
    entry->pending_before = old->pending_before;
    entry->replace_top = 1;
}

void
tiku_rd_coalesce_publish(tiku_richdoc_t *doc, tiku_rd_history_t *entry)
{
    tiku_rd_history_t *old = doc->undo;
    tiku_richanchor_t *anchor;
    size_t i = 0;

    for (anchor = doc->anchors; anchor; anchor = anchor->next)
        anchor->position = entry->bindings[i++].after;
    entry->root = old->root;
    doc->undo = old->next;
    tiku_rd_root_drop(doc->root);
    tiku_rd_free(old->maps);
    tiku_rd_free(old->bindings);
    tiku_rd_free(old);
    entry->replace_top = 0;
}
