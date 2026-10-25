/* tiku_circuit_app.c -- TikuCircuit's descriptor: one window, run in its
 * own process by tiku-run, asking the desktop for the files it opens.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_circuit_window.h"
#include "tiku_window_app.h"
#include "tiku_work.h"
#include <stdlib.h>
#include <stdio.h>

/** @brief The running application: its window and the desktop's calls. */
typedef struct {
    tiku_window_app_t *host;        /* hands the window to the desktop */
    tiku_circuitview_t *viewer;     /* the window's controller */
    tiku_app_services_t services;   /* the desktop's calls, copied */
    int dirty;                      /* the window wants another frame */
} circuit_app_t;

/** @brief Ask the desktop's file panel, starting at home, for a local
 *         file to open, or for Export View as PNG... a place to save
 *         circuit-view.png; when the panel cannot be put up the ask is
 *         forgotten and the status line says so. */
static void choose(void *ctx)
{
    circuit_app_t *a = ctx;
    int exporting = tiku_circuitview_picking_export(a->viewer);

    if (!a->services.pick || !a->services.pick(a->services.ctx,
        tiku_window_app_id(a->host, tiku_circuitview_handle(a->viewer)),
        exporting ? TIKU_APP_PICK_SAVE : TIKU_APP_PICK_LOCAL_OPEN,
        getenv("HOME"), exporting ? "circuit-view.png" : NULL)) {
        tiku_circuitview_picked(a->viewer, NULL);
        tiku_circuitview_note(a->viewer,
            "The file picker could not be opened. Try again.");
    }
}

/** @brief Open the window, on the document the app was started with. */
static int start(void **state, const tiku_app_services_t *services)
{
    circuit_app_t *a = calloc(1, sizeof *a);

    if (!a) {
        return -1;
    }
    a->services = *services;
    a->host = tiku_window_app_new(services);
    if (a->host) {
        a->viewer = tiku_circuitview_new(
            tiku_window_app_workspace(a->host), &a->dirty);
    }
    if (a->viewer) {
        char directory[4096];
        const char *base = getenv("XDG_CONFIG_HOME");
        int n;
        if (base && base[0] == '/')
            n = snprintf(directory, sizeof directory,
                         TIKU_PRODUCT_CIRCUIT_CONFIG_PATH, base);
        else {
            base = getenv("HOME");
            n = snprintf(directory, sizeof directory,
                         TIKU_PRODUCT_CIRCUIT_HOME_CONFIG_PATH,
                         base ? base : "");
        }
        if (base && base[0] == '/' && n > 0 && (size_t)n < sizeof directory)
            tiku_circuitview_settings(a->viewer,directory);
        tiku_circuitview_picker(a->viewer,
                                services->pick ? choose : NULL, a);
        tiku_circuitview_open(a->viewer);
        if (services->startup_document) {
            tiku_circuitview_file(a->viewer, services->startup_document);
        }
        if (services->help_page) {
            services->help_page(services->ctx, "apps/circuit.html");
        }
        if (tiku_window_app_sync(a->host, 1)) {
            *state = a;
            return 0;
        }
    }
    tiku_circuitview_destroy(a->viewer);
    tiku_window_app_free(a->host);
    free(a);
    return -1;
}

/** @brief Close the window and cancel its jobs; a job still running
 *         keeps the viewer until tiku_work_collect() retires it. */
static void stop(void *state)
{
    circuit_app_t *a = state;

    tiku_circuitview_destroy(a->viewer);
    tiku_window_app_free(a->host);
    free(a);
}

/** @brief Each turn: finished jobs land, new ones are posted, and the
 *         window is published when it changed. */
static void tick(void *state, int64_t now)
{
    circuit_app_t *a = state;

    (void)now;
    tiku_work_collect();
    tiku_circuitview_tick(a->viewer);
    int dirty = a->dirty;
    a->dirty = 0;
    tiku_window_app_sync(a->host, dirty);
}

/** @brief Input for the window; the app ends when the window has gone. */
static int event(void *state, uint32_t id, const tiku_event_t *ev)
{
    circuit_app_t *a = state;

    tiku_window_app_event(a->host, id, ev);
    a->dirty = 1;
    return !tiku_circuitview_handle(a->viewer);
}

/** @brief A menu pick; the app ends when the window has gone. */
static int pick(void *state, uint32_t id, int command)
{
    circuit_app_t *a = state;

    tiku_window_app_pick(a->host, id, command);
    a->dirty = 1;
    return !tiku_circuitview_handle(a->viewer);
}

/** @brief The desktop closed the window. */
static void closed(void *state, uint32_t id)
{
    circuit_app_t *a = state;

    tiku_window_app_closed(a->host, id);
}

/** @brief The file the panel chose, for the window that asked; an answer
 *         for another window or after this one closed is ignored. */
static void picked(void *state, uint32_t id, const char *path)
{
    circuit_app_t *a = state;
    tiku_window_t *w = tiku_circuitview_handle(a->viewer);

    if (w && id == tiku_window_app_id(a->host, w)) {
        tiku_circuitview_picked(a->viewer, path);
    }
}

static const tiku_app_descriptor_t descriptor = {.id = TIKU_PRODUCT_CIRCUIT_ID,
                                                 .name =
                                                     TIKU_PRODUCT_CIRCUIT_NAME,
                                                 .start = start,
                                                 .stop = stop,
                                                 .tick = tick,
                                                 .event_in = event,
                                                 .pick = pick,
                                                 .picked = picked,
                                                 .closed = closed};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
__attribute__((visibility("default"))) const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
/** @brief Borrow current document facts from the application's native viewer. */
static vr_document_t resource_document(void *state)
{
    circuit_app_t *app = state;
    vr_document_t document = tiku_circuitview_resource_document(app->viewer);
    document.window =
        tiku_window_app_id(app->host, tiku_circuitview_handle(app->viewer));
    return document;
}
/** @brief Enumerate the native document's current resource schema. */
static int resource_catalog(void *state, tiku_app_resources_t *owner,
                            size_t index, tiku_resource_initial_t *out)
{
    vr_document_t document = resource_document(state);
    return vr_catalog(owner, &document, index, out);
}
/** @brief Copy a public native document observation on its owner thread. */
static tiku_resource_error_t resource_capture(void *state,
                                              tiku_app_resources_t *owner,
                                              const char *path,
                                              tiku_resource_snapshot_t *out)
{
    vr_document_t document = resource_document(state);
    return vr_capture(owner, &document, path, out);
}
/** @brief Use the native viewer's supported document controller. */
static tiku_resource_error_t
resource_change(void *state, tiku_app_resources_t *owner, const char *path,
                const tiku_msg_t *value, int invoke,
                tiku_resource_snapshot_t *out, tiku_resource_effect_t *effect)
{
    vr_document_t document = resource_document(state);
    return vr_change(owner, &document, path, value, invoke, out, effect);
}
/** @brief Rebuild document schemas after the native viewer adopts another source. */
static uint64_t resource_revision(void *state)
{
 vr_document_t d=resource_document(state);
 return d.identity*UINT64_C(1099511628211)+d.generation;
}
/** @brief Optional versioned native application resource callbacks. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog_revision = resource_revision,
    .catalog = resource_catalog,
    .capture = resource_capture,
    .change = resource_change};
