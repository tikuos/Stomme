/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_flow.c - incremental line wrapping and baseline placement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

#include <math.h>
#include <string.h>

void
tiku_rl_flow_start(tiku_richlayout_t *layout)
{
    layout->left = layout->content_x + layout->props.left +
        layout->paragraphs[layout->paragraph].list_offset;
    if (layout->line_start == layout->paragraph_start)
        layout->left += layout->props.first;
    layout->available = layout->content_width - layout->props.right -
                         (layout->left - layout->content_x);
    if (layout->available < 1) {
        layout->available = 1;
        tiku_rl_warn(layout, TIKU_RICH_LAYOUT_OVERFLOW);
    }
    layout->flow = layout->line_start;
    layout->wrap = layout->line_start;
    layout->width = 0;
    layout->phase = RL_FLOW;
}

/** @brief Select an exclusive endpoint and start measuring that line. */
static void
finish_scan(tiku_richlayout_t *layout, size_t end)
{
    layout->end = end;
    layout->flow = layout->line_start;
    layout->width = 0;
    layout->top = layout->ascent;
    layout->bottom = layout->descent;
    layout->spaces = 0;
    layout->phase = RL_METRICS;
}

/** @brief Inspect one cluster to find a line's wrap or soft-break endpoint. */
static void
scan(tiku_richlayout_t *layout)
{
    tiku_rich_cluster_t *cluster;
    double width;

    if (layout->flow == layout->nclusters) {
        finish_scan(layout, layout->flow);
        return;
    }
    cluster = &layout->clusters[layout->flow];
    if (cluster->flags & RL_BREAK) {
        finish_scan(layout, layout->flow + 1);
        return;
    }
    width = cluster->box.width;
    if (cluster->flags & RL_TAB)
        width = layout->options.tab - fmod(layout->width, layout->options.tab);
    if (layout->width + width > layout->available &&
        layout->flow > layout->line_start) {
        finish_scan(layout, layout->wrap > layout->line_start ?
                     layout->wrap : layout->flow);
        return;
    }
    layout->width += width;
    layout->flow++;
    if (cluster->flags & RL_WRAP) {
        layout->wrap = layout->flow;
    }
    if (layout->width > layout->available) {
        tiku_rl_warn(layout, TIKU_RICH_LAYOUT_OVERFLOW);
        finish_scan(layout, layout->flow);
    }
}

/** @brief Allocate a visual line after its complete metrics are known. */
static tiku_rich_status_t
line_create(tiku_richlayout_t *layout)
{
    const tiku_rich_paragraph_t *p =
        &layout->source.root->paragraphs[layout->paragraph]->value;
    tiku_rich_line_t *line;
    const tiku_rich_cluster_t *last = layout->end > layout->line_start ?
        &layout->clusters[layout->end - 1] : NULL;
    double extra = layout->available - layout->width;

    if (layout->nlines == layout->options.lines)
        return TIKU_RICH_LIMIT;
    if (!tiku_rl_grow(layout, (void **)&layout->lines, &layout->clines,
                     layout->nlines + 1, sizeof *layout->lines))
        return layout->status;
    line = &layout->lines[layout->nlines++];
    line->paragraph = p->id;
    line->paragraph_index = layout->paragraph;
    line->cluster = layout->line_start;
    line->count = layout->end - layout->line_start;
    line->first = line->count ? layout->clusters[line->cluster].first :
                               p->length;
    line->last = last ? ((last->flags & RL_BREAK) ? last->first : last->last) :
                        p->length;
    line->box = (tiku_rich_rect_t){layout->left, layout->y, layout->width,
        layout->top + layout->bottom + layout->options.leading};
    line->baseline = layout->y + layout->top;
    if (layout->line_start == layout->paragraph_start) {
        memcpy(line->marker, layout->marker, sizeof line->marker);
        line->marker_x = layout->content_x + layout->props.left +
                         p->list.level * 12;
        line->marker_chars = layout->defaults;
        line->marker_glyph = layout->marker_glyph;
        line->marker_count = layout->marker_count;
    }
    layout->justify = 0;
    if (extra > 0) {
        if (layout->props.align == TIKU_RICH_CENTER) line->box.x += extra / 2;
        if (layout->props.align == TIKU_RICH_RIGHT) line->box.x += extra;
        if (layout->props.align == TIKU_RICH_JUSTIFY && layout->spaces &&
            layout->end < layout->nclusters && last &&
            !(last->flags & RL_BREAK)) {
            layout->justify = extra / layout->spaces;
            line->box.width = layout->available;
        }
    }
    layout->x = line->box.x;
    layout->flow = layout->line_start;
    layout->phase = RL_PLACE;
    return TIKU_RICH_OK;
}

/** @brief Measure one selected cluster, including line-relative tab stops. */
static tiku_rich_status_t
metrics(tiku_richlayout_t *layout)
{
    tiku_rich_cluster_t *cluster;
    double descent;

    if (layout->flow == layout->end)
        return line_create(layout);
    cluster = &layout->clusters[layout->flow++];
    if (cluster->flags & RL_TAB) {
        cluster->box.width = layout->options.tab -
                              fmod(layout->width, layout->options.tab);
        layout->carets[cluster->caret + cluster->caret_count - 1].x =
            cluster->box.width;
    }
    layout->width += cluster->box.width;
    if (cluster->flags & RL_SPACE) layout->spaces++;
    descent = cluster->box.height - cluster->baseline;
    if (cluster->baseline > layout->top) layout->top = cluster->baseline;
    if (descent > layout->bottom) layout->bottom = descent;
    return TIKU_RICH_OK;
}

/** @brief Finish one line, continuing at its wrap or the next paragraph. */
static void
line_done(tiku_richlayout_t *layout)
{
    const tiku_rich_line_t *line = &layout->lines[layout->nlines - 1];
    int final_break = layout->end > layout->line_start &&
        (layout->clusters[layout->end - 1].flags & RL_BREAK);

    layout->y += line->box.height;
    layout->line_start = layout->end;
    if (layout->end < layout->nclusters ||
        (final_break && !layout->tail_line)) {
        if (layout->end == layout->nclusters) layout->tail_line = 1;
        tiku_rl_flow_start(layout);
        return;
    }
    layout->y += layout->props.after;
    tiku_rl_paragraph_done(layout);
}

void
tiku_rl_paragraph_done(tiku_richlayout_t *layout)
{
    layout->paragraphs[layout->paragraph].bottom = layout->y;
    layout->paragraph++;
    if (layout->paragraph == layout->source.root->count) {
        layout->paragraphs[layout->paragraph] = (tiku_rl_paragraph_t){
            .cluster = layout->nclusters, .line = layout->nlines,
            .glyph = layout->nglyphs, .caret = layout->ncarets};
        layout->status = tiku_rl_containers(layout, 1);
    } else layout->phase = RL_PREPARE;
}

/** @brief Position one cluster using the final line baseline and alignment. */
static void
place(tiku_richlayout_t *layout)
{
    const tiku_rich_line_t *line = &layout->lines[layout->nlines - 1];
    tiku_rich_cluster_t *cluster;

    if (layout->flow == layout->end) {
        line_done(layout);
        return;
    }
    cluster = &layout->clusters[layout->flow++];
    cluster->box.x = layout->x;
    cluster->box.y = line->box.y;
    cluster->box.height = line->box.height;
    cluster->baseline = line->baseline;
    if (cluster->flags & RL_SPACE) {
        cluster->box.width += layout->justify;
        layout->carets[cluster->caret + cluster->caret_count - 1].x +=
            layout->justify;
    }
    layout->x += cluster->box.width;
}

tiku_rich_status_t
tiku_rl_line(tiku_richlayout_t *layout)
{
    if (layout->phase == RL_FLOW) scan(layout);
    else if (layout->phase == RL_METRICS) return metrics(layout);
    else place(layout);
    return TIKU_RICH_OK;
}
