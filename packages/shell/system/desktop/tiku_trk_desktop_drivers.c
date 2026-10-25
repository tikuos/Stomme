/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_drivers.c - what an outside driver may ask of the
 * desktop: the conductor channel's injected events, pixels and named facts,
 * and the redraw check the script adapter asks for.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_watch.h"

/**
 * @brief Append the label of each row of @p menu to @p out, each followed
 *        by '|'; stops when @p out has no room left.
 */
static void
conduct_append_menu(char *out, size_t max, const tiku_menu_t *menu)
{
    int i;

    if (menu == NULL) {
        return;
    }
    for (i = 0; i < menu->count; i++) {
        size_t used = strlen(out);

        if (used + 2u >= max) {
            return;
        }
        snprintf(out + used, max - used, "%s|", menu->item[i].label);
    }
}

/** @brief The labels of whatever menu is open, or the Deskbar leaf's. */
static const char *
conduct_menu_labels(desktop_t *app)
{
    static char labels[512];

    labels[0] = '\0';
    conduct_append_menu(labels, sizeof labels, app->popup);
    conduct_append_menu(labels, sizeof labels, app->popup_sub);
    if (labels[0] == '\0') {
        /* No popup rows: the labels of the Deskbar's leaf menu, then of
         * its open submenu. */
        size_t used;

        (void)tiku_trk_deskbar_menu_labels(app->deskbar, 0, labels,
                                           sizeof labels);
        used = strlen(labels);
        (void)tiku_trk_deskbar_menu_labels(app->deskbar, 1, labels + used,
                                           sizeof labels - used);
    }
    return labels;
}

/** @brief Queue an injected event for the loop that drains the display's. */
void
conduct_inject(void *context, const tiku_event_t *event)
{
    desktop_t *app = context;
    int next = (app->cq_tail + 1) % (int)(sizeof app->cq / sizeof app->cq[0]);

    /* A full queue drops the new event; the queued ones stay in order. */
    if (next == app->cq_head) {
        return;
    }
    app->cq[app->cq_tail] = *event;
    app->cq_tail = next;
}

/**
 * @brief The pixel at @p x, @p y of the surface as last drawn, as RGB; 0
 *        off the surface.
 *
 * The loop draws at the end of a turn that owes a frame, without a script
 * at most once every 16.7 ms, so a pixel can show an injected event's
 * result a turn or more after the event is handled.  Reading a pixel
 * draws nothing.
 */
unsigned
conduct_pixel(void *context, int x, int y)
{
    desktop_t *app = context;

    if (app->surf == NULL || x < 0 || y < 0 ||
        x >= app->surf->w || y >= app->surf->h) {
        return 0u;
    }
    return app->surf->px[(size_t)y * (size_t)app->surf->w + (size_t)x] &
           0xffffffu;
}

/**
 * @brief Answer question @p name into @p out when it is about the
 *        desktop's chrome: hot, tip, strip, minimized, cursor or
 *        menu-bottom.
 * @return 1 when @p name is one of them, else 0.
 */
static int
conduct_chrome(desktop_t *app, const char *name, char *out, size_t max)
{
    if (strcmp(name, "hot") == 0) {
        /* The label of the row under the pointer in the open menu, the
         * submenu first, else in the Deskbar's menus; empty when no row
         * is hot. */
        const tiku_menu_t *m = (app->popup_sub != NULL &&
                                app->popup_sub->hot >= 0)
                                   ? app->popup_sub : app->popup;

        out[0] = '\0';
        if (m != NULL) {
            if (m->hot >= 0 && m->hot < m->count) {
                snprintf(out, max, "%s", m->item[m->hot].label);
            }
            return 1;
        }
        (void)tiku_trk_deskbar_hot_label(app->deskbar, out, max);
        return 1;
    }
    if (strcmp(name, "tip") == 0) {
#if TIKU_SHELL_BOARD_SERVICES

        /* The text floating beside the pointer: a device tray cell's
         * tip, an Info window's field pop-up or the Deskbar's tooltip,
         * and empty for none. */
        if (!desktop_devtray_tip(app, out, max) &&
            !info_hover_text(app, out, max)) {
#else

        /* The text floating beside the pointer: an Info window's
         * field pop-up or the Deskbar's tooltip,
         * and empty for none. */
        if (!info_hover_text(app, out, max)) {
#endif

            snprintf(out, max, "%s", tiku_trk_deskbar_tip(app->deskbar));
        }
        return 1;
    }
    if (strcmp(name, "strip") == 0) {
        /* Whether the global menu bar is up. */
        snprintf(out, max, "%s", (app->gbar_on == 1) ? "on" : "off");
        return 1;
    }
    if (strcmp(name, "minimized") == 0) {
        /* The windows that are minimized, by title, "a|b|" style. */
        int i;
        size_t used = 0;

        out[0] = '\0';
        for (i = 0; i < TIKU_WINDOW_MAX; i++) {
            tiku_window_t *w = tiku_workspace_at(app->desk, i);

            if (w == NULL || !w->open || !w->minimized) {
                continue;
            }
            used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                     "%s|", w->title);
            if (used >= max) {
                break;
            }
        }
        return 1;
    }
    if (strcmp(name, "cursor") == 0) {
        /* The pointer shape shown, by name. */
        static const char *const names[] = {
            "arrow", "text", "resize-h", "resize-v", "resize-diag", "hand"
        };

        snprintf(out, max, "%s", (app->cursor_kind >= 0 &&
                                  app->cursor_kind <= TIKU_CURSOR_HAND)
                                     ? names[app->cursor_kind] : "");
        return 1;
    }
    if (strcmp(name, "menu-bottom") == 0) {
        /* The y where the Deskbar's leaf menu ends. */
        snprintf(out, max, "%d", tiku_trk_deskbar_menu_bottom(app->deskbar));
        return 1;
    }
    return 0;
}

/**
 * @brief Answer the conductor channel's question @p name into @p out: a
 *        conduct_chrome() question, the trail, the longest turn, the focused
 *        window's title or every window's, the status line, the count, the
 *        open menu, the alert, the sounds, the mounts, the focused window's frame,
 *        controls or narration, the running operation, or a path the
 *        namespace serves.
 * @return 1 when answered; 0 for a name this build does not know or a path
 *         nothing serves.
 */
int
conduct_text(void *context, const char *name, char *out, size_t max)
{
    desktop_t *app = context;

    if (name == NULL || out == NULL) {
        return 0;
    }
    if (conduct_chrome(app, name, out, max)) {
        return 1;
    }
    if (strncmp(name, "trail", 5) == 0 &&
        (name[5] == '\0' || name[5] == ':')) {
        /* The trail, newest lines last; "trail:<word>" gives only the
         * lines with that word, since the ring is longer than one answer
         * holds. */
        (void)tiku_trail_find((name[5] == ':') ? name + 6 : NULL, out, max);
        return 1;
    }
        if (strcmp(name, "turns") == 0) {
        /* The longest loop turn, in ms, since this question was last
         * answered or the desktop started; answering resets it. */
        snprintf(out, max, "%ld", tiku_watch_longest_ms());
        tiku_watch_longest_reset();
        return 1;
    }
    if (strcmp(name, "layout") == 0) {
        tiku_window_t *window = tiku_workspace_focused(app->desk);
        tiku_trk_folder_window_t *folder = window ? tiku_trk_folder_lifecycle_find(&app->folder_lifecycle,window) : NULL;
        const tiku_trk_column_t *columns; int i, count; size_t used;
        if (!folder) folder = tiku_trk_desktop_view_folder(&app->desktop_view);
        if (!folder || !folder->view) { out[0] = 0; return 1; }
        count = tiku_trk_poseview_columns(folder->view,&columns);
        used = (size_t)snprintf(out,max,"%d %d %d",tiku_trk_poseview_mode(folder->view),tiku_trk_poseview_icon_size(folder->view),count);
        for (i = 0; i < count && used < max; i++) {
            int written = snprintf(out+used,max-used," %d %d",columns[i].field,columns[i].width);
            if (written < 0 || (size_t)written >= max-used) break;
            used += (size_t)written;
        }
        return 1;
    }
    if (strcmp(name, "frame") == 0) {
        tiku_window_t *w = tiku_workspace_focused(app->desk);
        if (w) snprintf(out, max, "%d %d %d %d", w->frame.x, w->frame.y, w->frame.w, w->frame.h);
        else out[0] = 0;
        return 1;
    }
    if (strcmp(name, "title") == 0) {
        tiku_window_t *w = tiku_workspace_focused(app->desk);

        snprintf(out, max, "%s", (w != NULL) ? w->title : "");
        return 1;
    }
    if (strcmp(name, "titles") == 0) {
        /* Every open window's title, as "a|b|"; a window that has gone
         * is not in it. */
        int i;
        size_t used = 0u;

        out[0] = '\0';
        for (i = 0; i < TIKU_WINDOW_MAX; i++) {
            tiku_window_t *w = tiku_workspace_at(app->desk, i);

            if (w == NULL || !w->open) {
                continue;
            }
            used += (size_t)snprintf(out + used, (used < max) ? max - used : 0u,
                                     "%s|", w->title);
            if (used >= max) {
                break;
            }
        }
        return 1;
    }
    if (strcmp(name, "status") == 0) {
        snprintf(out, max, "%s", app->status);
        return 1;
    }
    if (strcmp(name, "count") == 0) {
        snprintf(out, max, "%s", app->count_text);
#if TIKU_SHELL_REMOTE_SERVICES

        return 1;
    }
    if (!strncmp(name, "menu-point:", 11)) {
        int x, y;
        if (tiku_trk_deskbar_menu_point(app->deskbar, name + 11, &x, &y))
            snprintf(out, max, "%d %d", x, y);
        else if (max)
            out[0] = 0;
#endif

        return 1;
    }
    if (strcmp(name, "menu") == 0) {
        /* Whatever menu is open, as its labels: the popup the shell is
         * showing, or the global bar's dropped one. */
        snprintf(out, max, "%s", conduct_menu_labels(app));
        return 1;
    }
    if (strcmp(name, "alert") == 0) {
        /* The open alert's text, or empty; watched as ~alert, it reports
         * an alert appearing. */
        tiku_alert_t *alert = alert_model(app);

        snprintf(out, max, "%s", alert->open ? alert->text : "");
        return 1;
    }
    if (strcmp(name, "sounds") == 0) {
        size_t used = 0;
        int i;

        out[0] = '\0';
        /* name:count:on|off for every moment, then the volume; the
         * counts and flags work with no sound card. */
        for (i = 0; i < TIKU_SOUND_EVENTS && used < max; i++) {
            used += (size_t)snprintf(out + used, max - used, "%s%s:%u:%s",
                                     i ? " " : "",
                                     tiku_sound_name((tiku_sound_event_t)i),
                                     tiku_sound_count((tiku_sound_event_t)i),
                                     tiku_sound_enabled((tiku_sound_event_t)i)
                                         ? "on" : "off");
        }
        if (used < max) {
            snprintf(out + used, max - used, " volume:%d",
                     tiku_sound_volume());
        }
        return 1;
    }
    if (strcmp(name, "mounts") == 0) {
        /* Every mounted volume by name; watched as ~mounts, it reports a
         * mount arriving. */
        tiku_volumes_t vs;
        size_t used = 0u;
        int i;

        out[0] = '\0';
        (void)tiku_volumes_scan(&vs);
        for (i = 0; i < vs.n; i++) {
            used += (size_t)snprintf(out + used,
                                     (used < max) ? max - used : 0u,
                                     "%s|", vs.v[i].name);
            if (used >= max) {
                break;
            }
        }
        return 1;
    }
    if (strcmp(name, "controls") == 0) {
        /* The focused window's controls, a line each of kind, label,
         * rectangle and state, for a driver that clicks them. */
        (void)tiku_workspace_controls(app->desk,
                                           tiku_workspace_focused(app->desk),
                                           out, max);
        return 1;
    }
    if (strcmp(name, "operation") == 0) {
        /* "idle", or each running job's caption, current item and
         * count; watched as ~operation, it reports a job finishing. */
        (void)tiku_trk_progress_say(progress_model(app), out, max);
        return 1;
    }
    if (name[0] == '/') {
        /* A path: a node's value or a container's children, from any
         * mount, a board's under /devices included; ~/path watches
         * it. */
        return tiku_backend_text(app->backend, name, out, max);
    }
    if (strcmp(name, "narrate") == 0) {
        /* The focused window described from its display list; empty
         * with no focused window, as "title" is. */
        (void)tiku_workspace_narrate(app->desk,
                                          tiku_workspace_focused(app->desk),
                                          out, max);
        return 1;
    }
    return 0;                   /* a name this build cannot say */
}

/**
 * @brief Redraw the desktop from nothing and count the pixels that differ
 *        from the frame on screen (PVL-045).  The copy is taken before
 *        anything is drawn, and the redrawn frame stays on screen.
 * @return that count; -1 with no surface or no memory for the copy.
 */
long
script_redraw_diff(void *ctx)
{
    desktop_t *app = ctx;
    tiku_rgb_t *was;
    size_t n, i;
    long differ = 0;

    if (app == NULL || app->surf == NULL || app->surf->px == NULL) {
        return -1;
    }
    n = (size_t)app->surf->w * (size_t)app->surf->h;
    if (app->surf->scale > 1) {
        n *= (size_t)app->surf->scale * (size_t)app->surf->scale;
    }
    was = malloc(n * sizeof *was);
    if (was == NULL) {
        return -1;
    }
    memcpy(was, app->surf->px, n * sizeof *was);
    for (i = 0u; i < (size_t)app->folder_count; i++) {
        tiku_trk_folder_window_forget(&app->folders[i]);
    }
    /* The redraw rebuilds every folder window's content from nothing. */
    desktop_damage_all(app);
    draw_all(app);
    for (i = 0u; i < n; i++) {
        if (was[i] != app->surf->px[i]) {
            differ++;
        }
    }
    free(was);
    return differ;
}
