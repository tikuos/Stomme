/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_cursor.c - the pointer's shape: what the desktop
 * offers under it, and what a window's content asks for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"

/**
 * @brief Choose the pointer's shape for what is under @p x, @p y: the
 *        diagonal resize over the focused window's resize corner, the shape
 *        a window's content asked for (focused or not), else the arrow.
 *
 * During a drag or with a menu open, the arrow.  The shape is kept in
 * app->cursor_kind with no host too, for a scripted run to read.
 */
void
desktop_pointer_says(desktop_t *app, int x, int y)
{
    tiku_window_t *under = NULL;
    int want = TIKU_CURSOR_ARROW, hit;

    if (app->resize_track != NULL) {
        /* While a corner is held the shape stays the diagonal resize: a
         * window at its smallest size leaves the pointer inside it, still
         * resizing. */
        want = TIKU_CURSOR_RESIZE_DIAG;
    } else if (!tiku_trk_drag_active(&app->drag) && app->popup == NULL) {
        hit = tiku_workspace_hit(app->desk, x, y, &under);
        if (hit == TIKU_HIT_RESIZE && under != NULL &&
            under == tiku_workspace_focused(app->desk) && under->resizable) {
            want = TIKU_CURSOR_RESIZE_DIAG;
        } else if (hit == TIKU_HIT_CONTENT && under != NULL) {
            want = under->cursor;
        }
    }
    if (want != app->cursor_kind) {
        app->cursor_kind = want;
        if (app->host != NULL) {
            tiku_host_set_cursor(app->host, want);
        }
    }
}

/**
 * @brief Apply session @p session's TIKU_MSG_CURSOR: set the shape one of
 *        its windows' content asks for, and update the pointer at once.
 */
void
desktop_cursor(desktop_t *app, int session, const tiku_msg_t *msg)
{
    tiku_remote_session_t *s;
    int32_t id = 0, shape = 0;
    int k;

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used ||
        !tiku_msg_find_int32(msg, "window", 0, &id) ||
        !tiku_msg_find_int32(msg, "shape", 0, &shape) ||
        shape < TIKU_CURSOR_ARROW || shape > TIKU_CURSOR_HAND) {
        return;
    }
    s = &app->remote.session[session];
    for (k = 0; k < TIKU_REMOTE_WINDOWS; k++) {
        if (s->win[k].window != NULL && s->win[k].win_id == (uint32_t)id) {
            s->win[k].window->cursor = shape;
            desktop_pointer_says(app, app->ptr_x, app->ptr_y);
            return;
        }
    }
}
#if TIKU_SHELL_REMOTE_SERVICES


/** @brief Apply native caret geometry from the focused session window. */
void desktop_text_rect(desktop_t *app, int session, const tiku_msg_t *message)
{
    int32_t id, x, y, width, height;
    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used ||
        !tiku_msg_find_int32(message, "window", 0, &id) ||
        !tiku_msg_find_int32(message, "x", 0, &x) ||
        !tiku_msg_find_int32(message, "y", 0, &y) ||
        !tiku_msg_find_int32(message, "width", 0, &width) ||
        !tiku_msg_find_int32(message, "height", 0, &height) || width < 0 ||
        height < 0 || width > 8192 || height > 8192 || x < -8192 || x > 8192 ||
        y < -8192 || y > 8192)
        return;
    tiku_window_t *focused = tiku_workspace_focused(app->desk);
    for (size_t i = 0; i < TIKU_REMOTE_WINDOWS; i++) {
        tiku_remote_window_t *w = app->remote.session[session].win + i;
        if (w->window != focused || w->win_id != (uint32_t)id || !focused)
            continue;
        tiku_rect_t content = tiku_window_content(focused);
        tiku_host_text_rect(
            app->host,
            (tiku_rect_t){content.x + x, content.y + y, width, height});
        return;
    }
}
#endif
