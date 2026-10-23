/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_info_icon_press.c - a press on the Info window's icon soon after a
 * press elsewhere.
 *
 * A press on a pose or on the desk records its time in app->press.at.  A
 * press on the Info window's icon within half a second of that arms a drag
 * of the item; two presses on the icon itself within half a second open
 * it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_infowin.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

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

/** @brief How many folder windows are open on @p path. */
static int
open_on(desktop_t *app, const char *path)
{
    int i, n = 0;

    for (i = 0; i < app->folder_count; i++) {
        if (app->folders[i].win != NULL && app->folders[i].win->open &&
            strcmp(app->folders[i].path, path) == 0) {
            n++;
        }
    }
    return n;
}

int
main(void)
{
    char home[] = "/tmp/tiku-infopress-XXXXXX";
    char item[64], cmd[96];
    desktop_t *app = calloc(1, sizeof *app);
    tiku_trk_info_t info;
    tiku_event_t ev;
    int i;

    if (app == NULL || mkdtemp(home) == NULL) {
        return 2;
    }
    (void)setenv("HOME", home, 1);
    snprintf(item, sizeof item, "%s/item", home);
    (void)mkdir(item, 0755);

    app->surf = tiku_surface_new(DESK_W, DESK_H, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    app->backend = tiku_mount_open();
    (void)tiku_mount_add(app->backend, "/", tiku_backend_posix_open(), 1);
    app->store = tiku_store_memory_open();
    app->deskbar = tiku_trk_deskbar_new(NULL);
    app->drag_target.deskbar = app->deskbar;
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

    memset(&info, 0, sizeof info);
    snprintf(info.path, sizeof info.path, "%s", item);
    info.model.kind = TIKU_KIND_DIRECTORY;
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.button = 1u;
    ev.x = 40;
    ev.y = 40;

    printf("A press on the Info icon after a press on the desk (D-200)\n");
    /* As press_note() records a press on the desk's backdrop. */
    app->press.at = tiku_trk_now();
    icon_press(app, NULL, &info, &ev);
    check(open_on(app, item) == 0, "the item does not open");
    check(app->drag.armed && tiku_trk_drag_carries(&app->drag, item),
          "a drag of the item is armed");

    printf("Two presses on the Info icon (D-200)\n");
    icon_press(app, NULL, &info, &ev);
    check(open_on(app, item) == 1, "the second press opens the item");

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
