/* tiku_grabview_app.c -- TikuGrab's descriptor: its window, run in its own
 * process by tiku-run, and its requests to the desktop's grab service.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_grabview.h"
#include "tiku_msg.h"
#include "tiku_window_app.h"
#include "tiku_work.h"
#include <stdlib.h>

/** @brief The running application: the desktop's calls and the window. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_grabview_t *view;
    int dirty;
} grab_app_t;

/** @brief Send @p msg to the desktop over the session: a grab request, or
 *         the countdown shown in the Deskbar's tray.
 *  @return 1 when it was sent, 0 when not. */
static int send(void *ctx, const struct tiku_msg *msg)
{
    grab_app_t *a = ctx;

    return a->services.send_message &&
           a->services.send_message(a->services.ctx, msg);
}

/** @brief Save As...: the desktop's Save panel asks where a copy goes and
 *         what it is called. */
static void ask_save(void *ctx, const char *start, const char *name)
{
    grab_app_t *a = ctx;

    if (a->services.pick)
        a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
            tiku_grabview_handle(a->view)), TIKU_APP_PICK_SAVE, start, name);
}

/** @brief Open @p path through the desktop's open_path(): a folder as a
 *         window, a document in its program.  @return 0 when the desktop
 *         has no such service or did not open it. */
static int open_path(void *ctx, const char *path)
{
    grab_app_t *a = ctx;

    return a->services.open_path &&
           a->services.open_path(a->services.ctx, path);
}

/** @brief The window, asking what windows there are to grab. */
static int start(void **state, const tiku_app_services_t *services)
{
    grab_app_t *a = calloc(1, sizeof *a);
    tiku_grabview_host_t host;

    if (!a) return -1;
    a->services = *services;
    host.send = send;
    host.ask_save = ask_save;
    host.open_path = open_path;
    host.ctx = a;
    a->host = tiku_window_app_new(services);
    if (a->host) a->view = tiku_grabview_new(
        tiku_window_app_workspace(a->host), &a->dirty, &host);
    if (!a->view) { tiku_window_app_free(a->host); free(a); return -1; }
    tiku_grabview_open(a->view);
    if (services->help_page)
        services->help_page(services->ctx, "apps/grab.html");
    *state = a;
    if (tiku_window_app_sync(a->host, 1)) return 0;
    tiku_grabview_destroy(a->view);
    tiku_window_app_free(a->host); free(a); *state = NULL;
    return -1;
}

/** @brief Destroy the window and free the application; a job still
 *         running keeps the window's state until it ends. */
static void stop(void *state)
{
    grab_app_t *a = state;
    tiku_grabview_destroy(a->view);
    tiku_window_app_free(a->host);
    free(a);
}

/** @brief Each turn: the delay counts down, and the window is published. */
static void tick(void *state, int64_t now)
{
    grab_app_t *a = state;
    tiku_grabview_tick(a->view, now);
    (void)tiku_work_collect();
    tiku_window_app_sync(a->host, a->dirty);
    a->dirty = 0;
}

/** @brief Input for the window; the app ends when the window has gone. */
static int event(void *state, uint32_t id, const tiku_event_t *ev)
{
    grab_app_t *a = state;
    tiku_window_app_event(a->host, id, ev);
    a->dirty = 1;
    return !tiku_grabview_handle(a->view);
}

/** @brief A menu pick; the app ends when the window has gone. */
static int menu_pick(void *state, uint32_t id, int command)
{
    grab_app_t *a = state;
    tiku_window_app_pick(a->host, id, command);
    a->dirty = 1;
    return !tiku_grabview_handle(a->view);
}

/** @brief Where Save As... put the copy. */
static void picked(void *state, uint32_t id, const char *path)
{
    grab_app_t *a = state;
    (void)id;
    tiku_grabview_picked(a->view, path);
    a->dirty = 1;
}

/** @brief The desktop's answers: the windows there are, or a picture. */
static void message(void *state, const struct tiku_msg *msg)
{
    grab_app_t *a = state;
    tiku_grabview_message(a->view, msg);
    a->dirty = 1;
}

/** @brief The desktop closed the window. */
static void closed(void *state, uint32_t id)
{
    grab_app_t *a = state;
    tiku_window_app_closed(a->host, id);
}

static const tiku_app_descriptor_t descriptor = {
    .id = TIKU_PRODUCT_GRAB_ID, .name = TIKU_PRODUCT_GRAB_NAME, .start = start, .stop = stop,
    .event_in = event, .tick = tick, .pick = menu_pick, .picked = picked,
    .closed = closed, .message = message
};
/** @brief The one symbol a loader looks for; see tiku_app.h. */
const tiku_app_export_t tiku_app_v1 = {
    TIKU_APP_ABI, sizeof(tiku_app_descriptor_t), &descriptor
};
