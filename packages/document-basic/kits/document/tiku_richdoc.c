/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc.c - document lifetime and atomic publication of loaded data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

tiku_rich_status_t
tiku_richdoc_create(const tiku_rich_limits_t *limits, tiku_richdoc_t **out)
{
    tiku_rich_limits_t effective;
    tiku_rich_status_t status;
    tiku_rd_memory_t *memory;
    tiku_richdoc_t *doc;
    const tiku_rich_source_t empty = {0};

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    status = tiku_rd_limits(limits, &effective);
    if (status != TIKU_RICH_OK)
        return status;
    memory = tiku_rd_memory_new(&effective, &status);
    if (!memory)
        return status;
    doc = tiku_rd_alloc(memory, sizeof *doc, &status);
    tiku_rd_memory_drop(memory);
    if (!doc)
        return status;
    doc->refs = 1;
    doc->memory = memory;
    doc->limits = effective;
    doc->generation = 1;
    doc->next_id = 2;
    doc->next_anchor = 1;
    doc->root = tiku_rd_build(doc, &empty, &doc->next_id, &status);
    if (!doc->root) {
        tiku_rd_free(doc);
        return status;
    }
    doc->root->state = doc->generation;
    doc->saved_state = doc->root->state;
    *out = doc;
    return TIKU_RICH_OK;
}

void
tiku_richdoc_retain(tiku_richdoc_t *doc)
{
    if (doc)
        doc->refs++;
}

void
tiku_richdoc_release(tiku_richdoc_t *doc)
{
    if (!doc || doc->notifying || --doc->refs)
        return;
    tiku_rd_clear_history(doc);
    tiku_rd_root_drop(doc->root);
    tiku_rd_free(doc);
}

tiku_rich_status_t
tiku_richdoc_import(tiku_richdoc_t *doc, const tiku_rich_source_t *source,
                     uint64_t generation)
{
    tiku_rich_status_t status;
    tiku_rd_root_t *root, *old;
    uint64_t next;

    if (!doc || !source)
        return TIKU_RICH_INVALID;
    if (doc->notifying)
        return TIKU_RICH_BUSY;
    if (doc->read_only)
        return TIKU_RICH_READ_ONLY;
    if (generation && generation != doc->generation)
        return TIKU_RICH_STALE;
    if (doc->generation == UINT64_MAX)
        return TIKU_RICH_LIMIT;
    status = tiku_rd_validate(&doc->limits, source);
    if (status != TIKU_RICH_OK)
        return status;
    next = doc->next_id;
    root = tiku_rd_build(doc, source, &next, &status);
    if (!root)
        return status;
    old = doc->root;
    tiku_rd_clear_history(doc);
    doc->root = root;
    doc->next_id = next;
    doc->generation++;
    root->state = doc->generation;
    doc->saved_state = root->state;
    tiku_rd_anchors_reset(doc);
    tiku_rs_history(doc, NULL, 1);
    tiku_rd_root_drop(old);
    tiku_rd_notify(doc, TIKU_RICH_CHANGE_IMPORT, 0);
    return TIKU_RICH_OK;
}

int
tiku_richdoc_read_only(const tiku_richdoc_t *doc)
{
    return doc && doc->read_only;
}

void
tiku_richdoc_set_read_only(tiku_richdoc_t *doc, int read_only)
{
    if (!doc || doc->notifying || doc->read_only == !!read_only)
        return;
    doc->read_only = !!read_only;
    if (doc->undo)
        doc->undo->groupable = 0;
    tiku_rd_notify(doc, TIKU_RICH_CHANGE_PERMISSION, 0);
}

uint64_t
tiku_richdoc_generation(const tiku_richdoc_t *doc)
{
    return doc ? doc->generation : 0;
}

size_t
tiku_richdoc_memory(const tiku_richdoc_t *doc)
{
    return doc ? atomic_load_explicit(&doc->memory->used,
                                      memory_order_relaxed) : 0;
}

tiku_rich_status_t
tiku_richdoc_snapshot(const tiku_richdoc_t *doc, tiku_richsnapshot_t **out)
{
    tiku_richsnapshot_t *snapshot;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!doc)
        return TIKU_RICH_INVALID;
    snapshot = tiku_rd_alloc(doc->memory, sizeof *snapshot, &status);
    if (!snapshot)
        return status;
    snapshot->root = doc->root;
    snapshot->generation = doc->generation;
    snapshot->memory = doc->memory;
    atomic_fetch_add_explicit(&doc->root->refs, 1, memory_order_relaxed);
    *out = snapshot;
    return TIKU_RICH_OK;
}
