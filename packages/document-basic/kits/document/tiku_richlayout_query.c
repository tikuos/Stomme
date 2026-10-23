/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_query.c - immutable geometry and source metadata queries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

tiku_rich_status_t
tiku_richlayout_snapshot(const tiku_richlayout_t *layout,
                         tiku_richsnapshot_t **out)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_richsnapshot_t *snapshot;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!layout) return TIKU_RICH_INVALID;
    snapshot = tiku_rd_alloc(layout->source.memory, sizeof *snapshot, &status);
    if (!snapshot) return status;
    *snapshot = layout->source;
    atomic_fetch_add_explicit(&snapshot->root->refs, 1, memory_order_relaxed);
    *out = snapshot;
    return TIKU_RICH_OK;
}

#include <string.h>

uint64_t
tiku_richlayout_generation(const tiku_richlayout_t *layout)
{
    return layout ? layout->source.generation : 0;
}

uint64_t
tiku_richlayout_fonts(const tiku_richlayout_t *layout)
{
    return layout ? layout->options.provider.generation : 0;
}

int
tiku_richlayout_provider_is(const tiku_richlayout_t *layout,
                           const tiku_rich_provider_t *provider)
{
    return layout && provider &&
        layout->options.provider.context == provider->context &&
        layout->options.provider.shape == provider->shape &&
        layout->options.provider.generation == provider->generation;
}

tiku_rich_status_t
tiku_richlayout_status(const tiku_richlayout_t *layout)
{
    return layout ? layout->status : TIKU_RICH_INVALID;
}

unsigned
tiku_richlayout_warnings(const tiku_richlayout_t *layout)
{
    return layout ? layout->warnings : 0;
}

size_t
tiku_richlayout_lines(const tiku_richlayout_t *layout)
{
    return layout && layout->status == TIKU_RICH_OK ? layout->nlines : 0;
}

size_t
tiku_richlayout_clusters(const tiku_richlayout_t *layout)
{
    return layout && layout->status == TIKU_RICH_OK ? layout->nclusters : 0;
}

double
tiku_richlayout_height(const tiku_richlayout_t *layout)
{
    return layout && layout->status == TIKU_RICH_OK ? layout->y : 0;
}

tiku_rich_status_t
tiku_richlayout_line(const tiku_richlayout_t *layout, size_t index,
                     tiku_rich_line_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    if (index >= layout->nlines) return TIKU_RICH_INVALID_RANGE;
    *out = layout->lines[index];
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_cluster(const tiku_richlayout_t *layout, size_t index,
                        tiku_rich_cluster_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    if (index >= layout->nclusters) return TIKU_RICH_INVALID_RANGE;
    *out = layout->clusters[index];
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_glyph(const tiku_richlayout_t *layout, size_t index,
                      tiku_rich_glyph_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    if (index >= layout->nglyphs) return TIKU_RICH_INVALID_RANGE;
    *out = layout->glyphs[index];
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rl_x(const tiku_richlayout_t *layout, const tiku_rich_line_t *line,
          size_t byte, double *out)
{
    size_t lo = line->cluster, hi = lo + line->count, mid, i;
    const tiku_rich_cluster_t *cluster;
    const tiku_rich_shape_caret_t *caret;

    if (byte < line->first || byte > line->last)
        return TIKU_RICH_INVALID_RANGE;
    if (!line->count) {
        *out = line->box.x;
        return TIKU_RICH_OK;
    }
    while (lo + 1 < hi) {
        mid = lo + (hi - lo) / 2;
        if (layout->clusters[mid].first <= byte) lo = mid;
        else hi = mid;
    }
    cluster = &layout->clusters[lo];
    for (i = 0; i < cluster->caret_count; i++) {
        caret = &layout->carets[cluster->caret + i];
        if (cluster->first + caret->byte == byte) {
            *out = cluster->box.x + caret->x;
            return TIKU_RICH_OK;
        }
    }
    return TIKU_RICH_INVALID_RANGE;
}
