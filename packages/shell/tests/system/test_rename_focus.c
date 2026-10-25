/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_rename_focus.c - pending folder renames after another window gains focus.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_desktop_file_actions.c"
#include "../../system/shared/ui/poseview/tiku_trk_poseview_private.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)
/** @brief Record redraw requests from the desktop rename timer. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }
/** @brief Arm a selected name with its click interval already elapsed. */
static void arm(tiku_trk_poseview_t *view)
{
    view->edit_pose = 0; view->edit_clicked_at = tiku_trk_now() - TIKU_TRK_CLICK_SPEED_US - 1;
}
int main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    tiku_trk_poseview_t view = {0};
    tiku_trk_pose_t pose = {0};
    tiku_window_t *info;
    tiku_fs_t *fs = tiku_fs_new(NULL);
    CHECK(app && fs);
    app->surf = tiku_surface_new(640, 480, TIKU_C_PANEL); app->desk = tiku_workspace_new(app->surf); CHECK(app->surf && app->desk);
    app->folder_count = 1;
    app->folders[0].win = tiku_workspace_open(app->desk, "Folder", (tiku_rect_t){0, 0, 300, 250}, NULL, NULL, NULL);
    app->folders[0].view = &view; CHECK(app->folders[0].win);
    view.poses = &pose; view.count = 1; view.allow_editing = 1;
    pose.selected = 1; pose.model.kind = TIKU_KIND_FILE; strcpy(pose.model.name, "sample.txt");
    tiku_trk_folder_edit_init(&app->folder_edit, NULL, fs, app->status, sizeof app->status, &app->dirty);
    info = tiku_workspace_open(app->desk, "Info", (tiku_rect_t){20, 20, 300, 250}, NULL, NULL, NULL); CHECK(info);
    arm(&view); strcpy(app->status, "Could not save the preferred application.");
    desktop_rename_pulse(app);
    CHECK(!app->folder_edit.renaming && !strcmp(app->status, "Could not save the preferred application."));
    CHECK(!tiku_trk_poseview_edit_armed(&view));
    tiku_workspace_activate(app->desk, app->folders[0].win); desktop_rename_pulse(app); CHECK(!app->folder_edit.renaming);
    arm(&view); desktop_rename_pulse(app); CHECK(app->folder_edit.renaming && app->folder_edit.rename_folder == &app->folders[0]);
    tiku_field_stop(&app->folder_edit.rename_edit); app->folder_edit.renaming = 0;
    arm(&view); tiku_workspace_close(app->desk, app->folders[0].win); desktop_rename_pulse(app); CHECK(!app->folder_edit.renaming && !tiku_trk_poseview_edit_armed(&view));
    tiku_workspace_free(app->desk); tiku_surface_free(app->surf); tiku_fs_free(fs); free(app);
    puts("rename focus: PASS"); return 0;
}
