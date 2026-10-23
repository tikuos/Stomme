/* tiku_reader_app_host.c -- the application calls TikuReader, Help and
 * TikuMarker share, around one viewer window.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_reader_app_host.h"
#include "tiku_window_app.h"
#include "tiku_msg.h"
#include "tiku_work.h"
#include <stdlib.h>

/** @brief The running application: the desktop's calls and the window. */
typedef struct {
    tiku_app_services_t services;
    tiku_window_app_t *host;
    tiku_reader_help_window_t reader;
    int dirty;
} reader_app_t;

/** @brief Ask the desktop's file panel for a document, starting in the
 *         shown document's folder, or at home. */
static void choose(void *ctx)
{
    reader_app_t *a = ctx;
    if (a->services.pick)
        a->services.pick(a->services.ctx, tiku_window_app_id(a->host,
            a->reader.window), TIKU_APP_PICK_LOCAL_OPEN,
            a->reader.root[0] ? a->reader.root : getenv("HOME"), NULL);
}

/** @brief Hand a device path a link names back to the desktop to open. */
static int open_path(void *ctx, const char *path)
{
    reader_app_t *a = ctx;
    return a->services.open_path &&
           a->services.open_path(a->services.ctx, path);
}

void tiku_reader_app_stop(void *state)
{
    reader_app_t *a = state;
    tiku_reader_help_window_destroy(&a->reader);
    tiku_work_collect();
    tiku_window_app_free(a->host);
    free(a);
}

int tiku_reader_app_start(void **state, const tiku_app_services_t *services,
                          const tiku_reader_profile_t *profile)
{
    if (!state || !services) return -1;
    *state = NULL;
    reader_app_t *a = calloc(1, sizeof *a);
    if (!a) return -1;
    a->services = *services;
    a->host = tiku_window_app_new(services);
    if (!a->host) { free(a); return -1; }
    const tiku_msg_t *m = profile ? NULL : services->startup_message;
    if (m) {
        const char *root = tiku_msg_find_string(m, "root", 0);
        const char *page = tiku_msg_find_string(m, "page", 0);
        const char *html = tiku_msg_find_string(m, "html", 0);
        const char *title = tiku_msg_find_string(m, "title", 0);
        tiku_reader_help_window_init(&a->reader,
            tiku_window_app_workspace(a->host), root ? root : "", 1024, 768,
            &a->dirty);
        tiku_reader_help_window_opener(&a->reader, open_path, a);
        if (html) tiku_reader_help_window_html(&a->reader, title, html);
        else tiku_reader_help_window_open_page(&a->reader,
                                               page ? page : "index.html");
    } else {
        tiku_reader_document_window_init_profile(&a->reader,
            tiku_window_app_workspace(a->host), 1024, 768, &a->dirty,
            choose, a, profile);
        tiku_reader_help_window_opener(&a->reader, open_path, a);
        if (services->startup_document)
            tiku_reader_document_window_file(&a->reader,
                                             services->startup_document);
        /* A document that cannot be read still leaves a window open,
         * showing the welcome page and why the document was refused. */
        tiku_reader_help_window_open(&a->reader);
    }
    if (!m && services->help_page)
        services->help_page(services->ctx, a->reader.profile->help_page);
    if (!tiku_window_app_sync(a->host, 1)) {
        tiku_reader_app_stop(a);
        return -1;
    }
    *state = a;
    return 0;
}

void tiku_reader_app_tick(void *state, int64_t now)
{
    reader_app_t *a = state; (void)now;
    tiku_work_collect();
    tiku_window_app_sync(a->host, a->dirty); a->dirty = 0;
}

int tiku_reader_app_event(void *state, uint32_t id, const tiku_event_t *ev)
{
    reader_app_t *a = state;
    tiku_window_app_event(a->host, id, ev); a->dirty = 1;
    return !a->reader.window;
}

int tiku_reader_app_pick(void *state, uint32_t id, int command)
{
    reader_app_t *a = state;
    tiku_window_app_pick(a->host, id, command); a->dirty = 1;
    return !a->reader.window;
}

void tiku_reader_app_picked(void *state, uint32_t id, const char *path)
{
    reader_app_t *a = state; (void)id;
    if (path) tiku_reader_document_window_file(&a->reader, path);
}

void tiku_reader_app_closed(void *state, uint32_t id)
{
    reader_app_t *a = state;
    tiku_window_app_closed(a->host, id);
}
