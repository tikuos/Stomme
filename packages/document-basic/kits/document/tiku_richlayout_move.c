/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_move.c - logical grapheme and shared visual-line navigation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

#include <math.h>
#include <string.h>

tiku_rich_status_t
tiku_richlayout_snap(const tiku_richlayout_t *layout,
    tiku_rich_pos_t position, int forward, tiku_rich_pos_t *out)
{
    tiku_rich_status_t status;
    size_t paragraph, low, high, byte;
    const tiku_rich_cluster_t *cluster;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!layout || (forward != 0 && forward != 1)) return TIKU_RICH_INVALID;
    if (layout->status != TIKU_RICH_OK) return layout->status;
    status = tiku_richsnapshot_position(&layout->source, position);
    if (status != TIKU_RICH_OK) return status;
    paragraph = tiku_rd_find(layout->source.root, position.paragraph);
    *out = position;
    if (!position.byte || position.byte ==
        layout->source.root->paragraphs[paragraph]->value.length)
        return TIKU_RICH_OK;
    low = layout->paragraphs[paragraph].cluster;
    high = layout->paragraphs[paragraph + 1].cluster;
    while (low + 1 < high) {
        size_t middle = low + (high - low) / 2;

        if (layout->clusters[middle].first <= position.byte) low = middle;
        else high = middle;
    }
    cluster = &layout->clusters[low];
    out->byte = cluster->first;
    for (size_t i = 0; i < cluster->caret_count; i++) {
        byte = cluster->first + layout->carets[cluster->caret + i].byte;
        if (byte <= position.byte || forward) out->byte = byte;
        if (byte >= position.byte) break;
    }
    return TIKU_RICH_OK;
}

/** @brief Move to a neighboring grapheme across paragraph breaks. */
static tiku_rich_status_t
adjacent(const tiku_richlayout_t *layout, tiku_rich_pos_t position,
         int forward, tiku_rich_pos_t *out)
{
    size_t p = tiku_rd_find(layout->source.root, position.paragraph);
    size_t lo = layout->paragraphs[p].cluster;
    size_t hi = layout->paragraphs[p + 1].cluster, mid, i, byte;
    const tiku_rich_paragraph_t *paragraph =
        &layout->source.root->paragraphs[p]->value;
    const tiku_rich_cluster_t *cluster;

    *out = position;
    if ((forward && position.byte == paragraph->length) ||
        (!forward && !position.byte)) {
        if ((forward && p + 1 == layout->source.root->count) ||
            (!forward && !p)) return TIKU_RICH_NO_CHANGE;
        p = forward ? p + 1 : p - 1;
        paragraph = &layout->source.root->paragraphs[p]->value;
        out->paragraph = paragraph->id;
        out->byte = forward ? 0 : paragraph->length;
    } else {
        while (lo + 1 < hi) {
            mid = lo + (hi - lo) / 2;
            byte = layout->clusters[mid].first;
            if (byte < position.byte || (forward && byte == position.byte))
                lo = mid;
            else hi = mid;
        }
        cluster = &layout->clusters[lo];
        for (i = 0; i < cluster->caret_count; i++) {
            byte = cluster->first + layout->carets[cluster->caret + i].byte;
            if (forward && byte > position.byte) { out->byte = byte; break; }
            if (!forward && byte < position.byte) out->byte = byte;
        }
    }
    out->affinity = forward ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE;
    return TIKU_RICH_OK;
}

/** @brief Find the visual line whose top matches a validated caret box. */
static size_t
caret_line(const tiku_richlayout_t *layout, uint64_t paragraph, double y)
{
    size_t lo = 0, hi = layout->nlines, mid;

    if (layout->ngrids) {
        for (size_t i = 0; i < hi; i++)
            if (layout->lines[i].paragraph == paragraph &&
                layout->lines[i].box.y == y) return i;
    }

    while (lo + 1 < hi) {
        mid = lo + (hi - lo) / 2;
        if (layout->lines[mid].box.y <= y) lo = mid;
        else hi = mid;
    }
    return lo;
}

/** @brief Find a vertically neighboring line using horizontal proximity. */
static size_t
vertical_line(const tiku_richlayout_t *layout, size_t current,
               double x, int down)
{
    size_t result = current;
    double best = INFINITY, y = layout->lines[current].box.y;

    for (size_t i = 0; i < layout->nlines; i++) {
        tiku_rich_rect_t box = layout->lines[i].box;
        double dy = down ? box.y - y : y - box.y;
        double dx = fmax(0, fmax(box.x - x, x - box.x - box.width));

        if (dy > 0 && dy * dy + dx * dx < best) {
            best = dy * dy + dx * dx;
            result = i;
        }
    }
    return result;
}

tiku_rich_status_t
tiku_richlayout_move(const tiku_richlayout_t *layout,
    tiku_rich_pos_t position, tiku_rich_motion_t motion, double preferred_x,
    tiku_rich_pos_t *out)
{
    tiku_rich_rect_t caret;
    tiku_rich_status_t status;
    size_t line, p;
    const tiku_rich_paragraph_t *paragraph;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (motion < TIKU_RICH_MOVE_LEFT || motion > TIKU_RICH_MOVE_FINISH ||
        isinf(preferred_x)) return TIKU_RICH_INVALID;
    status = tiku_richlayout_caret(layout, position, &caret);
    if (status != TIKU_RICH_OK) return status;
    if (motion == TIKU_RICH_MOVE_LEFT || motion == TIKU_RICH_MOVE_RIGHT)
        return adjacent(layout, position, motion == TIKU_RICH_MOVE_RIGHT, out);
    *out = position;
    if (motion == TIKU_RICH_MOVE_START || motion == TIKU_RICH_MOVE_FINISH) {
        p = motion == TIKU_RICH_MOVE_START ? 0 : layout->source.root->count - 1;
        paragraph = &layout->source.root->paragraphs[p]->value;
        out->paragraph = paragraph->id;
        out->byte = motion == TIKU_RICH_MOVE_START ? 0 : paragraph->length;
        out->affinity = TIKU_RICH_AFTER;
        return TIKU_RICH_OK;
    }
    line = caret_line(layout, position.paragraph, caret.y);
    if (motion == TIKU_RICH_MOVE_HOME || motion == TIKU_RICH_MOVE_END) {
        out->byte = motion == TIKU_RICH_MOVE_HOME ? layout->lines[line].first :
                                                  layout->lines[line].last;
        out->affinity = motion == TIKU_RICH_MOVE_HOME ? TIKU_RICH_AFTER :
                                                      TIKU_RICH_BEFORE;
        return TIKU_RICH_OK;
    }
    if (layout->ngrids) {
        p = vertical_line(layout, line,
            isnan(preferred_x) ? caret.x : preferred_x,
            motion == TIKU_RICH_MOVE_DOWN);
        if (p == line) return TIKU_RICH_NO_CHANGE;
        line = p;
    } else {
        if ((motion == TIKU_RICH_MOVE_UP && !line) ||
            (motion == TIKU_RICH_MOVE_DOWN && line + 1 == layout->nlines))
            return TIKU_RICH_NO_CHANGE;
        line = motion == TIKU_RICH_MOVE_UP ? line - 1 : line + 1;
    }
    return tiku_richlayout_hit(layout, isnan(preferred_x) ? caret.x :
        preferred_x, layout->lines[line].box.y, out);
}
