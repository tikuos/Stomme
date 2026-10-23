#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_desktop_closes.c - folder windows closed by Cmd-click on a close
 * box, by the Deskbar's Close all, and by their folder being removed.
 *
 * Each close empties the folder's slot, so a window object the workspace
 * hands out again is not matched to the closed folder.  The two closes a
 * person asks for also save the window's frame.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_windows.c"
#include "../../system/desktop/tiku_trk_desktop_deskbar.c"

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

/** @brief Whether every folder slot is empty. */
static int
slots_empty(desktop_t *app)
{
    int i;

    for (i = 0; i < app->folder_count; i++) {
        if (app->folders[i].win != NULL) {
            return 0;
        }
    }
    return 1;
}

/** @brief Open a plain window; the workspace may hand out a closed
 *         folder window's object.  @return whether no folder claims it. */
static int
stranger_unclaimed(desktop_t *app)
{
    tiku_window_t *w = tiku_workspace_open(app->desk, "Other",
        (tiku_rect_t){ 10, 10, 100, 80 }, NULL, NULL, NULL);
    int ok = w != NULL && folder_of(app, w) == NULL;

    if (w != NULL) {
        tiku_workspace_close(app->desk, w);
    }
    return ok;
}

/** @brief Whether the store holds a frame for @p path at x @p x. */
static int
frame_saved_at(desktop_t *app, const char *path, int x)
{
    tiku_trk_frame_t f;

    return tiku_trk_frame_load(app->store, path, &f) && f.x == x;
}

int
main(void)
{
    char home[] = "/tmp/tiku-closes-XXXXXX";
    char a[64], b[64], c[64], cmd[96];
    desktop_t *app = calloc(1, sizeof *app);
    desktop_loop_t lp;
    folder_t *fa, *fb, *fc;
    int team, i;

    if (app == NULL || mkdtemp(home) == NULL) {
        return 2;
    }
    (void)setenv("HOME", home, 1);
    snprintf(a, sizeof a, "%s/a", home);
    snprintf(b, sizeof b, "%s/b", home);
    snprintf(c, sizeof c, "%s/c", home);
    (void)mkdir(a, 0755);
    (void)mkdir(b, 0755);
    (void)mkdir(c, 0755);

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
    (void)tiku_trk_desktop_view_init(&app->desktop_view, app->backend,
        app->fs, prefs_model(app), app->deskbar, &app->folder_render,
        DESK_W, DESK_H);
    team = tiku_trk_deskbar_add_team(app->deskbar, TIKU_SHELL_TRACKER_NAME);
    if (app->desk == NULL || app->deskbar == NULL || team < 0) {
        printf("  [FAIL] a desk and a Deskbar\n");
        return 1;
    }

    printf("Cmd-click on a close box closes every folder window (D-054)\n");
    fa = open_folder(app, a);
    fb = open_folder(app, b);
    check(fa != NULL && fb != NULL, "two folder windows open");
    if (fa == NULL || fb == NULL) {
        return 1;
    }
    fa->win->frame.x = 123;
    tab_box_act(app, fb->win, TIKU_HIT_CLOSE, TIKU_MOD_CMD);
    check(slots_empty(app), "each folder slot lets go of its window");
    check(frame_saved_at(app, a, 123), "the moved window's frame is saved");
    check(stranger_unclaimed(app),
          "a window reusing the object is no folder's");

    printf("The Deskbar's Close all on the desktop's row (D-054)\n");
    fa = open_folder(app, a);
    fb = open_folder(app, b);
    check(fa != NULL && fb != NULL, "two folder windows open again");
    if (fa == NULL || fb == NULL) {
        return 1;
    }
    fa->win->frame.x = 77;
    team_close(app, team);
    check(slots_empty(app), "each folder slot lets go of its window");
    check(frame_saved_at(app, a, 77), "the moved window's frame is saved");
    check(stranger_unclaimed(app),
          "a window reusing the object is no folder's");

    printf("A folder removed while its window is open (D-054)\n");
    fc = open_folder(app, c);
    check(fc != NULL, "a folder window opens");
    if (fc == NULL) {
        return 1;
    }
    fc->win->frame.x = 55;
    (void)rmdir(c);
    memset(&lp, 0, sizeof lp);
    lp.last_requery = tiku_trk_now();
    desktop_notice_pump(app, &lp);
    check(fc->gone, "the poll finds the folder gone");
    check(slots_empty(app), "its slot lets go of its window");
    check(!frame_saved_at(app, c, 55), "and its frame is not saved");
    check(stranger_unclaimed(app),
          "a window reusing the object is no folder's");

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
