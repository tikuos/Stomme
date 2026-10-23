/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_navigation.c - viewport transforms, selection and caret reveal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"

#include <math.h>

void
tiku_rv_clamp(tiku_richview_t *view)
{
    double max;

    if (view->status != TIKU_RICH_OK || view->dirty) return;
    max = tiku_richlayout_height(view->layout) - view->height / view->zoom;
    if (max < 0) max = 0;
    if (view->scroll > max) view->scroll = max;
}

tiku_rich_status_t
tiku_richview_viewport(tiku_richview_t *view,
    double width, double height, double zoom)
{
    if (!view || !isfinite(width) || !isfinite(height) || !isfinite(zoom) ||
        width < 1 || width > 1000000 || height < 1 || height > 1000000 ||
        zoom < 0.1 || zoom > 8 || width / zoom < 1 || width / zoom > 1000000)
        return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    if (width != view->width || zoom != view->zoom) {
        view->width = width;
        view->zoom = zoom;
        tiku_richview_reflow(view);
    }
    if (height != view->height) {
        view->divider_drag = 0;
        view->cells_drag = 0;
    }
    view->height = height;
    tiku_rv_clamp(view);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_scroll(tiku_richview_t *view, double y)
{
    if (!view || !isfinite(y)) return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    if (view->scroll != y) view->divider_drag = 0;
    view->scroll = y < 0 ? 0 : y;
    tiku_rv_clamp(view);
    return TIKU_RICH_OK;
}

double
tiku_richview_scroll_y(const tiku_richview_t *view)
{
    return view ? view->scroll : 0;
}

double
tiku_richview_zoom(const tiku_richview_t *view)
{
    return view ? view->zoom : 1;
}

tiku_rich_status_t
tiku_richview_point(tiku_richview_t *view, double x, double y, int extend)
{
    tiku_rich_pos_t anchor, caret;
    tiku_rich_status_t status;

    if (!view || !isfinite(x) || !isfinite(y)) return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    if (view->status != TIKU_RICH_OK) return view->status;
    tiku_richsession_selection(view->session, &anchor, &caret);
    status = tiku_richlayout_hit(view->layout, x / view->zoom,
                                y / view->zoom + view->scroll, &caret);
    if (status != TIKU_RICH_OK) return status;
    if (!extend) anchor = caret;
    status = tiku_richsession_select(view->session,
        tiku_richlayout_generation(view->layout), anchor, caret);
    if (status == TIKU_RICH_OK) {
        view->preferred = NAN;
        view->cells.table = 0;
    }
    return status;
}

tiku_rich_status_t
tiku_richview_reveal(tiku_richview_t *view)
{
    tiku_rich_pos_t anchor, caret;
    tiku_rich_rect_t rect;
    tiku_rich_status_t status;
    double bottom;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    if (view->status != TIKU_RICH_OK) return view->status;
    tiku_richsession_selection(view->session, &anchor, &caret);
    status = tiku_richlayout_caret(view->layout, caret, &rect);
    if (status != TIKU_RICH_OK) return status;
    bottom = rect.y + rect.height - view->height / view->zoom;
    if (bottom > view->scroll) view->scroll = bottom;
    if (rect.y < view->scroll) view->scroll = rect.y;
    tiku_rv_clamp(view);
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_move(tiku_richview_t *view, tiku_rich_motion_t motion,
                   int extend)
{
    tiku_rich_pos_t anchor, caret, moved;
    tiku_rich_rect_t rect;
    tiku_rich_status_t status;
    int vertical = motion == TIKU_RICH_MOVE_UP || motion == TIKU_RICH_MOVE_DOWN;
    double preferred;
    tiku_richcmd_context_t context;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    if (view->status != TIKU_RICH_OK) return view->status;
    tiku_richsession_selection(view->session, &anchor, &caret);
    preferred = vertical && view->preferred_revision ==
        view->session->caret->revision ? view->preferred : NAN;
    if (vertical && isnan(preferred)) {
        status = tiku_richlayout_caret(view->layout, caret, &rect);
        if (status != TIKU_RICH_OK) return status;
        preferred = rect.x;
    }
    if (!extend && (motion == TIKU_RICH_MOVE_LEFT ||
                    motion == TIKU_RICH_MOVE_RIGHT) &&
        (anchor.paragraph != caret.paragraph || anchor.byte != caret.byte)) {
        context = tiku_rs_context(view->session);
        moved = motion == TIKU_RICH_MOVE_LEFT ? context.first : context.last;
        status = tiku_richlayout_caret(view->layout, moved, &rect);
    } else
        status = tiku_richlayout_move(view->layout, caret, motion, preferred,
                                      &moved);
    if (status == TIKU_RICH_NO_CHANGE)
        tiku_richsession_break_undo(view->session);
    if (status != TIKU_RICH_OK) return status;
    if (!extend) anchor = moved;
    status = tiku_richsession_select(view->session,
        tiku_richlayout_generation(view->layout), anchor, moved);
    if (status == TIKU_RICH_OK) {
        view->cells.table = 0;
        view->preferred = preferred;
        view->preferred_revision = view->session->caret->revision;
        tiku_richview_reveal(view);
    }
    return status;
}
