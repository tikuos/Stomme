/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_info_panel.c - where the Info window's hover box puts a value, and
 * what the file panel reports of its list.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* value_column() and layout() are static: the window's file is compiled
 * in here. */
#include "../../system/shared/ui/windows/tiku_trk_info_window.c"
#include "tiku_strings.h"
#include "tiku_trk_panel_window.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static void
sh(const char *cmd)
{
    if (system(cmd) != 0) { /* the caller checks the effect */ }
}

/** @brief Whether the hover box of @p slot's fields starts at the value. */
static int
box_at_value(tiku_trk_info_windows_t *controller, int slot, int *column)
{
    tiku_rect_t content, icon, name, tabs, body;
    int x = -1, ok;

    content = tiku_window_content(controller->window[slot]);
    layout(content, &icon, &name, &tabs, &body);
    *column = value_column(&controller->model[slot], controller->store);
    ok = tiku_trk_info_windows_field_rect(controller, slot, 0, &x, NULL,
                                          NULL, NULL);
    return ok && x == body.x + *column;
}

/**
 * @brief Whether the panel's shown rows are its visible poses: rows equals
 *        the poses not dot-named or invisible, and each vis[] entry is a
 *        pose that exists.
 */
static int
rows_match_poses(tiku_trk_panel_window_t *panel)
{
    int n = tiku_trk_poseview_count(panel->view), shown = 0, i;

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(panel->view, i);

        if (pose != NULL && pose->model.name[0] != '.' && !pose->invisible) {
            shown++;
        }
    }
    for (i = 0; i < panel->nvis; i++) {
        if (panel->vis[i] < 0 || panel->vis[i] >= n) {
            return 0;
        }
    }
    return tiku_trk_panel_window_rows(panel) == shown;
}

int
main(void)
{
    static tiku_trk_info_windows_t info;
    static tiku_trk_panel_window_t panel;
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_store_t *st = tiku_store_memory_open();
    tiku_surface_t *surf = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
    tiku_workspace_t *ws = tiku_workspace_new(surf);
    char work[128], cmd[512], path[256];
    int dirty = 0, slot, column, i;

    snprintf(work, sizeof work, "/tmp/trk-infopanel-%d", (int)getpid());
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s && cd %s && "
             "touch .one .two && for i in $(seq 10 49); do touch f$i; done",
             work, work, work);
    sh(cmd);

    printf("the Info window's hover box (D-026)\n");
    tiku_trk_info_windows_init(&info, ws, b, st, NULL, NULL, &dirty);
    snprintf(path, sizeof path, "%s/f10", work);
    slot = tiku_trk_info_windows_open(&info, path);
    check(slot >= 0, "D-026", "a Get Info window opens");
    if (slot >= 0) {
        check(box_at_value(&info, slot, &column), "D-026",
              "at the 12-pixel face the box starts where the value is "
              "drawn");
        check(tiku_strings_load_file("assets/strings/fr.xml") > 0 &&
              tiku_font_set_size(16) == 16, "D-026",
              "the French labels load, and the face goes to 16 pixels");
        check(box_at_value(&info, slot, &column) && column > 96, "D-026",
              "past the widest label the value column is wider than 96, "
              "and the box still starts where the value is drawn");
        (void)tiku_font_set_size(12);
        tiku_strings_reset();
        tiku_trk_info_windows_close(&info, slot);
    }

    printf("\nthe file panel's scroll and rows (D-027)\n");
    tiku_trk_panel_window_init(&panel, ws, b, st, NULL, 18, 1000, 700,
                               &dirty, NULL, NULL);
    tiku_trk_panel_window_open_any(&panel, &panel, 0, work);
    check(panel.window != NULL && panel.nvis == 40, "D-027",
          "the panel shows the forty files and not the two dot-named ones");
    check(tiku_trk_panel_window_rows(&panel) == panel.nvis, "D-027",
          "the rows it reports are the rows it shows");
    if (panel.window != NULL) {
        tiku_workspace_activate(ws, panel.window);
        for (i = 0; i < 40; i++) {
            (void)tiku_trk_panel_window_key(&panel, TIKU_KEY_DOWN, 0u, 0);
        }
    }
    check(panel.scroll > 0 &&
          tiku_trk_panel_window_scroll(&panel) == panel.scroll, "D-027",
          "walking down the list scrolls it, and the scroll it reports is "
          "the one it draws from");

    printf("\nthe file panel after its folder changes (D-027)\n");
    snprintf(cmd, sizeof cmd, "touch %s/g99", work);
    sh(cmd);
    (void)tiku_trk_panel_window_poll(&panel, 1000000, NULL, -1, 1);
    check(tiku_trk_poseview_count(panel.view) == 43 &&
          rows_match_poses(&panel), "D-027",
          "a file added under the panel is a row once the poll has run");
    snprintf(cmd, sizeof cmd, "cd %s && rm -f f2* f3* f4*", work);
    sh(cmd);
    (void)tiku_trk_panel_window_poll(&panel, 2000000, NULL, -1, 1);
    check(tiku_trk_poseview_count(panel.view) == 13 &&
          rows_match_poses(&panel), "D-027",
          "files removed under the panel leave no row pointing past the "
          "list");
    snprintf(cmd, sizeof cmd, "mkdir -p %s/sub && touch %s/sub/one "
             "%s/sub/two", work, work, work);
    sh(cmd);
    snprintf(path, sizeof path, "%s/sub", work);
    tiku_trk_panel_window_open_any(&panel, &panel, 0, path);
    check(rows_match_poses(&panel) && panel.nvis == 2, "D-027",
          "a panel opened on a folder shows its two files");
    snprintf(cmd, sizeof cmd, "rm -rf %s/sub", work);
    sh(cmd);
    (void)tiku_trk_panel_window_poll(&panel, 3000000, NULL, -1, 1);
    check(strcmp(panel.model.dir, path) != 0 && rows_match_poses(&panel),
          "D-027",
          "when the folder shown is deleted the panel moves, and its rows "
          "are the new folder's visible poses");
    tiku_trk_panel_window_destroy(&panel);

    snprintf(cmd, sizeof cmd, "rm -rf %s", work);
    sh(cmd);
    tiku_store_free(st);
    tiku_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "Info and panel reports hold");
    return fails ? 1 : 0;
}
