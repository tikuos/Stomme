/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_cells_scroll.c - bounded host-driven cell drag scrolling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <math.h>

/** @brief Clamp scrolling to the selected table's vertical extent. */
static double
table_scroll(tiku_richview_t *view, double y)
{
    tiku_rich_grid_box_t grid;
    double visible = view->height / view->zoom;

    for (size_t i = 0; i < tiku_richlayout_grids(view->layout); i++) {
        tiku_richlayout_grid(view->layout, i, &grid);
        if (grid.id == view->cells.table) {
            double low = fmin(view->scroll, grid.box.y);
            double high = fmax(view->scroll,
                grid.box.y + grid.box.height - visible);

            return fmax(0, fmax(low, fmin(y, high)));
        }
    }
    return view->scroll;
}

tiku_rich_status_t
tiku_richview_tick(tiku_richview_t *view, int64_t now_us)
{
    tiku_rich_cells_t cells;
    tiku_rich_status_t status;
    int64_t elapsed;
    double outside, direction, previous;

    if (!view || now_us < 0) return TIKU_RICH_INVALID;
    if (view->doc->notifying || view->doc->initiator) return TIKU_RICH_BUSY;
    if (!view->cells_drag) return TIKU_RICH_NO_CHANGE;
    if (tiku_richview_cells(view, &cells) != TIKU_RICH_OK) {
        view->cells_drag = 0;
        return TIKU_RICH_STALE;
    }
    if (!tiku_richview_layout(view)) return tiku_richview_status(view);
    elapsed = view->cells_tick < 0 || now_us < view->cells_tick ? 0 :
              now_us - view->cells_tick;
    view->cells_tick = now_us;
    if (!elapsed) return TIKU_RICH_NO_CHANGE;
    if (elapsed > 100000) elapsed = 100000;
    if (view->cells_y < 0) {
        outside = -(double)view->cells_y; direction = -1;
    } else if (view->cells_y >= view->height) {
        outside = view->cells_y - view->height; direction = 1;
    } else return TIKU_RICH_NO_CHANGE;
    previous = view->scroll;
    (void)tiku_richview_scroll(view, table_scroll(view, previous + direction *
        (120 + 6 * fmin(160, outside)) * elapsed / 1000000.0 / view->zoom));
    status = tiku_rv_cells_point(view, view->cells_x, view->cells_y, 1);
    if (status != TIKU_RICH_OK && status != TIKU_RICH_NO_CHANGE) {
        view->scroll = previous;
        return status;
    }
    return previous != view->scroll ? TIKU_RICH_OK : status;
}
