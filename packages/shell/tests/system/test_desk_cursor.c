/* The pointer's shape: over a window's content, the shape its application
 * asked for; on the front window's corner grip, the resize; on a tab, the
 * desk or under an open menu, the arrow.  A request names one of its own
 * session's windows and a shape there is, or it is let go.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_cursor.c"
#include "tiku_app.h"

/** @brief Session @p i, an application holding window @p w as @p id. */
static void hold(desktop_t *app, int i, tiku_window_t *w, uint32_t id)
{
    tiku_remote_session_t *s = &app->remote.session[i];

    s->used = 1;
    s->win[0].window = w;
    s->win[0].win_id = id;
    s->win[0].opened = 1;
}

/** @brief Session @p session asking for @p shape over window @p id. */
static void ask(desktop_t *app, int session, uint32_t id, int shape)
{
    tiku_msg_t *m = tiku_app_cursor_message(id, shape);

    assert(m);
    desktop_cursor(app, session, m);
    tiku_msg_free(m);
}

/** @brief The shape shown with the pointer at @p x, @p y. */
static int at(desktop_t *app, int x, int y)
{
    app->ptr_x = x;
    app->ptr_y = y;
    desktop_pointer_says(app, x, y);
    return app->cursor_kind;
}

int main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    tiku_window_t *back, *front;
    tiku_rect_t c;
    int cx, cy;
    tiku_menu_t *menu;

    assert(app);
    app->surf = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    back = tiku_workspace_open(app->desk, "Back",
                               (tiku_rect_t){ 40, 60, 300, 200 }, NULL,
                               NULL, NULL);
    front = tiku_workspace_open(app->desk, "Edit",
                                (tiku_rect_t){ 400, 60, 300, 200 }, NULL,
                                NULL, NULL);
    assert(back && front);
    front->resizable = 1;
    tiku_workspace_activate(app->desk, front);
    hold(app, 0, front, 7);
    hold(app, 1, back, 3);
    c = tiku_window_content(front);
    cx = c.x + c.w / 2;
    cy = c.y + c.h / 2;

    assert(at(app, cx, cy) == TIKU_CURSOR_ARROW);
    ask(app, 0, 7, TIKU_CURSOR_HAND);
    assert(app->cursor_kind == TIKU_CURSOR_HAND);  /* shown at once */
    assert(at(app, 980, 600) == TIKU_CURSOR_ARROW); /* the desk */
    assert(at(app, cx, front->frame.y + 4) == TIKU_CURSOR_ARROW); /* tab */
    assert(at(app, cx, cy) == TIKU_CURSOR_HAND);
    assert(at(app, c.x + c.w - 3, c.y + c.h - 3) ==
           TIKU_CURSOR_RESIZE_DIAG);                /* the grip wins */

    /* Another window's content says its own, the arrow until asked. */
    c = tiku_window_content(back);
    assert(at(app, c.x + 20, c.y + 20) == TIKU_CURSOR_ARROW);
    ask(app, 1, 3, TIKU_CURSOR_TEXT);
    assert(app->cursor_kind == TIKU_CURSOR_TEXT);

    /* What is let go: a window of another session, a session gone, a
     * shape there is not, a message with no window. */
    ask(app, 0, 3, TIKU_CURSOR_HAND);
    ask(app, 5, 3, TIKU_CURSOR_HAND);
    ask(app, 1, 3, TIKU_CURSOR_HAND + 1);
    ask(app, 1, 3, -1);
    {
        tiku_msg_t *m = tiku_msg_new(TIKU_MSG_CURSOR);

        assert(m && tiku_msg_add_int32(m, "shape", TIKU_CURSOR_HAND));
        desktop_cursor(app, 1, m);
        tiku_msg_free(m);
    }
    assert(back->cursor == TIKU_CURSOR_TEXT &&
           at(app, c.x + 20, c.y + 20) == TIKU_CURSOR_TEXT);

    /* A menu open over the desk keeps the arrow everywhere. */
    menu = calloc(1, sizeof *menu);
    assert(menu);
    app->popup = menu;
    assert(at(app, c.x + 20, c.y + 20) == TIKU_CURSOR_ARROW);
    app->popup = NULL;
    free(menu);
    assert(at(app, c.x + 20, c.y + 20) == TIKU_CURSOR_TEXT);

    /* A window opened again in the same place starts with the arrow. */
    tiku_workspace_close(app->desk, back);
    back = tiku_workspace_open(app->desk, "Again",
                               (tiku_rect_t){ 40, 60, 300, 200 }, NULL,
                               NULL, NULL);
    assert(back && back->cursor == TIKU_CURSOR_ARROW);

    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    puts("PASS: pointer shapes -- a window's content shows what its "
         "application asked for, the grip resizes, tabs, the desk and open "
         "menus keep the arrow, and stray requests are let go");
    return 0;
}
