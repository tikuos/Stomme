/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_edit.c - full-body selection and grapheme deletion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <math.h>

tiku_rich_status_t
tiku_richview_select_all(tiku_richview_t *view)
{
    tiku_rich_pos_t first, last;
    const tiku_rd_root_t *root;
    const tiku_rich_paragraph_t *end;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    view->cells.table = 0;
    root = view->doc->root;
    end = &root->paragraphs[root->count - 1]->value;
    first = (tiku_rich_pos_t){root->body, root->paragraphs[0]->value.id,
        0, TIKU_RICH_AFTER};
    last = (tiku_rich_pos_t){root->body, end->id, end->length,
        TIKU_RICH_BEFORE};
    return tiku_richsession_select(view->session, view->doc->generation,
                                    first, last);
}

tiku_rich_status_t
tiku_richview_delete(tiku_richview_t *view, int backward)
{
    tiku_richcmd_context_t context;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_DELETE};
    tiku_rich_pos_t adjacent;
    tiku_rich_rect_t rect;
    tiku_rich_status_t status;
    const tiku_richlayout_t *layout;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    tiku_rich_cells_t cells;
    if (tiku_richview_cells(view, &cells) == TIKU_RICH_OK)
        return tiku_richview_cells_clear(view);
    layout = tiku_richview_layout(view);
    if (!layout) return tiku_richview_status(view);
    context = tiku_rs_context(view->session);
    status = tiku_richlayout_caret(layout, context.first, &rect);
    if (status != TIKU_RICH_OK) return status;
    status = tiku_richlayout_caret(layout, context.last, &rect);
    if (status != TIKU_RICH_OK) return status;
    if (context.first.paragraph == context.last.paragraph &&
        context.first.byte == context.last.byte) {
        status = tiku_richlayout_move(layout, context.first,
            backward ? TIKU_RICH_MOVE_LEFT : TIKU_RICH_MOVE_RIGHT,
            NAN, &adjacent);
        if (status != TIKU_RICH_OK) return status;
        if (backward) context.first = adjacent;
        else context.last = adjacent;
    }
    view->session->collapse = 1;
    view->doc->initiator = view->session;
    status = tiku_richcmd_execute(view->doc, &context, &command);
    view->doc->initiator = NULL;
    view->session->collapse = 0;
    return status;
}
