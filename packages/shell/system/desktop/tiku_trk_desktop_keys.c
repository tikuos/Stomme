/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_keys.c - the desk's own keyboard, as two responders: F1
 * help, above the windows; and, below the windows' own keys and above the
 * folders' navigation, the add-on shortcuts, the chords the menus show
 * (aimed at the focused folder), the switcher, closing a window and the
 * settings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"

/**
 * @brief The add-on shortcuts, Cmd-F for the Find panel and Shift-Cmd-A for
 *        Select by pattern.
 * @return 1 when the key was handled.
 */
static int
keys_panels(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    (void)w;
    {
        unsigned addon_mods = 0u;
        int addon_command;

        if (ev->modifiers & TIKU_MOD_OPTION) addon_mods |= 0x1u;
        if (ev->modifiers & TIKU_MOD_CMD) addon_mods |= 0x2u;
        addon_command = tiku_trk_addons_shortcut(
            &app->addons, key_char(ev), addon_mods,
            TIKU_TRK_ADDON_COMMAND_BASE);
        if (addon_command >= 0) {
            menu_invoke(app, fl, addon_command);
            desktop_damage_all(app);
            return 1;
        }
    }
    if (key == 'f' && (ev->modifiers & TIKU_MOD_CMD)) {
        open_panel(app);
        return 1;
    }
    if (key == 'a' && (ev->modifiers & TIKU_MOD_CMD) &&
        (ev->modifiers & TIKU_MOD_SHIFT)) {
        /* Select by pattern, on the focused folder, opened near the
         * pointer (PVK-047). */
        int wx, wy;

        if (!tiku_host_pointer(app->host, &wx, &wy, NULL)) {
            wx = app->surf->w / 2;
            wy = app->surf->h / 2;
        }
        tiku_trk_select_window_open(&app->select_window, fl,
                                    wx, wy);
        return 1;
    }
    return 0;
}

/**
 * @brief The selection's chords: Select All, Unmount, the clipboard's
 *        Escape, Delete, and Edit Query, Get Info, Open, Edit Name,
 *        Duplicate and Clean Up.
 * @return 1 when the key was handled.
 */
static int
keys_selection(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    (void)w;
    if (key == 'a' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        tiku_trk_poseview_select_all(fl->view);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'u' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* Cmd+U, which the volume menus advertise (CW-034). */
        menu_invoke(app, fl, TIKU_TRK_CMD_UNMOUNT);
        desktop_damage_all(app);
        return 1;
    }
    if (key == TIKU_KEY_ESCAPE && fl != NULL &&
        !alert_model(app)->open) {
        /* Escape here is the clipboard's (PVK-034); while an alert
         * is open, Escape is the alert's. */
        if (ev->modifiers & TIKU_MOD_SHIFT) {
            /* Shift+Escape removes the selection from the
             * clipboard, or every pose in the folder when
             * nothing is selected. */
            int ci, cn = tiku_trk_poseview_count(fl->view);
            int any =
                tiku_trk_poseview_selection_count(fl->view) > 0;

            for (ci = 0; ci < cn; ci++) {
                tiku_trk_pose_t *p =
                    tiku_trk_poseview_at(fl->view, ci);

                if (p != NULL && (!any || p->selected)) {
                    (void)tiku_trk_clip_remove(&app->clip,
                                               &p->model, 1);
                }
            }
            desktop_damage_all(app);
            return 1;
        }
        if (tiku_trk_clip_count(&app->clip) > 0) {
            /* Escape empties the file clipboard, cut or copy. */
            tiku_trk_clip_clear(&app->clip);
            desktop_damage_all(app);
            return 1;
        }
        /* Nothing pending: Escape goes on to the next handler. */
    }
    if (key == TIKU_KEY_DELETE && fl == NULL) {
        /* No window focused: Delete acts on the Desktop's
         * selection. */
        fl = desktop_folder(app);
    }
    if (key == TIKU_KEY_DELETE && fl != NULL) {
        /* Delete runs Move to Trash and Shift-Delete runs Delete,
         * the commands the menu items run, which decide by where
         * the items are and ask the same questions (PVK-038,
         * FS-046). */
        menu_invoke(app, fl,
            (ev->modifiers & TIKU_MOD_SHIFT)
                ? TIKU_TRK_CMD_DELETE
                : TIKU_TRK_CMD_MOVE_TO_TRASH);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'g' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* Cmd+G runs Edit query; its menu item shows this shortcut
         * (CW-033). */
        menu_invoke(app, fl, TIKU_TRK_CMD_EDIT_QUERY);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'i' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        /* Cmd+I runs Get info; its menu item shows this shortcut
         * (AW-065). */
        menu_invoke(app, fl, TIKU_TRK_CMD_GET_INFO);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'o' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        tiku_trk_pose_t *p = tiku_trk_poseview_active(fl->view);

        if (p != NULL &&
            tiku_model_is_container(&p->model)) {
            (void)open_folder(app, p->model.path);
        } else if (p != NULL) {
            begin_edit(app, fl);
        } else {
            /* Open is grey in the menu here; the chord says so on
             * the status line (NeXT UIG ch6). */
            snprintf(app->status, sizeof app->status,
                     "Open is not available here");
        }
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'e' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        begin_rename(app, fl,
            tiku_trk_poseview_active(fl->view) != NULL
                ? tiku_trk_poseview_active(fl->view)->index : -1);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'd' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        menu_invoke(app, fl, TIKU_TRK_CMD_DUPLICATE);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'k' && (ev->modifiers & TIKU_MOD_CMD) &&
        fl != NULL) {
        menu_invoke(app, fl,
            (ev->modifiers & TIKU_MOD_SHIFT)
                ? TIKU_TRK_CMD_CLEANUP_ALL
                : TIKU_TRK_CMD_CLEANUP);
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief Cut, copy, paste, undo and redo, the standard set the menus draw
 *        their shortcuts from, the view modes and the icon size.
 * @return 1 when the key was handled.
 */
static int
keys_clipboard(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    (void)w;
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        (key == 'x' || key == 'c' || key == 'v')) {
        /* Cmd-X, Cmd-C and Cmd-V; with Shift, each runs the
         * variant the menu shows while Shift is held. */
        unsigned m = (ev->modifiers & TIKU_MOD_SHIFT)
                         ? TIKU_TRK_MOD_SHIFT : 0u;
        int base = (key == 'x') ? TIKU_TRK_CMD_CUT
                 : (key == 'c') ? TIKU_TRK_CMD_COPY
                                : TIKU_TRK_CMD_PASTE;

        menu_invoke(app, fl, tiku_trk_sc_command(base, m));
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && key == 'z') {
        /* Cmd+Z undoes and Shift+Cmd+Z redoes the last file
         * operation (CW-061).  A focused text field gets the key
         * first, in the windows responder above. */
        char what[64], err[200];

        if (ev->modifiers & TIKU_MOD_SHIFT) {
            if (tiku_fs_can_redo(app->fs, what,
                                     sizeof what)) {
                int n = tiku_fs_redo(app->fs, err,
                                         sizeof err);

                snprintf(app->status, sizeof app->status,
                         "redid %s (%d item%s)", what, n,
                         (n == 1) ? "" : "s");
            } else {
                snprintf(app->status, sizeof app->status,
                         "nothing to redo");
            }
        } else if (tiku_fs_can_undo(app->fs, what,
                                        sizeof what)) {
            int n = tiku_fs_undo(app->fs, err, sizeof err);

            snprintf(app->status, sizeof app->status,
                     "undid %s (%d item%s)", what, n,
                     (n == 1) ? "" : "s");
        } else {
            snprintf(app->status, sizeof app->status,
                     "nothing to undo");
        }
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        tiku_trk_sc_command_for_key(key, ev->modifiers) != 0) {
        /* The rest of the standard set, looked up in the table
         * the menus draw their shortcuts from, so every chord a
         * menu shows works (PVK-042). */
        menu_invoke(app, fl,
            tiku_trk_sc_command_for_key(key, ev->modifiers));
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        (key == '1' || key == '2' || key == '3')) {
        /* Cmd-1, Cmd-2 and Cmd-3: icon, mini icon and list view
         * (AW-025, CW-065). */
        menu_invoke(app, fl,
            (key == '1') ? TIKU_TRK_CMD_ICON_VIEW :
            (key == '2') ? TIKU_TRK_CMD_MINI_ICON_VIEW
                         : TIKU_TRK_CMD_LIST_VIEW);
        desktop_damage_all(app);
        return 1;
    }
    if ((ev->modifiers & TIKU_MOD_CMD) && fl != NULL &&
        (key == '+' || key == '=' || key == '-')) {
        (void)tiku_trk_poseview_step_icon_size(fl->view,
            (key == '-') ? 0 : 1);
        tiku_trk_poseview_cleanup(fl->view,
                                  tiku_trk_folder_window_content(fl).w);
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief The Deskbar preferences' keys, the switcher, closing the window,
 *        the desktop's own panels and the settings chord.
 * @return 1 when the key was handled.
 */
static int
keys_windows(desktop_t *app, const tiku_event_t *ev, unsigned key,
           tiku_window_t *w, folder_t *fl)
{
    {
        char printable[2];

        if (tiku_workspace_focused(app->desk) ==
                tiku_trk_deskbar_prefs_window_handle(
                    &app->deskbar_prefs_window) &&
            tiku_trk_deskbar_prefs_window_key(
                &app->deskbar_prefs_window,
                key_name(ev, printable))) {
            desktop_damage_all(app);
            return 1;
        }
    }
    if (tiku_trk_switcher_window_shown(&app->switcher_window)) {
        /* The switcher is modal: while it shows, every key goes to
         * it. */
        char printable[2];
        const char *nm = key_name(ev, printable);
        tiku_trk_sw_act_t act = tiku_trk_switcher_window_key(
            &app->switcher_window, nm,
            (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0);

        if (act == TIKU_TRK_SW_COMMIT) {
            tiku_trk_switcher_window_close(&app->switcher_window,
                                           1);
        } else if (act == TIKU_TRK_SW_CANCEL) {
            tiku_trk_switcher_window_close(&app->switcher_window,
                                           0);
        } else if (act == TIKU_TRK_SW_QUIT_APP ||
                   act == TIKU_TRK_SW_HIDE_APP) {
            const char *selected = tiku_trk_switcher_window_selected(
                &app->switcher_window);

            snprintf(app->status, sizeof app->status,
                     "%s %.50s",
                     (act == TIKU_TRK_SW_QUIT_APP) ? "quitting"
                                                   : "hiding",
                     (selected != NULL) ? selected : "");
        }
        desktop_damage_all(app);
        return 1;
    }
    if (key == TIKU_KEY_TAB && (ev->modifiers & TIKU_MOD_CMD)) {
        tiku_trk_switcher_window_open(&app->switcher_window,
            (ev->modifiers & TIKU_MOD_SHIFT) ? 1 : 0);
        desktop_damage_all(app);
        return 1;
    }
    if (key == 'w' && (ev->modifiers & TIKU_MOD_CMD)) {
        close_window(app, w);
        desktop_damage_all(app);
        return 1;
    }
#if TIKU_SHELL_BOARD_SERVICES

    if (w != NULL && (w == desktop_deviceprefs_handle(app) ||
                     w == desktop_startup_handle(app) ||
                     w == desktop_associations_handle(app) ||
                     w == desktop_forcequit_handle(app) ||
                     w == tiku_trk_fonts_window_handle(&app->fonts_window)) &&
        !alert_model(app)->open) {
#else

    if (w != NULL && (w == desktop_startup_handle(app) ||
                     w == desktop_associations_handle(app) ||
                     w == desktop_forcequit_handle(app) ||
                     w == tiku_trk_fonts_window_handle(&app->fonts_window)) &&
        !alert_model(app)->open) {
#endif

        /* A focused panel of the desktop's own takes every key that
         * reaches here; Cmd-Tab is handled above. */
        (void)tiku_window_send(w, ev);
        return 1;
    }
    if (key == ',' && (ev->modifiers & TIKU_MOD_CMD)) {
        /* Cmd-, opens the settings, with or without a folder
         * focused. */
        tiku_trk_prefs_window_open(&app->prefs_window);
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief The desk's chords, asked in this order: the add-on shortcuts and
 *        panels, the selection's chords, the clipboard and undo, the
 *        standard set, the view modes, the switcher, closing a window, the
 *        desktop's own panels and the settings.  While the switcher shows,
 *        every key goes to it.
 * @return 1 when a chord was handled.
 */
static int
keys_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned key = ev->key;
    tiku_window_t *w = tiku_workspace_focused(app->desk);
    folder_t *fl = folder_of(app, w);

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (tiku_trk_switcher_window_shown(&app->switcher_window)) {
        return keys_windows(app, ev, key, w, fl);
    }
    if (keys_panels(app, ev, key, w, fl) ||
        keys_selection(app, ev, key, w, fl) ||
        keys_clipboard(app, ev, key, w, fl) ||
        keys_windows(app, ev, key, w, fl)) {
        return 1;
    }
    return 0;
}

/**
 * @brief F1, while no menu shows, opens the focused window's help page
 *        (see desktop_help_page_of()), or the help index.  This responder
 *        sits above the windows, so an application that takes every key
 *        never gets F1.
 */
static int
help_key(void *self, const tiku_event_t *ev, tiku_rect_t *damage)
{
    desktop_t *app = self;
    unsigned key = ev->key;

    *damage = tiku_trk_damage_none();
    if (ev->type != TIKU_EVENT_KEY_DOWN) {
        return 0;
    }
    if (key == TIKU_KEY_F1 && !app->popup) {
        const char *topic = desktop_help_page_of(app,
            tiku_workspace_focused(app->desk));
        (void)desktop_help_open(app,
            topic ? topic : "index.html");
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/** @brief Put the desk's chords on the responder list. */
void
desktop_keys_attach(desktop_t *app)
{
    app->keys_responder = (tiku_trk_responder_t){
        .name = "keys", .self = app, .key = keys_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->keys_responder);
}

/** @brief Put F1 help on the responder list, above the Info windows and
 *         the frames. */
void
desktop_help_attach(desktop_t *app)
{
    app->help_responder = (tiku_trk_responder_t){
        .name = "help", .self = app, .key = help_key
    };
    (void)tiku_trk_responders_push(&app->responders, &app->help_responder);
}
