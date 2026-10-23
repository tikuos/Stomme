/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_select.c - word and wrapped-line pointer selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include "tiku_richtext.h"
#include <math.h>

/** @brief Find a word segment whose ends are also grapheme boundaries. */
static tiku_rich_status_t
word_range(tiku_richview_t *view, tiku_rich_pos_t *first,
           tiku_rich_pos_t *last)
{
    size_t index = tiku_rd_find(view->doc->root, first->paragraph);
    const tiku_rich_paragraph_t *p = &view->doc->root->paragraphs[index]->value;
    tiku_rich_status_t status = TIKU_RICH_OK;
    uint8_t *flags = tiku_rd_alloc(view->doc->memory, p->length + 1, &status);

    if (!flags) return status;
    status = tiku_richtext_boundaries(p->text, p->length, flags, p->length + 1);
    if (status == TIKU_RICH_OK) {
        size_t at = 0;

        for (size_t i = 0; i < p->run_count; i++) {
            if (p->runs[i].image) flags[at] = flags[at + 3] = 3;
            at += p->runs[i].length;
        }
    }
    if (status == TIKU_RICH_OK && p->length) {
        if (first->byte == p->length) first->byte--;
        while (first->byte && (flags[first->byte] & 3) != 3) first->byte--;
        last->byte = first->byte + 1;
        while (last->byte < p->length && (flags[last->byte] & 3) != 3)
            last->byte++;
    }
    tiku_rd_free(flags);
    return status;
}

tiku_rich_status_t
tiku_richview_select_at(tiku_richview_t *view, double x, double y, int line)
{
    const tiku_richlayout_t *layout;
    tiku_rich_pos_t first, last;
    tiku_rich_status_t status;

    if (!view || !isfinite(x) || !isfinite(y)) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    layout = tiku_richview_layout(view);
    if (!layout) return tiku_richview_status(view);
    status = tiku_richlayout_hit(layout, x / view->zoom,
        y / view->zoom + view->scroll, &first);
    if (status != TIKU_RICH_OK) return status;
    last = first;
    if (line) {
        status = tiku_richlayout_move(layout, first, TIKU_RICH_MOVE_HOME,
                                      NAN, &first);
        if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE)
            status = tiku_richlayout_move(layout, last, TIKU_RICH_MOVE_END,
                                          NAN, &last);
    } else {
        status = word_range(view, &first, &last);
    }
    if (status != TIKU_RICH_OK && status != TIKU_RICH_NO_CHANGE) return status;
    first.affinity = TIKU_RICH_AFTER;
    last.affinity = TIKU_RICH_BEFORE;
    status = tiku_richsession_select(view->session, 0, first, last);
    if (status == TIKU_RICH_OK) {
        view->preferred = NAN;
        view->cells.table = 0;
    }
    return status;
}
