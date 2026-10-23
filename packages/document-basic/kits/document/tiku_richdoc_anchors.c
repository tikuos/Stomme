/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_anchors.c - tracked owner-thread positions and history mapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Validate a position against an owned immutable root. */
static tiku_rich_status_t
valid_position(const tiku_rd_root_t *root, tiku_rich_pos_t position)
{
    tiku_richsnapshot_t snapshot = {.root = (tiku_rd_root_t *)root};

    return tiku_richsnapshot_position(&snapshot, position);
}

tiku_rich_status_t
tiku_richanchor_create(tiku_richdoc_t *doc, tiku_rich_pos_t position,
                        tiku_richanchor_t **out)
{
    tiku_richanchor_t *anchor;
    tiku_rich_status_t status;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!doc)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    status = valid_position(doc->root, position);
    if (status != TIKU_RICH_OK)
        return status;
    if (!doc->next_anchor || doc->anchor_count >= doc->limits.anchors ||
        doc->refs == SIZE_MAX)
        return TIKU_RICH_LIMIT;
    anchor = tiku_rd_alloc(doc->memory, sizeof *anchor, &status);
    if (!anchor)
        return status;
    anchor->doc = doc;
    anchor->position = position;
    anchor->id = doc->next_anchor;
    doc->next_anchor = doc->next_anchor == UINT64_MAX
        ? 0 : doc->next_anchor + 1;
    anchor->next = doc->anchors;
    doc->anchors = anchor;
    doc->anchor_count++;
    tiku_richdoc_retain(doc);
    *out = anchor;
    return TIKU_RICH_OK;
}

void
tiku_richanchor_release(tiku_richanchor_t *anchor)
{
    tiku_richdoc_t *doc;
    tiku_richanchor_t **at;

    if (!anchor || anchor->doc->notifying)
        return;
    doc = anchor->doc;
    at = &doc->anchors;
    while (*at != anchor)
        at = &(*at)->next;
    *at = anchor->next;
    doc->anchor_count--;
    tiku_rd_free(anchor);
    tiku_richdoc_release(doc);
}

tiku_rich_status_t
tiku_richanchor_get(const tiku_richanchor_t *anchor, tiku_rich_pos_t *out)
{
    if (!out)
        return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!anchor)
        return TIKU_RICH_INVALID;
    *out = anchor->position;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richanchor_set(tiku_richanchor_t *anchor, tiku_rich_pos_t position)
{
    tiku_rich_status_t status;

    if (!anchor)
        return TIKU_RICH_INVALID;
    if (anchor->doc->notifying)
        return TIKU_RICH_BUSY;
    status = valid_position(anchor->doc->root, position);
    if (status != TIKU_RICH_OK)
        return status;
    if (anchor->revision == UINT64_MAX)
        return TIKU_RICH_LIMIT;
    anchor->position = position;
    anchor->revision++;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_bindings(tiku_richdoc_t *doc, const tiku_rd_root_t *after,
                  tiku_rd_history_t *entry)
{
    tiku_richanchor_t *anchor;
    tiku_rd_binding_t *binding;
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_richcmd_context_t context;
    tiku_rich_pos_t collapsed = {0};

    if (!doc->anchor_count || (!entry->map_count && !doc->initiator))
        return TIKU_RICH_OK;
    if (doc->initiator && doc->initiator->collapse) {
        context = tiku_rs_context(doc->initiator);
        context.first.affinity = TIKU_RICH_AFTER;
        collapsed = tiku_rd_map_position(context.first,
            entry->maps, entry->map_count, 1, doc->root, after);
        if (entry->map_count == 1 &&
            entry->maps[0].kind == TIKU_RICH_TABLE_INSERT) {
            size_t index = tiku_rd_find(after, context.first.paragraph);

            collapsed.paragraph = after->paragraphs[index + 1]->value.id;
            collapsed.byte = 0;
        }
        if (entry->map_count == 1 &&
            entry->maps[0].kind == TIKU_RICH_TABLE_GRID) {
            collapsed.paragraph = entry->maps[0].second;
            collapsed.byte = 0;
        }
    }
    if (doc->anchor_count > SIZE_MAX / sizeof *entry->bindings)
        return TIKU_RICH_LIMIT;
    entry->bindings = tiku_rd_alloc(doc->memory,
        doc->anchor_count * sizeof *entry->bindings, &status);
    if (!entry->bindings)
        return status;
    for (anchor = doc->anchors; anchor; anchor = anchor->next) {
        binding = &entry->bindings[entry->binding_count++];
        binding->id = anchor->id;
        binding->revision = anchor->revision;
        binding->before = anchor->position;
        binding->after = tiku_rd_map_position(anchor->position,
            entry->maps, entry->map_count, 1, doc->root, after);
        binding->initiating = doc->initiator &&
            (anchor == doc->initiator->anchor ||
             anchor == doc->initiator->caret);
        if (binding->initiating && doc->initiator->collapse)
            binding->after = collapsed;
        status = valid_position(after, binding->after);
        if (status != TIKU_RICH_OK)
            return status;
    }
    return TIKU_RICH_OK;
}

/** @brief Compare logical position values, excluding structure padding. */
static int
same_position(tiku_rich_pos_t a, tiku_rich_pos_t b)
{
    return a.body == b.body && a.paragraph == b.paragraph &&
           a.byte == b.byte && a.affinity == b.affinity;
}

void
tiku_rd_anchors_move(tiku_richdoc_t *doc, const tiku_rd_history_t *entry,
                      int forward, const tiku_rd_root_t *after)
{
    tiku_richanchor_t *anchor;
    const tiku_rd_binding_t *binding;
    size_t i = 0;

    for (anchor = doc->anchors; anchor; anchor = anchor->next) {
        while (i < entry->binding_count &&
               entry->bindings[i].id > anchor->id)
            i++;
        if (i < entry->binding_count) {
            binding = &entry->bindings[i];
            if (anchor->id == binding->id &&
                (binding->initiating ||
                (anchor->revision == binding->revision &&
                same_position(anchor->position,
                    forward ? binding->before : binding->after)))) {
                anchor->position = forward ? binding->after : binding->before;
                continue;
            }
        }
        anchor->position = tiku_rd_map_position(anchor->position,
            entry->maps, entry->map_count, forward, doc->root, after);
    }
}

void
tiku_rd_anchors_reset(tiku_richdoc_t *doc)
{
    tiku_richanchor_t *anchor;

    for (anchor = doc->anchors; anchor; anchor = anchor->next) {
        anchor->position.body = doc->root->body;
        anchor->position.paragraph = doc->root->paragraphs[0]->value.id;
        anchor->position.byte = 0;
    }
}
