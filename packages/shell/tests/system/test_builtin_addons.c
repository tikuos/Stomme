/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_builtin_addons.c - the desktop's three built-in add-ons, chosen from
 * the Add-ons menu on a folder window's selection.
 *
 * Open in Tracker opens a folder window on the selected folder, Open with
 * Editor opens the selected file in the Editor application through the
 * launcher, and Inspect opens Get Info on the selected item.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
static char opened_with[64], opened_path[TIKU_PATH_MAX];

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }

/** @brief The launcher's hosted-application opener, standing in. */
static int
open_document(void *context, const tiku_trk_app_t *a, const char *path)
{
    (void)context;
    snprintf(opened_with, sizeof opened_with, "%s", a->id);
    snprintf(opened_path, sizeof opened_path, "%s", path);
    return 1;
}

/** @brief The add-on called @p name, chosen from the Add-ons menu of
 *         @p fl's window, which is focused first.  @return 0 when there is
 *         none. */
static int
choose(desktop_t *app, folder_t *fl, const char *name)
{
    int i;

    tiku_workspace_activate(app->desk, fl->win);
    for (i = 0; i < app->addons.count; i++) {
        if (strcmp(app->addons.item[i].name, name) == 0) {
            menu_invoke(app, fl, TIKU_TRK_ADDON_COMMAND_BASE + i);
            return 1;
        }
    }
    return 0;
}

/** @brief Whether a folder window is open on @p path. */
static int
folder_open_on(desktop_t *app, const char *path)
{
    int i;

    for (i = 0; i < app->folder_count; i++) {
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            strcmp(app->folders[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Whether a Get Info window is open on @p path. */
static int
info_open_on(desktop_t *app, const char *path)
{
    int i;

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (app->info_windows.window[i] != NULL &&
            strcmp(app->info_windows.model[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
main(void)
{
    char home[] = "/tmp/tiku-addons-XXXXXX";
    char sub[64], note[64], cmd[96];
    desktop_t *app = calloc(1, sizeof *app);
    static tiku_alert_t alert;
    folder_t *fl;
    FILE *f;
    int at, i;

    if (app == NULL || mkdtemp(home) == NULL) {
        return 2;
    }
    (void)setenv("HOME", home, 1);
    snprintf(sub, sizeof sub, "%s/sub", home);
    snprintf(note, sizeof note, "%s/note.txt", home);
    (void)mkdir(sub, 0755);
    f = fopen(note, "w");
    if (f != NULL) {
        fputs("hello\n", f);
        (void)fclose(f);
    }

    app->surf = tiku_surface_new(DESK_W, DESK_H, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    app->backend = tiku_mount_open();
    (void)tiku_mount_add(app->backend, "/", tiku_backend_posix_open(), 1);
    app->store = tiku_store_memory_open();
    app->fs = tiku_fs_new(app->backend);
    app->deskbar = tiku_trk_deskbar_new(NULL);
    app->row_h = 18;
    tiku_trk_prefs_init(prefs_model(app));
    app->folder_render = (tiku_trk_folder_render_t){
        .workspace = app->desk, .menus = &app->menus,
        .prefs = prefs_model(app), .backend = app->backend,
        .store = app->store, .row_h = app->row_h, .screen_w = DESK_W,
        .screen_h = DESK_H, .edit = &app->folder_edit, .press = &app->press
    };
    tiku_trk_folder_lifecycle_init(&app->folder_lifecycle, app->folders,
        TIKU_WINDOW_MAX, &app->folder_count, &app->cascade_x,
        &app->cascade_y, app->desk, app->backend, app->store,
        prefs_model(app), &app->folder_render, app->status,
        sizeof app->status, &app->dirty, DESK_W, DESK_H);
    tiku_trk_info_windows_init(&app->info_windows, app->desk, app->backend,
        app->store, app->fs, &alert, &app->dirty);
    tiku_trk_launch_controller_init(&app->launcher, &app->apps, app->store,
        app->status, sizeof app->status, NULL, app);
    app->launcher.open_document = open_document;
    tiku_trk_folder_menu_init(&app->folder_menu, &app->menus, &app->clip,
        app->backend, app->fs, &app->types, &app->launcher, &app->bar_fl,
        &app->bar_open);
    at = tiku_trk_launch_add_document(&app->apps, "Editor", "/bin/true",
                                      "editor.so", "text/");
    if (at >= 0) {
        snprintf(app->apps.app[at].id, sizeof app->apps.app[at].id,
                 "editor");
    }
    desktop_addons_register(app);

    fl = open_folder(app, home);
    check(fl != NULL && fl->view != NULL, "a folder window opens");
    if (fl == NULL || fl->view == NULL) {
        return 1;
    }

    printf("Open in Tracker (D-053)\n");
    (void)select_named(app, fl, "sub");
    check(choose(app, fl, "Open in Tracker"), "the add-on is listed");
    printf("  (status: \"%s\")\n", app->status);
    check(folder_open_on(app, sub), "a folder window opens on the folder");

    printf("Open with Editor (D-053)\n");
    (void)select_named(app, fl, "note.txt");
    check(choose(app, fl, "Open with Editor"), "the add-on is listed");
    printf("  (status: \"%s\")\n", app->status);
    check(strcmp(opened_with, "editor") == 0 &&
          strcmp(opened_path, note) == 0,
          "the file opens in the Editor application");

    printf("Inspect (D-053)\n");
    (void)select_named(app, fl, "note.txt");
    check(choose(app, fl, "Inspect"), "the add-on is listed");
    printf("  (status: \"%s\")\n", app->status);
    check(info_open_on(app, note), "Get Info opens on the file");

    for (i = 0; i < TIKU_TRK_INFO_WINDOW_MAX; i++) {
        if (app->info_windows.window[i] != NULL) {
            tiku_trk_info_windows_close(&app->info_windows, i);
        }
    }
    for (i = 0; i < app->folder_count; i++) {
        tiku_trk_poseview_free(app->folders[i].view);
    }
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", home);
    if (system(cmd) != 0) {
        printf("  (could not remove %s)\n", home);
    }
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
