/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_cache.c - incremental reuse of unchanged paragraph geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"
#include <string.h>

void
tiku_rl_cache_drop(tiku_richlayout_t *layout)
{
    tiku_richlayout_release((tiku_richlayout_t *)layout->previous);
    layout->previous = NULL;
    tiku_rl_free(layout, layout->cache_index);
    layout->cache_index = NULL;
    layout->cache_capacity = 0;
}

tiku_rich_status_t
tiku_rl_cache_attach(tiku_richlayout_t *layout,
                     const tiku_richlayout_t *previous)
{
    const tiku_richlayout_options_t *a = &layout->options, *b;
    size_t capacity = 16;

    if (!previous || previous->status != TIKU_RICH_OK ||
        previous->source.memory != layout->source.memory ||
        previous->source.root->nstyles != layout->source.root->nstyles ||
        !tiku_richlayout_provider_is(previous, &a->provider))
        return TIKU_RICH_OK;
    b = &previous->options;
    if (a->leading != b->leading || a->tab != b->tab ||
        !tiku_rd_chars_equal(&a->defaults, &b->defaults)) return TIKU_RICH_OK;
    while (capacity / 2 < previous->source.root->count) {
        if (capacity > SIZE_MAX / 2) return TIKU_RICH_LIMIT;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof *layout->cache_index)
        return TIKU_RICH_LIMIT;
    layout->cache_index = tiku_rl_alloc(layout,
        capacity * sizeof *layout->cache_index);
    if (!layout->cache_index) return layout->status;
    layout->cache_capacity = capacity;
    atomic_fetch_add_explicit(&((tiku_richlayout_t *)previous)->refs, 1,
                              memory_order_relaxed);
    layout->previous = previous;
    layout->cache_next_phase = layout->phase;
    layout->phase = RL_CACHE_STYLES;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_create_reusing(const tiku_richsnapshot_t *snapshot,
    const tiku_richlayout_options_t *options,
    const tiku_richlayout_t *previous, tiku_richlayout_t **out)
{
    tiku_rich_status_t status = tiku_richlayout_create(snapshot, options, out);

    if (status != TIKU_RICH_OK) return status;
    status = tiku_rl_cache_attach(*out, previous);
    if (status != TIKU_RICH_OK) {
        tiku_richlayout_release(*out);
        *out = NULL;
    }
    return status;
}

size_t
tiku_richlayout_reused(const tiku_richlayout_t *layout)
{
    return layout ? layout->reused : 0;
}

/** @brief Locate a paragraph or empty slot; stop after sixteen probes. */
static size_t
slot(const tiku_richlayout_t *layout, uint64_t id)
{
    uint64_t hash = id ^ (id >> 32);
    size_t at;

    hash *= UINT64_C(11400714819323198485);
    at = (size_t)(hash ^ (hash >> 32)) & (layout->cache_capacity - 1);
    for (size_t probes = 0; probes < 16; probes++) {
        if (!layout->cache_index[at] ||
            layout->previous->source.root->paragraphs[
                layout->cache_index[at] - 1]->value.id == id) return at;
        at = (at + 1) & (layout->cache_capacity - 1);
    }
    return SIZE_MAX;
}

tiku_rich_status_t
tiku_rl_cache_prepare(tiku_richlayout_t *layout)
{
    const tiku_rd_root_t *old = layout->previous->source.root;
    const tiku_rd_root_t *root = layout->source.root;
    size_t at;

    if (layout->phase == RL_CACHE_STYLES) {
        if (layout->cache_cursor < root->nstyles) {
            size_t i = layout->cache_cursor++;
            const tiku_rich_style_t *a = &root->styles[i].value;
            const tiku_rich_style_t *b = &old->styles[i].value;

            if (a->id != b->id || a->parent != b->parent ||
                a->kind != b->kind ||
                !tiku_rd_chars_equal(&a->chars, &b->chars) ||
                !tiku_rd_props_equal(&a->paragraph, &b->paragraph)) {
                layout->phase = layout->cache_next_phase;
                tiku_rl_cache_drop(layout);
            }
            return TIKU_RICH_OK;
        }
        layout->phase = RL_CACHE_INDEX;
        layout->cache_cursor = 0;
    }
    if (layout->cache_cursor == old->count) {
        layout->phase = layout->cache_next_phase;
        return TIKU_RICH_OK;
    }
    at = slot(layout, old->paragraphs[layout->cache_cursor]->value.id);
    if (at != SIZE_MAX) layout->cache_index[at] = layout->cache_cursor + 1;
    layout->cache_cursor++;
    return TIKU_RICH_OK;
}

int
tiku_rl_cache_begin(tiku_richlayout_t *layout)
{
    const tiku_richlayout_t *old = layout->previous;
    const tiku_rd_paragraph_t *p =
        layout->source.root->paragraphs[layout->paragraph];
    const tiku_rl_paragraph_t *from;
    size_t index;

    if (!old) return 0;
    index = slot(layout, p->value.id);
    if (index == SIZE_MAX) return 0;
    index = layout->cache_index[index];
    if (!index || old->source.root->paragraphs[index - 1] != p) return 0;
    from = &old->paragraphs[index - 1];
    if (from->width != layout->content_width ||
        strcmp(old->lines[from->line].marker, layout->marker)) return 0;
    layout->cache_paragraph = index - 1;
    layout->paragraphs[layout->paragraph].list_offset = from->list_offset;
    layout->cache_cursor = from->glyph;
    layout->phase = RL_REUSE_GLYPHS;
    return 1;
}

void
tiku_rl_warn(tiku_richlayout_t *layout, unsigned warnings)
{
    layout->warnings |= warnings;
    layout->paragraphs[layout->paragraph].warnings |= warnings;
}

tiku_rich_status_t
tiku_rl_cache_step(tiku_richlayout_t *layout)
{
    const tiku_richlayout_t *old = layout->previous;
    const tiku_rl_paragraph_t *from = &old->paragraphs[layout->cache_paragraph];
    const tiku_rl_paragraph_t *end = from + 1;
    const tiku_rl_paragraph_t *to = &layout->paragraphs[layout->paragraph];
    double dx = to->x - from->x, dy = to->top - from->top;

    if (layout->phase == RL_REUSE_GLYPHS) {
        if (layout->cache_cursor == end->glyph) {
            layout->phase = RL_REUSE_CARETS;
            layout->cache_cursor = from->caret;
        } else {
            if (!tiku_rl_grow(layout, (void **)&layout->glyphs,
                &layout->cglyphs, layout->nglyphs + 1, sizeof *layout->glyphs))
                return layout->status;
            layout->glyphs[layout->nglyphs++] =
                old->glyphs[layout->cache_cursor++];
        }
    } else if (layout->phase == RL_REUSE_CARETS) {
        if (layout->cache_cursor == end->caret) {
            layout->phase = RL_REUSE_CLUSTERS;
            layout->cache_cursor = from->cluster;
        } else {
            if (!tiku_rl_grow(layout, (void **)&layout->carets,
                &layout->ccarets, layout->ncarets + 1, sizeof *layout->carets))
                return layout->status;
            layout->carets[layout->ncarets++] =
                old->carets[layout->cache_cursor++];
        }
    } else if (layout->phase == RL_REUSE_CLUSTERS) {
        tiku_rich_cluster_t *cluster;

        if (layout->cache_cursor == end->cluster) {
            layout->phase = RL_REUSE_LINES;
            layout->cache_cursor = from->line;
            return TIKU_RICH_OK;
        }
        if (!tiku_rl_grow(layout, (void **)&layout->clusters, &layout->cclusters,
            layout->nclusters + 1, sizeof *layout->clusters))
            return layout->status;
        cluster = &layout->clusters[layout->nclusters++];
        *cluster = old->clusters[layout->cache_cursor++];
        cluster->glyph = to->glyph + (cluster->glyph - from->glyph);
        cluster->caret = to->caret + (cluster->caret - from->caret);
        cluster->box.x += dx;
        cluster->box.y += dy;
        cluster->baseline += dy;
    } else if (layout->cache_cursor < end->line) {
        tiku_rich_line_t *line;

        if (layout->nlines == layout->options.lines) return TIKU_RICH_LIMIT;
        if (!tiku_rl_grow(layout, (void **)&layout->lines, &layout->clines,
            layout->nlines + 1, sizeof *layout->lines)) return layout->status;
        line = &layout->lines[layout->nlines++];
        *line = old->lines[layout->cache_cursor++];
        line->cluster = to->cluster + (line->cluster - from->cluster);
        if (line->marker_count)
            line->marker_glyph = to->glyph + (line->marker_glyph - from->glyph);
        else if (line->marker[0] || layout->cache_cursor == from->line + 1)
            line->marker_glyph = to->glyph;
        line->paragraph_index = layout->paragraph;
        line->box.x += dx;
        line->box.y += dy;
        line->baseline += dy;
        if (layout->cache_cursor == from->line + 1) line->marker_x += dx;
    } else {
        layout->y = to->top + (from->bottom - from->top);
        tiku_rl_warn(layout, from->warnings);
        layout->reused++;
        tiku_rl_paragraph_done(layout);
    }
    return TIKU_RICH_OK;
}
