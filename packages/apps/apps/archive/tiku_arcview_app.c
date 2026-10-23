/* tiku_arcview_app.c -- TikuArchive's descriptor: the viewer's window,
 * run in its own process by tiku-run, asking the desktop for the archives
 * it opens and the folders it extracts into.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_arcview.h"
#include "tiku_window_app.h"
#include "tiku_work.h"
#include <stdlib.h>
#include <string.h>

/** @brief The running application: the desktop's calls and the viewer. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_arcview_t *viewer;
    int dirty;
    int asking;                     /* 1: a file to open; 2: a folder to */
                                    /* extract into                      */
} arc_app_t;

/** @brief Open... asks for a host file; Extract To... for a new folder's
 *         place and name, through the Save panel. */
static void pick(void *ctx, int save, const char *start, const char *name)
{
    arc_app_t *a = ctx;

    if (!a->services.pick) return;
    a->asking = save ? 2 : 1;
    a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
        tiku_arcview_handle(a->viewer)), save ? TIKU_APP_PICK_SAVE
                                              : TIKU_APP_PICK_LOCAL_OPEN,
        start, name);
}

/** @brief Open @p path through the desktop's open_path(): a folder as a
 *         window, a document in its program.  @return 0 when the desktop
 *         has no such service or did not open it. */
static int open_path(void *ctx, const char *path)
{
    arc_app_t *a = ctx;

    return a->services.open_path &&
           a->services.open_path(a->services.ctx, path);
}

/** @brief The window, opened on the archive it was started with. */
static int start(void **state, const tiku_app_services_t *services)
{
    arc_app_t *a = calloc(1, sizeof *a);
    tiku_arcview_host_t host;

    if (!a) return -1;
    a->services = *services;
    host.pick = pick;
    host.open_path = open_path;
    host.ctx = a;
    a->host = tiku_window_app_new(services);
    if (a->host) a->viewer = tiku_arcview_new(
        tiku_window_app_workspace(a->host), &a->dirty, &host);
    if (!a->viewer) { tiku_window_app_free(a->host); free(a); return -1; }
    tiku_arcview_open(a->viewer);
    if (services->startup_document)
        tiku_arcview_file(a->viewer, services->startup_document);
    if (services->help_page)
        services->help_page(services->ctx, "apps/archive.html");
    *state = a;
    if (tiku_window_app_sync(a->host, 1)) return 0;
    tiku_arcview_destroy(a->viewer);
    tiku_window_app_free(a->host); free(a); *state = NULL;
    return -1;
}

/** @brief Let the viewer go; its workers finish on their own. */
static void stop(void *state)
{
    arc_app_t *a = state;
    tiku_arcview_destroy(a->viewer);
    tiku_work_collect();
    tiku_window_app_free(a->host);
    free(a);
}

/** @brief Each turn: finished jobs land, progress is drawn, and the
 *         window is published. */
static void tick(void *state, int64_t now)
{
    arc_app_t *a = state;
    (void)now;
    tiku_work_collect();
    tiku_arcview_tick(a->viewer);
    tiku_window_app_sync(a->host, a->dirty);
    a->dirty = 0;
}

/** @brief Input for the window; the app ends when the window has gone. */
static int event(void *state, uint32_t id, const tiku_event_t *ev)
{
    arc_app_t *a = state;
    tiku_window_app_event(a->host, id, ev);
    a->dirty = 1;
    return !tiku_arcview_handle(a->viewer);
}

/** @brief A menu pick; the app ends when the window has gone. */
static int menu_pick(void *state, uint32_t id, int command)
{
    arc_app_t *a = state;
    tiku_window_app_pick(a->host, id, command);
    a->dirty = 1;
    return !tiku_arcview_handle(a->viewer);
}

/** @brief The archive Open... chose, or the folder Extract To... named. */
static void picked(void *state, uint32_t id, const char *path)
{
    arc_app_t *a = state;
    (void)id;
    if (path && a->asking == 2) (void)tiku_arcview_extract_to(a->viewer, path);
    else if (path) (void)tiku_arcview_file(a->viewer, path);
    a->asking = 0;
}

/** @brief The desktop closed the window. */
static void closed(void *state, uint32_t id)
{
    arc_app_t *a = state;
    tiku_window_app_closed(a->host, id);
}

static const tiku_app_descriptor_t descriptor = {
    .id = TIKU_PRODUCT_ARCHIVE_ID, .name = TIKU_PRODUCT_ARCHIVE_NAME, .start = start, .stop = stop,
    .event_in = event, .tick = tick, .pick = menu_pick, .picked = picked,
    .closed = closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
