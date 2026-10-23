/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richview.c - observed per-view editing and cancellable layout jobs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richview_private.h"

#include <math.h>

/** @brief Mark geometry stale after content publication without allocating. */
static void
changed(void *context, tiku_richdoc_t *doc, const tiku_rich_change_t *change)
{
    tiku_richview_t *view = context;

    (void)doc;
    if (change->kind <= TIKU_RICH_CHANGE_IMPORT) {
        view->dirty = 1;
        view->status = TIKU_RICH_PENDING;
        view->preferred = NAN;
        view->picture_drag = 0;
        view->divider_drag = 0;
        view->cells.table = 0;
        view->cells_drag = 0;
    }
}

tiku_rich_status_t
tiku_richview_create(tiku_richdoc_t *doc,
    const tiku_richlayout_options_t *options, tiku_richview_t **out)
{
    tiku_richview_t *view;
    tiku_richsnapshot_t *snapshot;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!doc || !options) return TIKU_RICH_INVALID;
    if (doc->notifying) return TIKU_RICH_BUSY;
    view = tiku_rd_alloc(doc->memory, sizeof *view, &status);
    if (!view) return status;
    view->doc = doc;
    status = tiku_richsession_create(doc, &view->session);
    if (status == TIKU_RICH_OK)
        status = tiku_richwatch_create(doc, changed, view, &view->watch);
    if (status == TIKU_RICH_OK) {
        status = tiku_richdoc_snapshot(doc, &snapshot);
        if (status == TIKU_RICH_OK) {
            status = tiku_richlayout_create(snapshot, options, &view->layout);
            tiku_richsnapshot_release(snapshot);
        }
    }
    if (status != TIKU_RICH_OK) {
        tiku_richwatch_destroy(view->watch);
        tiku_richsession_destroy(view->session);
        tiku_rd_free(view);
        return status;
    }
    view->options = *options;
    if (options->provider.retain)
        options->provider.retain(options->provider.context);
    view->width = options->width;
    view->height = 400;
    view->zoom = 1;
    view->preferred = NAN;
    view->status = TIKU_RICH_PENDING;
    *out = view;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_destroy(tiku_richview_t *view)
{
    if (!view) return TIKU_RICH_OK;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    tiku_richwatch_destroy(view->watch);
    tiku_richsession_destroy(view->session);
    tiku_richlayout_release(view->layout);
    if (view->options.provider.release)
        view->options.provider.release(view->options.provider.context);
    tiku_rd_free(view);
    return TIKU_RICH_OK;
}

tiku_richsession_t *
tiku_richview_session(tiku_richview_t *view)
{
    return view ? view->session : NULL;
}

tiku_rich_status_t
tiku_richview_reflow(tiku_richview_t *view)
{
    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    view->picture_drag = 0;
    view->divider_drag = 0;
    view->cells_drag = 0;
    view->dirty = 1;
    view->status = TIKU_RICH_PENDING;
    view->preferred = NAN;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richview_step(tiku_richview_t *view, size_t work)
{
    tiku_richsnapshot_t *snapshot;
    tiku_richlayout_t *previous;

    if (!view) return TIKU_RICH_INVALID;
    if (view->doc->notifying) return TIKU_RICH_BUSY;
    if (!work) return view->status;
    if (view->dirty) {
        previous = view->layout;
        if (tiku_richlayout_status(previous) != TIKU_RICH_OK) {
            tiku_richlayout_release(previous);
            previous = NULL;
        }
        view->layout = NULL;
        view->cached_attempt = previous != NULL;
        view->dirty = 0;
        view->options.width = view->width / view->zoom;
        view->status = tiku_richdoc_snapshot(view->doc, &snapshot);
        if (view->status == TIKU_RICH_OK) {
            view->status = tiku_richlayout_create_reusing(snapshot,
                &view->options, previous, &view->layout);
            tiku_richsnapshot_release(snapshot);
        }
        tiku_richlayout_release(previous);
        if (view->status == TIKU_RICH_OK) view->status = TIKU_RICH_PENDING;
        --work;
    }
    if (view->status == TIKU_RICH_PENDING)
        view->status = tiku_richlayout_step(view->layout, work);
    if (view->cached_attempt && (view->status == TIKU_RICH_LIMIT ||
                                view->status == TIKU_RICH_NO_MEMORY)) {
        tiku_richlayout_release(view->layout);
        view->layout = NULL;
        view->cached_attempt = 0;
        view->dirty = 1;
        view->status = TIKU_RICH_PENDING;
    }
    if (view->status == TIKU_RICH_OK) tiku_rv_clamp(view);
    return view->status;
}

const tiku_richlayout_t *
tiku_richview_layout(const tiku_richview_t *view)
{
    return view && !view->dirty && view->status == TIKU_RICH_OK &&
        tiku_richlayout_generation(view->layout) == view->doc->generation ?
        view->layout : NULL;
}

tiku_rich_status_t
tiku_richview_status(const tiku_richview_t *view)
{
    if (!view) return TIKU_RICH_INVALID;
    if (view->layout && tiku_richlayout_generation(view->layout) !=
        view->doc->generation) return TIKU_RICH_PENDING;
    return view->status;
}
