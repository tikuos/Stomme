/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_automation.c - driving the desktop from outside: the add-on
 * inventory and its shortcuts, the scripting command file, and running an
 * add-on on the selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/**
 * @brief Keep the add-on inventory true to the add-ons directory and the
 *        user's shortcut file, re-reading either when its modification
 *        time changes; checked at most every half second unless @p force
 *        (AW-022, AW-023, CW-038).
 */
void
sync_addons(desktop_t *app, int force)
{
    struct stat st;
    int64_t now = tiku_trk_now();

    if (!force && now - app->addons_checked < 500000) {
        return;
    }
    app->addons_checked = now;
    if (app->addons_dir[0] != '\0') {
        time_t mtime = (stat(app->addons_dir, &st) == 0) ? st.st_mtime : 0;

        if (force || mtime != app->addons_mtime) {
            app->addons_mtime = mtime;
            (void)tiku_trk_addons_discover(&app->addons, app->addons_dir);
            desktop_damage_all(app);
        }
    }
    if (app->addons_keys[0] != '\0') {
        time_t mtime = (stat(app->addons_keys, &st) == 0) ? st.st_mtime : 0;

        if ((force || mtime != app->keys_mtime) && mtime != 0) {
            FILE *f = fopen(app->addons_keys, "r");

            app->keys_mtime = mtime;
            if (f != NULL) {
                char buf[2048];
                size_t got = fread(buf, 1u, sizeof buf - 1u, f);

                buf[got] = '\0';
                (void)fclose(f);
                (void)tiku_trk_addons_rebind(&app->addons, buf);
                desktop_damage_all(app);
            }
        }
    }
}

/** @brief Dispatch a native file-manager script through its current folder
 * owner.
 */
void desktop_script_request(desktop_t *app, folder_t *folder,
                            const tiku_trk_req_t *req, tiku_trk_rep_t *rep)
{
    int i;
    memset(rep,0,sizeof *rep);
    if(!folder||!folder->view){rep->error=TIKU_TRK_ERR_NOT_FOUND;return;}
    tiku_trk_script_run(folder->view,req,rep);
    if(rep->error!=TIKU_TRK_ERR_OK)return;
    if (req->verb == TIKU_TRK_VERB_EXECUTE) {
        for (i = 0; i < rep->nresult; i++) {
            tiku_model_t m;

            if (app->backend->ops->stat(app->backend, rep->result[i],
                                        &m) != 0) {
                continue;
            }
            if (tiku_model_is_container(&m)) {
                (void)open_folder(app, rep->result[i]);
            } else {
                (void)open_one(app, &m);
            }
        }
    } else if (req->verb == TIKU_TRK_VERB_DELETE) {
        const char *sel[64];
        int n = 0;

        for (i = 0; i < rep->nresult && n < 64; i++) {
            sel[n++] = rep->result[i];
        }
        if (n > 0) {
            (void)tiku_fs_trash(app->fs, sel, n);
        }
    }
    desktop_damage_all(app);
}

/**
 * @brief Run one scripting line against the focused folder window, or the
 *        first folder (PVK-052).
 *
 * The scripting suite resolves what a verb acts on and names it; this
 * acts on the names: execute opens each folder with open_folder() and each
 * other item with open_one(), and delete moves them to the Trash.  The
 * outcome, errors included, is written to @p reply (PVK-054).
 */
static void
script_command_line(desktop_t *app, const char *line, FILE *reply)
{
    char store[8][TIKU_PATH_MAX];
    tiku_trk_req_t req;
    tiku_trk_rep_t rep;
    folder_t *fl = NULL;
    int i;

    {
        tiku_window_t *fw = tiku_workspace_focused(app->desk);

        fl = folder_of(app, fw);
        if (fl == NULL && app->folder_count > 0) {
            fl = &app->folders[0];
        }
    }
    if (fl == NULL || fl->view == NULL ||
        tiku_trk_script_parse(line, &req, store) != 0) {
        if (reply != NULL) {
            fprintf(reply, "error unhandled\n");
        }
        return;
    }
    desktop_script_request(app,fl,&req,&rep);
    if(rep.error!=TIKU_TRK_ERR_OK) {
        if(reply)fprintf(reply,"error %d\n",rep.error);
        return;
    }
    if (reply != NULL) {
        fprintf(reply, "ok %d\n", rep.nresult);
        for (i = 0; i < rep.nresult; i++) {
            fprintf(reply, "%s\n", rep.result[i]);
        }
    }
    desktop_damage_all(app);
}

/**
 * @brief Run every line of the command file @p path, skipping blank lines
 *        and # comments, and write the replies to <path>.reply (PVK-052).
 */
void
script_command_file(desktop_t *app, const char *path)
{
    FILE *f = fopen(path, "r");
    FILE *reply = NULL;
    char rpath[TIKU_PATH_MAX];
    char line[512];

    if (f == NULL) {
        return;
    }
    snprintf(rpath, sizeof rpath, "%s.reply", path);
    reply = fopen(rpath, "w");
    while (fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');

        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] != '\0' && line[0] != '#') {
            script_command_line(app, line, reply);
        }
    }
    (void)fclose(f);
    if (reply != NULL) {
        (void)fclose(reply);
    }
}

/**
 * @brief An empty add-on table holding the desktop's built-in add-ons, each
 *        with the command desktop_invoke_addon() runs it by.
 */
void
desktop_addons_register(desktop_t *app)
{
    tiku_trk_addons_init(&app->addons);
    (void)tiku_trk_addons_register(&app->addons, "Open in Tracker", "open",
                                   "inode/directory", 'o');
    (void)tiku_trk_addons_register(&app->addons, "Open with Editor",
                                   "editor", "text/plain,text/markdown",
                                   'e');
    (void)tiku_trk_addons_register(&app->addons, "Inspect", "get-info", "*",
                                   'i');
}

/**
 * @brief Run @p addon on the focused folder's selection.  An external one
 *        runs in a process of its own.  A built-in one runs by its command:
 *        "open" is the desktop's Open, "get-info" is Get Info, and "editor"
 *        opens the selection in the Editor application through the
 *        launcher.  A run that fails is reported on the status line.
 * @return 1 for any @p addon, run or not; 0 when @p addon or @p context is
 *         NULL.
 */
int
desktop_invoke_addon(const tiku_trk_addon_t *addon, void *context)
{
    desktop_t *app = context;
    char paths[64][TIKU_PATH_MAX];
    const char *sel[64];
    folder_t *fl;
    int n, i;

    if (addon == NULL || app == NULL) {
        return 0;
    }
    fl = folder_of(app, tiku_workspace_focused(app->desk));
    if (fl == NULL) {
        fl = desktop_folder(app);
    }
    if (!addon->external && strcmp(addon->command, "open") == 0) {
        menu_invoke(app, fl, TIKU_TRK_CMD_OPEN);
        return 1;
    }
    if (!addon->external && strcmp(addon->command, "get-info") == 0) {
        menu_invoke(app, fl, TIKU_TRK_CMD_GET_INFO);
        return 1;
    }
    if (!addon->external && strcmp(addon->command, "editor") == 0) {
        n = tiku_trk_folder_file_actions_selection(fl, paths, sel, 64);
        for (i = 0; i < app->apps.count; i++) {
            if (strcmp(app->apps.app[i].id, "editor") == 0) {
                (void)tiku_trk_launch_controller_run(&app->launcher,
                    &app->apps.app[i], sel, n);
                return 1;
            }
        }
        snprintf(app->status, sizeof app->status,
                 "Application 'editor' is not installed.");
        return 1;
    }
    if (addon->external) {
        n = tiku_trk_folder_file_actions_selection(fl, paths, sel, 64);
        if (tiku_trk_addons_run_external(addon, sel, n) == 0) {
            snprintf(app->status, sizeof app->status,
                     "running %.48s on %d item%s", addon->name, n,
                     (n == 1) ? "" : "s");
        } else {
            snprintf(app->status, sizeof app->status,
                     "could not run %.60s", addon->name);
        }
        return 1;
    }
    snprintf(app->status, sizeof app->status, "could not run %.60s",
             addon->name);
    return 1;
}
