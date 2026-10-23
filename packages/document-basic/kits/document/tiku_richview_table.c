/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_table.c - captured column-divider previews and atomic resizing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <math.h>
#include <string.h>

tiku_rich_status_t
tiku_richview_divider_at(const tiku_richview_t *view,
    double x, double y, tiku_rich_divider_t *out)
{
    const tiku_richlayout_t *layout;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!view || !isfinite(x) || !isfinite(y)) return TIKU_RICH_INVALID;
    if (tiku_richsession_read_only(view->session)) return TIKU_RICH_READ_ONLY;
    layout = tiku_richview_layout(view);
    if (!layout) return tiku_richview_status(view);
    if (x < 0 || y < 0 || x >= view->width || y >= view->height)
        return TIKU_RICH_NOT_FOUND;
    return tiku_richlayout_divider(layout, x / view->zoom,
        y / view->zoom + view->scroll, fmin(32, 4 / view->zoom), out);
}

tiku_rich_status_t
tiku_richview_divider(const tiku_richview_t *view, tiku_rich_divider_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!view) return TIKU_RICH_INVALID;
    if (!view->divider_drag || !tiku_richview_layout(view) ||
        tiku_richsession_read_only(view->session) ||
        view->divider_generation != view->doc->generation ||
        view->divider_anchor != view->session->anchor->revision ||
        view->divider_caret != view->session->caret->revision)
        return TIKU_RICH_NOT_FOUND;
    *out = view->divider;
    out->line.x = view->divider_x;
    return TIKU_RICH_OK;
}

/** @brief Capture geometry and units without changing text selection. */
static void
begin(tiku_richview_t *view, const tiku_rich_divider_t *hit, double x)
{
    size_t at = tiku_rd_block_find(view->doc->root, hit->table);
    double total = 0;
    tiku_rich_grid_box_t grid;

    view->divider_table = view->doc->root->blocks->nodes[at].table;
    for (unsigned i = 0; i < view->divider_table.columns; i++)
        total += view->divider_table.weights[0] ?
                 view->divider_table.weights[i] : 1;
    for (size_t i = 0; i < tiku_richlayout_grids(view->layout); i++) {
        tiku_richlayout_grid(view->layout, i, &grid);
        if (grid.id == hit->table) {
            view->divider_unit = grid.box.width / total;
            break;
        }
    }
    view->divider = *hit;
    view->divider_x = hit->line.x;
    view->divider_pointer = x;
    view->divider_generation = view->doc->generation;
    view->divider_anchor = view->session->anchor->revision;
    view->divider_caret = view->session->caret->revision;
    view->divider_drag = 1;
    view->cells.table = 0;
    view->dragging = view->picture_drag = 0;
}

/** @brief Publish one width change, retaining the table's other columns. */
static tiku_rich_status_t
commit(tiku_richview_t *view)
{
    tiku_rich_edit_t edit = {.kind = TIKU_RICH_TABLE_SET,
        .object = view->divider.table, .table = view->divider_table};
    tiku_rich_transaction_t tx = {view->divider_generation, &edit, 1};
    unsigned column = view->divider.column;
    double delta = view->divider_x - view->divider.line.x;
    double left, right;

    view->divider_drag = 0;
    if (fabs(delta) < 0.01) return TIKU_RICH_NO_CHANGE;
    if (!edit.table.weights[0])
        for (unsigned i = 0; i < edit.table.columns; i++)
            edit.table.weights[i] = 1;
    left = edit.table.weights[column] + delta / view->divider_unit;
    right = edit.table.weights[column + 1] - delta / view->divider_unit;
    edit.table.weights[column] = (float)fmax(0.001, fmin(1000, left));
    edit.table.weights[column + 1] = (float)fmax(0.001, fmin(1000, right));
    return tiku_richdoc_apply(view->doc, &tx);
}

tiku_rich_status_t
tiku_rv_table_event(tiku_richview_t *view, const tiku_event_t *e, int *handled)
{
    tiku_rich_divider_t hit;
    tiku_rich_status_t status;
    double x = e->x / view->zoom;

    *handled = 0;
    if (view->divider_drag) {
        if (e->type == TIKU_EVENT_KEY_DOWN ||
            e->type == TIKU_EVENT_DEACTIVATED ||
            e->type == TIKU_EVENT_TEXT_INPUT || e->type == TIKU_EVENT_WHEEL) {
            view->divider_drag = 0;
            if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_ESCAPE) {
                *handled = 1;
                return TIKU_RICH_OK;
            }
        } else if (e->type == TIKU_EVENT_POINTER_MOVE ||
            (e->type == TIKU_EVENT_POINTER_UP && e->button == 1)) {
            *handled = 1;
            if (tiku_richview_divider(view, &hit) != TIKU_RICH_OK) {
                view->divider_drag = 0;
                return TIKU_RICH_STALE;
            }
            view->divider_x = fmax(view->divider.minimum,
                fmin(view->divider.maximum,
                    view->divider.line.x + x - view->divider_pointer));
            return e->type == TIKU_EVENT_POINTER_UP ? commit(view) :
                                                     TIKU_RICH_OK;
        }
    }
    if (e->type != TIKU_EVENT_POINTER_DOWN || e->button != 1 ||
        e->modifiers || view->picture_drag) return TIKU_RICH_NO_CHANGE;
    status = tiku_richview_divider_at(view, e->x, e->y, &hit);
    if (status != TIKU_RICH_OK) return TIKU_RICH_NO_CHANGE;
    begin(view, &hit, x);
    *handled = 1;
    return TIKU_RICH_OK;
}
