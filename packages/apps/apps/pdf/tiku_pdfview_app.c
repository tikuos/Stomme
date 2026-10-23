/* tiku_pdfview_app.c -- TikuPDF's descriptor: the viewer's window, run in
 * its own process by tiku-run, asking the desktop for the documents and
 * key files it opens.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_pdfview.h"
#include "tiku_window_app.h"
#include "tiku_work.h"
#include <stdlib.h>

/** @brief The running application: the desktop's calls and the viewer. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_pdfview_t *viewer;
    int dirty;
    int key;                        /* the pick asked is for a key file */
} pdf_app_t;

/** @brief Open... asks the desktop for a file, starting at home: a PDF,
 *         or the key file a sealed one wants. */
static void choose(void *ctx, int key)
{
    pdf_app_t *a = ctx;
    a->key = key;
    if (a->services.pick)
        a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
            tiku_pdfview_handle(a->viewer)), TIKU_APP_PICK_LOCAL_OPEN,
            getenv("HOME"), NULL);
}

/** @brief The window, opened on the document it was started with. */
static int start(void **state, const tiku_app_services_t *services)
{
    pdf_app_t *a = calloc(1, sizeof *a);
    if (!a) return -1;
    a->services = *services;
    a->host = tiku_window_app_new(services);
    if (a->host) a->viewer = tiku_pdfview_new(
        tiku_window_app_workspace(a->host), &a->dirty, choose, a);
    if (!a->viewer) { tiku_window_app_free(a->host); free(a); return -1; }
    tiku_pdfview_open(a->viewer);
    if (services->startup_document)
        tiku_pdfview_file(a->viewer, services->startup_document);
    if (services->help_page)
        services->help_page(services->ctx, "apps/pdf.html");
    *state = a;
    if (tiku_window_app_sync(a->host, 1)) return 0;
    tiku_pdfview_destroy(a->viewer);
    tiku_window_app_free(a->host); free(a); *state = NULL;
    return -1;
}

/** @brief Let the viewer go; its workers finish on their own. */
static void stop(void *state)
{
    pdf_app_t *a = state;
    tiku_pdfview_destroy(a->viewer);
    tiku_work_collect();
    tiku_window_app_free(a->host);
    free(a);
}

/** @brief Each turn: finished jobs land, then the window is published. */
static void tick(void *state, int64_t now)
{
    pdf_app_t *a = state;
    (void)now;
    tiku_work_collect();
    tiku_window_app_sync(a->host, a->dirty);
    a->dirty = 0;
}

/** @brief Input for the window; the app ends when the window has gone. */
static int event(void *state, uint32_t id, const tiku_event_t *ev)
{
    pdf_app_t *a = state;
    tiku_window_app_event(a->host, id, ev);
    a->dirty = 1;
    return !tiku_pdfview_handle(a->viewer);
}

/** @brief A menu pick; the app ends when the window has gone. */
static int pick(void *state, uint32_t id, int command)
{
    pdf_app_t *a = state;
    tiku_window_app_pick(a->host, id, command);
    a->dirty = 1;
    return !tiku_pdfview_handle(a->viewer);
}

/** @brief The file Open... or Open Key... chose. */
static void picked(void *state, uint32_t id, const char *path)
{
    pdf_app_t *a = state; (void)id;
    if (path && a->key) (void)tiku_pdfview_key(a->viewer, path);
    else if (path) tiku_pdfview_file(a->viewer, path);
    a->key = 0;
}

/** @brief The desktop closed the window. */
static void closed(void *state, uint32_t id)
{
    pdf_app_t *a = state;
    tiku_window_app_closed(a->host, id);
}

static const tiku_app_descriptor_t descriptor = {
    .id = TIKU_PRODUCT_PDF_ID, .name = TIKU_PRODUCT_PDF_NAME, .start = start, .stop = stop,
    .event_in = event, .tick = tick, .pick = pick, .picked = picked,
    .closed = closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
