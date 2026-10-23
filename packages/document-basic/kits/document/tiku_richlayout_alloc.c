/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_alloc.c - checked retained geometry allocation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

#include <string.h>

void *
tiku_rl_alloc(tiku_richlayout_t *layout, size_t bytes)
{
    void *pointer;
    size_t charge;

    if (bytes > layout->options.memory_bytes - layout->used) {
        layout->status = TIKU_RICH_LIMIT;
        return NULL;
    }
    pointer = tiku_rd_alloc(layout->source.memory, bytes, &layout->status);
    if (!pointer)
        return NULL;
    charge = tiku_rd_size(pointer);
    if (charge > layout->options.memory_bytes - layout->used) {
        tiku_rd_free(pointer);
        layout->status = TIKU_RICH_LIMIT;
        return NULL;
    }
    layout->used += charge;
    return pointer;
}

void
tiku_rl_free(tiku_richlayout_t *layout, void *pointer)
{
    layout->used -= tiku_rd_size(pointer);
    tiku_rd_free(pointer);
}

int
tiku_rl_grow(tiku_richlayout_t *layout, void **array, size_t *capacity,
             size_t needed, size_t item)
{
    size_t count;
    void *next;

    if (needed <= *capacity)
        return 1;
    count = *capacity ? *capacity : 16;
    while (count < needed && count <= SIZE_MAX / 2)
        count *= 2;
    if (count < needed || count > SIZE_MAX / item) {
        layout->status = TIKU_RICH_LIMIT;
        return 0;
    }
    next = tiku_rl_alloc(layout, count * item);
    if (!next)
        return 0;
    if (*array)
        memcpy(next, *array, *capacity * item);
    tiku_rl_free(layout, *array);
    *array = next;
    *capacity = count;
    return 1;
}

void
tiku_richlayout_release(tiku_richlayout_t *layout)
{
    if (!layout)
        return;
    if (atomic_fetch_sub_explicit(&layout->refs, 1, memory_order_acq_rel) != 1)
        return;
    tiku_rl_cache_drop(layout);
    if (layout->options.provider.release)
        layout->options.provider.release(layout->options.provider.context);
    tiku_rd_root_drop(layout->source.root);
    tiku_rd_free(layout->boundaries);
    tiku_rd_free(layout->paragraphs);
    tiku_rd_free(layout->clusters);
    tiku_rd_free(layout->lines);
    tiku_rd_free(layout->glyphs);
    tiku_rd_free(layout->carets);
    tiku_rd_free(layout->containers);
    tiku_rd_free(layout->grids);
    tiku_rd_free(layout->minimums);
    tiku_rd_free(layout);
}
