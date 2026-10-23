/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_cells.c - independent cell selection and checked bulk actions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <string.h>

tiku_rich_status_t
tiku_richview_cells(const tiku_richview_t *view, tiku_rich_cells_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!view) return TIKU_RICH_INVALID;
    if (!view->cells.table || view->cells_generation != view->doc->generation ||
        view->cells_anchor != view->session->anchor->revision ||
        view->cells_caret != view->session->caret->revision)
        return TIKU_RICH_NOT_FOUND;
    *out = view->cells;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_select_cells(tiku_richview_t *view, uint64_t generation,
                           const tiku_rich_cells_t *cells)
{
    tiku_rd_cells_scan_t scan;
    tiku_rich_status_t status;
    tiku_rich_pos_t position;
    tiku_rich_cells_t selected;
    size_t paragraph;
    const tiku_rich_block_t *node;
    tiku_rich_block_t focus;
    tiku_richsnapshot_t snapshot;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (generation && generation != view->doc->generation)
        return TIKU_RICH_STALE;
    if (!cells) {
        view->cells.table = 0;
        view->cells_drag = 0;
        tiku_text_input_reset(&view->input);
        return tiku_richsession_break_undo(view->session);
    }
    selected = *cells;
    status = tiku_rd_cells_begin(view->doc->root, &selected, &scan);
    if (status != TIKU_RICH_OK) return status;
    node = tiku_rd_cells_next(&scan, &paragraph);
    uint64_t origin = node->id;
    snapshot = (tiku_richsnapshot_t){.root = view->doc->root};
    tiku_richsnapshot_cell_at(&snapshot, &selected,
        (size_t)selected.rows * selected.columns - 1, &focus);
    position = (tiku_rich_pos_t){view->doc->root->body,
        tiku_rd_grid_paragraph(view->doc->root, selected.table,
            selected.row * scan.columns + selected.column), 0, TIKU_RICH_AFTER};
    status = tiku_richsession_select(view->session, view->doc->generation,
                                      position, position);
    if (status != TIKU_RICH_OK) return status;
    view->cells = selected;
    view->cells_origin = origin;
    view->cells_focus = focus.id;
    view->cells_generation = view->doc->generation;
    view->cells_anchor = view->session->anchor->revision;
    view->cells_caret = view->session->caret->revision;
    view->dragging = view->picture_drag = view->divider_drag = 0;
    view->cells_drag = 0;
    tiku_text_input_reset(&view->input);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_cells_rectangles(const tiku_richview_t *view,
    tiku_rich_rect_t *out, size_t capacity, size_t *count)
{
    tiku_rich_cells_t cells;
    const tiku_richlayout_t *layout;
    tiku_rich_grid_box_t grid;
    size_t ordinal = 0;
    unsigned columns = 0, depth = 0;
    int found = 0;
    tiku_rich_status_t status;

    if (!count) return TIKU_RICH_INVALID;
    *count = 0;
    if (!out && capacity) return TIKU_RICH_INVALID;
    status = tiku_richview_cells(view, &cells);
    if (status != TIKU_RICH_OK) return status;
    layout = tiku_richview_layout(view);
    if (!layout) return tiku_richview_status(view);
    *count = cells.rows;
    if (!out) return TIKU_RICH_OK;
    if (capacity < cells.rows) return TIKU_RICH_LIMIT;
    columns = view->doc->root->blocks->nodes[
        tiku_rd_block_find(view->doc->root, cells.table)].table.columns;
    for (size_t i = 0; i < tiku_richlayout_grids(layout); i++) {
        unsigned row, column;

        tiku_richlayout_grid(layout, i, &grid);
        if (grid.id == cells.table) { found = 1; depth = grid.depth; continue; }
        if (!found) continue;
        if (grid.depth <= depth) break;
        if (grid.kind != TIKU_RICH_BLOCK_CELL || grid.depth != depth + 1)
            continue;
        row = (unsigned)(ordinal / columns);
        column = (unsigned)(ordinal++ % columns);
        if (row < cells.row || row - cells.row >= cells.rows) continue;
        if (column == cells.column) out[row - cells.row] = grid.box;
        if (column == cells.column + cells.columns - 1)
            out[row - cells.row].width = grid.box.x + grid.box.width -
                                         out[row - cells.row].x;
    }
    return TIKU_RICH_OK;
}

/** @brief Apply a bulk edit with model and session read-only checks. */
static tiku_rich_status_t
edit_cells(tiku_richview_t *view, int clear, uint32_t color)
{
    tiku_rich_cells_t cells;
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_CELLS_SET, .background = color};
    tiku_rich_transaction_t tx;
    tiku_rich_status_t status = tiku_richview_cells(view, &cells);
    uint64_t origin, focus;

    if (status != TIKU_RICH_OK) return status;
    origin = view->cells_origin;
    focus = view->cells_focus;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    edit.cells = cells;
    tx = (tiku_rich_transaction_t){view->doc->generation, &edit, 1};
    status = clear ?
        tiku_richdoc_cells_clear(view->doc, tx.generation, &cells) :
        tiku_richdoc_apply(view->doc, &tx);
    if (status == TIKU_RICH_OK) {
        (void)tiku_richview_select_cells(view, view->doc->generation, &cells);
        view->cells_origin = origin;
        view->cells_focus = focus;
    }
    return status;
}

tiku_rich_status_t
tiku_richview_cells_shade(tiku_richview_t *view, uint32_t background)
{
    return edit_cells(view, 0, background);
}

tiku_rich_status_t
tiku_richview_cells_clear(tiku_richview_t *view)
{
    return edit_cells(view, 1, 0);
}

tiku_rich_status_t
tiku_richview_cells_paste_text(tiku_richview_t *view,
                              const char *text, size_t length)
{
    tiku_rich_cells_t cells, affected;
    tiku_rich_status_t status = tiku_richview_cells(view, &cells);

    if (status != TIKU_RICH_OK) return status;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    status = tiku_richdoc_cells_paste_text(view->doc, view->doc->generation,
        &cells, text, length, &affected);
    if (status == TIKU_RICH_OK || status == TIKU_RICH_NO_CHANGE)
        return tiku_richview_select_cells(view, view->doc->generation,
                                           &affected);
    return status;
}

tiku_rich_status_t
tiku_richview_cells_paste(tiku_richview_t *view,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source)
{
    tiku_rich_cells_t cells, affected;
    tiku_rich_status_t status = tiku_richview_cells(view, &cells);

    if (status != TIKU_RICH_OK) return status;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    status = tiku_richdoc_cells_paste(view->doc, view->doc->generation,
        &cells, snapshot, source, &affected);
    if (status == TIKU_RICH_OK)
        return tiku_richview_select_cells(view, view->doc->generation,
                                           &affected);
    return status;
}

tiku_rich_status_t
tiku_richview_cells_insert(tiku_richview_t *view,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source)
{
    tiku_rich_cells_t cells;
    tiku_rich_pos_t first, last;
    tiku_rich_status_t status;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    if (view->session->anchor->revision == UINT64_MAX ||
        view->session->caret->revision == UINT64_MAX) return TIKU_RICH_LIMIT;
    if (tiku_richview_cells(view, &cells) == TIKU_RICH_OK)
        return TIKU_RICH_INVALID_RANGE;
    status = tiku_richsession_range(view->session, &first, &last);
    if (status != TIKU_RICH_OK) return status;
    if (first.paragraph != last.paragraph || first.byte != last.byte)
        return TIKU_RICH_INVALID_RANGE;
    status = tiku_richdoc_cells_insert(view->doc, view->doc->generation,
        first, snapshot, source, &cells);
    if (status == TIKU_RICH_OK)
        return tiku_richview_select_cells(view, view->doc->generation, &cells);
    return status;
}
