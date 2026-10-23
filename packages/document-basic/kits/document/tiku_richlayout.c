/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout.c - continuous layout lifecycle and incremental scheduling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief Validate settings and supply display defaults without model edits. */
static tiku_rich_status_t
settings(const tiku_richlayout_options_t *input,
         tiku_richlayout_options_t *out)
{
    tiku_rich_chars_t checked;

    if (!input || !input->provider.shape ||
        !!input->provider.retain != !!input->provider.release ||
        !isfinite(input->width) || input->width < 1 ||
        input->width > 1000000 || !isfinite(input->leading) ||
        input->leading < 0 || input->leading > 4096 ||
        !isfinite(input->tab) || input->tab < 0 || input->tab > 1000000 ||
        tiku_rd_chars(&input->defaults, &checked) != TIKU_RICH_OK)
        return TIKU_RICH_INVALID;
    *out = *input;
    out->defaults = checked;
    if (!(checked.present & TIKU_RICH_SIZE)) out->defaults.size = 12;
    if (!(checked.present & TIKU_RICH_FOREGROUND))
        out->defaults.foreground = 0xff000000u;
    out->defaults.present |= TIKU_RICH_SIZE | TIKU_RICH_FOREGROUND;
    if (!out->tab) out->tab = 48;
    if (!out->lines) out->lines = 1000000;
    if (!out->memory_bytes) out->memory_bytes = (size_t)128 << 20;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_create(const tiku_richsnapshot_t *snapshot,
    const tiku_richlayout_options_t *options, tiku_richlayout_t **out)
{
    tiku_richlayout_options_t checked;
    tiku_richlayout_t *layout;
    tiku_rich_status_t status;

    if (!out)
        return TIKU_RICH_INVALID;
    *out = NULL;
    if (!snapshot)
        return TIKU_RICH_INVALID;
    status = settings(options, &checked);
    if (status != TIKU_RICH_OK)
        return status;
    layout = tiku_rd_alloc(snapshot->memory, sizeof *layout, &status);
    if (!layout)
        return status;
    atomic_init(&layout->refs, 1);
    layout->used = tiku_rd_size(layout);
    if (layout->used > checked.memory_bytes) {
        tiku_rd_free(layout);
        return TIKU_RICH_LIMIT;
    }
    layout->source = *snapshot;
    atomic_fetch_add_explicit(&snapshot->root->refs, 1, memory_order_relaxed);
    layout->options = checked;
    layout->content_width = checked.width;
    layout->phase = RL_PREPARE;
    layout->status = TIKU_RICH_PENDING;
    if (checked.provider.retain)
        checked.provider.retain(checked.provider.context);
    if (snapshot->root->count >= SIZE_MAX / sizeof *layout->paragraphs) {
        tiku_richlayout_release(layout);
        return TIKU_RICH_LIMIT;
    }
    layout->paragraphs = tiku_rl_alloc(layout,
        (snapshot->root->count + 1) * sizeof *layout->paragraphs);
    if (!layout->paragraphs) {
        status = layout->status;
        tiku_richlayout_release(layout);
        return status;
    }
    if (snapshot->root->blocks &&
        snapshot->root->blocks->count > snapshot->root->count) {
        layout->minimums = tiku_rl_alloc(layout,
            snapshot->root->blocks->count * sizeof *layout->minimums);
        if (!layout->minimums) {
            status = layout->status;
            tiku_richlayout_release(layout);
            return status;
        }
        layout->phase = RL_MINIMUM;
    }
    *out = layout;
    return TIKU_RICH_OK;
}

/** @brief Update bounded list counters and produce the paragraph label. */
static tiku_rich_status_t
list_marker(tiku_richlayout_t *layout, const tiku_rich_list_t *item)
{
    size_t level = item->level, i;

    memset(layout->marker, 0, sizeof layout->marker);
    if (item->kind == TIKU_RICH_LIST_NONE) {
        memset(layout->numbers, 0, sizeof layout->numbers);
        memset(layout->kinds, 0, sizeof layout->kinds);
        return TIKU_RICH_OK;
    }
    for (i = level + 1; i < 9; i++) {
        layout->numbers[i] = 0;
        layout->kinds[i] = TIKU_RICH_LIST_NONE;
    }
    if (layout->kinds[level] != item->kind) layout->numbers[level] = 0;
    layout->kinds[level] = item->kind;
    if (item->kind == TIKU_RICH_LIST_BULLET) {
        memcpy(layout->marker, "\xe2\x80\xa2", 4);
    } else {
        if (item->restart) layout->numbers[level] = item->restart;
        else if (layout->numbers[level] == UINT64_MAX)
            return TIKU_RICH_LIMIT;
        else layout->numbers[level]++;
        snprintf(layout->marker, sizeof layout->marker, "%llu.",
                 (unsigned long long)layout->numbers[level]);
    }
    return TIKU_RICH_OK;
}

/** @brief Prepare one paragraph's segmentation, defaults and empty metrics. */
static tiku_rich_status_t
prepare(tiku_richlayout_t *layout)
{
    const tiku_rich_paragraph_t *p =
        &layout->source.root->paragraphs[layout->paragraph]->value;
    tiku_rich_chars_t chars;
    tiku_rich_shape_t shape = {0};
    const uint8_t boundaries[2] = {1, 1};
    tiku_rich_status_t status;

    status = tiku_rl_containers(layout, 0);
    if (status != TIKU_RICH_OK) return status;
    layout->paragraphs[layout->paragraph] = (tiku_rl_paragraph_t){
        .cluster = layout->nclusters, .line = layout->nlines,
        .glyph = layout->nglyphs, .caret = layout->ncarets,
        .x = layout->content_x, .width = layout->content_width,
        .top = layout->y, .owner = layout->depth ?
            layout->containers[layout->depth - 1].node.id :
            layout->source.root->body};
    status = list_marker(layout, &p->list);
    if (status != TIKU_RICH_OK) return status;
    if (tiku_rl_cache_begin(layout)) return TIKU_RICH_OK;
    status = tiku_rd_resolve_value(layout->source.root, p, NULL, &chars,
                                   &layout->props);
    if (status != TIKU_RICH_OK)
        return status;
    layout->defaults = layout->options.defaults;
    tiku_rd_chars_overlay(&layout->defaults, &chars);
    status = layout->options.provider.shape(layout->options.provider.context,
        &layout->defaults, " ", 1, boundaries, &shape);
    if (status != TIKU_RICH_OK)
        return status == TIKU_RICH_PENDING ? TIKU_RICH_INVALID : status;
    if (!tiku_rl_valid_shape(&shape, boundaries, 1))
        return TIKU_RICH_INVALID;
    layout->ascent = shape.ascent;
    layout->descent = shape.descent;
    tiku_rl_warn(layout, shape.warnings);
    layout->boundaries = tiku_rl_alloc(layout, p->length + 1);
    if (!layout->boundaries)
        return layout->status;
    memset(&layout->grapheme, 0, sizeof layout->grapheme);
    layout->boundaries[p->length] = 1;
    layout->byte = layout->run = layout->run_end = 0;
    layout->shape_end = 0;
    if (p->run_count) layout->run_end = p->runs[0].length;
    layout->paragraph_start = layout->line_start = layout->nclusters;
    layout->tail_line = 0;
    layout->y += layout->props.before;
    layout->phase = RL_BOUNDARIES;
    status = tiku_rl_marker(layout);
    if (status == TIKU_RICH_OK && p->list.kind != TIKU_RICH_LIST_NONE)
        layout->paragraphs[layout->paragraph].list_offset =
            fmax(24, layout->marker_width + 12) + p->list.level * 12;
    return status;
}

/** @brief Index one Unicode scalar without splitting a grapheme cluster. */
static tiku_rich_status_t
boundary(tiku_richlayout_t *layout)
{
    const tiku_rich_paragraph_t *p =
        &layout->source.root->paragraphs[layout->paragraph]->value;
    size_t start = layout->byte;
    uint32_t scalar;

    if (start == p->length) {
        layout->phase = RL_SHAPE;
        layout->byte = 0;
        return TIKU_RICH_OK;
    }
    if (tiku_rt_decode(p->text, p->length, &layout->byte, &scalar) !=
        TIKU_RICH_OK)
        return TIKU_RICH_INVALID_TEXT;
    layout->boundaries[start] = (uint8_t)tiku_rt_grapheme(&layout->grapheme,
                                                   tiku_rt_properties(scalar));
    if (scalar == 0xfffc || (start >= 3 &&
        !memcmp(p->text + start - 3, "\xef\xbf\xbc", 3)))
        layout->boundaries[start] = 1;
    if (!start) layout->boundaries[0] = 1;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_step(tiku_richlayout_t *layout, size_t work)
{
    tiku_rich_status_t status;

    if (!layout)
        return TIKU_RICH_INVALID;
    while (work-- && layout->status == TIKU_RICH_PENDING) {
        if (layout->phase == RL_CACHE_STYLES || layout->phase == RL_CACHE_INDEX)
            status = tiku_rl_cache_prepare(layout);
        else if (layout->phase >= RL_REUSE_GLYPHS)
            status = tiku_rl_cache_step(layout);
        else if (layout->phase == RL_MINIMUM) status = tiku_rl_minimum(layout);
        else if (layout->phase == RL_PREPARE) status = prepare(layout);
        else if (layout->phase == RL_BOUNDARIES) status = boundary(layout);
        else if (layout->phase == RL_SHAPE) status = tiku_rl_shape(layout);
        else status = tiku_rl_line(layout);
        if (status != TIKU_RICH_OK)
            layout->status = status == TIKU_RICH_PENDING ?
                TIKU_RICH_INVALID : status;
    }
    if (layout->status != TIKU_RICH_PENDING) tiku_rl_cache_drop(layout);
    return layout->status;
}
