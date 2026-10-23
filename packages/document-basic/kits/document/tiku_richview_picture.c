/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview_picture.c - picture selection and one-command corner resizing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"
#include <math.h>
#include <string.h>

/** @brief Borrow the selected image from current completed view geometry. */
static tiku_rich_status_t
selected(const tiku_richview_t *view, tiku_rich_cluster_t *cluster)
{
    const tiku_richlayout_t *layout = tiku_richview_layout(view);
    tiku_rich_pos_t first, last;

    if (!layout) return tiku_richview_status(view);
    tiku_richsession_range(view->session, &first, &last);
    return tiku_richlayout_image_selection(layout, first, last, cluster);
}

tiku_rich_status_t
tiku_richview_picture(const tiku_richview_t *view, tiku_rich_rect_t *out)
{
    tiku_rich_cluster_t cluster;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!view) return TIKU_RICH_INVALID;
    status = selected(view, &cluster);
    if (status != TIKU_RICH_OK) return status;
    *out = (tiku_rich_rect_t){cluster.box.x,
        cluster.baseline - cluster.image->height,
        cluster.image->width, cluster.image->height};
    if (view->picture_drag && view->picture_generation == view->doc->generation)
    {
        *out = view->picture_box;
        out->width *= view->picture_scale;
        out->height *= view->picture_scale;
    }
    return TIKU_RICH_OK;
}

/** @brief Update the resize preview and publish once on primary-button up. */
static tiku_rich_status_t
drag(tiku_richview_t *view, const tiku_event_t *e, double x, double y)
{
    tiku_rich_cluster_t cluster;
    tiku_rich_image_t image;
    tiku_richcmd_t command = {.kind = TIKU_RICHCMD_IMAGE_SET};
    tiku_rich_rect_t box = view->picture_box;
    tiku_rich_status_t status;

    if (view->picture_generation != view->doc->generation ||
        view->picture_anchor != view->session->anchor->revision ||
        view->picture_caret != view->session->caret->revision) {
        view->picture_drag = 0;
        return TIKU_RICH_STALE;
    }
    view->picture_scale = fmax((x - box.x) / box.width,
                                (y - box.y) / box.height);
    view->picture_scale = fmax(1 / fmin(box.width, box.height),
        fmin(14400 / fmax(box.width, box.height), view->picture_scale));
    if (e->type != TIKU_EVENT_POINTER_UP) return TIKU_RICH_OK;
    status = selected(view, &cluster);
    view->picture_drag = 0;
    if (status != TIKU_RICH_OK) return status;
    image = *cluster.image;
    image.width = (float)(box.width * view->picture_scale);
    image.height = (float)(box.height * view->picture_scale);
    if (image.width == cluster.image->width &&
        image.height == cluster.image->height) return TIKU_RICH_NO_CHANGE;
    command.value.image = &image;
    return tiku_richsession_execute(view->session,
                                     view->picture_generation, &command);
}

tiku_rich_status_t
tiku_rv_picture_event(tiku_richview_t *view, const tiku_event_t *e,
                      int *handled)
{
    tiku_rich_cluster_t cluster;
    tiku_rich_pos_t first, last;
    tiku_rich_rect_t box;
    tiku_rich_status_t status;
    double x = e->x / view->zoom, y = e->y / view->zoom + view->scroll;

    *handled = 0;
    if (view->picture_drag) {
        if (e->type == TIKU_EVENT_DEACTIVATED ||
            e->type == TIKU_EVENT_KEY_DOWN) view->picture_drag = 0;
        if (e->type == TIKU_EVENT_POINTER_MOVE ||
            (e->type == TIKU_EVENT_POINTER_UP && e->button == 1)) {
            *handled = 1;
            return drag(view, e, x, y);
        }
    }
    if (e->type != TIKU_EVENT_POINTER_DOWN || e->button != 1 ||
        (e->modifiers & TIKU_MOD_SHIFT)) return TIKU_RICH_NO_CHANGE;
    if (!tiku_richsession_read_only(view->session) &&
        tiku_richview_picture(view, &box) == TIKU_RICH_OK &&
        fabs(x - box.x - box.width) <= 5 / view->zoom &&
        fabs(y - box.y - box.height) <= 5 / view->zoom) {
        view->picture_box = box;
        view->picture_scale = 1;
        view->picture_generation = view->doc->generation;
        view->picture_anchor = view->session->anchor->revision;
        view->picture_caret = view->session->caret->revision;
        view->picture_drag = 1;
        view->dragging = 0;
        *handled = 1;
        return TIKU_RICH_OK;
    }
    status = tiku_richlayout_image_at(tiku_richview_layout(view), x, y,
                                      &cluster);
    if (status != TIKU_RICH_OK) return TIKU_RICH_NO_CHANGE;
    first = (tiku_rich_pos_t){view->doc->root->body, cluster.paragraph,
        cluster.first, TIKU_RICH_AFTER};
    last = first;
    last.byte = cluster.last;
    last.affinity = TIKU_RICH_BEFORE;
    view->dragging = 0;
    *handled = 1;
    return tiku_richsession_select(view->session, 0, first, last);
}
