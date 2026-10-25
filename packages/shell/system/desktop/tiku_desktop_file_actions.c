/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_file_actions.c - what a folder window does to its entries:
 * the inline rename and value edit, the progress of file operations, new
 * folders and entries from templates, and the Move to, Copy to and Create
 * Link destinations picked from the menus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief Select what an operation made, once it shows up (AW-033).
 *
 * Retried on its period until the pose exists or the loop's timeout gives
 * up: the entry may still be arriving through the listing.
 *
 * @return 1 when there is nothing more to do, 0 to try again.
 */
static int
select_created(void *ctx)
{
    select_created_t *sc = ctx;
    char parent[TIKU_PATH_MAX];
    char *slash;
    int i;

    snprintf(parent, sizeof parent, "%s", sc->path);
    slash = strrchr(parent, '/');
    if (slash == NULL) {
        return 1;
    }
    if (slash == parent) { parent[1] = '\0'; } else { *slash = '\0'; }
    for (i = 0; i < sc->app->folder_count; i++) {
        folder_t *fl = &sc->app->folders[i];

        if (fl->win == NULL || !fl->win->open || fl->view == NULL ||
            strcmp(fl->path, parent) != 0) {
            continue;
        }
        slash = strrchr(sc->path, '/');
        if (select_named(sc->app, fl,
                         slash != NULL ? slash + 1 : sc->path) >= 0) {
            desktop_damage_all(sc->app);
            return 1;           /* found and selected: done */
        }
        return 0;               /* the window is there; the entry not yet */
    }
    return 1;                   /* no window shows the parent */
}

/**
 * @brief Finish a value edit: check the value against the node's
 *        descriptor, then write it.
 *
 * The check comes before the write, so a refusal names the range or the
 * spelling expected, and the device is not written to.
 */
void
commit_edit(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_edit_commit_value(&app->folder_edit, fl);
}

/**
 * @brief Start the inline rename of row @p row.
 *
 * A namespace node has no name of its own to change, so its rename is
 * refused before an editor opens over it.
 */
void
begin_rename(desktop_t *app, folder_t *fl, int row)
{
    tiku_trk_folder_edit_begin_rename(&app->folder_edit, fl, row);
}

/** @brief Finish the rename, keeping or discarding what was typed. */
void
commit_rename(desktop_t *app, int commit)
{
    tiku_trk_folder_edit_finish_rename(&app->folder_edit, commit);
}

/** @brief Begin editing the active pose's value, if it has one to edit. */
void
begin_edit(desktop_t *app, folder_t *fl)
{
    tiku_trk_folder_edit_begin_value(&app->folder_edit, fl);
}

/**
 * @brief The selected rows' paths, for an operation that acts on them.
 *
 * @return How many were collected.
 */
int
folder_selection(desktop_t *app, folder_t *fl,
                 char paths[][TIKU_PATH_MAX], const char **out, int max)
{
    (void)app;
    return tiku_trk_folder_file_actions_selection(fl, paths, out, max);
}

/**
 * @brief Where in the view a new entry goes, into @p vx, @p vy (PVL-025,
 *        PVL-098).
 *
 * The open menu's corner, which is where the click that asked for the
 * entry was; for a keyboard shortcut, which has no menu, the pointer's
 * place.  Either is used only while it is inside the window's content.  A
 * list view places nothing.
 *
 * @return 1 when a place was found.
 */
static int
seed_new_entry(desktop_t *app, folder_t *fl, int *vx, int *vy)
{
    tiku_rect_t c;
    int ox = 0, oy = 0, x, y;

    if (fl == NULL || fl->view == NULL ||
        tiku_trk_poseview_mode(fl->view) == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    if (app->popup != NULL) {
        x = app->popup_frame.x;
        y = app->popup_frame.y;
    } else {
        x = app->ptr_x;
        y = app->ptr_y;
    }
    c = tiku_trk_folder_window_content(fl);
    if (x < c.x || x >= c.x + c.w || y < c.y || y >= c.y + c.h) {
        return 0;
    }
    tiku_trk_poseview_origin(fl->view, &ox, &oy);
    *vx = x - (c.x + 2) + ox;
    *vy = y - (c.y + 2) + oy;
    return 1;
}

/** @brief Give the entry just made the place it was asked for. */
static void
land_new_entry(desktop_t *app, folder_t *fl, const char *made, int seeded,
               int vx, int vy)
{
    const char *slash = (made != NULL) ? strrchr(made, '/') : NULL;

    if (!seeded || fl == NULL || fl->view == NULL) {
        return;
    }
    (void)app;
    tiku_trk_poseview_drop_land(fl->view,
                                (slash != NULL) ? slash + 1 : made, vx, vy);
}

/**
 * @brief Each turn: keep the file engine's operation going on a worker,
 *        raise its questions, update the status line and the progress
 *        window, select what a finished operation made, and give the focus
 *        back when the progress window closes.
 */
void
desktop_progress_tick(desktop_t *app)
{
    int busy = (app->fs != NULL) && tiku_fs_busy(app->fs);
    int over = app->fs_was_busy && !busy && !app->fs_job;

    app->fs_was_busy = busy;
    if (app->fs != NULL) {
        tiku_fs_moved_pump(app->fs);
    }
    if (busy && !progress_model(app)->row[0].paused) {
        const tiku_progress_t *pr = tiku_fs_progress(app->fs);

        /* The stepping is a worker's, one job for the whole operation;
         * it stops when the engine asks, and is posted again once the
         * question is answered. */
        if (!app->fs_job && !pr->conflict && !pr->error_ask) {
            desktop_wire_fs_run(app);
        }
    }
    if (over || (busy && !progress_model(app)->row[0].paused)) {
        const tiku_progress_t *pr;

        if (over) {
            /* The operation just finished: what it made gets selected
             * where it lives, once it shows up (AW-033). */
            char made[8][TIKU_PATH_MAX];
            int nmade = tiku_fs_last_created(app->fs, made, 8);
            int mi;

            for (mi = 0; mi < nmade; mi++) {
                select_created_t *scx = malloc(sizeof *scx);

                if (scx == NULL) {
                    break;
                }
                scx->app = app;
                snprintf(scx->path, sizeof scx->path, "%s", made[mi]);
                (void)tiku_trk_run_periodic(app->loop, select_created,
                                            scx, free, 200000,
                                            5000000);
            }
        }
        /* The second half of a mixed paste waits for the engine to go
         * idle, and starts here (FS-055). */
        (void)tiku_trk_folder_file_actions_pump(&app->file_actions);
        pr = tiku_fs_progress(app->fs);
        if (pr->conflict) {
            /* The engine chose the form of the question; it is put up,
             * and the work waits for the answer. */
            if (!alert_model(app)->open) {
                tiku_trk_alert_for_conflict(alert_model(app), pr);
            }
        } else if (pr->error_ask) {
            if (!alert_model(app)->open) {
                tiku_trk_alert_for_error(alert_model(app), pr);
            }
        } else {
            snprintf(app->status, sizeof app->status, "%d of %d%s%s",
                     pr->done + pr->skipped, pr->total,
                     pr->skipped ? " (some skipped)" : "",
                     pr->failed ? " -- some failed" : "");
        }
        if (tiku_trk_progress_wanted(pr->op, pr->total)) {
            /* The progress window shows the destination beside the file
             * name (FS-069). */
            tiku_trk_progress_update(progress_model(app), 0, pr, 1,
                                     pr->to[0] != '\0' ? pr->to
                                     : app->prog_dest[0] != '\0'
                                         ? app->prog_dest : NULL);
            tiku_trk_progress_sample(progress_model(app), 0, pr->bytes,
                                     tiku_trk_now());
        }
        desktop_damage_all(app);
        if (tiku_trk_status_overlay_progress_opened(
                &app->status_overlay) == 0) {
            tiku_trk_status_overlay_progress_start(&app->status_overlay,
                                                   tiku_trk_now());
            /* The window in front when the job began gets the focus
             * back when it ends (FS-073). */
            app->prog_was_active = tiku_workspace_focused(app->desk);
        }
    } else if (progress_model(app)->open) {
        tiku_trk_progress_update(progress_model(app), 0, NULL, 0, NULL);
        tiku_trk_status_overlay_progress_finish(&app->status_overlay);
        if (app->prog_was_active != NULL &&
            app->prog_was_active->open) {
            tiku_workspace_activate(app->desk, app->prog_was_active);
        }
        app->prog_was_active = NULL;
        desktop_damage_all(app);
    }
}

/**
 * @brief Start an armed rename in the focused folder after its click interval.
 * @note Focus loss cancels a folder's pending rename.
 */
void
desktop_rename_pulse(desktop_t *app)
{
    int i;

    for (i = 0; i < app->folder_count; i++) {
        folder_t *fl = &app->folders[i];
        int due;

        if (fl->win == NULL || !fl->win->open ||
            fl->win != tiku_workspace_focused(app->desk)) {
            tiku_trk_poseview_cancel_edit_wait(fl->view);
            continue;
        }
        due = tiku_trk_poseview_edit_due(fl->view, tiku_trk_now());
        if (due >= 0 && !app->folder_edit.editing) {
            begin_rename(app, fl, due);
            desktop_damage_all(app);
        }
    }
}

/**
 * @brief Make a new folder in @p fl, place it where it was asked for,
 *        select it and start its rename.
 * @return 1 when @p command was New Folder.
 */
int
desktop_cmd_new_folder(desktop_t *app, folder_t *fl, int command, int seeded,
                       int seed_x, int seed_y)
{
    if (command == TIKU_TRK_CMD_NEW_FOLDER) {
        char made[TIKU_PATH_MAX], error[200];
        const char *slash;
        int row;

        error[0] = '\0';
        if (app->fs != NULL && tiku_fs_new_folder(
                app->fs, fl->path, made, sizeof made, error,
                sizeof error) == 0) {
            slash = strrchr(made, '/');
            snprintf(app->status, sizeof app->status, "created '%.150s'",
                     slash != NULL ? slash + 1 : made);
            land_new_entry(app, fl, made, seeded, seed_x, seed_y);
            (void)tiku_trk_poseview_reload(fl->view);
            row = select_named(app, fl,
                               slash != NULL ? slash + 1 : made);
            if (row >= 0) {
                begin_rename(app, fl, row);
                /* Set again: begin_rename() can replace the status
                 * line, which scripts read for what was made. */
                snprintf(app->status, sizeof app->status,
                         "created '%.150s'",
                         slash != NULL ? slash + 1 : made);
            }
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error :
                     "could not create a new folder");
        }
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief Work out where a new entry goes, then make one from
 *        @p template_path and start its rename, add a submenu folder to the
 *        templates, or open the templates folder.
 * @return 1 when @p command was one of these; New Folder only has its
 *         place worked out here.
 */
int
desktop_cmd_template(desktop_t *app, folder_t *fl, int command,
                     const char *template_path, const char **template_parent,
                     int *seeded, int *seed_x, int *seed_y)
{
    /* Where the person asked for it, worked out before the entry exists,
     * while a menu that makes it is still up: seed_new_entry() takes that
     * menu's corner, or the pointer for a shortcut (PVL-025, PVL-098). */
    if (template_path != NULL || command == TIKU_TRK_CMD_NEW_FOLDER) {
        (*seeded) = seed_new_entry(app, fl, &(*seed_x), &(*seed_y));
    }
    if (template_path != NULL) {
        char made[TIKU_PATH_MAX], error[200];
        const char *slash;
        int row;

        if (tiku_fs_new_from_template(app->fs, template_path, fl->path,
                                          made, sizeof made, error,
                                          sizeof error) == 0) {
            slash = strrchr(made, '/');
            land_new_entry(app, fl, made, (*seeded), (*seed_x), (*seed_y));
            (void)tiku_trk_poseview_reload(fl->view);
            row = select_named(app, fl,
                               slash != NULL ? slash + 1 : made);
            if (row >= 0) {
                begin_rename(app, fl, row);
            }
            snprintf(app->status, sizeof app->status, "created '%.150s'",
                     slash != NULL ? slash + 1 : made);
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error :
                     "could not create from template");
        }
        desktop_damage_all(app);
        return 1;
    }
    (*template_parent) = tiku_trk_cwmenu_template_add_path(&app->menus,
                                                           command);
    if ((*template_parent) != NULL) {
        char made[TIKU_PATH_MAX], error[200];

        if (tiku_fs_new_folder(app->fs, (*template_parent), made,
                                   sizeof made, error, sizeof error) == 0) {
            const char *root = tiku_trk_cwmenu_templates_root(&app->menus);

            snprintf(app->status, sizeof app->status,
                     "created template submenu '%.130s'", strrchr(made, '/')
                     != NULL ? strrchr(made, '/') + 1 : made);
            if (root != NULL) {
                folder_t *templates = open_folder(app, (*template_parent));
                const char *slash = strrchr(made, '/');

                if (templates != NULL && slash != NULL) {
                    (void)select_named(app, templates, slash + 1);
                }
            }
        } else {
            snprintf(app->status, sizeof app->status, "%.180s",
                     error[0] != '\0' ? error :
                     "could not create template submenu");
        }
        desktop_damage_all(app);
        return 1;
    }
    if (command == TIKU_TRK_CMD_EDIT_TEMPLATES) {
        const char *root = tiku_trk_cwmenu_templates_root(&app->menus);

        if (root != NULL) {
            (void)open_folder(app, root);
        }
        desktop_damage_all(app);
        return 1;
    }
    return 0;
}

/**
 * @brief Move to, Copy to or Create Link to a destination picked from the
 *        menu (CW-035); for any other command, set @p template_path to the
 *        template it names, or NULL.
 * @return 1 when @p command was a destination row.
 */
int
desktop_cmd_destination(desktop_t *app, folder_t *fl, int command,
                        const char **template_path)
{
    if (command >= TIKU_TRK_CMD_DEST_BASE &&
        command < TIKU_TRK_CMD_DEST_BASE + 3 * TIKU_TRK_CMD_DEST_MAX) {
        /* A picker row's path is the destination (CW-035): Move to and
         * Copy to go through fpanel_commit(), as the destination panel
         * does, and Create Link links the selection there. */
        int k = (command - TIKU_TRK_CMD_DEST_BASE) / TIKU_TRK_CMD_DEST_MAX;
        int at = (command - TIKU_TRK_CMD_DEST_BASE) % TIKU_TRK_CMD_DEST_MAX;
        const char *dest = app->menus.picker_path[k][at];

        if (at >= app->menus.picker_paths[k]) {
            return 1;
        }
        if (k == 2) {
            char paths[16][TIKU_PATH_MAX];
            const char *selection[16];
            int count = folder_selection(app, fl, paths, selection, 16);

            if (count > 0) {
                (void)tiku_fs_link(app->fs, selection, count, dest, 0);
                snprintf(app->status, sizeof app->status,
                         "linking %d item%s", count, count == 1 ? "" : "s");
                tiku_trk_recent_add(app->store, TIKU_TRK_RECENT_FOLDERS,
                                    dest);
            }
        } else {
            fpanel_commit(app, fl, (k == 0) ? TIKU_TRK_CMD_MOVE_TO
                                            : TIKU_TRK_CMD_COPY_TO, dest);
        }
        return 1;
    }
    (*template_path) = tiku_trk_cwmenu_template_path(&app->menus, command);
    return 0;
}
