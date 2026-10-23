/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_hit.c - grapheme-safe caret and selection geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

#include <math.h>
#include <string.h>

size_t
tiku_rl_hit_line(const tiku_richlayout_t *layout, double x, double y)
{
    size_t lo = 0, hi = layout->nlines, mid;
    const tiku_rich_line_t *line;

    if (layout->ngrids) {
        double best = INFINITY;
        size_t result = 0;
        unsigned depth = 0;

        for (size_t i = 0; i < layout->ngrids; i++) {
            const tiku_rich_grid_box_t *g = &layout->grids[i];

            if (g->kind == TIKU_RICH_BLOCK_CELL && g->depth >= depth &&
                x >= g->box.x && x < g->box.x + g->box.width &&
                y >= g->box.y && y < g->box.y + g->box.height) {
                lo = g->line;
                hi = lo + g->line_count;
                depth = g->depth;
            }
        }
        for (size_t i = lo; i < hi; i++) {
            tiku_rich_rect_t box = layout->lines[i].box;
            double dx = depth ? 0 : fmax(0, fmax(box.x - x,
                                                x - box.x - box.width));
            double dy = fmax(0, fmax(box.y - y, y - box.y - box.height));
            double distance = dx * dx + dy * dy;

            if (distance < best) { best = distance; result = i; }
        }
        return result;
    }

    while (lo + 1 < hi) {
        mid = lo + (hi - lo) / 2;
        if (layout->lines[mid].box.y <= y) lo = mid;
        else hi = mid;
    }
    line = &layout->lines[lo];
    if (lo + 1 < layout->nlines && y > line->box.y + line->box.height &&
        y - line->box.y - line->box.height > layout->lines[lo + 1].box.y - y)
        lo++;
    return lo;
}

tiku_rich_status_t
tiku_richlayout_hit(const tiku_richlayout_t *layout, double x, double y,
                    tiku_rich_pos_t *out)
{
    const tiku_rich_line_t *line;
    const tiku_rich_cluster_t *cluster;
    const tiku_rich_shape_caret_t *caret;
    size_t lo, hi, mid, i;
    double best = INFINITY, distance;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout || !isfinite(x) || !isfinite(y)) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    line = &layout->lines[tiku_rl_hit_line(layout, x, y)];
    *out = (tiku_rich_pos_t){layout->source.root->body, line->paragraph,
                            line->first, TIKU_RICH_AFTER};
    lo = line->cluster;
    hi = lo + line->count;
    if (!line->count) return TIKU_RICH_OK;
    while (lo + 1 < hi) {
        mid = lo + (hi - lo) / 2;
        if (layout->clusters[mid].box.x <= x) lo = mid;
        else hi = mid;
    }
    cluster = &layout->clusters[lo];
    for (i = 0; i < cluster->caret_count; i++) {
        caret = &layout->carets[cluster->caret + i];
        if (cluster->first + caret->byte > line->last) break;
        distance = fabs(cluster->box.x + caret->x - x);
        if (distance < best) {
            best = distance;
            out->byte = cluster->first + caret->byte;
        }
    }
    if (out->byte == line->last && line->first != line->last)
        out->affinity = TIKU_RICH_BEFORE;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_image_at(const tiku_richlayout_t *layout, double x, double y,
                         tiku_rich_cluster_t *out)
{
    const tiku_rich_line_t *line;
    const tiku_rich_cluster_t *c;
    size_t lo, hi;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout || !isfinite(x) || !isfinite(y)) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    line = &layout->lines[tiku_rl_hit_line(layout, x, y)];
    lo = line->cluster;
    hi = lo + line->count;
    if (lo == hi) return TIKU_RICH_NOT_FOUND;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (layout->clusters[mid].box.x <= x) lo = mid;
        else hi = mid;
    }
    c = &layout->clusters[lo];
    if (!c->image || x < c->box.x || x >= c->box.x + c->image->width ||
        y < c->baseline - c->image->height || y >= c->baseline)
        return TIKU_RICH_NOT_FOUND;
    *out = *c;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_image_selection(const tiku_richlayout_t *layout,
    tiku_rich_pos_t first, tiku_rich_pos_t last, tiku_rich_cluster_t *out)
{
    size_t index, lo, hi;
    const tiku_rich_cluster_t *c;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    if (tiku_richsnapshot_position(&layout->source, first) != TIKU_RICH_OK ||
        tiku_richsnapshot_position(&layout->source, last) != TIKU_RICH_OK)
        return TIKU_RICH_INVALID_RANGE;
    if (first.paragraph != last.paragraph || last.byte - first.byte != 3)
        return TIKU_RICH_NOT_FOUND;
    index = tiku_rd_find(layout->source.root, first.paragraph);
    lo = layout->paragraphs[index].cluster;
    hi = layout->paragraphs[index + 1].cluster;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (layout->clusters[mid].first < first.byte) lo = mid + 1;
        else hi = mid;
    }
    if (lo == layout->nclusters) return TIKU_RICH_NOT_FOUND;
    c = &layout->clusters[lo];
    if (!c->image || c->paragraph != first.paragraph ||
        c->first != first.byte || c->last != last.byte)
        return TIKU_RICH_NOT_FOUND;
    *out = *c;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_caret(const tiku_richlayout_t *layout,
                      tiku_rich_pos_t position, tiku_rich_rect_t *out)
{
    const tiku_rich_line_t *line = NULL;
    size_t i, paragraph;
    double x;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    status = tiku_richsnapshot_position(&layout->source, position);
    if (status != TIKU_RICH_OK) return status;
    paragraph = tiku_rd_find(layout->source.root, position.paragraph);
    for (i = layout->paragraphs[paragraph].line;
         i < layout->paragraphs[paragraph + 1].line; i++) {
        if (layout->lines[i].paragraph != position.paragraph ||
            position.byte < layout->lines[i].first ||
            position.byte > layout->lines[i].last) continue;
        line = &layout->lines[i];
        if (position.affinity == TIKU_RICH_BEFORE ||
            position.byte < line->last) break;
    }
    if (!line) return TIKU_RICH_INVALID_RANGE;
    status = tiku_rl_x(layout, line, position.byte, &x);
    if (status != TIKU_RICH_OK) return status;
    *out = (tiku_rich_rect_t){x, line->box.y, 1, line->box.height};
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richlayout_selection(const tiku_richlayout_t *layout,
    tiku_rich_pos_t first, tiku_rich_pos_t last, size_t index,
    tiku_rich_rect_t *out)
{
    const tiku_rich_line_t *line;
    tiku_rich_rect_t checked;
    size_t ranks[3], start, end;
    double x, right;
    int order;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    if (index >= layout->nlines) return TIKU_RICH_INVALID_RANGE;
    status = tiku_richsnapshot_compare(&layout->source, first, last, &order);
    if (status != TIKU_RICH_OK) return status;
    if (order > 0) return TIKU_RICH_INVALID_RANGE;
    status = tiku_richlayout_caret(layout, first, &checked);
    if (status == TIKU_RICH_OK)
        status = tiku_richlayout_caret(layout, last, &checked);
    if (status != TIKU_RICH_OK) return status;
    if (!order) return TIKU_RICH_NO_CHANGE;
    line = &layout->lines[index];
    ranks[0] = tiku_rd_find(layout->source.root, first.paragraph);
    ranks[1] = tiku_rd_find(layout->source.root, last.paragraph);
    ranks[2] = tiku_rd_find(layout->source.root, line->paragraph);
    if (ranks[2] < ranks[0] || ranks[2] > ranks[1]) return TIKU_RICH_NO_CHANGE;
    start = ranks[2] == ranks[0] && first.byte > line->first ?
        first.byte : line->first;
    end = ranks[2] == ranks[1] && last.byte < line->last ?
        last.byte : line->last;
    if (start > end || (start == end && ranks[2] == ranks[1] &&
                       last.byte <= end))
        return TIKU_RICH_NO_CHANGE;
    if (tiku_rl_x(layout, line, start, &x) != TIKU_RICH_OK ||
        tiku_rl_x(layout, line, end, &right) != TIKU_RICH_OK)
        return TIKU_RICH_NO_CHANGE;
    if (end == line->last && (ranks[2] < ranks[1] || last.byte > line->last) &&
        (index + 1 == layout->nlines ||
         layout->lines[index + 1].paragraph != line->paragraph ||
         layout->lines[index + 1].first > line->last))
        right += 4;
    *out = (tiku_rich_rect_t){x, line->box.y, right - x, line->box.height};
    return TIKU_RICH_OK;
}
