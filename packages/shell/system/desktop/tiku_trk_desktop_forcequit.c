/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_forcequit.c - the Force Quit window: the list of
 * connected applications, where one whose process the desktop holds a
 * handle on can be ended after a second confirmation; and a pick mode that
 * chooses an application, or one of the desktop's own windows, by a click
 * on a window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dialog.h"
#include "tiku_text_layout_paint.h"
#include "tiku_trk_desktop_private.h"
#include <errno.h>
#include <unistd.h>

enum { FQ_PICK = 100, FQ_REFRESH, FQ_CLOSE, FQ_KILL, FQ_DONE };
struct fq_row {
    char name[80];
    int handle, session, windows;
    long pid;
};
struct desktop_forcequit {
    desktop_t *app;
    tiku_window_t *window, *builtin;
    void *builtin_context;
    char builtin_title[TIKU_WINDOW_TITLE_MAX];
    tiku_dialog_t dialog;
    struct fq_row rows[TIKU_REMOTE_SESSIONS];
    int count, selected, confirm, picking, swallow_up;
    char status[180];
};
typedef struct {
    tiku_rect_t list, detail, status, pick, refresh, close, kill, done;
} fq_layout_t;

/** @brief One geometry for drawing and hit testing, scaled to the font. */
static fq_layout_t fq_layout(tiku_rect_t c)
{
    fq_layout_t l;
    int bh = tiku_ui_button_h(), row = tiku_font_plain()->height + 12;
    l.list = (tiku_rect_t){c.x + 16, c.y + 68, c.w - 32,
                           TIKU_REMOTE_SESSIONS * row + 8};
    l.detail = (tiku_rect_t){c.x + 16, l.list.y + l.list.h + 10,
                             c.w - 32, row};
    l.status = l.detail; l.status.y += row;
    l.pick = (tiku_rect_t){c.x + 16, l.status.y + row + 8, 142, bh};
    l.refresh = l.pick; l.refresh.x += 152; l.refresh.w = 84;
    l.close = l.pick; l.close.y += bh + 12; l.close.w = 130;
    l.done = l.close; l.done.x = c.x + c.w - 100; l.done.w = 84;
    l.kill = l.close; l.kill.x = l.done.x - 140; l.kill.w = 130;
    return l;
}

/** @brief Row @p i of the list. */
static tiku_rect_t fq_row_rect(tiku_rect_t list, int i)
{
    int row = tiku_font_plain()->height + 12;
    return (tiku_rect_t){list.x + 4, list.y + 4 + i * row,
                          list.w - 8, row};
}

/**
 * @brief Close every process handle held, and clear the list, the
 *        selection, the confirmation and the picked window.
 */
static void fq_release(struct desktop_forcequit *p)
{
    for (int i = 0; i < p->count; i++)
        if (p->rows[i].handle >= 0) close(p->rows[i].handle);
    p->count = 0; p->selected = -1; p->confirm = 0; p->builtin = NULL;
}

/**
 * @brief List the connected sessions, taking a handle on each one's process
 *        before its name is shown, and count its open windows.
 */
static void fq_refresh(struct desktop_forcequit *p)
{
    fq_release(p);
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &p->app->remote.session[i];
        if (!s->used) continue;
        struct fq_row *r = &p->rows[p->count++];
        memset(r, 0, sizeof *r);
        r->session = i;
        r->handle = tiku_remote_process_handle(s, &r->pid);
        snprintf(r->name, sizeof r->name, "%s",
                 s->name[0] ? s->name : "Connecting application");
        for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++)
            if (s->win[j].window && s->win[j].window->open) r->windows++;
    }
    snprintf(p->status, sizeof p->status,
             "Select an application, or choose one of its windows.");
}

/**
 * @brief Whether the picked desktop window is still open as it was picked
 *        (same object, context and title, no session's), so it can be
 *        passed to close_window().
 */
static int fq_builtin_valid(struct desktop_forcequit *p)
{
    for (int i = 0; p->builtin && i < tiku_workspace_count(p->app->desk); i++) {
        tiku_window_t *w = tiku_workspace_at(p->app->desk, i);
        if (w == p->builtin && w->open && w->context == p->builtin_context &&
            !strcmp(w->title, p->builtin_title) &&
            !tiku_remote_owner(&p->app->remote, w)) return 1;
    }
    return 0;
}

/**
 * @brief Register the controls: Force Quit is enabled only for a process
 *        the desktop holds a handle on, and Done is the default.
 */
static void fq_controls(struct desktop_forcequit *p, fq_layout_t l)
{
    tiku_dialog_begin(&p->dialog, FQ_DONE, FQ_DONE);
    for (int i = 0; i < p->count; i++)
        tiku_dialog_add(&p->dialog, i, fq_row_rect(l.list, i),
                        TIKU_DIALOG_TOGGLE, !p->confirm);
    tiku_dialog_add(&p->dialog, FQ_PICK, l.pick,
                    TIKU_DIALOG_BUTTON, !p->confirm);
    tiku_dialog_add(&p->dialog, FQ_REFRESH, l.refresh,
                    TIKU_DIALOG_BUTTON, !p->confirm);
    tiku_dialog_add(&p->dialog, FQ_CLOSE, l.close,
                    TIKU_DIALOG_BUTTON, fq_builtin_valid(p));
    tiku_dialog_add(&p->dialog, FQ_KILL, l.kill, TIKU_DIALOG_BUTTON,
                    p->selected >= 0 &&
                    p->rows[p->selected].handle >= 0);
    tiku_dialog_add(&p->dialog, FQ_DONE, l.done, TIKU_DIALOG_BUTTON, 1);
    tiku_dialog_end(&p->dialog);
}

/** @brief A control's drawing flags: focused, pressed, disabled. */
static unsigned fq_flags(struct desktop_forcequit *p, int id)
{
    unsigned f = tiku_dialog_focused(&p->dialog, id) ? TIKU_S_FOCUS : 0;
    if (tiku_dialog_pressed(&p->dialog, id)) f |= TIKU_S_PRESSED;
    for (int i = 0; i < p->dialog.count; i++)
        if (p->dialog.control[i].id == id && !p->dialog.control[i].enabled)
            f |= TIKU_S_DISABLED;
    return f;
}

/** @brief Draw one line of text, clipped to @p r: names come from the
 *         applications. */
static void fq_line(tiku_surface_t *s, tiku_rect_t r, const char *text)
{
    tiku_text_line_draw(s, r, TIKU_MEASURE_PLAIN, text, TIKU_C_TEXT);
}

/**
 * @brief Draw the window: the warning, the application list, the
 *        selection's details, the status line and the buttons.
 */
static void fq_draw(tiku_window_t *w, tiku_surface_t *s,
                    tiku_rect_t c, void *ctx)
{
    struct desktop_forcequit *p = ctx;
    fq_layout_t l = fq_layout(c);
    char text[180];
    (void)w;
    fq_controls(p, l);
    tiku_fill(s, c, TIKU_C_PANEL);
    tiku_text(s, tiku_font_bold(), c.x + 16,
              c.y + 12 + tiku_font_bold()->ascent, "Force Quit", TIKU_C_TEXT);
    fq_line(s, (tiku_rect_t){c.x + 16, c.y + 36, c.w - 32, 24},
            "Use when an app stops responding. Unsaved changes may be lost.");
    tiku_ui_sunken(s, l.list, TIKU_C_DOC);
    for (int i = 0; i < p->count; i++) {
        tiku_rect_t r = fq_row_rect(l.list, i);
        snprintf(text, sizeof text, "%s  —  %d window%s%s", p->rows[i].name,
            p->rows[i].windows, p->rows[i].windows == 1 ? "" : "s",
            p->rows[i].handle < 0 ? "  (protected / remote)" : "");
        if (i == p->selected) tiku_fill(s, r, TIKU_C_SELECT);
        tiku_text_line_draw(s, tiku_inset(r, 4), TIKU_MEASURE_PLAIN, text,
            i == p->selected ? TIKU_C_SELTEXT : TIKU_C_TEXT);
        if (tiku_dialog_focused(&p->dialog, i))
            tiku_frame(s, tiku_inset(r, 1), TIKU_C_FOCUS);
    }
    if (!p->count) fq_line(s, fq_row_rect(l.list, 0),
                           "No connected applications. Choose a window below.");
    if (p->selected >= 0 && p->rows[p->selected].handle >= 0)
        snprintf(text, sizeof text, "%s — PID %ld. All its windows will close.",
            p->rows[p->selected].name, p->rows[p->selected].pid);
    else snprintf(text, sizeof text, "%s", p->builtin ? p->builtin_title :
                  "The desktop and device-side processes are protected.");
    fq_line(s, l.detail, text);
    fq_line(s, l.status, p->status);
    tiku_ui_button(s, l.pick, "Choose window…", fq_flags(p, FQ_PICK));
    tiku_ui_button(s, l.refresh, "Refresh", fq_flags(p, FQ_REFRESH));
    tiku_ui_button(s, l.close, "Close Window", fq_flags(p, FQ_CLOSE));
    tiku_ui_button(s, l.kill, p->confirm ? "Force Quit" : "Force Quit…",
                    fq_flags(p, FQ_KILL));
    tiku_ui_button(s, l.done, p->confirm ? "Cancel" : "Done",
                    TIKU_S_DEFAULT | fq_flags(p, FQ_DONE));
}

/**
 * @brief Force Quit the selected application: the first activation asks
 *        for confirmation and makes Cancel the default; the second signals
 *        the process through its handle.
 */
static void fq_kill(struct desktop_forcequit *p)
{
    if (p->selected < 0 || p->rows[p->selected].handle < 0) return;
    if (!p->confirm) {
        p->confirm = 1;
        p->dialog.focus = FQ_DONE;
        snprintf(p->status, sizeof p->status,
                 "Force quit this application? Unsaved changes will be lost.");
        return;
    }
    int rc = tiku_remote_process_kill(p->rows[p->selected].handle);
    int error = errno;
    if (!rc) tiku_trk_launch_expect_stop((pid_t)p->rows[p->selected].pid);
    fq_release(p);
    snprintf(p->status, sizeof p->status, "%s", !rc ?
        "Application terminated. Refresh to update the list." :
        error == ESRCH ? "The application has already exited." :
        "Could not terminate the application. No other process was signalled.");
}

/**
 * @brief The window's input: its controls, by pointer or keyboard, and
 *        Cmd-W or Cmd-Q as Done.
 */
static int fq_event(tiku_window_t *w, const tiku_event_t *ev, void *ctx)
{
    struct desktop_forcequit *p = ctx;
    fq_controls(p, fq_layout(tiku_window_content(w)));
    int id = tiku_dialog_event(&p->dialog, ev).activated;
    if (ev->type == TIKU_EVENT_KEY_DOWN && (ev->modifiers & TIKU_MOD_CMD) &&
        (ev->key == 'w' || ev->key == 'q')) id = FQ_DONE;
    if (id >= 0 && id < p->count && !p->confirm) {
        p->selected = id; p->builtin = NULL;
        snprintf(p->status, sizeof p->status, "%s",
            p->rows[id].handle >= 0 ? "Force Quit stops the entire application."
            : "This peer cannot be safely terminated from this desktop.");
    }
    if (id == FQ_REFRESH && !p->confirm) fq_refresh(p);
    if (id == FQ_PICK && !p->confirm) {
        p->picking = 1; p->window->minimized = 1;
    }
    if (id == FQ_CLOSE && fq_builtin_valid(p)) {
        close_window(p->app, p->builtin); p->builtin = NULL;
        snprintf(p->status, sizeof p->status, "Normal window close requested.");
    }
    if (id == FQ_KILL) fq_kill(p);
    if (id == FQ_DONE) {
        if (p->confirm) {
            p->confirm = 0;
            snprintf(p->status, sizeof p->status, "Force Quit cancelled.");
        } else desktop_forcequit_close(p->app);
    }
    desktop_damage_all(p->app);
    return 1;
}

/** @brief The Force Quit window, or NULL. */
tiku_window_t *desktop_forcequit_handle(desktop_t *app)
{
    return app->forcequit ? app->forcequit->window : NULL;
}

/**
 * @brief Called when the workspace destroys the window, by any close:
 *        release the handles and the press capture.
 */
static void fq_destroyed(tiku_window_t *w, void *ctx)
{
    struct desktop_forcequit *p = ctx;
    if (p->app->prefs_track == w) p->app->prefs_track = NULL;
    p->window = NULL; p->picking = p->swallow_up = 0;
    fq_release(p);
}

/** @brief Dismiss without signalling any application. */
void desktop_forcequit_close(desktop_t *app)
{
    struct desktop_forcequit *p = app->forcequit;
    if (!p) return;
    if (app->prefs_track == p->window) app->prefs_track = NULL;
    if (p->window && p->window->open)
        tiku_workspace_close(app->desk, p->window);
    p->window = NULL; p->picking = 0; p->swallow_up = 0;
    fq_release(p); desktop_damage_all(app);
}

/**
 * @brief Open the window on a fresh list of applications, or raise it when
 *        it is open, ending a pick.
 */
void desktop_forcequit_open(desktop_t *app)
{
    if (!app->forcequit) {
        app->forcequit = calloc(1, sizeof *app->forcequit);
        if (!app->forcequit) return;
        app->forcequit->app = app;
        app->forcequit->selected = -1;
    }
    struct desktop_forcequit *p = app->forcequit;
    if (p->window && p->window->open) {
        p->picking = 0; p->window->minimized = 0;
        tiku_workspace_activate(app->desk, p->window);
        desktop_damage_all(app); return;
    }
    fq_refresh(p); tiku_dialog_init(&p->dialog);
    tiku_rect_t r = {0, 0, 620, 0};
    fq_layout_t l = fq_layout(r);
    r.h = l.done.y + l.done.h + 44;
    r.x = (app->surf->w - r.w) / 2;
    r.y = (app->surf->h - r.h) / 3;
    p->window = tiku_workspace_open(app->desk, "Force Quit", r,
                                    fq_draw, p, NULL);
    if (p->window) {
        tiku_window_set_size_controls(p->window, 0, 0);
        tiku_window_set_handlers(p->window, fq_event, NULL, fq_destroyed);
    } else fq_release(p);
    desktop_damage_all(app);
}

/** @brief At shutdown: close the window and drop the handles; no
 *         application is signalled. */
void desktop_forcequit_destroy(desktop_t *app)
{
    desktop_forcequit_close(app);
    free(app->forcequit); app->forcequit = NULL;
}

/**
 * @brief End a pick on the window under the press: its application is
 *        selected, or a desktop window is held for Close Window.  The press
 *        reaches no window.
 */
static void fq_choose(struct desktop_forcequit *p, const tiku_event_t *ev)
{
    tiku_window_t *w = NULL;
    (void)tiku_workspace_hit(p->app->desk, ev->x, ev->y, &w);
    fq_refresh(p);
    if (w && w != p->window) {
        tiku_remote_session_t *s = tiku_remote_owner(&p->app->remote, w);
        if (s) {
            for (int i = 0; i < p->count; i++)
                if (&p->app->remote.session[p->rows[i].session] == s)
                    p->selected = i;
        } else if (!w->plain) {
            p->builtin = w; p->builtin_context = w->context;
            snprintf(p->builtin_title, sizeof p->builtin_title, "%s", w->title);
            snprintf(p->status, sizeof p->status,
                "Desktop-owned window: use Close Window; the desktop is safe.");
        }
    }
    p->picking = 0; p->swallow_up = 1; p->window->minimized = 0;
    tiku_workspace_activate(p->app->desk, p->window);
    desktop_damage_all(p->app);
}

/**
 * @brief Cmd-Option-K (Ctrl+Alt+K under X11) opens the window from
 *        anywhere; in pick mode, a click picks, and Escape or a secondary
 *        click cancels.
 * @return 1 when the event was taken.
 */
int desktop_forcequit_input(desktop_t *app, const tiku_event_t *ev)
{
    struct desktop_forcequit *p = app->forcequit;
    if (ev->type == TIKU_EVENT_KEY_DOWN &&
        (ev->key == 'k' || ev->key == 'K') &&
        (ev->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) ==
        (TIKU_MOD_CMD | TIKU_MOD_OPTION)) {
        desktop_forcequit_open(app); return 1;
    }
    if (!p) return 0;
    if (p->swallow_up && ev->type == TIKU_EVENT_POINTER_UP) {
        p->swallow_up = 0; return 1;
    }
    if (!p->picking) return 0;
    if ((ev->type == TIKU_EVENT_KEY_DOWN && ev->key == TIKU_KEY_ESCAPE) ||
        (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 3)) {
        desktop_forcequit_open(app);
        p->swallow_up = ev->type == TIKU_EVENT_POINTER_DOWN;
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1)
        fq_choose(p, ev);
    return 1;
}

/** @brief Whether the picker is up, taking every event until it is done. */
int desktop_forcequit_picking(desktop_t *app)
{
    return app->forcequit != NULL && app->forcequit->picking;
}

/** @brief Draw the pick mode's banner near the bottom of the screen. */
void desktop_forcequit_overlay(desktop_t *app)
{
    if (!app->forcequit || !app->forcequit->picking) return;
    tiku_rect_t r = {100, app->surf->h - 60, app->surf->w - 200, 40};
    tiku_ui_sunken(app->surf, r, TIKU_C_PANEL);
    fq_line(app->surf, tiku_inset(r, 10),
        "Choose a window for Force Quit — click to select, Escape to cancel.");
}
