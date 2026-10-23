/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_commands.c - the commands: the menu bar, the open popup, the
 * context menu, the navigator strip, and the commands this shell carries out
 * itself (mount, unmount, new folder and templates).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/* Used before they are defined. */
int browser_cmd_template(app_t *a, int command, const char *template_path,
                         const char **template_parent);
int browser_cmd_new_folder(app_t *a, int command);
int browser_cmd_unmount(app_t *a, int command);
int browser_cmd_mount(app_t *a, int command);
/* End of the forward declarations. */

/** @brief Unmount callback: save the view's state when it shows the
 *         leaving volume, before the volume goes (AW-096). */
static void
save_before_unmount(dev_t dev, void *context)
{
    app_t *a = context;

    if (tiku_trk_poseview_on_device(a->view, (uint64_t)dev)) {
        (void)tiku_trk_poseview_save_state(a->view);
    }
}

/**
 * @brief Where a new entry goes in the icon modes (PVL-025, PVL-098): the
 *        open menu's corner, which is where the asking click was, else the
 *        last press.
 * @return 1 with the point in view coordinates; 0 in list mode or when the
 *         point is outside the view.
 */
static int
seed_new_entry(app_t *a, int *vx, int *vy)
{
    tiku_rect_t lr = browser_list_rect(a);
    int x, y;

    if (tiku_trk_poseview_mode(a->view) == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (a->popup != NULL) {
        x = a->popup_frame.x;
        y = a->popup_frame.y;
    } else {
        x = a->press_x;
        y = a->press_y;
    }
    if (x < lr.x || x >= lr.x + lr.w || y < lr.y || y >= lr.y + lr.h) {
        return 0;
    }
    *vx = x - lr.x;
    *vy = y - lr.y;
    return 1;
}

/** @brief Carry out a command from the shared container-window menus. */
void
browser_menu_invoke(app_t *a, int command)
{
    const char *template_path;
    const char *template_parent;
    int field = -1;

    {
        /* Every route here, except for the three commands below, passes
         * the menus' enable rules: a chord for a command the menu shows
         * grey puts a refusal in the status line (NeXT UIG ch6). */
        tiku_trk_sc_ctx_t gc = browser_menu_ctx(a, 0u);

        /* Unmount, Move to Trash and Delete are exempt: their handlers
         * apply wider rules and report their own refusals.  Unmount acts
         * on the volume shown when nothing is selected (AW-096); Move to
         * Trash and Delete send a selected volume to an unmount, refusing
         * the boot volume (FS-041). */
        if (command != TIKU_TRK_CMD_UNMOUNT &&
            command != TIKU_TRK_CMD_MOVE_TO_TRASH &&
            command != TIKU_TRK_CMD_DELETE &&
            !tiku_trk_sc_enabled(command, &gc)) {
            snprintf(a->status, sizeof a->status,
                     "%s is not available here",
                     tiku_trk_sc_label(command, 0u));
            a->dirty = 1;
            return;
        }
    }
    if (browser_cmd_mount(a, command)) {
        return;
    }
    if (browser_cmd_unmount(a, command)) {
        return;
    }
    if (browser_cmd_new_folder(a, command)) {
        return;
    }
    /* The shared controller first: Delete, Duplicate, Create link,
     * Identify and the clipboard verbs run the code the desktop shell
     * runs. */
    a->folder.view = a->view;
    snprintf(a->folder.path, sizeof a->folder.path, "%s",
             tiku_trk_poseview_path(a->view));
    if (tiku_trk_folder_file_actions_invoke(&a->file_actions, &a->folder,
                                            command)) {
        /* No reload here: a command's changes arrive as change notices
         * and through browser_after_op() once the engine is idle, and a
         * reload clears the selection the command leaves, which holds the
         * cut it tints and the rows the modal delete question reads when
         * answered (FS-046, FS-060). */
        a->dirty = 1;
        return;
    }

    template_path = tiku_trk_cwmenu_template_path(&a->menus, command);
    if (browser_cmd_template(a, command, template_path, &template_parent)) {
        return;
    }
    switch (command) {
    case TIKU_TRK_CMD_COPY_LAYOUT:
        (void)tiku_trk_poseview_copy_layout(a->view);
        a->dirty = 1;
        return;
    case TIKU_TRK_CMD_PASTE_LAYOUT:
        if (tiku_trk_poseview_paste_layout(a->view) > 0) {
            (void)tiku_trk_poseview_save_columns(a->view);
            snprintf(a->status, sizeof a->status, "pasted layout");
        }
        a->dirty = 1;
        return;
    case TIKU_TRK_CMD_NEW_FOLDER:
        browser_menu_key(a, 'n', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_OPEN:
        browser_menu_key(a, TIKU_KEY_RETURN, 0u); return;
    case TIKU_TRK_CMD_EDIT_NAME:
        browser_menu_key(a, TIKU_KEY_F2, 0u); return;
    case TIKU_TRK_CMD_DUPLICATE:
        browser_menu_key(a, 'd', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_MOVE_TO_TRASH:
        browser_menu_key(a, TIKU_KEY_DELETE, 0u); return;
    case TIKU_TRK_CMD_DELETE:
        browser_menu_key(a, TIKU_KEY_DELETE, TIKU_MOD_SHIFT); return;
    case TIKU_TRK_CMD_SELECT_ALL:
        browser_menu_key(a, 'a', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_INVERT_SELECTION:
        browser_menu_key(a, 's', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_OPEN_PARENT:
        browser_menu_key(a, TIKU_KEY_UP, TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_ICON_VIEW:
        browser_menu_key(a, '1', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_MINI_ICON_VIEW:
        browser_menu_key(a, '2', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_LIST_VIEW:
        browser_menu_key(a, '3', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_CLEANUP:
        browser_menu_key(a, 'k', TIKU_MOD_CMD); return;
    case TIKU_TRK_CMD_CLEANUP_ALL:
        browser_menu_key(a, 'k', TIKU_MOD_CMD | TIKU_MOD_SHIFT); return;
    case TIKU_TRK_CMD_CLOSE:
    case TIKU_TRK_CMD_CLOSE_ALL:
    case TIKU_TRK_CMD_CLOSE_ALL_WORKSPACE:
        a->running = 0;
        return;
    case TIKU_TRK_CMD_ICON_SIZE_32:  field = 32; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_40:  field = 40; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_48:  field = 48; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_64:  field = 64; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_96:  field = 96; goto icon_size;
    case TIKU_TRK_CMD_ICON_SIZE_128: field = 128; goto icon_size;
    case TIKU_TRK_CMD_SORT_BY_NAME:  field = TIKU_TRK_COL_NAME; break;
    case TIKU_TRK_CMD_SORT_BY_KIND:  field = TIKU_TRK_COL_KIND; break;
    case TIKU_TRK_CMD_SORT_BY_SIZE:  field = TIKU_TRK_COL_SIZE; break;
    case TIKU_TRK_CMD_SORT_BY_VALUE: field = TIKU_TRK_COL_VALUE; break;
    case TIKU_TRK_CMD_SORT_BY_CAP:   field = TIKU_TRK_COL_CAP; break;
    case TIKU_TRK_CMD_REVERSE_ORDER:
        field = tiku_trk_poseview_sort_field(a->view);
        break;
    case TIKU_TRK_CMD_COL_KIND:  field = TIKU_TRK_COL_KIND; goto column;
    case TIKU_TRK_CMD_COL_VALUE: field = TIKU_TRK_COL_VALUE; goto column;
    case TIKU_TRK_CMD_COL_SIZE:  field = TIKU_TRK_COL_SIZE; goto column;
    case TIKU_TRK_CMD_COL_CAP:   field = TIKU_TRK_COL_CAP; goto column;
    case TIKU_TRK_CMD_COL_DATE:  field = TIKU_TRK_COL_DATE; goto column;
    case TIKU_TRK_CMD_COL_PATH:  field = TIKU_TRK_COL_PATH; goto column;
    case TIKU_TRK_CMD_COL_PERM:  field = TIKU_TRK_COL_PERM; goto column;
    case TIKU_TRK_CMD_COL_ORIGINAL: field = TIKU_TRK_COL_ORIGINAL; goto column;
    case TIKU_TRK_CMD_FIND:
    case TIKU_TRK_CMD_GET_INFO:
    case TIKU_TRK_CMD_PREFERENCES:
        snprintf(a->status, sizeof a->status, "%s is not available in this shell",
                 tiku_trk_sc_label(command, 0u));
        break;
    default:
        break;
    }
    if (field >= 0) {
        tiku_trk_poseview_sort_by(a->view, field);
    }
    browser_settle_layout(a);
    (void)tiku_trk_poseview_save_state(a->view);
    a->dirty = 1;
    return;

column:
    (void)tiku_trk_poseview_toggle_column(a->view, field);
    (void)tiku_trk_poseview_save_columns(a->view);
    a->dirty = 1;
    return;

icon_size:
    tiku_trk_poseview_set_mode(a->view, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(a->view, field);
    browser_settle_layout(a);
    (void)tiku_trk_poseview_save_state(a->view);
    a->dirty = 1;
}

/**
 * @brief A press on the menu bar: the title under (@p x, @p y) opens its
 *        menu, rebuilt for the current selection and the modifiers held.
 *        @return 1 when a title was hit, 0 when the press was elsewhere.
 */
int
browser_menubar_press(app_t *a, int x, int y, unsigned state)
{
    tiku_rect_t bar = browser_menubar_rect(a);
    tiku_trk_sc_ctx_t c;
    unsigned mods = 0u;
    int which;

    which = tiku_menubar_title_at(&a->menus.bar, bar, x, y);
    if (which < 0) {
        return 0;
    }
    if (state & TIKU_MOD_SHIFT) { mods |= TIKU_MENU_MOD_SHIFT; }
    if (state & TIKU_MOD_CMD) { mods |= TIKU_MENU_MOD_CMD; }
    c = browser_menu_ctx(a, mods);
    tiku_trk_cwmenu_rebuild(&a->menus, &c);
    a->bar_open = which;
    a->menus.bar.open = which;
    a->popup = a->menus.bar.menu[which];
    a->popup_sub = NULL;
    a->popup_frame = tiku_menubar_panel(&a->menus.bar, bar,
                         (tiku_rect_t){ 0, 0, a->surf->w, a->surf->h });
    a->dirty = 1;
    return 1;
}

/**
 * @brief Route a press to the open menu, or to its open submenu: a row with
 *        a submenu opens it, another enabled row runs its command, and any
 *        other press closes the menu.
 * @return 1 while a menu is open, else 0.
 */
int
browser_popup_press(app_t *a, int x, int y)
{
    tiku_menu_t *m;
    tiku_rect_t frame;
    int item;

    if (a->popup == NULL) {
        return 0;
    }
    m = (a->popup_sub != NULL) ? a->popup_sub : a->popup;
    frame = (a->popup_sub != NULL) ? a->popup_sub_frame : a->popup_frame;
    item = tiku_menu_item_at(m, frame, x, y);
    if (item >= 0 && m->item[item].enabled) {
        if (m->item[item].submenu != NULL) {
            m->open_index = item;
            a->popup_sub = m->item[item].submenu;
            a->popup_sub_frame = tiku_menu_place_sub(
                m, item, frame,
                (tiku_rect_t){ 0, 0, a->surf->w, a->surf->h });
            a->dirty = 1;
            return 1;
        }
        browser_menu_invoke(a, m->item[item].command);
    }
    a->popup = NULL;
    a->popup_sub = NULL;
    a->bar_open = -1;
    a->menus.bar.open = -1;
    a->dirty = 1;
    return 1;
}

/** @brief Open the context menu of pose @p row, or of the background when
 *         @p row is -1, at (@p x, @p y). */
void
browser_open_context_menu(app_t *a, int row, int x, int y, unsigned state)
{
    tiku_trk_pose_t *p = (row >= 0)
        ? tiku_trk_poseview_at(a->view, row) : NULL;
    unsigned mods = 0u;
    tiku_trk_sc_ctx_t c;

    if (state & TIKU_MOD_SHIFT) { mods |= TIKU_MENU_MOD_SHIFT; }
    if (state & TIKU_MOD_CMD) { mods |= TIKU_MENU_MOD_CMD; }
    c = browser_menu_ctx(a, mods);
    tiku_trk_cwmenu_rebuild(&a->menus, &c);
    a->popup = tiku_trk_cwmenu_context(&a->menus,
                                       (p != NULL) ? &p->model : NULL, &c);
    a->popup_sub = NULL;
    a->bar_open = -1;
    a->menus.bar.open = -1;
    a->popup_frame = tiku_menu_place(a->popup, x, y,
        (tiku_rect_t){ 0, 0, a->surf->w, a->surf->h });
    a->dirty = 1;
}

/**
 * @brief A press on the navigator strip: the location field takes the
 *        keys, or Back, Forward or Up runs when it is enabled.
 * @return 1 when the press was on the strip.
 */
int
browser_navigator_press(app_t *a, int x, int y)
{
    tiku_rect_t nav = browser_navigator_rect(a);
    tiku_rect_t back, forward, up, field;

#define INSIDE(r) (x >= (r).x && x < (r).x + (r).w && \
                   y >= (r).y && y < (r).y + (r).h)
    if (!a->show_nav || !INSIDE(nav)) {
        return 0;
    }
    browser_navigator_layout(a, &back, &forward, &up, &field);
    if (INSIDE(field)) {
        browser_begin_location(a);
    } else {
        browser_end_location(a, 0);
        if (INSIDE(back) && tiku_trk_navigator_can_back(&a->nav)) {
            browser_navigate_back(a);
        } else if (INSIDE(forward) &&
                   tiku_trk_navigator_can_forward(&a->nav)) {
            browser_navigate_forward(a);
        } else if (INSIDE(up) &&
                   strcmp(tiku_trk_navigator_current(&a->nav), "/") != 0) {
            browser_navigate_up(a);
        }
    }
    a->dirty = 1;
#undef INSIDE
    return 1;
}

/**
 * @brief A new entry from a template, a new template submenu, or Edit
 *        Templates.
 * @return 1 when the command was one of these.
 */
int
browser_cmd_template(app_t *a, int command, const char *template_path,
                     const char **template_parent)
{
    if (template_path != NULL) {
        char made[TIKU_PATH_MAX], error[200];
        const char *slash;
        int row;

        if (tiku_fs_new_from_template(
                a->fs, template_path, tiku_trk_poseview_path(a->view),
                made, sizeof made, error, sizeof error) == 0) {
            slash = strrchr(made, '/');
            (void)tiku_trk_poseview_reload(a->view);
            row = browser_index_of_path(a, made);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                browser_begin_rename(a);
            }
            snprintf(a->status, sizeof a->status, "created '%.180s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(a->status, sizeof a->status, "%.200s",
                     error[0] != '\0' ? error : "could not create from template");
        }
        a->dirty = 1;
        return 1;
    }
    (*template_parent) = tiku_trk_cwmenu_template_add_path(&a->menus, command);
    if ((*template_parent) != NULL) {
        char made[TIKU_PATH_MAX], error[200];

        if (tiku_fs_new_folder(a->fs, (*template_parent), made,
                                   sizeof made, error, sizeof error) == 0) {
            const char *root = tiku_trk_cwmenu_templates_root(&a->menus);

            snprintf(a->status, sizeof a->status,
                     "created template submenu '%.150s'", strrchr(made, '/')
                     != NULL ? strrchr(made, '/') + 1 : made);
            if (root != NULL && browser_navigate_to(a, (*template_parent), 1)) {
                int row = browser_index_of_path(a, made);

                if (row >= 0) {
                    tiku_trk_poseview_click(a->view, row, 0u);
                }
            }
        } else {
            snprintf(a->status, sizeof a->status, "%.200s",
                     error[0] != '\0' ? error :
                     "could not create template submenu");
        }
        a->dirty = 1;
        return 1;
    }
    if (command == TIKU_TRK_CMD_EDIT_TEMPLATES) {
        const char *root = tiku_trk_cwmenu_templates_root(&a->menus);

        if (root != NULL) {
            (void)browser_navigate_to(a, root, 1);
        }
        a->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief New folder: made in the folder shown, placed where the asking
 *        click was in the icon modes, and opened for renaming.
 * @return 1 when the command was New folder.
 */
int
browser_cmd_new_folder(app_t *a, int command)
{
    if (command == TIKU_TRK_CMD_NEW_FOLDER) {
        /* Made here, where the asking click is known, so the folder is
         * placed at it (PVL-025, PVL-098). */
        char made[TIKU_PATH_MAX], error[200];
        int seeded, sx = 0, sy = 0, row;
        const char *slash;

        error[0] = '\0';
        seeded = seed_new_entry(a, &sx, &sy);
        if (a->fs != NULL && tiku_fs_new_folder(a->fs,
                tiku_trk_poseview_path(a->view), made, sizeof made,
                error, sizeof error) == 0) {
            slash = strrchr(made, '/');
            if (seeded) {
                tiku_trk_poseview_drop_land(a->view,
                    (slash != NULL) ? slash + 1 : made, sx, sy);
            }
            (void)tiku_trk_poseview_reload(a->view);
            /* tiku_trk_poseview_reload() reads the folder in one pass,
             * not in the batches browser_turn_end() loads, so the landing
             * recorded above is applied here or not at all. */
            browser_settle_layout(a);
            row = browser_index_of_path(a, made);
            if (row >= 0) {
                tiku_trk_poseview_click(a->view, row, 0u);
                browser_begin_rename(a);
            }
            snprintf(a->status, sizeof a->status, "created '%.180s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(a->status, sizeof a->status, "%.200s",
                error[0] != '\0' ? error : "could not create a new folder");
        }
        a->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief Unmount: the selected volume, else the volume whose root is
 *        shown.
 * @return 1 when the command was Unmount.
 */
int
browser_cmd_unmount(app_t *a, int command)
{
    if (command == TIKU_TRK_CMD_UNMOUNT) {
        /* The selection names the volume; with none, the volume root
         * shown (CW-034).  The view's state is saved before the volume
         * goes, and a refusal with a reason goes up as an alert
         * (AW-096). */
        char paths[1][TIKU_PATH_MAX];
        const char *sel[1];
        const char *path = (browser_selection_paths(a, paths, sel, 1) > 0)
                               ? sel[0] : tiku_trk_poseview_path(a->view);
        tiku_volumes_t vols;
        char why[200];

        why[0] = '\0';
        if (tiku_volumes_scan(&vols) > 0 &&
            tiku_volume_is_root(&vols, path)) {
            const tiku_volume_t *vol = tiku_volume_of(&vols, path);

            if (tiku_volume_unmount(&vols, path, save_before_unmount,
                                        a, why, sizeof why) ==
                    TIKU_UNMOUNT_OK) {
                snprintf(a->status, sizeof a->status, "unmounted \"%.60s\"",
                         (vol != NULL) ? vol->name : path);
            } else if (why[0] != '\0') {
                tiku_sound_play(TIKU_SOUND_BEEP);
                tiku_alert_open(
                    tiku_trk_status_overlay_alert(&a->status_overlay),
                    TIKU_ALERT_STOP, TIKU_TRK_ALERT_TAG_REFUSAL, why,
                    "Cancel");
            }
        } else {
            /* Nothing here is a volume: the refusal goes to the status
             * line (NeXT UIG ch6). */
            snprintf(a->status, sizeof a->status,
                     "Unmount is not available here");
        }
        a->dirty = 1;
        return 1;
    }
    return 0;
}

/**
 * @brief Mount settings, Mount all, and mounting one partition from the
 *        Mount submenu.
 * @return 1 when the command was one of these.
 */
int
browser_cmd_mount(app_t *a, int command)
{
    if (command == TIKU_TRK_CMD_MOUNT_SETTINGS) {
        snprintf(a->status, sizeof a->status,
                 "mount settings live in the desktop shell's preferences");
        a->dirty = 1;
        return 1;
    }
    if (command == TIKU_TRK_CMD_MOUNT_ALL ||
        (command >= TIKU_TRK_CMD_MOUNT_PART_BASE &&
         command < TIKU_TRK_CMD_MOUNT_PART_BASE +
                       TIKU_TRK_MOUNT_PART_MAX)) {
        /* Mounting is asked of the system; Mount all stops at the first
         * failure, and a failure with a reason, such as a missing
         * permission, goes up as an alert (CW-034). */
        char why[200];
        int failed = 0, tried = 0;

        why[0] = '\0';
        if (command == TIKU_TRK_CMD_MOUNT_ALL) {
            tiku_partition_t part[TIKU_TRK_MOUNT_PART_MAX];
            int pn = tiku_partitions_scan(part,
                                              TIKU_TRK_MOUNT_PART_MAX);
            int k;

            for (k = 0; k < pn; k++) {
                if (part[k].mounted) {
                    continue;
                }
                tried++;
                if (tiku_volume_mount(part[k].name, why,
                                          sizeof why) != 0) {
                    failed++;
                    break;
                }
            }
        } else {
            int idx = command - TIKU_TRK_CMD_MOUNT_PART_BASE;

            if (idx < a->menus.mount_parts) {
                tried++;
                failed = tiku_volume_mount(a->menus.mount_part[idx],
                                               why, sizeof why) != 0;
            }
        }
        if (failed && why[0] != '\0') {
            tiku_sound_play(TIKU_SOUND_BEEP);
            tiku_alert_open(
                tiku_trk_status_overlay_alert(&a->status_overlay),
                TIKU_ALERT_STOP, TIKU_TRK_ALERT_TAG_REFUSAL, why,
                "Cancel");
        } else if (tried == 0) {
            snprintf(a->status, sizeof a->status,
                     "every partition is already mounted");
        }
        a->dirty = 1;
        return 1;
    }
    return 0;
}
