/* tiku_circuit_window.c -- the window's lifetime, its status line and
 * the reference count its jobs share with it.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_app_product.h"
#include <stdio.h>
#include <stdlib.h>

#define CV_RGB      0xffffffu   /* a layer colour without its alpha */
#define CV_WINDOW_X 85          /* where the window opens on the desk */
#define CV_WINDOW_Y 65
#define CV_WINDOW_W 800         /* the size it opens at */
#define CV_WINDOW_H 550

void tiku_circuitview_note(tiku_circuitview_t *v, const char *s)
{
    if (!v) {
        return;
    }
    snprintf(v->status, sizeof v->status, "%s", s ? s : "");
    if (v->dirty) {
        *v->dirty = 1;
    }
}

void tiku_circuitview_changed(tiku_circuitview_t *v)
{
    v->generation++;
    v->needs_render = !!v->document;
    tiku_work_cancel(v->key);
    if (v->dirty) {
        *v->dirty = 1;
    }
}

void tiku_circuitview_unref(tiku_circuitview_t *v)
{
    if (--v->refs) {
        return;
    }
    tiku_circuit_project_free(v->project);
    tiku_circuit_release(v->document);
    tiku_image_free(&v->image);
    free(v);
}

void tiku_circuitview_picker(tiku_circuitview_t *v,
                             void (*choose)(void *), void *ctx)
{
    if (v) {
        v->choose = choose;
        v->choose_ctx = ctx;
    }
}

void tiku_circuitview_ready(tiku_circuitview_t *v)
{
    const tiku_circuit_layer_t *layer =
        tiku_circuit_layer(v->document, v->sheet);

    if (layer) {
        char message[CV_STATUS_MAX];

        if (!layer->scene) {
            snprintf(message, sizeof message,
                     "Unavailable layer %.120s: %.250s", layer->path,
                     layer->diagnostic.message);
        } else {
            snprintf(message, sizeof message,
                     "Read-only drawing. Layer %zu/%zu: %.140s; %s, "
                     "opacity %u%%, colour #%06X. Front layer is first.",
                     v->sheet + 1, tiku_circuit_layer_count(v->document),
                     layer->label, layer->visible ? "visible" : "hidden",
                     (layer->opacity * 100 + CV_OPAQUE / 2) /
                         CV_OPAQUE,
                     layer->colour & CV_RGB);
        }
        tiku_circuitview_note(v, message);
        return;
    }
    if (v->project) {
        const tiku_circuit_sheet_t *s =
            tiku_circuit_project_sheet(v->project, v->sheet);
        char message[CV_STATUS_MAX];

        if (!v->document) {
            if (s->diagnostic->line) {
                snprintf(message, sizeof message,
                         "Unavailable: %.140s, line %u:%u. %.250s", s->path,
                         s->diagnostic->line, s->diagnostic->column,
                         s->diagnostic->message);
            } else {
                snprintf(message, sizeof message,
                         "Unavailable: %.170s. %.250s", s->path,
                         s->diagnostic->message);
            }
        } else {
            snprintf(message, sizeof message,
                     "Sheet %zu of %zu: %.160s%s. Drag to pan; Find "
                     "searches all loaded sheets.",
                     v->sheet + 1, tiku_circuit_project_count(v->project),
                     s->name,
                     tiku_circuit_project_incomplete(v->project) ?
                         " (incomplete project)" : "");
        }
        tiku_circuitview_note(v, message);
        return;
    }
    tiku_circuitview_note(v, v->path[0] ?
        "Read-only drawing. Click an object to inspect it; drag to pan." :
        v->document ?
        "Authored example. Click an object to inspect it; drag to pan." :
        "Open a KiCad schematic, EAGLE XML schematic, Gerber layer/job or "
        "decimal-coordinate drill file.");
}

/** @brief The window closed: the document, the project and the import
 *         asked for go with it; the controller stays until destroy. */
static void gone(tiku_window_t *window, void *ctx)
{
    tiku_circuitview_t *v = ctx;

    (void)window;
    v->window = NULL;
    tiku_work_cancel(v->export_key);
    cv_measure_reset(v);
    v->dragging = 0;
    tiku_circuitview_files_cancel(v);
    v->path[0] = 0;
    tiku_circuitview_clear_project(v);
    tiku_circuit_release(v->document);
    v->document = NULL;
    tiku_image_free(&v->image);
    v->selected = SIZE_MAX;
    v->fit = 1;
    tiku_circuitview_changed(v);
}

tiku_circuitview_t *tiku_circuitview_new(tiku_workspace_t *workspace,
                                         int *dirty)
{
    tiku_circuitview_t *v = calloc(1, sizeof *v);

    if (!v) {
        return NULL;
    }
    v->workspace = workspace;
    v->dirty = dirty;
    v->refs = 1;
    v->fit = 1;
    v->selected = SIZE_MAX;
    v->view.scale = 1;
    snprintf(v->key, sizeof v->key, "circuit:%p", (void *)v);
    snprintf(v->load_key, sizeof v->load_key, "circuit-load:%p", (void *)v);
    snprintf(v->export_key, sizeof v->export_key, "circuit-export:%p", (void *)v);
    tiku_dialog_init(&v->dialog);
    tiku_app_product_register_icons();
    tiku_field_start(&v->query, "");
    tiku_list_init(&v->sheets, 1);
    v->show_sheets = 1;
    tiku_scroll_init(&v->sheet_scroll, 1);
    v->show_details = 1;
    tiku_scroll_init(&v->inspect_scroll, 1);
    tiku_circuitview_ready(v);
    return v;
}

void tiku_circuitview_open(tiku_circuitview_t *v)
{
    if (!v || !v->workspace) {
        return;
    }
    if (!v->window) {
        v->window = tiku_workspace_open(v->workspace, TIKU_PRODUCT_CIRCUIT_NAME,
            (tiku_rect_t){CV_WINDOW_X, CV_WINDOW_Y,
                          CV_WINDOW_W, CV_WINDOW_H},
            tiku_circuitview_draw, v, v);
        if (!v->window) {
            return;
        }
        tiku_window_set_handlers(v->window, tiku_circuitview_event, NULL,
                                 gone);
        tiku_window_set_size_controls(v->window, 1, 1);
    }
    tiku_workspace_activate(v->workspace, v->window);
    tiku_circuitview_menus(v);
    if (v->dirty) {
        *v->dirty = 1;
    }
}

void tiku_circuitview_destroy(tiku_circuitview_t *v)
{
    if (!v) {
        return;
    }
    if (v->window) {
        tiku_workspace_close(v->workspace, v->window);
    }
    tiku_work_cancel(v->key);
    v->generation++;
    tiku_circuitview_files_cancel(v);
    v->choose = NULL;
    v->choose_ctx = NULL;
    v->workspace = NULL;
    v->dirty = NULL;
    tiku_circuitview_unref(v);
}

tiku_window_t *tiku_circuitview_handle(tiku_circuitview_t *v)
{
    return v ? v->window : NULL;
}
