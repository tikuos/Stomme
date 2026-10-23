/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pick_refusal.c - an application's file request while the file panel
 * asks the desktop's own question.
 *
 * The panel asks where Move to puts a folder's selection.  A hosted
 * application's request and a remote application's request are both
 * refused, and the panel keeps the Move to question.  Once the panel has
 * closed, the hosted application's request opens it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_embed.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/** @brief Whether the panel is up asking Move to for @p fl. */
static int
asks_move_to(desktop_t *app, folder_t *fl)
{
    return tiku_trk_panel_window_handle(&app->file_panel) != NULL &&
           app->file_panel.target == fl &&
           app->file_panel.command == TIKU_TRK_CMD_MOVE_TO;
}

int
main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    struct desktop_embed slot;
    folder_t *fl;
    tiku_msg_t *msg;

    if (app == NULL) {
        return 2;
    }
    app->surf = tiku_surface_new(DESK_W, DESK_H, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    app->backend = tiku_mount_open();
    (void)tiku_mount_add(app->backend, "/", tiku_backend_posix_open(), 1);
    app->store = tiku_store_memory_open();
    tiku_trk_panel_window_init(&app->file_panel, app->desk, app->backend,
        app->store, NULL, 18, DESK_W, DESK_H, &app->dirty, NULL, NULL);
    memset(&slot, 0, sizeof slot);
    slot.app = app;
    fl = &app->folders[0];
    snprintf(fl->path, sizeof fl->path, "/tmp");
    app->folder_count = 1;

    printf("An application's request while Move to asks (D-056)\n");
    fpanel_open(app, fl, TIKU_TRK_CMD_MOVE_TO);
    check(asks_move_to(app, fl), "the panel asks where Move to goes");
    check(embed_svc_pick(&slot, 7, TIKU_APP_PICK_OPEN, "/tmp", NULL) == 0,
          "a hosted application's request is refused");
    check(asks_move_to(app, fl) && app->pending_pick.kind == 0,
          "and the panel still asks Move to");

    app->remote.session[0].used = 1;
    msg = tiku_msg_new(0);
    if (msg != NULL) {
        (void)tiku_msg_add_int32(msg, "window", 3);
        (void)tiku_msg_add_int32(msg, "mode", TIKU_APP_PICK_OPEN);
        (void)tiku_msg_add_string(msg, "start", "/tmp");
        desktop_remote_pick(app, 0, msg);
        tiku_msg_free(msg);
    }
    check(asks_move_to(app, fl) && app->pending_pick.kind == 0,
          "a remote application's request leaves Move to asking");

    tiku_trk_panel_window_close(&app->file_panel);
    check(embed_svc_pick(&slot, 7, TIKU_APP_PICK_OPEN, "/tmp", NULL) == 1 &&
          app->file_panel.target == &app->pending_pick,
          "with the panel closed, a hosted application's request opens it");
    tiku_trk_panel_window_close(&app->file_panel);

    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
