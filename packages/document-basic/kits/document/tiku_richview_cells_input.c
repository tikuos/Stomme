/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_cells_input.c - rectangular pointer selection and input guards.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <float.h>
#include <math.h>

/** @brief Hit the deepest cell, or a direct cell in the specified table. */
static uint64_t
hit(tiku_richview_t *view, int px, int py, uint64_t table)
{
    const tiku_richlayout_t *layout = tiku_richview_layout(view);
    tiku_rich_grid_box_t grid;
    uint64_t id = 0;
    double x = px / view->zoom, y = py;
    double best = DBL_MAX;
    unsigned depth = 0;
    int found = 0;

    if (!layout) return 0;
    if (table) y = fmax(0, fmin(view->height - 0.5, y));
    y = y / view->zoom + view->scroll;
    for (size_t i = 0; i < tiku_richlayout_grids(layout); i++) {
        double dx, dy, distance;

        tiku_richlayout_grid(layout, i, &grid);
        if (table) {
            if (grid.id == table) {
                found = 1; depth = grid.depth; continue;
            }
            if (!found) continue;
            if (grid.depth <= depth) break;
            if (grid.depth != depth + 1) continue;
        }
        if (grid.kind != TIKU_RICH_BLOCK_CELL) continue;
        dx = fmax(grid.box.x - x, fmax(0, x - grid.box.x - grid.box.width));
        dy = fmax(grid.box.y - y, fmax(0, y - grid.box.y - grid.box.height));
        distance = dx + dy;
        if (table && distance >= best) continue;
        if (!table && (distance || x == grid.box.x + grid.box.width ||
                      y == grid.box.y + grid.box.height)) continue;
        best = distance;
        id = grid.id;
    }
    return id;
}

tiku_rich_status_t
tiku_rv_cells_point(tiku_richview_t *view, int x, int y, int extend)
{
    uint64_t id = hit(view, x, y, extend ? view->cells.table : 0);
    uint64_t origin = extend ? view->cells_origin : id;
    tiku_rich_status_t status;

    if (!tiku_richview_layout(view)) return tiku_richview_status(view);
    if (!id) return TIKU_RICH_NO_CHANGE;
    if (extend && id == view->cells_focus) return TIKU_RICH_NO_CHANGE;
    status = tiku_rv_cells_endpoints(view, origin, id);
    if (status == TIKU_RICH_OK) {
        view->cells_origin = origin;
        view->cells_drag = 1;
    }
    return status;
}

/** @brief Map shifted navigation keys to cell motions. */
static int
cell_motion(const tiku_event_t *e, tiku_rich_motion_t *motion)
{
    if (!(e->modifiers & TIKU_MOD_SHIFT) ||
        (e->modifiers & TIKU_MOD_OPTION)) return 0;
    if (e->modifiers & TIKU_MOD_CMD) {
        if (e->key == TIKU_KEY_HOME) *motion = TIKU_RICH_MOVE_START;
        else if (e->key == TIKU_KEY_END) *motion = TIKU_RICH_MOVE_FINISH;
        else return 0;
        return 1;
    }
    switch (e->key) {
    case TIKU_KEY_LEFT: *motion = TIKU_RICH_MOVE_LEFT; break;
    case TIKU_KEY_RIGHT: *motion = TIKU_RICH_MOVE_RIGHT; break;
    case TIKU_KEY_UP: *motion = TIKU_RICH_MOVE_UP; break;
    case TIKU_KEY_DOWN: *motion = TIKU_RICH_MOVE_DOWN; break;
    case TIKU_KEY_HOME: *motion = TIKU_RICH_MOVE_HOME; break;
    case TIKU_KEY_END: *motion = TIKU_RICH_MOVE_END; break;
    default: return 0;
    }
    return 1;
}

tiku_rich_status_t
tiku_rv_cells_event(tiku_richview_t *view, const tiku_event_t *e, int *handled)
{
    tiku_rich_cells_t cells;
    tiku_rich_motion_t motion;
    int active = tiku_richview_cells(view, &cells) == TIKU_RICH_OK;

    *handled = 0;
    if (e->type == TIKU_EVENT_DEACTIVATED || e->type == TIKU_EVENT_WHEEL)
        view->cells_drag = 0;
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 &&
        ((e->modifiers & TIKU_MOD_OPTION) ||
         (active && (e->modifiers & TIKU_MOD_SHIFT)))) {
        *handled = 1;
        view->cells_x = e->x;
        view->cells_y = e->y;
        view->cells_tick = -1;
        view->cells_drag = 0;
        tiku_rich_status_t status = tiku_rv_cells_point(view, e->x, e->y,
            active && (e->modifiers & TIKU_MOD_SHIFT));

        if (status == TIKU_RICH_NO_CHANGE && active &&
            (e->modifiers & TIKU_MOD_SHIFT)) view->cells_drag = 1;
        return status;
    }
    if (view->cells_drag &&
        (e->type == TIKU_EVENT_POINTER_MOVE ||
         (e->type == TIKU_EVENT_POINTER_UP && e->button == 1))) {
        tiku_rich_status_t status = active ?
            tiku_rv_cells_point(view, e->x, e->y, 1) :
                                            TIKU_RICH_STALE;

        *handled = 1;
        view->cells_x = e->x;
        view->cells_y = e->y;
        if (!active || e->type == TIKU_EVENT_POINTER_UP) view->cells_drag = 0;
        return status;
    }
    if (!active) return TIKU_RICH_NO_CHANGE;
    if (e->type == TIKU_EVENT_TEXT_INPUT) {
        tiku_text_input_reset(&view->input);
        *handled = 1;
        return TIKU_RICH_UNSUPPORTED;
    }
    if (e->type != TIKU_EVENT_KEY_DOWN) return TIKU_RICH_NO_CHANGE;
    view->cells_drag = 0;
    if (cell_motion(e, &motion)) {
        *handled = 1;
        return tiku_richview_cells_extend(view, motion);
    }
    if (e->key == TIKU_KEY_ESCAPE) {
        *handled = 1;
        return tiku_richview_select_cells(view, 0, NULL);
    }
    if (e->key == TIKU_KEY_DELETE || e->key == TIKU_KEY_BACKSPACE) {
        *handled = 1;
        return tiku_richview_cells_clear(view);
    }
    if (e->modifiers & TIKU_MOD_CMD) {
        if (e->key != 'b' && e->key != 'B' && e->key != 'i' && e->key != 'I' &&
            e->key != 'u' && e->key != 'U') return TIKU_RICH_NO_CHANGE;
    } else if (e->key == TIKU_KEY_LEFT || e->key == TIKU_KEY_RIGHT ||
               e->key == TIKU_KEY_UP || e->key == TIKU_KEY_DOWN ||
               e->key == TIKU_KEY_HOME || e->key == TIKU_KEY_END ||
               e->key == TIKU_KEY_TAB) {
        tiku_richview_select_cells(view, 0, NULL);
        return TIKU_RICH_NO_CHANGE;
    }
    if ((e->modifiers & TIKU_MOD_CMD) || e->key == TIKU_KEY_RETURN ||
        (unsigned char)e->text[0] >= 32) {
        *handled = 1;
        return TIKU_RICH_UNSUPPORTED;
    }
    return TIKU_RICH_NO_CHANGE;
}
