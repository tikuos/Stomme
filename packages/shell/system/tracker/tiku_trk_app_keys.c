/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_keys.c - the window's keyboard: an alert, the location field
 * and the rename editor first, then the chords, navigation, the command
 * keys and type-ahead.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/* Used before they are defined. */
int browser_key_edit(app_t *a, const tiku_event_t *event, unsigned key,
                     unsigned modifiers, unsigned mods);
int browser_key_view(app_t *a, const tiku_event_t *event, unsigned key,
                     unsigned modifiers, unsigned mods);
int browser_key_open(app_t *a, const tiku_event_t *event, unsigned key,
                     unsigned modifiers, unsigned mods);
int browser_key_nav(app_t *a, const tiku_event_t *event, unsigned key,
                    unsigned modifiers, unsigned mods);
int browser_key_cmd(app_t *a, const tiku_event_t *event, unsigned key,
                    unsigned modifiers, unsigned mods);
int browser_key_rename(app_t *a, const tiku_event_t *event, unsigned key,
                       unsigned modifiers, unsigned mods);
int browser_key_location(app_t *a, const tiku_event_t *event, unsigned key,
                         unsigned modifiers, unsigned mods);
/* End of the forward declarations. */

/**
 * @brief Whether (@p px, @p py) is on the title strip in list mode,
 *        writing the strip-relative coordinates to @p cx and @p cy when
 *        given.  Icon mode has no strip and answers 0.
 */
int
browser_title_hit(app_t *a, int px, int py, int *cx, int *cy)
{
    tiku_rect_t list = browser_list_rect(a);
    int top = TAB_H + BORDER + a->menus.bar.height +
              browser_navigator_height(a);

    if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (cx != NULL) {
        *cx = px - list.x + tiku_trk_poseview_hscroll(a->view);
    }
    if (cy != NULL) {
        *cy = py - top;
    }
    return (py >= top && py <= top + TITLE_H);
}

/**
 * @brief A key for the window: an alert, the location field or a rename
 *        in progress takes it first; then the Cmd chords, the navigation
 *        keys, the open, view and edit keys, the menus' chord table, and
 *        type-ahead for the rest.
 */
void
browser_handle_key(app_t *a, const tiku_event_t *event)
{
    unsigned key = event->key;
    unsigned modifiers = event->modifiers;
    unsigned mods = 0;
    int r;

    if (tiku_trk_status_overlay_key(&a->status_overlay, key)) {
        /* A modal alert owns the keyboard: Escape is Cancel, Return the
         * default, and a letter presses the button it starts with. */
        a->dirty = 1;
        return;
    }
    if (browser_key_location(a, event, key, modifiers, mods) == 2) {
        return;
    }
    if (browser_key_rename(a, event, key, modifiers, mods) == 2) {
        return;
    }
    if (modifiers & TIKU_MOD_SHIFT) { mods |= TIKU_TRK_MOD_SHIFT; }
    if (modifiers & TIKU_MOD_CMD) { mods |= TIKU_TRK_MOD_CMD; }

    if (browser_key_cmd(a, event, key, modifiers, mods) == 2) {
        return;
    }
    if (browser_key_nav(a, event, key, modifiers, mods) == 2) {
        return;
    }
    r = browser_key_open(a, event, key, modifiers, mods);
    if (r == 0) { r = browser_key_view(a, event, key, modifiers, mods); }
    if (r == 0) { r = browser_key_edit(a, event, key, modifiers, mods); }
    if (r == 2) {
        return;
    }
    if ((modifiers & TIKU_MOD_CMD) != 0u) {
        /* The rest of the standard chords, from the table the menus read,
         * so every chord a menu shows works here (PVK-042). */
        int fcmd = tiku_trk_sc_command_for_key(key, modifiers);

        if (fcmd != 0) {
            browser_menu_invoke(a, fcmd);
            a->dirty = 1;
            return;
        }
    }
    {
        char typed = browser_key_char(event);

        if (typed >= 0x20 && typed < 0x7f) {
            tiku_trk_poseview_type_ahead(a->view, typed);
            a->dirty = 1;
        }
    }
}

/** @brief Run a menu command through the keyboard path: @p key with
 *         @p modifiers, as if typed. */
void
browser_menu_key(app_t *a, unsigned key, unsigned modifiers)
{
    tiku_event_t event;

    memset(&event, 0, sizeof event);
    event.type = TIKU_EVENT_KEY_DOWN;
    event.key = key;
    event.modifiers = modifiers;
    if (key >= 0x20u && key < 0x7fu) {
        event.text[0] = (char)key;
    }
    browser_handle_key(a, &event);
}

/**
 * @brief The editing keys: rename (F2, Cmd+E), Delete, duplicate, undo and
 *        redo, new folder, and clean up.
 * @return 0 for a key this group does not handle, 1 for one of its keys
 *         without the modifier it needs (left to type-ahead), 2 when the
 *         key was handled.
 */
int
browser_key_edit(app_t *a, const tiku_event_t *event, unsigned key,
                 unsigned modifiers, unsigned mods)
{
    (void)event;
    (void)mods;
    switch (key) {

    case TIKU_KEY_F2:
        browser_begin_rename(a);
        return 2;
    case 'e':
        if (modifiers & TIKU_MOD_CMD) {
            browser_begin_rename(a);
            return 2;
        }
        break;
    case TIKU_KEY_DELETE: {
        /* Delete is Move to Trash, and Shift+Delete is Delete (PVK-038);
         * both go through the menu command, so the question they put is
         * the menu's (FS-046). */
        /* Noted before the listing changes: afterwards the index is
         * all that marks where the item was (FS-050). */
        a->select_next = browser_first_selected_index(a);
        browser_menu_invoke(a, (modifiers & TIKU_MOD_SHIFT)
                           ? TIKU_TRK_CMD_DELETE
                           : TIKU_TRK_CMD_MOVE_TO_TRASH);
        a->dirty = 1;
        return 2;
    }
    case 'd':
        if (modifiers & TIKU_MOD_CMD) {
            char paths[64][TIKU_PATH_MAX];
            const char *sel[64];
            int n = browser_selection_paths(a, paths, sel, 64);

            if (n > 0 && a->fs != NULL) {
                (void)tiku_fs_duplicate(a->fs, sel, n);
                snprintf(a->status, sizeof a->status, "duplicating %d item%s",
                         n, (n == 1) ? "" : "s");
            }
            a->dirty = 1;
            return 2;
        }
        break;
    case 'z':
        if ((modifiers & TIKU_MOD_CMD) &&
            (modifiers & TIKU_MOD_SHIFT)) {
            char what[64], err[200];

            if (a->fs != NULL && tiku_fs_can_redo(a->fs, what,
                                                      sizeof what)) {
                int n = tiku_fs_redo(a->fs, err, sizeof err);

                snprintf(a->status, sizeof a->status, "redid %s (%d item%s)",
                         what, n, (n == 1) ? "" : "s");
                browser_after_op(a);
            } else {
                snprintf(a->status, sizeof a->status, "nothing to redo");
            }
            a->dirty = 1;
            return 2;
        }
        if (modifiers & TIKU_MOD_CMD) {
            char what[64], err[200];

            if (a->fs != NULL && tiku_fs_can_undo(a->fs, what,
                                                      sizeof what)) {
                int n = tiku_fs_undo(a->fs, err, sizeof err);
                snprintf(a->status, sizeof a->status, "undid %s (%d item%s)",
                         what, n, (n == 1) ? "" : "s");
                browser_after_op(a);
            } else {
                snprintf(a->status, sizeof a->status, "nothing to undo");
            }
            a->dirty = 1;
            return 2;
        }
        break;
    case 'n':
        if (modifiers & TIKU_MOD_CMD) {
            char made[TIKU_PATH_MAX], err[200];

            if (a->fs != NULL &&
                tiku_fs_new_folder(a->fs, tiku_trk_poseview_path(a->view), made, sizeof made, err,
                                       sizeof err) == 0) {
                const char *slash = strrchr(made, '/');

                snprintf(a->status, sizeof a->status, "created '%.180s'",
                         (slash != NULL) ? slash + 1 : made);
                browser_after_op(a);
                /* Selected and opened for renaming at once. */
                {
                    int i = browser_index_of_path(a, made);

                    if (i >= 0) {
                        tiku_trk_poseview_click(a->view, i, 0u);
                        browser_begin_rename(a);
                        snprintf(a->status, sizeof a->status,
                                 "created '%.180s'",
                                 (slash != NULL) ? slash + 1 : made);
                    }
                }
            } else {
                snprintf(a->status, sizeof a->status, "%s",
                         err[0] ? err : "could not create a new folder");
            }
            a->dirty = 1;
            return 2;
        }
        break;
    case 'k':
        if (modifiers & TIKU_MOD_CMD) {
            tiku_rect_t lr = browser_list_rect(a);

            /* Cmd+K snaps what is there to the grid; Cmd+Shift+K lays it
             * all out again in sort order (PVL-021). */
            tiku_scroll_detach(&a->sb);
            if (modifiers & TIKU_MOD_SHIFT) {
                tiku_trk_poseview_cleanup(a->view, lr.w - 4);
            } else {
                (void)tiku_trk_poseview_snap_to_grid(a->view);
            }
            tiku_scroll_attach(&a->sb);
            (void)tiku_trk_poseview_save_state(a->view);
            a->dirty = 1;
            return 2;
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief The view and window keys: quit, unmount, select all, invert, icon
 *        size, reload and the three modes.
 * @return 0 for a key this group does not handle, 1 for one of its keys
 *         without Cmd (left to type-ahead), 2 when the key was handled.
 */
int
browser_key_view(app_t *a, const tiku_event_t *event, unsigned key,
                 unsigned modifiers, unsigned mods)
{
    (void)event;
    (void)mods;
    switch (key) {

    case 'q':
        if (modifiers & TIKU_MOD_CMD) { a->running = 0; return 2; }
        break;
    case 'u':
        if (modifiers & TIKU_MOD_CMD) {
            /* Cmd+U runs Unmount, the chord the volume menus show
             * (CW-034). */
            browser_menu_invoke(a, TIKU_TRK_CMD_UNMOUNT);
            return 2;
        }
        break;
    case 'a':
        if (modifiers & TIKU_MOD_CMD) {
            tiku_trk_poseview_select_all(a->view);
            a->dirty = 1;
            return 2;
        }
        break;
    case 's':
        if (modifiers & TIKU_MOD_CMD) {
            /* PVK-046: invert the selection. */
            tiku_trk_poseview_invert_selection(a->view);
            a->dirty = 1;
            return 2;
        }
        break;
    case '+':
    case '=':
        if (modifiers & TIKU_MOD_CMD) {
            (void)tiku_trk_poseview_step_icon_size(a->view, 1);
            browser_settle_layout(a);
            a->dirty = 1;
            return 2;
        }
        break;
    case '-':
        if (modifiers & TIKU_MOD_CMD) {
            (void)tiku_trk_poseview_step_icon_size(a->view, 0);
            browser_settle_layout(a);
            a->dirty = 1;
            return 2;
        }
        break;
    case 'r':
        if (modifiers & TIKU_MOD_CMD) {
            (void)tiku_trk_poseview_reload(a->view);
            (void)tiku_trk_poseview_restore_state(a->view);
            browser_settle_layout(a);
            a->dirty = 1;
            return 2;
        }
        break;
    case '1':
    case '2':
    case '3':
        if (modifiers & TIKU_MOD_CMD) {
            tiku_rect_t lr = browser_list_rect(a);
            tiku_trk_poseview_set_mode(a->view,
                (key == '1') ? TIKU_TRK_MODE_ICON :
                (key == '2') ? TIKU_TRK_MODE_MINI : TIKU_TRK_MODE_LIST);
            if (tiku_trk_poseview_mode(a->view) != TIKU_TRK_MODE_LIST) {
                (void)tiku_trk_poseview_place_unplaced(a->view, lr.w - 4);
            } else if (a->filter_pref &&
                       !tiku_trk_poseview_is_filtering(a->view)) {
                /* Leaving list mode turned filtering off; back in list
                 * mode the preference turns it on again (TS-052). */
                tiku_trk_poseview_set_filtering(a->view, 1);
            }
            (void)tiku_trk_poseview_save_state(a->view);
            a->dirty = 1;
            return 2;
        }
        break;
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief Return (open), Shift+Space (a new filter term), Backspace (erase
 *        a typed character, else go up), Escape (clear the clipboard and
 *        the type-ahead) and Tab (the next pose).
 * @return 0 for a key this group does not handle, 1 for a Space it leaves
 *         to type-ahead, 2 when the key was handled.
 */
int
browser_key_open(app_t *a, const tiku_event_t *event, unsigned key,
                 unsigned modifiers, unsigned mods)
{
    (void)event;
    switch (key) {

    case TIKU_KEY_RETURN:
        /* PVK-015/016: Return opens what a filter has narrowed the
         * listing to, with no arrow key first. */
        if (tiku_trk_poseview_return_pressed(a->view,
                                             (mods & TIKU_TRK_MOD_SHIFT))) {
            browser_open_selection(a);
            tiku_trk_poseview_set_filtering(a->view, 0);
            if (a->filter_pref) {
                /* Turning filtering off cleared the terms; with the
                 * preference on, the next keystroke filters again
                 * (TS-052). */
                tiku_trk_poseview_set_filtering(a->view, 1);
            }
        } else {
            browser_open_selection(a);
        }
        a->dirty = 1;
        return 2;
    case ' ':
        if (mods & TIKU_TRK_MOD_SHIFT) {
            /* PVK-031: a second filter term; each term may match a
             * different column. */
            if (tiku_trk_poseview_is_filtering(a->view)) {
                (void)tiku_trk_poseview_filter_term(a->view);
                a->dirty = 1;
                return 2;
            }
        }
        break;
    case TIKU_KEY_BACKSPACE:
        if (tiku_trk_poseview_type_string(a->view)[0] != '\0') {
            tiku_trk_poseview_type_backspace(a->view);
        } else {
            browser_navigate_up(a);
        }
        a->dirty = 1;
        return 2;
    case TIKU_KEY_ESCAPE:
        if (mods & TIKU_TRK_MOD_SHIFT) {
            /* Shift+Escape removes the selection from the clipboard, or
             * every pose in the folder when nothing is selected
             * (PVK-034). */
            int i, n = tiku_trk_poseview_count(a->view);
            int any = tiku_trk_poseview_selection_count(a->view) > 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(a->view, i);

                if (p != NULL && (!any || p->selected)) {
                    (void)tiku_trk_clip_remove(&a->clip, &p->model, 1);
                }
            }
        } else {
            if (tiku_trk_clip_count(&a->clip) > 0) {
                /* The clipboard is emptied, which aborts a pending cut,
                 * then the type-ahead is cleared (PVK-034). */
                tiku_trk_clip_clear(&a->clip);
            }
            tiku_trk_poseview_type_clear(a->view);
        }
        a->dirty = 1;
        return 2;
    case TIKU_KEY_TAB:
        if (!tiku_trk_poseview_is_filtering(a->view)) {
            (void)tiku_trk_poseview_tab_next(a->view,
                (modifiers & TIKU_MOD_SHIFT) != 0u);
        }
        a->dirty = 1;
        return 2;
    default:
        return 0;
    }
    return 1;
}

/**
 * @brief Cmd+O, and the arrows, Home, End and the page keys, handed to the
 *        pose view.
 * @return 2 when the key was handled, else 1.
 */
int
browser_key_nav(app_t *a, const tiku_event_t *event, unsigned key,
                unsigned modifiers, unsigned mods)
{
    (void)event;
    (void)modifiers;
    int nav = 0;

    switch (key) {
    case 'o':
        if (mods & TIKU_TRK_MOD_CMD) {
            if (tiku_trk_poseview_active(a->view) == NULL) {
                /* Open is grey in the menu; the chord's refusal goes to
                 * the status line (NeXT UIG ch6). */
                snprintf(a->status, sizeof a->status,
                         "Open is not available here");
                a->dirty = 1;
                return 2;
            }
            browser_open_selection(a);
            return 2;
        }
        break;
    case TIKU_KEY_UP:        nav = TIKU_TRK_KEY_UP;    break;
    case TIKU_KEY_DOWN:      nav = TIKU_TRK_KEY_DOWN;  break;
    case TIKU_KEY_LEFT:      nav = TIKU_TRK_KEY_LEFT;  break;
    case TIKU_KEY_RIGHT:     nav = TIKU_TRK_KEY_RIGHT; break;
    case TIKU_KEY_HOME:      nav = TIKU_TRK_KEY_HOME;  break;
    case TIKU_KEY_END:       nav = TIKU_TRK_KEY_END;   break;
    case TIKU_KEY_PAGE_UP:   nav = TIKU_TRK_KEY_PGUP;  break;
    case TIKU_KEY_PAGE_DOWN: nav = TIKU_TRK_KEY_PGDN;  break;
    default: break;
    }
    if (nav != 0) {
        int before = tiku_trk_poseview_scroll(a->view);
        (void)tiku_trk_poseview_key_nav(a->view, nav, mods);
        {   /* PVK-004: the arrow scrolls its target into view. */
            tiku_trk_pose_t *act = tiku_trk_poseview_active(a->view);
            int i, n = tiku_trk_poseview_count(a->view);
            for (i = 0; act != NULL && i < n; i++) {
                if (tiku_trk_poseview_at(a->view, i) == act) {
                    tiku_trk_poseview_scroll_into_view(a->view, i,
                                                       browser_visible_rows(a));
                    break;
                }
            }
        }
        (void)before;
        a->dirty = 1;
        return 2;
    }
    return 1;
}

/**
 * @brief The chords taken before navigation: Cmd+L for the location field,
 *        Cmd+X, C and V, Cmd with Down or Return to open, Cmd with Left,
 *        Right or Up to move between folders, and Option+Return to open.
 * @return 2 when the key was handled, else 1.
 */
int
browser_key_cmd(app_t *a, const tiku_event_t *event, unsigned key,
                unsigned modifiers, unsigned mods)
{
    (void)event;
    if ((modifiers & TIKU_MOD_CMD) && key == 'l') {
        browser_begin_location(a);
        return 2;
    }
    if ((modifiers & TIKU_MOD_CMD) &&
        (key == 'x' || key == 'c' || key == 'v')) {
        /* The clipboard verbs go through the shared command layer, as
         * the menu items do. */
        int base = (key == 'x') ? TIKU_TRK_CMD_CUT
                 : (key == 'c') ? TIKU_TRK_CMD_COPY : TIKU_TRK_CMD_PASTE;

        browser_menu_invoke(a, tiku_trk_sc_command(base, mods));
        return 2;
    }
    if (modifiers & TIKU_MOD_CMD) {
        if (key == TIKU_KEY_DOWN || key == TIKU_KEY_RETURN) {
            browser_open_selection(a);
            return 2;
        }
        if (key == TIKU_KEY_LEFT) {
            browser_navigate_back(a);
            return 2;
        }
        if (key == TIKU_KEY_RIGHT) {
            browser_navigate_forward(a);
            return 2;
        }
        if (key == TIKU_KEY_UP) {
            browser_navigate_up(a);
            return 2;
        }
    }
    if ((modifiers & TIKU_MOD_OPTION) && key == TIKU_KEY_RETURN) {
        browser_open_selection(a);
        return 2;
    }

    return 1;
}

/**
 * @brief Keys for the inline rename editor while it is open: every key goes
 *        to the editor, except Tab and Shift+Tab, which move the edit to
 *        the next or previous row, and Cmd+N, which commits it and goes on
 *        to New folder.
 * @return 2 when the editor took the key, 1 when no rename is in progress
 *         or Cmd+N ended it.
 */
int
browser_key_rename(app_t *a, const tiku_event_t *event, unsigned key,
                   unsigned modifiers, unsigned mods)
{
    (void)mods;
    if (a->editing) {
        /* While a name is being edited the view's keys are off: an arrow
         * moves the caret, and 'd' types a letter. */
        tiku_field_key_t k = TIKU_FIELD_CHAR;
        int shift = (modifiers & TIKU_MOD_SHIFT) ? 1 : 0;
        char ch = '\0';

        if ((modifiers & TIKU_MOD_CMD) &&
            (key == 'x' || key == 'c' || key == 'v')) {
            tiku_field_key_t text_key = (key == 'x') ? TIKU_FIELD_CUT
                                      : (key == 'c') ? TIKU_FIELD_COPY
                                                     : TIKU_FIELD_PASTE;
            if (tiku_field_key(&a->ed, text_key, 0, '\0') ==
                TIKU_FIELD_REJECTED) {
                /* A paste that does not fit is refused whole, with a
                 * note in the status line. */
                snprintf(a->status, sizeof a->status,
                         "The clipboard does not fit this field.");
            }
            a->dirty = 1;
            return 2;
        }
        if ((modifiers & TIKU_MOD_CMD) && key == 'n') {
            browser_end_rename(a, 1);
            goto not_editing;
        }
        if (key == TIKU_KEY_TAB) {
            int row = browser_next_edit_row(a, a->edit_row,
                                    (modifiers & TIKU_MOD_SHIFT) != 0u);

            browser_end_rename(a, 1);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                browser_begin_rename(a);
            }
            a->dirty = 1;
            return 2;
        }
        switch (key) {
        case TIKU_KEY_RETURN: k = TIKU_FIELD_COMMIT;    break;
        case TIKU_KEY_ESCAPE: k = TIKU_FIELD_CANCEL;    break;
        case TIKU_KEY_BACKSPACE: k = TIKU_FIELD_BACKSPACE; break;
        case TIKU_KEY_DELETE: k = TIKU_FIELD_DELETE;    break;
        case TIKU_KEY_LEFT:   k = TIKU_FIELD_LEFT;      break;
        case TIKU_KEY_RIGHT:  k = TIKU_FIELD_RIGHT;     break;
        case TIKU_KEY_HOME:   k = TIKU_FIELD_HOME;      break;
        case TIKU_KEY_END:    k = TIKU_FIELD_END;       break;
        case 'a':
            if (modifiers & TIKU_MOD_CMD) {
                k = TIKU_FIELD_SELECT_ALL;
            }
            break;
        default: break;
        }
        if (k == TIKU_FIELD_CHAR) {
            ch = browser_key_char(event);
            if (ch == '\0') {
                return 2;
            }
        }
        switch (tiku_field_key(&a->ed, k, shift, ch)) {
        case TIKU_FIELD_COMMITTED: browser_end_rename(a, 1); break;
        case TIKU_FIELD_CANCELLED: browser_end_rename(a, 0); break;
        default: break;
        }
        a->dirty = 1;
        return 2;
    }

not_editing:
    return 1;
}

/**
 * @brief Keys for the location field while it is being edited: every key
 *        goes to the field, and Cmd+Space completes the last component.
 * @return 2 when the field took the key, 1 when it is not being edited.
 */
int
browser_key_location(app_t *a, const tiku_event_t *event, unsigned key,
                     unsigned modifiers, unsigned mods)
{
    (void)mods;
    if (a->nav_editing) {
        tiku_field_key_t k = TIKU_FIELD_CHAR;
        tiku_field_result_t result;
        int shift = (modifiers & TIKU_MOD_SHIFT) ? 1 : 0;
        char ch = '\0';

        if (key == ' ' && (modifiers & TIKU_MOD_CMD) != 0u) {
            /* Cmd-Space completes the last path component against the
             * folder the rest of the path names (NeXT UIG ch3). */
            char dir[TIKU_PATH_MAX], full[TIKU_PATH_MAX];
            char *slash;

            snprintf(dir, sizeof dir, "%s", a->nav_edit.text);
            slash = strrchr(dir, '/');
            if (slash != NULL) {
                char leaf[TIKU_NAME_MAX];

                snprintf(leaf, sizeof leaf, "%s", slash + 1);
                slash[(slash == dir) ? 1 : 0] = '\0';
                if (tiku_trk_panel_complete(a->backend, dir, leaf, leaf,
                                            sizeof leaf) > 0) {
                    snprintf(full, sizeof full, "%s%s%s", dir,
                             (dir[strlen(dir) - 1u] == '/') ? "" : "/",
                             leaf);
                    tiku_field_start(&a->nav_edit, full);
                    a->nav_edit.sel_a = a->nav_edit.sel_b =
                        a->nav_edit.caret = a->nav_edit.len;
                }
            }
            a->dirty = 1;
            return 2;
        }
        switch (key) {
        case TIKU_KEY_RETURN: k = TIKU_FIELD_COMMIT;     break;
        case TIKU_KEY_ESCAPE: k = TIKU_FIELD_CANCEL;     break;
        case TIKU_KEY_BACKSPACE: k = TIKU_FIELD_BACKSPACE; break;
        case TIKU_KEY_DELETE: k = TIKU_FIELD_DELETE;     break;
        case TIKU_KEY_LEFT:   k = TIKU_FIELD_LEFT;       break;
        case TIKU_KEY_RIGHT:  k = TIKU_FIELD_RIGHT;      break;
        case TIKU_KEY_HOME:   k = TIKU_FIELD_HOME;       break;
        case TIKU_KEY_END:    k = TIKU_FIELD_END;        break;
        case 'a':
            if (modifiers & TIKU_MOD_CMD) {
                k = TIKU_FIELD_SELECT_ALL;
            }
            break;
        default: break;
        }
        if (k == TIKU_FIELD_CHAR) {
            ch = browser_key_char(event);
            if (ch == '\0') {
                return 2;
            }
        }
        result = tiku_field_key(&a->nav_edit, k, shift, ch);
        if (result == TIKU_FIELD_COMMITTED) {
            browser_end_location(a, 1);
        } else if (result == TIKU_FIELD_CANCELLED) {
            browser_end_location(a, 0);
        }
        a->dirty = 1;
        return 2;
    }
    return 1;
}
