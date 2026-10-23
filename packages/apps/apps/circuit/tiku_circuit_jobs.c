/* tiku_circuit_jobs.c -- the canvas's renders: one on a worker at a time,
 * its image kept only when the view it was asked for is still shown, and
 * no larger than the kit renders at once.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CV_RENDER_STEP 1.001    /* how much a render too large shrinks */

/** @brief One render: a snapshot of the scene and view, and its result. */
typedef struct {
    tiku_circuitview_t *owner;      /* the window; one reference */
    tiku_circuit_t *document;       /* the scene drawn; one reference */
    uint64_t generation;            /* the window's view when it was asked */
    tiku_circuit_view_t view;       /* the view rendered: the canvas's, or
                                       the same scaled down */
    tiku_image_t image;             /* the pixels, until the window has them */
    tiku_circuit_err_t result;      /* TIKU_CIRCUIT_OK or why it failed */
} circuit_job_t;

/** @brief How many times smaller than a @p w by @p h canvas its image is
 *         rendered: 1, or the least factor the kit renders it at. */
static double render_factor(int w, int h)
{
    double k = sqrt((double)w * h / TIKU_CIRCUIT_RENDER_PIXELS);

    if (k < (double)w / TIKU_CIRCUIT_RENDER_SIDE) {
        k = (double)w / TIKU_CIRCUIT_RENDER_SIDE;
    }
    if (k < (double)h / TIKU_CIRCUIT_RENDER_SIDE) {
        k = (double)h / TIKU_CIRCUIT_RENDER_SIDE;
    }
    if (k <= 1) {
        return 1;
    }
    while (ceil(w / k) > TIKU_CIRCUIT_RENDER_SIDE ||
           ceil(h / k) > TIKU_CIRCUIT_RENDER_SIDE ||
           ceil(w / k) * ceil(h / k) > TIKU_CIRCUIT_RENDER_PIXELS) {
        k *= CV_RENDER_STEP;
    }
    return k;
}

int tiku_circuitview_fit(const tiku_circuit_t *doc, int w, int h,
                         tiku_circuit_view_t *view)
{
    double k = render_factor(w, h);
    tiku_circuit_view_t small;

    if (k == 1) {
        return tiku_circuit_fit(doc, w, h, view);
    }
    if (!tiku_circuit_fit(doc, (int)ceil(w / k), (int)ceil(h / k), &small)) {
        return 0;
    }
    *view = (tiku_circuit_view_t){w, h, small.scale * k, small.x * k,
                                  small.y * k};
    return 1;
}

tiku_circuit_view_t tiku_circuitview_render_view(
    const tiku_circuit_view_t *view)
{
    double k = render_factor(view->width, view->height);

    if (k == 1) {
        return *view;
    }
    return (tiku_circuit_view_t){(int)ceil(view->width / k),
                                 (int)ceil(view->height / k),
                                 view->scale / k, view->x / k, view->y / k};
}

/** @brief Whether this job was cancelled; asked by the kit's renderer. */
static int cancelled(void *ctx)
{
    return tiku_work_cancelled(ctx);
}

/** @brief On the worker: render the snapshot. */
static void run(void *ctx)
{
    circuit_job_t *j = ctx;

    j->result = tiku_circuit_render(j->document, &j->view, cancelled, j,
                                    &j->image);
}

/** @brief On the loop: keep the image when the view has not moved since,
 *         or say why the render failed. */
static void done(void *ctx)
{
    circuit_job_t *j = ctx;
    tiku_circuitview_t *v = j->owner;

    if (!v->window || v->generation != j->generation) {
        return;
    }
    if (!j->result) {
        tiku_image_free(&v->image);
        v->image = j->image;
        v->cached_view = j->view;
        v->rendered_generation = j->generation;
        memset(&j->image, 0, sizeof j->image);
    } else {
        tiku_circuitview_note(v, tiku_circuit_error(j->result));
    }
    if (v->dirty) {
        *v->dirty = 1;
    }
}

/** @brief After done, or in place of it when cancelled: let the snapshot
 *         go, free the window for the next render, drop the reference. */
static void dispose(void *ctx)
{
    circuit_job_t *j = ctx;
    tiku_circuitview_t *v = j->owner;

    v->busy = 0;
    tiku_image_free(&j->image);
    tiku_circuit_release(j->document);
    if (v->window) {
        tiku_circuitview_menus(v);
    }
    if (v->dirty) {
        *v->dirty = 1;
    }
    tiku_circuitview_unref(v);
    free(j);
}

void tiku_circuitview_tick(tiku_circuitview_t *v)
{
    if (v && v->window) {
        tiku_circuitview_files_tick(v);
    }
    if (v && v->window) {
        tiku_circuitview_sheets_tick(v);
    }
    if (v && v->window) {
        tiku_circuitview_details_tick(v);
    }
    if (!v || !v->window || v->busy || !v->document || !v->needs_render) {
        return;
    }
    tiku_circuitview_layout(v, tiku_window_content(v->window));
    if (v->view.width < 1 || v->view.height < 1) {
        return;
    }
    circuit_job_t *j = calloc(1, sizeof *j);
    if (!j) {
        v->needs_render = 0;
        tiku_circuitview_note(v,
            "Not enough memory to render the drawing.");
        return;
    }
    j->owner = v;
    j->document = tiku_circuit_retain(v->document);
    j->generation = v->generation;
    j->view = tiku_circuitview_render_view(&v->view);
    tiku_work_job_t work = {.run = run, .done = done, .free = dispose,
                            .ctx = j};
    memcpy(work.key, v->key, sizeof work.key);
    v->refs++;
    v->busy = 1;
    if (tiku_work_post(&work) < 0) {
        dispose(j);
        return;
    }
    v->needs_render = 0;
    tiku_circuitview_menus(v);
}
