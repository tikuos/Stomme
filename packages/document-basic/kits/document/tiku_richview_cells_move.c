/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_cells_move.c - directed cell selection and keyboard extension.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"

tiku_rich_status_t
tiku_rv_cells_endpoints(tiku_richview_t *view, uint64_t origin, uint64_t focus)
{
    tiku_richsnapshot_t snapshot = {.root = view->doc->root};
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;

    status = tiku_richsnapshot_cells(&snapshot, origin, focus, &cells);
    if (status != TIKU_RICH_OK) return status;
    status = tiku_richview_select_cells(view, view->doc->generation, &cells);
    if (status == TIKU_RICH_OK) {
        view->cells_origin = origin;
        view->cells_focus = focus;
    }
    return status;
}

/** @brief Reveal the moving cell, using its top if taller than the viewport. */
static void
reveal_cell(tiku_richview_t *view)
{
    tiku_rich_grid_box_t grid;
    double visible = view->height / view->zoom, y = view->scroll;

    for (size_t i = 0; i < tiku_richlayout_grids(view->layout); i++) {
        tiku_richlayout_grid(view->layout, i, &grid);
        if (grid.id != view->cells_focus) continue;
        if (grid.box.y + grid.box.height > y + visible)
            y = grid.box.y + grid.box.height - visible;
        if (grid.box.y < y) y = grid.box.y;
        (void)tiku_richview_scroll(view, y);
        break;
    }
}

/** @brief Clamp a logical motion to this table without row wrapping. */
static tiku_rich_status_t
move_cell(tiku_rich_cells_t *cell, const tiku_rich_table_t *table,
          tiku_rich_motion_t motion)
{
    switch (motion) {
    case TIKU_RICH_MOVE_LEFT: if (cell->column) cell->column--; break;
    case TIKU_RICH_MOVE_RIGHT:
        if (cell->column + 1 < table->columns) cell->column++;
        break;
    case TIKU_RICH_MOVE_UP: if (cell->row) cell->row--; break;
    case TIKU_RICH_MOVE_DOWN:
        if (cell->row + 1 < table->rows) cell->row++;
        break;
    case TIKU_RICH_MOVE_HOME: cell->column = 0; break;
    case TIKU_RICH_MOVE_END: cell->column = table->columns - 1; break;
    case TIKU_RICH_MOVE_START: cell->row = cell->column = 0; break;
    case TIKU_RICH_MOVE_FINISH:
        cell->row = table->rows - 1; cell->column = table->columns - 1;
        break;
    default: return TIKU_RICH_INVALID;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_cells_extend(tiku_richview_t *view, tiku_rich_motion_t motion)
{
    tiku_rich_cells_t cells;
    tiku_richsnapshot_t snapshot;
    tiku_rich_block_t focus;
    const tiku_rich_table_t *table;
    size_t ordinal;
    tiku_rich_status_t status;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    status = tiku_richview_cells(view, &cells);
    if (status != TIKU_RICH_OK) return status;
    if (!tiku_richview_layout(view)) return tiku_richview_status(view);
    table = &view->doc->root->blocks->nodes[
        tiku_rd_block_find(view->doc->root, cells.table)].table;
    ordinal = tiku_rd_grid_cell(view->doc->root, cells.table,
                                view->cells_focus);
    if (ordinal == SIZE_MAX) return TIKU_RICH_STALE;
    cells.row = (unsigned)(ordinal / table->columns);
    cells.column = (unsigned)(ordinal % table->columns);
    cells.rows = cells.columns = 1;
    status = move_cell(&cells, table, motion);
    if (status != TIKU_RICH_OK) return status;
    snapshot = (tiku_richsnapshot_t){.root = view->doc->root};
    status = tiku_richsnapshot_cell_at(&snapshot, &cells, 0, &focus);
    if (status != TIKU_RICH_OK) return status;
    if (focus.id == view->cells_focus) return TIKU_RICH_NO_CHANGE;
    status = tiku_rv_cells_endpoints(view, view->cells_origin, focus.id);
    if (status == TIKU_RICH_OK) reveal_cell(view);
    return status;
}
