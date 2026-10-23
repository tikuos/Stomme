/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_folder_windows.c - opening and closing windows: folder
 * windows through the folder lifecycle, the union Trash window, the close
 * that serves every kind of window, and the offer made for a broken link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"

/** @brief The directory above @p path into @p out; "/" for the root. */
void
parent_path(const char *path, char *out, size_t size)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, size, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= size) {
        n = size - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

/** @brief The launch controller's way to open @p path in a folder window. */
void
launch_open_path(void *context, const char *path)
{
    (void)open_folder(context, path);
}

/**
 * @brief Open the Deskbar menu folder at @p path for editing, making its
 *        directory first when local files serve the path.
 */
void
deskbar_edit_path(void *context, const char *path)
{
    desktop_t *app = context;

    {
        /*
         * mkdir() acts on the local filesystem, so it runs only when the
         * backend serving this path is the posix one.  The desktop's own
         * backend is the mount router, named "mount" for every path.
         */
        char local[TIKU_PATH_MAX];
        tiku_backend_t *serving =
            tiku_backend_serving(app->backend, path, local, sizeof local);

        if (serving != NULL && serving->ops != NULL &&
            strcmp(serving->ops->name, "posix") == 0) {
            char parent[TIKU_PATH_MAX];
            char *slash;

            snprintf(parent, sizeof parent, "%s", local);
            slash = strrchr(parent, '/');
            if (slash != NULL && slash != parent) {
                *slash = '\0';
                (void)mkdir(parent, 0700);
            }
            (void)mkdir(local, 0700);
        }
    }
    (void)open_folder(app, path);
}

/** @brief The file actions' way to close the windows on @p path and in it. */
void
file_actions_close_under(void *context, const char *path)
{
    close_windows_under(context, path);
}

/**
 * @brief Offer Get Info for a link that leads nowhere (AW-031).
 *
 * Raises an alert whose button opens Get Info on the link, where it can be
 * re-pointed.
 */
void
offer_broken_link(desktop_t *app, const char *path)
{
    snprintf(app->broken_open_path, sizeof app->broken_open_path, "%s",
             path);
    tiku_sound_play(TIKU_SOUND_BEEP);
    tiku_alert_open(alert_model(app), TIKU_ALERT_WARN,
                        TIKU_TRK_ALERT_TAG_BROKEN_OPEN,
                        tiku_str("alert.msg.broken_open"),
                        tiku_str("alert.btn.get_info"));
}

/**
 * @brief The union every volume's Trash makes (PVN-027), into @p out.
 *
 * Each Trash on the machine, up to eight, contributes its entries; the
 * models keep their real paths, so restore and delete act on the file
 * wherever its volume keeps it.
 *
 * @return how many models.
 */
static int
trash_union_models(desktop_t *app, tiku_model_t *out, int max)
{
    char dirs[8][TIKU_PATH_MAX];
    int ndirs = tiku_fs_trash_dirs(app->fs, dirs, 8);
    int i, n = 0;

    for (i = 0; i < ndirs; i++) {
        n += list_children(app->backend, dirs[i], out + n, max - n);
    }
    return n;
}

/**
 * @brief Re-derive a union Trash window from its contributors.
 * @return nonzero when the window's contents changed.
 */
int
trash_union_refresh(desktop_t *app, folder_t *fl)
{
    tiku_model_t *models;
    unsigned nf;
    int n;

    models = calloc(256, sizeof *models);
    if (models == NULL) {
        return 0;
    }
    n = trash_union_models(app, models, 256);
    nf = tiku_trk_poseview_apply_results(fl->view, models, n,
                                         tiku_trk_now());
    free(models);
    return nf != TIKU_TRK_NOTIFY_NOTHING;
}

/**
 * @brief Close window @p w, whatever kind it is: the file panel, an
 *        application's window, one of the desktop's own windows, an Info
 *        window, or a folder window.
 *
 * A folder window saves its state and its frame as it closes (CW-001,
 * CW-002), unless its folder has gone.
 */
void
close_window(desktop_t *app, tiku_window_t *w)
{
    folder_t *fl;
    int info_slot;

    if (w == NULL) {
        return;
    }
    if (w == tiku_trk_panel_window_handle(&app->file_panel)) {
        tiku_trk_panel_window_close(&app->file_panel);
        return;
    }
    {
        struct embed_win *ew = embed_win_of_window(app, w);

        if (ew != NULL) {
            embed_drop_window(ew, 1);
            return;
        }
    }
    if (tiku_remote_owner(&app->remote, w) != NULL) {
        tiku_remote_window_closed(&app->remote, w);
        tiku_workspace_close(app->desk, w);
        return;
    }
    if (w == tiku_trk_find_window_handle(&app->find_window)) {
        tiku_trk_find_window_close(&app->find_window);
        return;
    }
    if (w == tiku_trk_deskbar_prefs_window_handle(
                 &app->deskbar_prefs_window)) {
        tiku_trk_deskbar_prefs_window_close(&app->deskbar_prefs_window);
        return;
    }
    if (w == tiku_trk_desktop_prefs_window_handle(
                 &app->desktop_prefs_window)) {
        tiku_trk_desktop_prefs_window_close(&app->desktop_prefs_window);
        return;
    }
    if (w == tiku_trk_fonts_window_handle(&app->fonts_window)) {
        tiku_trk_fonts_window_close(&app->fonts_window);
        return;
    }
#if TIKU_SHELL_BOARD_SERVICES

    if (w == desktop_deviceprefs_handle(app)) {
        desktop_deviceprefs_close(app);
        return;
    }
#endif

    if (w == desktop_startup_handle(app)) {
        desktop_startup_close(app);
        return;
    }
    if (w == desktop_forcequit_handle(app)) {
        desktop_forcequit_close(app);
        return;
    }
    if (w == desktop_associations_handle(app)) {
        desktop_associations_close(app);
        return;
    }
    if (w == tiku_trk_sounds_window_handle(&app->sounds_window)) {
        tiku_trk_sounds_window_close(&app->sounds_window);
        return;
    }
    info_slot = tiku_trk_info_windows_slot(&app->info_windows, w);
    if (info_slot >= 0) {
        tiku_trk_info_windows_close(&app->info_windows, info_slot);
        return;
    }
    fl = folder_of(app, w);
    if (fl != NULL) {
        if (app->bar_fl == fl) {
            app->bar_fl = NULL;
            app->bar_open = -1;
            app->menus.bar.open = -1;
            app->popup = NULL;
            app->popup_sub = NULL;
        }
        tiku_trk_folder_lifecycle_close(&app->folder_lifecycle, fl);
        return;
    }
    tiku_workspace_close(app->desk, w);
    desktop_damage_all(app);
}

/**
 * @brief Close the window on @p path and every window on a folder
 *        inside it; a sibling sharing the prefix is left open.
 */
void
close_windows_under(desktop_t *app, const char *path)
{
    size_t n;
    int i;

    if (app == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    n = strlen(path);
    for (i = app->folder_count - 1; i >= 0; i--) {
        const char *p = app->folders[i].path;

        if (strncmp(p, path, n) == 0 && (p[n] == '\0' || p[n] == '/')) {
            close_window(app, app->folders[i].win);
        }
    }
}

/**
 * @brief Open a window on @p path through the folder lifecycle, or bring
 *        its open window forward.  A Trash folder becomes a result set over
 *        every volume's trash (PVN-027).
 * @return the folder, or NULL when no window could be opened.
 */
folder_t *
open_folder(desktop_t *app, const char *path)
{
    folder_t *fl = tiku_trk_folder_lifecycle_open(&app->folder_lifecycle,
                                                  path);

    if (fl != NULL && fl->view != NULL && !fl->trash_union &&
        path != NULL && tiku_fs_in_trash(app->fs, path)) {
        /* A Trash window shows the union of every volume's trash as a
         * result set, refreshed with the live queries (PVN-027). */
        tiku_model_t m;

        if (app->backend->ops->stat(app->backend, path, &m) == 0 &&
            m.kind == TIKU_KIND_TRASH) {
            tiku_model_t *models = calloc(256, sizeof *models);

            if (models != NULL) {
                int n = trash_union_models(app, models, 256);
                (void)tiku_trk_poseview_set_results(fl->view, models, n);
                free(models);
                fl->trash_union = 1;
            }
        }
    }
    return fl;
}
