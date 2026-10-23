/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_folders.c - a folder window's content as a responder,
 * the desk's own folder included: the icon in its bar, the bar's titles,
 * the scrollbar, the title strip, the rows and the band between them, the
 * drags they begin and the drops that end them, and the folder's keys.
 *
 * Window frames belong to the windows responder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief The folder under the point when the press is its content's:
 *         a folder window's content, or the desk when no window is. */
static folder_t *
folder_under(desktop_t *app, int x, int y, tiku_window_t **win)
{
    tiku_window_t *w = NULL;
    tiku_hit_t hit = tiku_workspace_hit(app->desk, x, y, &w);

    *win = w;
    if (tiku_trk_find_window_popup_open(&app->find_window)) {
        return NULL;            /* the Find window's popup owns it */
    }
    if (w == NULL) {
        return desktop_folder(app);
    }
    return (hit == TIKU_HIT_CONTENT) ? folder_of(app, w) : NULL;
}

/** @brief Whether @p x, @p y is on a folder's content, the desk's
 *         included. */
static int
folders_hit(void *self, int x, int y)
{
    desktop_t *app = self;
    tiku_window_t *w;
    folder_t *fl = folder_under(app, x, y, &w);

    return fl != NULL && fl->view != NULL;
}

/** @brief The modifiers of a press, in the pose view's own terms. */
static unsigned
pose_mods(const tiku_event_t *ev)
{
    unsigned mods = 0;

    if (ev->modifiers & TIKU_MOD_SHIFT) { mods |= TIKU_TRK_MOD_SHIFT; }
    if (ev->modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }
    return mods;
}

/** @brief Remember a press on a pose for the release and the long press. */
static void
press_note(desktop_t *app, folder_t *fl, tiku_window_t *w, int row,
           const tiku_event_t *ev, int64_t now)
{
    app->press.at = now;
    app->press.x = ev->x;
    app->press.y = ev->y;
    app->press.row = row;
    app->press.folder = fl;
    app->press.window = w;
    app->press.focus = tiku_workspace_focused(app->desk);
    app->press.down = 1;
    app->press.menu = 0;
}

/**
 * @brief Mark the drag as coming from a merged view (a virtual directory)
 *        when the folder is one: its drop links by default (FS-078).
 */
static void
drag_source_kind(desktop_t *app, folder_t *fl)
{
    tiku_model_t source;

    tiku_trk_drag_set_source_virtual(&app->drag,
        app->backend->ops->stat(app->backend, fl->path, &source) == 0 &&
        source.kind == TIKU_KIND_VIRTUAL_DIR);
}

/** @brief Every selected pose joins the drag, where it sat (PVS-045). */
static void
drag_add_selection(desktop_t *app, folder_t *fl)
{
    int k, kn = tiku_trk_poseview_count(fl->view);
    int lst = (tiku_trk_poseview_mode(fl->view) == TIKU_TRK_MODE_LIST);

    for (k = 0; k < kn; k++) {
        tiku_trk_pose_t *q = tiku_trk_poseview_at(fl->view, k);
        int qx = 0, qy = 0, qw, qh;

        if (q == NULL || !q->selected) {
            continue;
        }
        if (!lst) {
            tiku_trk_poseview_pose_rect(fl->view, q, &qx, &qy, &qw, &qh);
        }
        (void)tiku_trk_drag_add_at(&app->drag, q->model.path, q->model.type,
                                   qx, qy, lst);
    }
}

/**
 * @brief A press on the folder's own icon in its bar picks the folder up;
 *        only a press on an opaque pixel of the icon counts (IV-057).
 * @return 1 when the press was on the icon.
 */
static int
icon_press(desktop_t *app, folder_t *bfl, tiku_window_t *w,
           const tiku_event_t *ev)
{
    tiku_rect_t fi = tiku_trk_folder_window_icon(bfl);
    tiku_model_t model;
    char parent[TIKU_PATH_MAX];
    char art[TIKU_NAME_MAX];

    if (ev->x < fi.x || ev->x >= fi.x + fi.w || ev->y < fi.y ||
        ev->y >= fi.y + fi.h) {
        return 0;
    }
    art[0] = '\0';
    if (app->backend->ops->stat(app->backend, bfl->path, &model) == 0) {
        (void)tiku_trk_nodeicon_resolve(&app->nicons, app->store, &model,
                                        art, sizeof art);
    }
    if (art[0] == '\0' ||
        !tiku_icon_hit(art, fi.w, ev->x - fi.x, ev->y - fi.y)) {
        return 0;
    }
    parent_path(bfl->path, parent, sizeof parent);
    tiku_workspace_activate(app->desk, w);
    press_note(app, bfl, NULL, -1, ev, tiku_trk_now());
    tiku_trk_drag_arm(&app->drag, ev->x, ev->y, ev->button, 0u, 1, w,
                      parent);
    (void)tiku_trk_drag_add_at(&app->drag, bfl->path, model.type, fi.x, fi.y,
                               1);
    tiku_trk_drag_target_reset(&app->drag_target);
    return 1;
}

/**
 * @brief A press on a title of the folder's menu bar opens that menu.
 * @return 1 when the press was on a title.
 */
static int
bar_press(desktop_t *app, folder_t *bfl, tiku_window_t *w,
          const tiku_event_t *ev)
{
    tiku_rect_t bf = tiku_trk_folder_window_bar(bfl);
    tiku_trk_sc_ctx_t bc;
    int bi;

    build_open_with(app, bfl);
    build_per_type(app, bfl);
    bc = menu_ctx(app, bfl, ev->modifiers);
    tiku_trk_cwmenu_rebuild(&app->menus, &bc);
    bi = tiku_menubar_title_at(&app->menus.bar, bf, ev->x, ev->y);
    if (bi < 0) {
        return 0;
    }
    commit_rename(app, 1);
    tiku_workspace_activate(app->desk, w);
    app->bar_fl = bfl;
    app->bar_open = bi;
    app->menus.bar.open = bi;
    app->popup = app->menus.bar.menu[bi];
    app->popup_sub = NULL;
    app->popup_frame = tiku_menubar_panel(&app->menus.bar, bf,
        (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
    app->nav_on = 0;
    return 1;
}

/**
 * @brief A press in the scrollbar: an arrow, the thumb, or a page.
 * @return 1 when the press was in the scrollbar.
 */
static int
scrollbar_press(desktop_t *app, folder_t *sfl, const tiku_event_t *ev)
{
    tiku_rect_t sb = tiku_trk_folder_window_scrollbar(sfl);
    int at, btn;

    if (sb.w <= 0 || ev->x < sb.x || ev->x >= sb.x + sb.w || ev->y < sb.y ||
        ev->y >= sb.y + sb.h) {
        return 0;
    }
    at = ev->y - sb.y;
    btn = sb.w;
    if (at < btn) {
        tiku_scroll_arrow_press(&sfl->sbar, -1);
        tiku_trk_poseview_scroll_to(sfl->view,
            tiku_trk_poseview_scroll(sfl->view) - tiku_scroll_line(&sfl->sbar));
    } else if (at >= sb.h - btn) {
        tiku_scroll_arrow_press(&sfl->sbar, 1);
        tiku_trk_poseview_scroll_to(sfl->view,
            tiku_trk_poseview_scroll(sfl->view) + tiku_scroll_line(&sfl->sbar));
    } else {
        tiku_trk_poseview_scroll_to(sfl->view,
            tiku_scroll_press(&sfl->sbar, sb.h - 2 * btn, at - btn));
    }
    app->sbar_fl = sfl;
    return 1;
}

/**
 * @brief A primary press on the title strip, which may start dragging or
 *        resizing a column.
 * @return 1 when the strip is now tracking the press.
 */
static int
strip_press(desktop_t *app, folder_t *tfl, tiku_window_t *w,
            const tiku_event_t *ev)
{
    tiku_rect_t title = tiku_trk_folder_window_title(tfl);
    unsigned tf;

    if (title.w <= 0 || ev->x < title.x || ev->x >= title.x + title.w ||
        ev->y < title.y || ev->y >= title.y + title.h) {
        return 0;
    }
    commit_rename(app, 1);
    tiku_trk_poseview_cancel_edit_wait(tfl->view);
    if (app->tstrip_fl != tfl) {
        tiku_trk_titleview_init(&app->tstrip, tfl->view, title.h);
        app->tstrip_fl = tfl;
    }
    tf = tiku_trk_titleview_mouse_down(&app->tstrip, ev->x - title.x,
                                       ev->y - title.y, 1,
                                       w == tiku_workspace_focused(app->desk),
                                       tiku_trk_now());
    if ((tf & TIKU_TRK_TT_ACTIVATE) != 0u) {
        tiku_workspace_activate(app->desk, w);
    }
    if (tiku_trk_titleview_tracking(&app->tstrip) == TIKU_TRK_TRACK_NONE) {
        return 0;
    }
    app->strip_ptr_x = ev->x;
    app->strip_ptr_y = ev->y;
    return 1;
}

/**
 * @brief A secondary press on the title strip opens the attributes menu.
 * @return 1 when the press was on the strip.
 */
static int
strip_menu(desktop_t *app, folder_t *tfl, tiku_window_t *w,
           const tiku_event_t *ev)
{
    tiku_rect_t title = tiku_trk_folder_window_title(tfl);
    tiku_trk_sc_ctx_t tc;

    if (ev->x < title.x || ev->x >= title.x + title.w || ev->y < title.y ||
        ev->y >= title.y + title.h) {
        return 0;
    }
    tc = menu_ctx(app, tfl, ev->modifiers);
    build_open_with(app, tfl);
    build_per_type(app, tfl);
    tiku_trk_cwmenu_rebuild(&app->menus, &tc);
    tiku_workspace_activate(app->desk, w);
    app->bar_fl = NULL;
    app->bar_open = -1;
    app->menus.bar.open = -1;
    app->popup = &app->menus.attributes;
    app->popup_sub = NULL;
    app->popup_frame = tiku_menu_place(app->popup, ev->x, ev->y,
        (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
    app->press.down = 0;
    return 1;
}

/**
 * @brief A double click on a row.  On the path column, or on a file among
 *        a query's results, the folder holding the entry opens.  Otherwise
 *        a folder opens, a broken link is offered to Get Info, and anything
 *        else opens through open_one(); with Option, the window that held
 *        the row then closes, after a folder or after an entry that
 *        open_one() opened.
 */
static void
row_opened(desktop_t *app, folder_t *fl, tiku_window_t *w, int row,
           const tiku_event_t *ev)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, row);
    char parent[TIKU_PATH_MAX];
    char *slash;

    if (p == NULL) {
        return;
    }
    if (app->host != NULL &&
        !tiku_trk_prefs_get(prefs_model(app), "ReduceMotion")) {
        tiku_rect_t from, content = tiku_trk_folder_window_content(fl);
        int ox = 0, oy = 0;

        if (tiku_trk_poseview_open_anim_rect(fl->view, row, app->row_h,
                &from.x, &from.y, &from.w, &from.h)) {
            tiku_trk_poseview_origin(fl->view, &ox, &oy);
            from.x += content.x;
            from.y += content.y;
            if (tiku_trk_poseview_mode(fl->view) != TIKU_TRK_MODE_LIST) {
                from.x += 2 - ox;
                from.y += 2 - oy;
            } else {
                from.x -= tiku_trk_poseview_hscroll(fl->view);
            }
            /* An outline still running is replaced, and its ring erased. */
            if (app->activation_motion.active) {
                desktop_damage(app,
                    tiku_motion_outline_bounds(app->activation_rect));
            }
            app->activation_rect = from;
            tiku_motion_start(&app->activation_motion, tiku_trk_now(),
                              TIKU_MOTION_ICON_US);
            desktop_damage(app, tiku_motion_outline_bounds(from));
        }
    }
    if (tiku_trk_folder_window_column_at(fl, ev->x) == TIKU_TRK_COL_PATH ||
        (!tiku_model_is_container(&p->model) &&
         tiku_trk_poseview_is_query(fl->view))) {
        snprintf(parent, sizeof parent, "%s", p->model.path);
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            if (slash == parent) {
                parent[1] = '\0';
            } else {
                *slash = '\0';
            }
            (void)open_folder(app, parent);
        }
        return;
    }
    if (tiku_model_is_container(&p->model)) {
        (void)open_folder(app, p->model.path);
        if (ev->modifiers & TIKU_MOD_OPTION) {
            close_window(app, w);
        }
    } else if (p->model.facts.link_broken) {
        offer_broken_link(app, p->model.path);
    } else {
        int went = open_one(app, &p->model);

        if ((ev->modifiers & TIKU_MOD_OPTION) && went) {
            close_window(app, w);
        }
    }
}

/** @brief A secondary press on the rows: the selection is armed to be
 *         carried or, held still, offered its menu on the release. */
static void
rows_secondary(desktop_t *app, folder_t *fl, tiku_window_t *w, int row,
               const tiku_event_t *ev, int64_t now)
{
    tiku_trk_poseview_cancel_edit_wait(fl->view);
    commit_rename(app, 1);
    (void)tiku_trk_poseview_context_click(fl->view, row);
    tiku_workspace_activate(app->desk, w);
    press_note(app, fl, w, row, ev, now);
    tiku_trk_drag_arm(&app->drag, ev->x, ev->y, 3u, pose_mods(ev), 1, w,
                      fl->path);
    drag_source_kind(app, fl);
    drag_add_selection(app, fl);
    tiku_trk_drag_target_reset(&app->drag_target);
    (void)tiku_trk_click_press(&app->click, NULL, 0, 0, 3u, 0u, now);
}

/** @brief A primary press on the rows: the click updates the selection,
 *         a band begins in empty space, the selection is armed to be
 *         carried, a name may begin a rename, a second click opens. */
static void
rows_primary(desktop_t *app, folder_t *fl, tiku_window_t *w, int row,
             const tiku_event_t *ev, int64_t now)
{
    tiku_trk_pose_t *clicked = (row >= 0)
        ? tiku_trk_poseview_at(fl->view, row) : NULL;
    unsigned mods = pose_mods(ev);
    unsigned click_flags = 0;
    int twice;

    if (ev->modifiers & TIKU_MOD_SHIFT) {
        click_flags |= TIKU_TRK_CLICK_EXTEND;
    }
    if (ev->modifiers & TIKU_MOD_CMD) {
        click_flags |= TIKU_TRK_CLICK_EXTEND | TIKU_TRK_CLICK_CONTROL;
    }
    twice = tiku_trk_click_press(&app->click, clicked, ev->x, ev->y,
                                 ev->button, click_flags, now);
    {
        tiku_rect_t c = tiku_trk_folder_window_content(fl);
        int ox = 0, oy = 0;

        tiku_trk_poseview_origin(fl->view, &ox, &oy);
        tiku_trk_poseview_click_at(fl->view, row, ev->x - (c.x + 2) + ox,
                                   ev->y - (c.y + 2) + oy, mods);
    }
    press_note(app, fl, w, row, ev, now);
    tiku_trk_drag_arm(&app->drag, ev->x, ev->y, ev->button, mods,
                      twice ? 2 : 1, w, fl->path);
    drag_source_kind(app, fl);
    if (row < 0 && ev->button == 1u) {
        if (mods == 0u) {
            tiku_trk_poseview_select_none(fl->view);
        }
        tiku_trk_band_controller_arm(&app->band_controller, fl, ev->x, ev->y,
                                     mods);
        (void)tiku_trk_click_press(&app->click, NULL, 0, 0, ev->button, 0u,
                                   now);
        return;
    }
    if (row >= 0) {
        drag_add_selection(app, fl);
    }
    tiku_trk_drag_target_reset(&app->drag_target);
    if (row >= 0 && tiku_trk_folder_window_name_at(fl, row, ev->x, ev->y)) {
        (void)tiku_trk_poseview_name_clicked(fl->view, row, now);
    }
    if (twice) {
        row_opened(app, fl, w, row, ev);
    }
}

/** @brief A press in a folder's content. */
static int
folders_press(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    tiku_window_t *w;
    folder_t *fl = folder_under(app, ev->x, ev->y, &w);
    int64_t now = tiku_trk_now();
    int row;

    *damage = tiku_trk_damage_all();
    if (fl == NULL || fl->view == NULL) {
        return 0;
    }
    if (w != NULL && ev->button == 1u) {
        if (icon_press(app, fl, w, ev) || bar_press(app, fl, w, ev) ||
            scrollbar_press(app, fl, ev) || strip_press(app, fl, w, ev)) {
            return 1;
        }
    }
    if (w != NULL && ev->button == 3u && strip_menu(app, fl, w, ev)) {
        return 1;
    }
    if (ev->button == 1u && tiku_trk_folder_window_count_at(fl, ev->x, ev->y)) {
        /* A press on the item count opens the parent (IV-062). */
        menu_invoke(app, fl, TIKU_TRK_CMD_OPEN_PARENT);
        return 1;
    }
    row = tiku_trk_folder_window_row_at(fl, ev->x, ev->y);
    if (ev->button == 3u) {
        rows_secondary(app, fl, w, row, ev, now);
    } else {
        rows_primary(app, fl, w, row, ev, now);
    }
    return 1;
}

/** @brief A move: a tracked scroll bar or column title follows it; a press
 *         that moved more than four pixels gets no long-press menu and no
 *         rename; a drag under way tells its target, and a band grows. */
static int
folders_move(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;

    *damage = tiku_trk_damage_none();
    if (app->press.down &&
        (ev->x - app->press.x > 4 || app->press.x - ev->x > 4 ||
         ev->y - app->press.y > 4 || app->press.y - ev->y > 4)) {
        /* Moved more than four pixels: no long-press menu and no rename
         * (PVS-015, PVS-017). */
        if (app->press.folder != NULL) {
            tiku_trk_poseview_cancel_edit_wait(app->press.folder->view);
        }
        app->press.down = 0;
    }
    if (app->sbar_fl != NULL && app->sbar_fl->sbar.tracking) {
        tiku_rect_t sb = tiku_trk_folder_window_scrollbar(app->sbar_fl);

        tiku_trk_poseview_scroll_to(app->sbar_fl->view,
            tiku_scroll_drag(&app->sbar_fl->sbar, sb.h - 2 * sb.w,
                             ev->y - sb.y - sb.w));
        *damage = tiku_trk_damage_all();
        return 1;
    }
    if (tiku_trk_titleview_tracking(&app->tstrip) != TIKU_TRK_TRACK_NONE &&
        app->tstrip_fl != NULL) {
        tiku_rect_t st = tiku_trk_folder_window_title(app->tstrip_fl);
        unsigned tf = tiku_trk_titleview_mouse_moved(&app->tstrip,
            ev->x - st.x, ev->y - st.y, tiku_trk_now());

        app->strip_ptr_x = ev->x;
        app->strip_ptr_y = ev->y;
        if ((tf & (TIKU_TRK_TT_REDRAW | TIKU_TRK_TT_LINE |
                   TIKU_TRK_TT_NO_LINE)) != 0u) {
            *damage = tiku_trk_damage_all();
        }
        return 1;
    }
    if (tiku_trk_drag_moved(&app->drag, ev->x, ev->y) ||
        tiku_trk_drag_active(&app->drag)) {
        (void)tiku_trk_drag_target_motion(&app->drag_target, ev->x, ev->y,
                                          tiku_trk_now());
        /* The drag badge reads the live modifiers, so its copy mark
         * follows the keys during the drag (PVS-036). */
        if (app->drag_live_mods != ev->modifiers) {
            app->drag_live_mods = ev->modifiers;
            *damage = tiku_trk_damage_all();
        }
    }
    (void)tiku_trk_band_controller_motion(&app->band_controller, ev->x, ev->y,
                                          pose_mods(ev));
    return 0;
}

/** @brief Open the context menu for a secondary press held still. */
static void
rows_menu(desktop_t *app, const tiku_event_t *ev)
{
    folder_t *pfl = app->press.folder;
    tiku_trk_pose_t *p = (app->press.row >= 0)
        ? tiku_trk_poseview_at(pfl->view, app->press.row) : NULL;
    unsigned cmods = 0u;
    tiku_trk_sc_ctx_t c;

    if (ev->modifiers & TIKU_MOD_SHIFT) { cmods |= TIKU_MENU_MOD_SHIFT; }
    if (ev->modifiers & TIKU_MOD_CMD) { cmods |= TIKU_MENU_MOD_CMD; }
    c = menu_ctx(app, pfl, cmods);
    app->popup_fl = pfl;
    build_open_with(app, pfl);
    build_per_type(app, pfl);
    app->popup = tiku_trk_cwmenu_context(&app->menus,
                                         (p != NULL) ? &p->model : NULL, &c);
    app->bar_fl = NULL;
    app->bar_open = -1;
    app->menus.bar.open = -1;
    app->nav_on = 0;
    if (p != NULL && tiku_trk_nav_wanted(&p->model)) {
        tiku_trk_nav_open(&app->nav, app->backend, p->model.path);
        app->nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(app),
                                                    "HideDotFiles");
        app->nav.folders_first = tiku_trk_prefs_get(prefs_model(app),
                                                    "SortFolderNamesFirst");
        app->nav_on = 1;
    }
    app->popup_sub = NULL;
    app->popup_frame = tiku_menu_place(app->popup, ev->x, ev->y,
        (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h });
}

/**
 * @brief A release: a tracked column title is released, a band ends, a
 *        drag drops (on a spring entry, the file panel, an Info icon or the
 *        folder under the pointer), a secondary press held still opens its
 *        menu, a press that did not become a drag raises its window, and
 *        the press ends.
 */
static int
folders_release(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    int dropped = tiku_trk_drag_active(&app->drag);

    *damage = tiku_trk_damage_all();
    if (tiku_trk_titleview_tracking(&app->tstrip) != TIKU_TRK_TRACK_NONE &&
        app->tstrip_fl != NULL) {
        tiku_rect_t st = tiku_trk_folder_window_title(app->tstrip_fl);

        (void)tiku_trk_titleview_mouse_up(&app->tstrip, ev->x - st.x,
            ev->y - st.y, (ev->modifiers & TIKU_MOD_SHIFT) != 0,
            tiku_trk_now());
        return 1;
    }
    (void)tiku_trk_band_controller_finish(&app->band_controller);
    if (dropped && app->spring_nav_on) {
        /* Choosing a spring entry is the drop (PVS-051): a disabled row
         * swallows the release, and a release outside the menu falls back
         * to the drop under the pointer. */
        char sdest[TIKU_PATH_MAX];
        int got = spring_nav_hit(app, ev->x, ev->y, sdest, sizeof sdest);

        spring_nav_close(app);
        if (got == 1) {
            tiku_trk_drop_controller_execute_into(&app->drop_controller,
                sdest, ev->x, ev->y, ev->modifiers, ev->button, 0);
        } else if (got == 0) {
            do_drop(app, ev->x, ev->y, ev->modifiers, ev->button);
        }
    } else if (dropped && drop_on_panel(app, ev->x, ev->y)) {
        /* Consumed by the panel, action or not (AW-059). */
    } else if (dropped &&
               drop_on_info_icon(app, ev->x, ev->y, ev->modifiers)) {
        /* Taken by an Info window's icon. */
    } else if (dropped) {
        do_drop(app, ev->x, ev->y, ev->modifiers, ev->button);
    } else if (ev->button == 3u && app->press.down && !app->press.menu &&
               app->press.folder != NULL) {
        rows_menu(app, ev);
    }
    if (!dropped && app->press.down && !app->press.menu &&
        app->press.window != NULL &&
        tiku_workspace_focused(app->desk) == app->press.focus) {
        /* A press that did not become a drag raises its window on the
         * release; a drag out of a window behind leaves it behind
         * (PVS-022). */
        tiku_workspace_activate(app->desk, app->press.window);
    }
    if (app->sbar_fl != NULL) {
        tiku_scroll_release(&app->sbar_fl->sbar);
        app->sbar_fl = NULL;
    }
    tiku_trk_drag_reset(&app->drag);
    tiku_trk_drag_target_reset(&app->drag_target);
    app->info_windows.drop_hot = -1;
    app->press.down = 0;
    app->press.menu = 0;
    app->press.window = NULL;
    app->press.focus = NULL;
    return 0;
}

/**
 * @brief The focused folder's own keys: a new folder, inverting the
 *        selection, the Tab walk, opening the active pose or the parent,
 *        and the arrows and pages over the poses.
 */
static int
folders_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned key = ev->key;
    tiku_window_t *w = tiku_workspace_focused(app->desk);
    folder_t *fl = folder_of(app, w);

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    unsigned mods = 0;

    if (ev->modifiers & TIKU_MOD_SHIFT) {
        mods |= TIKU_TRK_MOD_SHIFT;
    }
    if (ev->modifiers & TIKU_MOD_CMD) {
        mods |= TIKU_TRK_MOD_CMD;
    }
    if (key == 'n' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        menu_invoke(app, fl, TIKU_TRK_CMD_NEW_FOLDER);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 's' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* Cmd+S inverts the selection (PVK-046). */
        tiku_trk_poseview_invert_selection(fl->view);
        desktop_damage_all(app);
        return 1;
    }
    if (key == TIKU_KEY_TAB && fl != NULL) {
        /* Tab walks the poses in name order; while a filter is being
         * typed, Tab is consumed and does nothing. */
        if (!tiku_trk_poseview_is_filtering(fl->view)) {
            (void)tiku_trk_poseview_tab_next(fl->view,
                (ev->modifiers & TIKU_MOD_SHIFT) != 0u);
        }
        desktop_damage_all(app);
        return 1;
    }
    if (fl != NULL && key == TIKU_KEY_DOWN &&
        (ev->modifiers & TIKU_MOD_CMD) &&
        (ev->modifiers & TIKU_MOD_OPTION)) {
        tiku_trk_pose_t *p =
            tiku_trk_poseview_active(fl->view);

        if (p != NULL &&
            tiku_model_is_container(&p->model)) {
            tiku_window_t *from = fl->win;

            if (open_folder(app, p->model.path) != NULL) {
                close_window(app, from);
            }
        }
        desktop_damage_all(app);
        return 1;
    }
    if (fl != NULL && key == TIKU_KEY_DOWN &&
        (ev->modifiers & TIKU_MOD_CMD)) {
        tiku_trk_pose_t *p =
            tiku_trk_poseview_active(fl->view);

        if (p != NULL &&
            tiku_model_is_container(&p->model)) {
            (void)open_folder(app, p->model.path);
        } else if (p != NULL) {
            begin_edit(app, fl);
        }
        desktop_damage_all(app);
        return 1;
    }
    if (fl != NULL && key == TIKU_KEY_UP &&
        (ev->modifiers & TIKU_MOD_CMD)) {
        /* Cmd+Up opens the parent; with Option, the window left behind
         * closes (PVK-040, CW-051). */
        if (strcmp(fl->path, "/") != 0) {
            char parent[TIKU_PATH_MAX];
            char *slash;
            tiku_window_t *from = fl->win;

            snprintf(parent, sizeof parent, "%s", fl->path);
            slash = strrchr(parent, '/');
            if (slash == parent) {
                parent[1] = '\0';
            } else if (slash != NULL) {
                *slash = '\0';
            }
            if (slash != NULL &&
                open_folder(app, parent) != NULL &&
                (ev->modifiers & TIKU_MOD_OPTION)) {
                close_window(app, from);
            }
        }
        desktop_damage_all(app);
        return 1;
    }
    if (fl != NULL) {
        /* The arrows, the pages, Home and End move over the poses;
         * Left and Right matter in the icon modes, whose arrangement is
         * two-dimensional (PVK-007, PVK-011). */
        int nav = (key == TIKU_KEY_UP)    ? TIKU_TRK_KEY_UP
                : (key == TIKU_KEY_DOWN)  ? TIKU_TRK_KEY_DOWN
                : (key == TIKU_KEY_LEFT)  ? TIKU_TRK_KEY_LEFT
                : (key == TIKU_KEY_RIGHT) ? TIKU_TRK_KEY_RIGHT
                : (key == TIKU_KEY_PAGE_UP) ? TIKU_TRK_KEY_PGUP
                : (key == TIKU_KEY_PAGE_DOWN)  ? TIKU_TRK_KEY_PGDN
                : (key == TIKU_KEY_HOME)  ? TIKU_TRK_KEY_HOME
                : (key == TIKU_KEY_END)   ? TIKU_TRK_KEY_END : 0;
        if (nav != 0) {
            (void)tiku_trk_poseview_key_nav(fl->view, nav, mods);
        } else if (key == TIKU_KEY_RETURN) {
            tiku_trk_pose_t *p =
                tiku_trk_poseview_active(fl->view);
            if (p != NULL &&
                tiku_model_is_container(&p->model)) {
                /* Option+Return opens the folder and closes the
                 * window it came from, as an Option double click does
                 * (PVK-017). */
                int chase = (ev->modifiers & TIKU_MOD_OPTION) != 0;
                tiku_window_t *from = fl->win;

                if (open_folder(app, p->model.path) != NULL &&
                    chase) {
                    close_window(app, from);
                }
            } else if (p != NULL) {
                /* Return on a leaf edits its value. */
                begin_edit(app, fl);
            }
        } else if (key == TIKU_KEY_BACKSPACE) {
            char parent[TIKU_PATH_MAX];
            char *slash;
            snprintf(parent, sizeof parent, "%s", fl->path);
            slash = strrchr(parent, '/');
            if (slash != NULL) {
                if (slash == parent) { parent[1] = '\0'; }
                else { *slash = '\0'; }
                (void)open_folder(app, parent);
            }
        }
        desktop_damage_all(app);
    }
    return 1;
    return 1;
}

/**
 * @brief Put the folder windows' content, the desk's own included, on the
 *        responder list.
 */
void
desktop_folders_attach(desktop_t *app)
{
    app->folders_responder = (tiku_trk_responder_t){
        .name = "folders", .self = app, .hit = folders_hit,
        .press = folders_press, .move = folders_move,
        .release = folders_release, .key = folders_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->folders_responder);
}
