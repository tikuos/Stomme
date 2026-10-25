/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_remote.c - remote sessions: the windows of applications
 * running in processes of their own, over the desk socket; their messages
 * (file picks, documents, boards, help, the clipboard, device requests);
 * and closing what a session leaves behind when it ends.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_clip_rpc.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"

#if TIKU_SHELL_REMOTE_SERVICES
#include "tiku_uri.h"

#endif
#include "tiku_vfs_rpc.h"

#if TIKU_SHELL_BOARD_SERVICES

#else
#include "tiku_work.h"

#endif
#include <stdio.h>

/**
 * @brief Draw a remote window: when recording, its description, or its
 *        display list when a fresh one came; otherwise the pixels its
 *        session last sent, on the panel colour.
 */
static void
remote_window_draw(tiku_window_t *window, tiku_surface_t *surface,
                   tiku_rect_t content, void *context)
{
    tiku_remote_window_t *rw = context;
    const uint32_t *px;
    int y, w, h;

    (void)window;
    if (rw && surface->record && rw->description[0]) {
        (void)tiku_dl_text(surface->record, TIKU_DL_PLAIN, content.x,
                           content.y, rw->description, TIKU_C_TEXT);
        return;
    }
    if (rw != NULL && rw->list_fresh && rw->list != NULL &&
        surface->record != NULL) {
        /* While recording, the window's display list is copied into the
         * record as it came, words and controls included. */
        int rec = tiku_gfx_rec_enter(surface);

        if (rec) {
            (void)tiku_dl_append(surface->record, rw->list);
        }
        tiku_gfx_rec_leave(surface, rec);
        return;
    }
    tiku_fill(surface, content, TIKU_C_PANEL);
    if (rw == NULL) {
        return;
    }
    /* Published pixels belong to the desktop and stay fixed while drawing. */
    px = tiku_remote_pixels(rw);
    if (px == NULL) {
        return;
    }
    w = (rw->fw < content.w) ? rw->fw : content.w;
    h = (rw->fh < content.h) ? rw->fh : content.h;
    for (y = 0; y < h; y++) {
        int x;

        for (x = 0; x < w; x++) {
            tiku_pixel(surface, content.x + x, content.y + y,
                            px[(size_t)y * (size_t)rw->fw + x]);
        }
    }
}

/** @brief Send a pick in a remote window's published menu back to its
 *         session. */
static void
remote_menu_pick(tiku_window_t *window, int command, void *context)
{
    desktop_t *app = context;

    tiku_remote_pick(&app->remote, window, command);
}

/**
 * @brief Apply each session's window requests: close a reaped window, open
 *        a window for an OPEN, publish its menus, turn on its size
 *        controls, apply a configuration, a raise or a placement, and
 *        close a window given back.  Menus are published with
 *        tiku_window_publish_menus(), as an embedded window's are.
 */
void
remote_reconcile(desktop_t *app)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &app->remote.session[i];
        int j;

        if (!s->used) {
            continue;
        }
        for (j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            tiku_remote_window_t *rw = &s->win[j];

            if (rw->reap != NULL) {
                /* A window whose row was reused: closed before the row's
                 * new state is read. */
                if (app->remote_press == rw->reap) {
                    app->remote_press = NULL;
                }
                tiku_workspace_close(app->desk, rw->reap);
                rw->reap = NULL;
                desktop_damage_all(app);
            }
            if (rw->opened && rw->window == NULL) {
                tiku_rect_t frame;

                frame.w = rw->open_w + 2 * TIKU_WINDOW_BORDER;
                frame.h = rw->open_h + tiku_window_tab_h() +
                          2 * TIKU_WINDOW_BORDER;
                frame.x = (app->surf->w - frame.w) / 2;
                frame.y = (app->surf->h - frame.h) / 3;
                /* A session's windows are laid side by side, each tagged
                 * by its own window record. */
                {
                    int across = frame.w + 8;
                    int cols = (across > 0) ? (app->surf->w / across) : 1;

                    if (cols < 1) {
                        cols = 1;
                    }
                    frame.x += (j % cols) * across;
                    frame.y += (j / cols) * (frame.h + 8);
                    if (frame.x + frame.w > app->surf->w) {
                        frame.x = app->surf->w - frame.w;
                    }
                    if (frame.y + frame.h > app->surf->h) {
                        frame.y = app->surf->h - frame.h;
                    }
                    if (frame.x < 0) {
                        frame.x = 0;
                    }
                    if (frame.y < 0) {
                        frame.y = 0;
                    }
                    /* Never under the global bar, so the tab stays
                     * reachable. */
                    if (app->gbar_on == 1 &&
                        frame.y < app->menus.bar.height) {
                        frame.y = app->menus.bar.height;
                    }
                }
                rw->window = tiku_workspace_open(app->desk, rw->title,
                    frame, remote_window_draw, rw, rw);
                if (rw->window != NULL) {
                    tiku_window_set_size_controls(rw->window, 0, 0);
                }
                desktop_damage_all(app);
            }
            if (rw->window != NULL && rw->menus_fresh) {
                tiku_window_publish_menus(rw->window, &rw->menus,
                                               remote_menu_pick, app);
                rw->menus_fresh = 0;
                desktop_damage_all(app);
            }
            if (rw->window != NULL && rw->wants_size) {
                tiku_window_set_size_controls(rw->window, 1, 1);
                rw->wants_size = 0;
            }
            if (rw->window != NULL && rw->configured) {
                tiku_window_t *w = rw->window;
                snprintf(w->title, sizeof w->title, "%s", rw->title);
                tiku_window_set_size_controls(w, rw->resizable, rw->zoomable);
                if (rw->config_w && rw->config_h) {
                    tiku_rect_t content = tiku_window_content(w);
                    w->frame.w += rw->config_w - content.w;
                    w->frame.h += rw->config_h - content.h;
                }
                rw->configured = 0;
                desktop_damage_all(app);
            }
            if (rw->window != NULL && rw->wants_front) {
                tiku_workspace_activate(app->desk, rw->window);
                rw->wants_front = 0;
                desktop_damage_all(app);
            }
#if TIKU_SHELL_NOTE_TABS

            if (rw->window != NULL &&
                rw->wants_place == TIKU_APP_PLACE_NOTE) {
                tiku_window_set_tab_auto_hide(rw->window, 1);
                rw->wants_place = 0;
                desktop_damage_all(app);
            }
#endif

            if (rw->window != NULL &&
                (rw->wants_place == TIKU_APP_PLACE_ANNOUNCE ||
                 rw->wants_place == TIKU_APP_PLACE_PANEL)) {
                int plain =
                    (rw->wants_place == TIKU_APP_PLACE_ANNOUNCE);
#if TIKU_SHELL_NOTE_TABS


                tiku_window_set_tab_auto_hide(rw->window, 0);
#endif


                if (plain) {
                    /* An announcement has no tab: its frame shrinks to
                     * the content and a hairline. */
                    rw->window->frame.w -= 2 * TIKU_WINDOW_BORDER - 2;
                    rw->window->frame.h -=
                        tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER - 2;
                }
                /* Both roles are centred in the upper third, at a fixed
                 * size.  An announcement is plain, with no tab, and its
                 * program takes it down; a panel keeps its tab and its
                 * close box. */
                rw->window->frame.x =
                    (app->surf->w - rw->window->frame.w) / 2;
                rw->window->frame.y =
                    (app->surf->h - rw->window->frame.h) / 3;
                tiku_window_set_size_controls(rw->window, 0, 0);
                tiku_window_set_plain(rw->window, plain);
                rw->wants_place = 0;
                desktop_damage_all(app);
            }
            /* A row the application gave back: its window closes, and
             * the session stays. */
            if (rw->window != NULL && !rw->opened) {
                if (app->remote_press == rw->window) {
                    app->remote_press = NULL;
                }
                tiku_workspace_close(app->desk, rw->window);
                rw->window = NULL;
                desktop_damage_all(app);
            }
        }
    }
}

/**
 * @brief Open the local image the session names through the associations,
 *        and reply TIKU_MSG_IMAGEOPENED with whether it was accepted; the
 *        client waits for the reply before it exits.
 */
static void remote_image_open(desktop_t *app, int session, tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int ok = path && desktop_document_local(app, path);
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_IMAGEOPENED);
    if (reply) {
        (void)tiku_msg_add_int32(reply, "accepted", ok);
        (void)tiku_remote_tell(&app->remote, session, reply);
        tiku_msg_free(reply);
    }
}

int desktop_remote_document(const char *path)
{
    /* The extensions of the documents TikuTracker (system/tracker)
     * forwards from browser_open_image(), as tiku_backend_posix.c types
     * them. */
    static const char *const known[] = {
#if TIKU_SHELL_BOARD_SERVICES

        ".bas", ".tiku", ".txt", ".md", ".markdown", ".html", ".htm"
    }
#else

        ".bas", ".txt", ".md", ".markdown", ".html", ".htm"
    }
#endif
;
    const char *ext = (path != NULL) ? strrchr(path, '.') : NULL;

    if (ext == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
        if (!strcasecmp(ext, known[i])) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Open the local document the session names, when its name is one
 *        desktop_remote_document() accepts, through the associations; reply
 *        TIKU_MSG_DOCUMENTOPENED with whether it was accepted.
 */
static void remote_document_open(desktop_t *app, int session, tiku_msg_t *m)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int ok = desktop_remote_document(path) &&
        desktop_document_local(app, path);
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_DOCUMENTOPENED);
    if (reply) {
        (void)tiku_msg_add_int32(reply, "accepted", ok);
        (void)tiku_remote_tell(&app->remote, session, reply);
        tiku_msg_free(reply);
    }
}

/**
 * @brief Tell each remote window whether it has the focus, when that
 *        changes: an application's own workspace cannot see the desktop's
 *        keyboard focus.
 */
void
remote_focus(desktop_t *app, tiku_window_t *focused)
{
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &app->remote.session[i];
        if (!s->used) continue;
        for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            tiku_remote_window_t *rw = &s->win[j];
            if (!rw->window || !rw->opened) continue;
            int active = rw->window == focused;
            if (active == rw->focused) continue;
#if TIKU_SHELL_REMOTE_SERVICES

            rw->link_gesture = 0;
#endif

            rw->focused = active;
            tiku_event_t event = {.type = active ? TIKU_EVENT_ACTIVATED :
                                                   TIKU_EVENT_DEACTIVATED};
            tiku_remote_event(&app->remote, rw->window, &event);
        }
    }
}
#if TIKU_SHELL_REMOTE_SERVICES


/** @brief Route a native user action to the current owning application. */
static int
access_action(void *context, uint32_t node, uint64_t document,
              uint64_t generation, unsigned action, int first, int last,
              const char *text)
{
    tiku_remote_window_t *window=context;
    return desktop_access_perform(window ? window->access_context : NULL,
                                  window, node, document, generation, action,
                                  first, last, text, 1);
}
/** @brief Accept a bounded logical tree only for a live session-owned window.
 */
static void
remote_access_tree(desktop_t *app, int session, const tiku_msg_t *message)
{
    int32_t id;
    if (!tiku_msg_find_int32(message, "window", 0, &id))
        return;
    tiku_remote_session_t *source = app->remote.session + session;
    for (unsigned i = 0; i < TIKU_REMOTE_WINDOWS; i++) {
        tiku_remote_window_t *window = source->win + i;
        if (window->win_id != (uint32_t)id || !window->opened)
            continue;
        size_t bytes = 0;
        const void *packet = tiku_msg_find_blob(message, "tree", 0, &bytes);
        tiku_access_tree_t *tree = NULL;
        if (packet && tiku_access_decode(packet, bytes, &tree))
            return;
        if (tree && window->access &&
            tiku_access_document(tree) ==
                tiku_access_document(window->access) &&
            tiku_access_generation(tree) <
                tiku_access_generation(window->access)) {
            tiku_access_release(tree);
            return;
        }
        tiku_access_release(window->access);
        window->access = tree;
        window->access_fresh = 1;
        return;
    }
}
/** @brief Refresh native geometry/focus and remove closed virtual windows. */
static void
access_refresh(desktop_t *app)
{
    if (!app->host)
        return;
    for (unsigned i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        for (unsigned j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            tiku_remote_window_t *window = &app->remote.session[i].win[j];
            uint64_t *owner = app->access_owners[i][j];
            uint64_t session = app->remote.session[i].generation;
            if (owner[0] != session || owner[1] != window->generation) {
                tiku_host_access(app->host, i * TIKU_REMOTE_WINDOWS + j + 1,
                                 NULL, 0, 0, 0, NULL, NULL);
                owner[0] = session;
                owner[1] = window->generation;
                window->access_fresh = 1;
            }
            tiku_rect_t content = window->window
                                      ? tiku_window_content(window->window)
                                      : (tiku_rect_t){0};
            if (!window->access_fresh && content.x == window->access_x &&
                content.y == window->access_y &&
                window->focused == window->access_focus)
                continue;
            window->access_fresh = 0;
            window->access_context = app;
            window->access_x = content.x;
            window->access_y = content.y;
            window->access_focus = window->focused;
            tiku_host_access(
                app->host, i * TIKU_REMOTE_WINDOWS + j + 1,
                window->opened && window->window ? window->access : NULL,
                content.x, content.y, window->focused, access_action, window);
        }
    }
}

/** @brief Dispatch a copied URI without blocking the desktop event loop. */
static void
link_run(void *ctx)
{
    (void)tiku_uri_open(ctx);
}
/** @brief Queue one validated URI for the native host launcher. */
static int
link_dispatch(void *ctx, const char *target)
{
    (void)ctx;
    char *copy = strdup(target);
    if (!copy)
        return 0;
    tiku_work_job_t job = {.run = link_run, .free = free, .ctx = copy};
    if (tiku_work_post(&job)) {
        free(copy);
        return 0;
    }
    return 1;
}
/** @brief Reply to explicit URI dispatch after host-owned gesture validation.
 */
static void
remote_link_open(desktop_t *app, int session, const tiku_msg_t *message)
{
    int64_t request = 0;
    tiku_msg_find_int64(message, "request", 0, &request);
    int accepted =
        request > 0 &&
        tiku_remote_link(app->remote.session + session, message,
                         tiku_remote_gesture_clock(), link_dispatch, NULL);
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_LINK_OPENED);
    if (reply && tiku_msg_add_int64(reply, "request", request) &&
        tiku_msg_add_int32(reply, "accepted", accepted))
        (void)tiku_remote_tell(&app->remote, session, reply);
    tiku_msg_free(reply);
}


/** @brief Deliver a native print result to the original live application
 * window. */
static void
print_done(void *context, uint64_t ticket, int status, const char *reason)
{
    desktop_t *app = context;
    desktop_host_vfs_print_done(app,ticket,status,reason);
    tiku_print_rpc_done(&app->printing, ticket, status, reason);
}
/** @brief Submit an owned final PDF to the native host after its print grant.
 */
static int
print_dispatch(void *context, uint64_t ticket, const void *pdf, size_t bytes,
               const char *title)
{
    desktop_t *app = context;
    return tiku_host_print(app->host, ticket, pdf, bytes, title, print_done,
                           app);
}
/** @brief Cancel only the native operation belonging to a retired print ticket.
 */
static void
print_cancel(void *context, uint64_t ticket)
{
    desktop_t *app = context;
    tiku_host_print_cancel(app->host, ticket);
}
#endif


/**
 * @brief Drain each session's messages and act on them: opening an image, a
 *        document or a path, a file pick, a device request, a screenshot, a
 *        tray icon, a cursor, the clipboard, help, and a board run, look,
 *        release, reclaim, save, input or stop.
 */
void
remote_hear(desktop_t *app)
{
#if TIKU_SHELL_REMOTE_SERVICES

    app->printing.dispatch = print_dispatch;
    app->printing.cancel = print_cancel;
    app->printing.context = app;
    tiku_print_rpc_poll(&app->printing, tiku_remote_gesture_clock());
#endif

    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_msg_t *m;

        while ((m = tiku_remote_said(&app->remote, i)) != NULL) {
#if TIKU_SHELL_REMOTE_SERVICES

            (void)tiku_print_rpc_message(&app->printing,
                                         app->remote.session + i, m,
                                         tiku_remote_gesture_clock());
#endif

            if(desktop_app_vfs_message(app,i,m)){tiku_msg_free(m);continue;}
            if (tiku_msg_what(m) == TIKU_MSG_CLIP_REQUEST) {
                tiku_msg_t *reply = tiku_clip_reply(m);

                if (reply) {
                    (void)tiku_remote_tell(&app->remote, i, reply);
                    tiku_msg_free(reply);
                }
            }
#if TIKU_SHELL_REMOTE_SERVICES

            if (tiku_msg_what(m) == TIKU_MSG_ACCESS_TREE)
                remote_access_tree(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_LINK_OPEN)
                remote_link_open(app, i, m);
#endif

            if (tiku_msg_what(m) == TIKU_MSG_IMAGEOPEN)
                remote_image_open(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_DOCUMENTOPEN)
                remote_document_open(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_PICK)
                desktop_remote_pick(app, i, m);
#if TIKU_SHELL_BOARD_SERVICES

            if (tiku_msg_what(m) == TIKU_MSG_VFS_REQUEST)
                desktop_vfs_request(app, i, m);
#endif

            if (tiku_msg_what(m) == TIKU_MSG_GRAB)
                desktop_grab(app, i, m);
            if (tiku_msg_what(m) == TIKU_MSG_TRAY)
                desktop_tray(app, i, m);
#if TIKU_SHELL_REMOTE_SERVICES

            if (tiku_msg_what(m) == TIKU_MSG_TEXT_CARET)
                desktop_text_rect(app, i, m);
#endif

            if (tiku_msg_what(m) == TIKU_MSG_CURSOR)
                desktop_cursor(app, i, m);
#if TIKU_SHELL_BOARD_SERVICES

            if (tiku_msg_what(m) == TIKU_MSG_BOARDRUN) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                const char *prog = tiku_msg_find_string(m, "program", 0);

                /* Which session asked is remembered, so what the program
                 * prints goes back to it and not to every session. */
                app->board_run_session = i;
                if (!desktop_board_run(app, NULL, board,
                                       (prog != NULL) ? prog : "")) {
                    app->board_run_session = -1;
                    desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED, "");
                }
            }
#endif

                                    {
                uint32_t w = tiku_msg_what(m);

                tiku_trail("wire %c%c%c%c from session %d",
                           (char)(w >> 24), (char)(w >> 16),
                           (char)(w >> 8), (char)w, i);
#if TIKU_SHELL_BOARD_SERVICES

            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDLOOK) {
                /* The asker is told when the last port has answered. */
                desktop_board_look_begin(app, i, NULL);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDFREE) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                int ok = desktop_board_release(app, board);

                /* Success is told (RELEASED) once the close worker has
                 * closed the port; only a refusal is told here. */
                if (!ok) desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED,
                                           (board != NULL) ? board : "");
                desktop_board_names_tell(app);
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDBACK) {
                const char *board =
                    tiku_msg_find_string(m, "board", 0);

                /* A reclaim taken is answered when every port has; a
                 * reclaim refused is answered FAILED now. */
                if (!desktop_board_reclaim(app, board, i, NULL)) {
                    desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED,
                                       (board != NULL) ? board : "");
                }
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDSAVE) {
                const char *board = tiku_msg_find_string(m, "board", 0);
                const char *name = tiku_msg_find_string(m, "name", 0);
                const char *prog = tiku_msg_find_string(m, "program", 0);

                app->board_run_session = i;
                if (!desktop_board_save(app, NULL, board, name,
                                        (prog != NULL) ? prog : "")) {
                    app->board_run_session = -1;
                    desktop_board_tell(app, i, TIKU_APP_BOARD_FAILED, "");
                }
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDIN) {
                const char *text = tiku_msg_find_string(m, "text", 0);

                (void)desktop_board_input(app, (text != NULL) ? text : "");
            }
            if (tiku_msg_what(m) == TIKU_MSG_BOARDSTOP) {
                (void)desktop_board_stop(app);
#endif

            }
            if (tiku_msg_what(m) == TIKU_MSG_CLIP) {
                /* Into the desk's clipboard; clip_mirror() sends it on
                 * to every session, this one included. */
                const char *text = tiku_msg_find_string(m, "text", 0);

                if (text) (void)tiku_clipboard_import(text, strlen(text));
            }
            if (tiku_msg_what(m) == TIKU_MSG_HELPPAGE) {
                const char *page = tiku_msg_find_string(m, "page", 0);

                snprintf(app->remote.session[i].help_page,
                         sizeof app->remote.session[i].help_page, "%s",
                         (page != NULL) ? page : "");
            }
            if (tiku_msg_what(m) == TIKU_MSG_HELPOPEN) {
                const char *page = tiku_msg_find_string(m, "page", 0);
                if (page && strlen(page) < 256)
                    (void)desktop_help_open(app, page);
            }
            if (tiku_msg_what(m) == TIKU_MSG_HELPHTML) {
                const char *title = tiku_msg_find_string(m, "title", 0);
                const char *html = tiku_msg_find_string(m, "html", 0);
                if (html && strlen(html) <= 15000 &&
                    (!title || strlen(title) <= 127))
                    (void)desktop_help_html(app,
                                                    title, html);
            }
            if (tiku_msg_what(m) == TIKU_MSG_OPENPATH) {
                const char *path = tiku_msg_find_string(m, "path", 0);
                if (path && !desktop_app_open_path(app, path)) {
                    snprintf(app->status, sizeof app->status,
                             "Could not open %.180s", path);
                    desktop_damage_all(app);
                }
            }
            tiku_msg_free(m);
        }
    }
#if TIKU_SHELL_REMOTE_SERVICES

    access_refresh(app);
#else

}

/** @brief Tell session @p session something about its board run, as
 *         TIKU_MSG_BOARDOUT. */
void
desktop_board_tell(desktop_t *app, int session, int what, const char *text)
{
    tiku_msg_t *m;

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS) {
        return;
    }
    m = tiku_msg_new(TIKU_MSG_BOARDOUT);
    if (m == NULL) {
        return;
    }
    (void)tiku_msg_add_int32(m, "what", (int32_t)what);
    (void)tiku_msg_add_string(m, "text", (text != NULL) ? text : "");
    (void)tiku_remote_tell(&app->remote, session, m);
    tiku_msg_free(m);
}

/**
 * @brief Tell every session the mounted boards and the current device, when
 *        either changes or the number of sessions does.
 */
void
desktop_board_names_tell(desktop_t *app)
{
    char names[512];
    tiku_msg_t *m;
    int i, sessions = 0;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (app->remote.session[i].used) {
            sessions++;
        }
    }
        (void)desktop_board_list(app, names, sizeof names);
    /* Also when the number of sessions changes, so a session that
     * connects later hears them. */
    if (sessions == app->board_names_sessions &&
        strcmp(names, app->board_names_told) == 0 &&
        strcmp(app->current_device, app->board_current_told) == 0) {
        return;
    }
    app->board_names_sessions = sessions;
    (void)snprintf(app->board_names_told, sizeof app->board_names_told,
                   "%s", names);
    (void)snprintf(app->board_current_told,
                   sizeof app->board_current_told, "%s",
                   app->current_device);
    m = tiku_msg_new(TIKU_MSG_BOARDS);
    if (m == NULL) {
        return;
    }
    (void)tiku_msg_add_string(m, "names", names);
    /* The current device goes with them: no other message carries it to
     * the remote sessions. */
    (void)tiku_msg_add_string(m, "current", app->current_device);
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        (void)tiku_remote_tell(&app->remote, i, m);
    }
    tiku_msg_free(m);
#endif

}

/**
 * @brief Send the desk's clipboard to each session that has not had its
 *        current version: after a change of the clipboard's stamp, or when
 *        a session joins while text is held.
 *
 * The clipboard does not record who changed it, so its stamp is checked
 * once a turn.  A session whose output is full is told on a later turn.
 */
void
clip_mirror(desktop_t *app)
{
    static uint64_t heard[TIKU_REMOTE_SESSIONS];    /* who was told ...   */
    static unsigned told[TIKU_REMOTE_SESSIONS];     /* ... which version  */
    unsigned now = tiku_clipboard_stamp();
    tiku_msg_t *m = NULL;
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        const tiku_remote_session_t *s = &app->remote.session[i];
        int joined = heard[i] != s->generation;

        if (!s->used || (!joined && told[i] == now)) {
            continue;
        }
        if (joined && tiku_clipboard_length() == 0u) {
            heard[i] = s->generation;   /* nothing held: nothing to say */
            told[i] = now;
            continue;
        }
        if (m == NULL && (m = tiku_msg_new(TIKU_MSG_CLIP)) != NULL) {
            if (!tiku_msg_add_string(m, "text", tiku_clipboard_text())) {
                tiku_msg_free(m);
                m = NULL;
            }
        }
        if (m != NULL && tiku_remote_tell(&app->remote, i, m)) {
            heard[i] = s->generation;
            told[i] = now;
        }
    }
    tiku_msg_free(m);
}

/**
 * @brief Forget an application's file-panel question once the panel no
 *        longer asks it (closed, or taken by another question).  The
 *        panel sends nothing on cancel, and while pending_pick.kind is set
 *        every question but a newer one from the same window is refused.
 */
void
pick_sweep(desktop_t *app)
{
    if (app->pending_pick.kind != 0 &&
        app->file_panel.target != &app->pending_pick) {
        app->pending_pick.kind = 0;
        app->pending_pick.slot = NULL;
    }
}

/** @brief Close the windows of sessions that ended since the last poll. */
void
remote_sweep(desktop_t *app)
{
    /* A window drawn by remote_window_draw() that no session owns any
     * more belongs to a session that has ended; the workspace still
     * holds it. */
    tiku_window_t *w;
    int i;

    for (i = 0; ; ) {
        w = tiku_workspace_at(app->desk, i);
        if (w == NULL) {
            break;
        }
        if (w->open && w->draw == remote_window_draw &&
            tiku_remote_owner(&app->remote, w) == NULL) {
            /* Closing compacts the workspace's order, so the next window
             * is at this index: the index does not advance. */
            tiku_workspace_close(app->desk, w);
            desktop_damage_all(app);
            continue;
        }
        i++;
    }
}

int desktop_access_perform(desktop_t *app, tiku_remote_window_t *window,
                           uint32_t node, uint64_t document,
                           uint64_t generation, unsigned action, int first,
                           int last, const char *text, int native_gesture)
{
#if TIKU_SHELL_REMOTE_SERVICES

    if (!app || !window || !window->session || !window->session->used ||
        window->session->fd<0 ||
        !window->window || !window->opened || !window->access ||
        tiku_access_document(window->access) != document ||
        tiku_access_generation(window->access) != generation)
        return 0;
    tiku_workspace_activate(app->desk, window->window);
    remote_focus(app, window->window);
    desktop_damage_all(app);
    tiku_msg_t *message = tiku_msg_new(TIKU_MSG_ACCESS_ACTION);
    int sent = message &&
               tiku_msg_add_int32(message, "window", window->win_id) &&
               tiku_msg_add_int32(message, "node", node) &&
               tiku_msg_add_int64(message, "document", document) &&
               tiku_msg_add_int64(message, "generation", generation) &&
               tiku_msg_add_int32(message, "action", action) &&
               tiku_msg_add_int32(message, "first", first) &&
               tiku_msg_add_int32(message, "last", last) &&
               (!text || tiku_msg_add_string(message, "text", text));
    if (sent) {
        if (native_gesture)
            tiku_remote_gesture(window, tiku_remote_gesture_clock());
        /* The listener/session APIs keep the window's owning channel. */
        sent = tiku_remote_session_tell(window->session, message);
    }
    tiku_msg_free(message);
    return sent;

#else
    (void)app;
    (void)window;
    (void)node;
    (void)document;
    (void)generation;
    (void)action;
    (void)first;
    (void)last;
    (void)text;
    (void)native_gesture;
    return 0;
#endif
}
