/* tiku_diffview_app.c -- TikuDiff's descriptor: the viewer's window, run
 * in its own process by tiku-run, asking the desktop for the files and
 * folders it compares and where a patch is saved.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_diffview.h"
#include "tiku_window_app.h"
#include "tiku_work.h"
#include <stdlib.h>
#include <string.h>

/** @brief The running application: the desktop's calls and the viewer. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_diffview_t *viewer;
    int dirty;
} diff_app_t;

/** @brief A question for the desktop's file panel: a local file or
 *         folder to compare, or where to save the differences. */
static void pick(void *ctx, int what, const char *start, const char *name)
{
    diff_app_t *a = ctx;
    int mode = what == TIKU_DIFFVIEW_ASK_SAVE ? TIKU_APP_PICK_SAVE
             : what == TIKU_DIFFVIEW_ASK_FOLDER ? TIKU_APP_PICK_LOCAL_FOLDER
                                                : TIKU_APP_PICK_LOCAL_OPEN;

    if (!a->services.pick) return;
    a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
        tiku_diffview_handle(a->viewer)), mode, start, name);
}

/** @brief Open @p path through the desktop's open_path(): a folder as a
 *         window, a document in its program.  @return 0 when the desktop
 *         has no such service or did not open it. */
static int open_path(void *ctx, const char *path)
{
    diff_app_t *a = ctx;

    return a->services.open_path &&
           a->services.open_path(a->services.ctx, path);
}

/** @brief The window, blank, or asking what to compare a document given
 *         at launch with. */
static int start(void **state, const tiku_app_services_t *services)
{
    diff_app_t *a = calloc(1, sizeof *a);
    tiku_diffview_host_t host;

    if (!a) return -1;
    a->services = *services;
    host.pick = pick;
    host.open_path = open_path;
    host.ctx = a;
    a->host = tiku_window_app_new(services);
    if (a->host) a->viewer = tiku_diffview_new(
        tiku_window_app_workspace(a->host), &a->dirty, &host);
    if (!a->viewer) { tiku_window_app_free(a->host); free(a); return -1; }
    tiku_diffview_open(a->viewer);
    if (services->help_page)
        services->help_page(services->ctx, "apps/diff.html");
    *state = a;
    if (tiku_window_app_sync(a->host, 1)) {
        if (services->startup_document)
            tiku_diffview_start_with(a->viewer, services->startup_document);
        return 0;
    }
    tiku_diffview_destroy(a->viewer);
    tiku_window_app_free(a->host); free(a); *state = NULL;
    return -1;
}

/** @brief Let the viewer go; its workers finish on their own. */
static void stop(void *state)
{
    diff_app_t *a = state;
    tiku_diffview_destroy(a->viewer);
    tiku_work_collect();
    tiku_window_app_free(a->host);
    free(a);
}

/** @brief Each turn: finished jobs land, progress is drawn, and the
 *         window is published. */
static void tick(void *state, int64_t now)
{
    diff_app_t *a = state;
    (void)now;
    tiku_work_collect();
    tiku_diffview_tick(a->viewer);
    tiku_window_app_sync(a->host, a->dirty);
    a->dirty = 0;
}

/** @brief Input for the window; the app ends when the window has gone. */
static int event(void *state, uint32_t id, const tiku_event_t *ev)
{
    diff_app_t *a = state;
    tiku_window_app_event(a->host, id, ev);
    a->dirty = 1;
    return !tiku_diffview_handle(a->viewer);
}

/** @brief A menu pick; the app ends when the window has gone. */
static int menu_pick(void *state, uint32_t id, int command)
{
    diff_app_t *a = state;
    tiku_window_app_pick(a->host, id, command);
    a->dirty = 1;
    return !tiku_diffview_handle(a->viewer);
}

/** @brief The file, folder or place the panel came back with. */
static void picked(void *state, uint32_t id, const char *path)
{
    diff_app_t *a = state;
    (void)id;
    tiku_diffview_picked(a->viewer, path);
    a->dirty = 1;
}

/** @brief The desktop closed the window. */
static void closed(void *state, uint32_t id)
{
    diff_app_t *a = state;
    tiku_window_app_closed(a->host, id);
}

static const tiku_app_descriptor_t descriptor = {
    .id = TIKU_PRODUCT_DIFF_ID, .name = TIKU_PRODUCT_DIFF_NAME, .start = start, .stop = stop,
    .event_in = event, .tick = tick, .pick = menu_pick, .picked = picked,
    .closed = closed
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
