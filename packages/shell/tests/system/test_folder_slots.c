/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_folder_slots.c - folder window slots, the list row's hash and the
 * drag target's spring area.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* row_signature() is static: the window's file is compiled in here. */
#include "../../system/shared/ui/windows/tiku_trk_folder_window.c"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_drag_target.h"

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

int
main(void)
{
    static tiku_trk_folder_window_t folders[TIKU_WINDOW_MAX];
    static tiku_trk_cwmenu_t menus;
    static tiku_trk_folder_edit_t edit;
    static tiku_trk_prefs_t prefs;
    tiku_trk_folder_render_t render;
    tiku_trk_folder_lifecycle_t life;
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_store_t *st = tiku_store_memory_open();
    tiku_surface_t *surf = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
    tiku_workspace_t *ws = tiku_workspace_new(surf);
    tiku_trk_folder_window_t *fl;
    char work[128], cmd[512], path[256], status[256];
    int count = 0, cx = 0, cy = 0, dirty = 0, i;

    tiku_trk_prefs_init(&prefs);
    memset(&render, 0, sizeof render);
    render.workspace = ws;
    render.menus = &menus;
    render.prefs = &prefs;
    render.backend = b;
    render.store = st;
    render.edit = &edit;
    render.row_h = 18;
    render.screen_w = 1000;
    render.screen_h = 700;
    tiku_trk_folder_lifecycle_init(&life, folders, TIKU_WINDOW_MAX, &count,
        &cx, &cy, ws, b, st, &prefs, &render, status, sizeof status,
        &dirty, 1000, 700);
    snprintf(work, sizeof work, "/tmp/trk-slots-%d", (int)getpid());
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s && cd %s && "
             "for i in $(seq 10 49); do mkdir d$i; done", work, work, work);
    sh(cmd);

    printf("the list row's hash (D-025)\n");
    fl = tiku_trk_folder_lifecycle_open(&life, work);
    check(fl != NULL && fl->view != NULL, "D-025", "a list window opens");
    if (fl != NULL && fl->view != NULL) {
        tiku_trk_pose_t *pose;
        uint32_t before;

        tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_LIST);
        pose = tiku_trk_poseview_at(fl->view, 0);
        pose->model.facts.total = 1000u;
        pose->model.facts.avail = 500u;
        tiku_trk_press_t press = {0};
        render.press = &press;
        uint32_t idle = pose_signature(fl, pose);
        press.down = 1; press.folder = fl; press.row = 0;
        check(pose_signature(fl, pose) != idle, "D-045",
              "held press participates in the icon cache key");
        render.press = NULL;
        before = row_signature(fl, pose, 0, 0, 0);
        pose->model.facts.total = 4000u;
        check(row_signature(fl, pose, 0, 0, 0) != before, "D-025",
              "a change to the volume's capacity alone changes the row's "
              "hash, so its space bar is painted again");
    }

    printf("\nthe spring area in a scrolled window (D-023)\n");
    if (fl != NULL && fl->view != NULL) {
        tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(b);
        tiku_trk_drag_target_t target;
        tiku_trk_drag_t drag;
        tiku_rect_t content;
        int row, x, y, scroll;

        memset(&drag, 0, sizeof drag);
        drag.active = 1;
        drag.count = 1;
        snprintf(drag.path[0], sizeof drag.path[0], "/tmp/elsewhere");
        tiku_workspace_activate(ws, fl->win);
        tiku_trk_drag_target_init(&target, ws, db, NULL, &drag, folders,
                                  &count, render.row_h, &dirty, NULL, NULL);
        /* What a draw sets: the row height the window steps by, and the
         * viewport the list fills. */
        render.row_h = tiku_trk_poseview_row_height(fl->view);
        content = tiku_trk_folder_window_content(fl);
        tiku_trk_poseview_set_viewport(fl->view, content.w - 4,
                                       content.h - 4);
        tiku_trk_poseview_scroll_to(fl->view, 10);
        scroll = tiku_trk_poseview_scroll(fl->view);
        x = content.x + 40;
        y = content.y + 2 + render.row_h + render.row_h / 2;
        row = tiku_trk_folder_window_row_at(fl, x, y);
        (void)tiku_trk_drag_target_motion(&target, x, y, 1);
        check(scroll > 0 && row == scroll + 1 && target.row == row,
              "D-023", "the drag targets the folder row under the pointer");
        check(tiku_trk_springmenu_inside(&target.spring, x, y), "D-023",
              "the spring area covers the row where it is on screen");
        tiku_trk_deskbar_free(db);
    }

    if (fl != NULL) {
        tiku_trk_poseview_set_results(fl->view, NULL, 0);
        tiku_theme_set(tiku_theme_dark());
        check(abs((int)(folder_background(fl->view) & 255) -
                  (int)(TIKU_C_DOC & 255)) >= 12, "D-044",
              "query background remains distinguishable in the dark theme");
        tiku_theme_set(NULL);
        int finding = 0; char count_text[256] = {0};
        render.finding = &finding;
        render.status = "";
        render.count_text = count_text;
        render.count_text_size = sizeof count_text;
        fl->live = 1;
        draw_count(fl, surf, (tiku_rect_t){0, 0, 500, 100}, 0);
        check(strstr(count_text, "live") != NULL, "D-047",
              "finished query count identifies live results");
        tiku_trk_folder_lifecycle_close(&life, fl);
    }

    printf("\nfolder window slots (D-028)\n");
    for (i = 0; i <= TIKU_WINDOW_MAX; i++) {
        snprintf(path, sizeof path, "%s/d%d", work, 10 + i);
        fl = tiku_trk_folder_lifecycle_open(&life, path);
        if (fl == NULL) {
            break;
        }
        tiku_trk_folder_lifecycle_close(&life, fl);
    }
    check(i == TIKU_WINDOW_MAX + 1, "D-028",
          "a folder window opens after TIKU_WINDOW_MAX have been opened "
          "and closed");
    check(count <= TIKU_WINDOW_MAX, "D-028",
          "and the slots in use stay within the capacity");

    snprintf(cmd, sizeof cmd, "rm -rf %s", work);
    sh(cmd);
    tiku_store_free(st);
    tiku_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "folder window slots hold");
    return fails ? 1 : 0;
}
