/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_folder_columns.c - a folder window's list rows against the columns
 * as placed: the selection fill, the selected-text colour, the rename
 * editor and the press that arms a rename follow the Name column wherever
 * it sits in the strip.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* draw_list() and draw_transients() are static: the window's file is
 * compiled in here. */
#include "../../system/shared/ui/windows/tiku_trk_folder_window.c"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_drag_target.h"
#include "tiku_trk_band_controller.h"

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

/** @brief Whether any pixel of @p r on @p s is @p c. */
static int
has_colour(const tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    int x, y;

    for (y = r.y; y < r.y + r.h; y++) {
        for (x = r.x; x < r.x + r.w; x++) {
            if (tiku_peek(s, x, y) == c) { return 1; }
        }
    }
    return 0;
}

/** @brief How many pixels of @p r differ between two surfaces. */
static int
differ(const tiku_surface_t *s, const tiku_surface_t *t, tiku_rect_t r)
{
    int x, y, n = 0;

    for (y = r.y; y < r.y + r.h; y++) {
        for (x = r.x; x < r.x + r.w; x++) {
            n += tiku_peek(s, x, y) != tiku_peek(t, x, y);
        }
    }
    return n;
}

/** @brief Row 0's cell under the column showing @p field, inset by two
 *         pixels; the first column that is not Name when @p field is -1. */
static tiku_rect_t
cell_of(tiku_trk_folder_window_t *fl, tiku_rect_t list, int field)
{
    const tiku_trk_column_t *c;
    int n = tiku_trk_poseview_columns(fl->view, &c), i;
    int row_h = fl->render->row_h;

    for (i = 0; i < n; i++) {
        if ((field < 0) ? c[i].field != TIKU_TRK_COL_NAME
                        : c[i].field == field) {
            return (tiku_rect_t){ list.x + 2 + c[i].offset + 2,
                                  list.y + 3, c[i].width - 4, row_h - 2 };
        }
    }
    return (tiku_rect_t){ 0, 0, 0, 0 };
}

/** @brief Screen x of the right end, less @p in, of the column showing
 *         @p field; the first column that is not Name when it is -1. */
static int
right_end(tiku_trk_folder_window_t *fl, int field, int in)
{
    tiku_rect_t content = tiku_trk_folder_window_content(fl);
    tiku_rect_t r = cell_of(fl, content, field);

    return r.x + r.w + 2 - in;
}

/** @brief Whether a press at @p x on row 0 counts as on its name. */
static int
on_name(tiku_trk_folder_window_t *fl, int x)
{
    tiku_rect_t content = tiku_trk_folder_window_content(fl);

    return tiku_trk_folder_window_name_at(fl, 0, x, content.y + 5);
}

/** @brief Paint the visible rows of @p fl onto @p s, from a clean ground. */
static void
paint_rows(tiku_trk_folder_window_t *fl, tiku_surface_t *s, tiku_rect_t list)
{
    int total = tiku_trk_poseview_count(fl->view);

    tiku_fill(s, (tiku_rect_t){ 0, 0, s->w, s->h }, TIKU_C_DOC);
    draw_list(fl, s, list, 4, 0, total);
    tiku_clip_reset(s);
}

int
main(void)
{
    static tiku_trk_folder_window_t folders[TIKU_WINDOW_MAX];
    static tiku_trk_cwmenu_t menus;
    static tiku_trk_folder_edit_t edit;
    static tiku_trk_band_controller_t band;
    static tiku_trk_drag_target_t drop;
    static tiku_trk_prefs_t prefs;
    tiku_trk_folder_render_t render;
    tiku_trk_folder_lifecycle_t life;
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_store_t *st = tiku_store_memory_open();
    tiku_surface_t *surf = tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
    tiku_workspace_t *ws = tiku_workspace_new(surf);
    tiku_surface_t *rows = tiku_surface_new(700, 120, TIKU_C_DOC);
    tiku_surface_t *plain = tiku_surface_new(700, 120, TIKU_C_DOC);
    tiku_rect_t list = { 0, 0, 700, 120 };
    tiku_trk_folder_window_t *fl;
    char work[128], cmd[512], status[256];
    int count = 0, cx = 0, cy = 0, dirty = 0;

    tiku_trk_prefs_init(&prefs);
    memset(&render, 0, sizeof render);
    render.workspace = ws;
    render.menus = &menus;
    render.prefs = &prefs;
    render.backend = b;
    render.store = st;
    render.edit = &edit;
    render.band = &band;
    render.drop = &drop;
    render.row_h = 18;
    render.screen_w = 1000;
    render.screen_h = 700;
    tiku_trk_folder_lifecycle_init(&life, folders, TIKU_WINDOW_MAX, &count,
        &cx, &cy, ws, b, st, &prefs, &render, status, sizeof status,
        &dirty, 1000, 700);
    snprintf(work, sizeof work, "/tmp/trk-columns-%d", (int)getpid());
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s && "
             "echo x > %s/alpha.txt", work, work, work);
    sh(cmd);

    fl = tiku_trk_folder_lifecycle_open(&life, work);
    check(fl != NULL && fl->view != NULL &&
          tiku_trk_poseview_count(fl->view) == 1, "fixture",
          "a list window opens on a folder of one file");
    if (fl == NULL || fl->view == NULL || rows == NULL || plain == NULL) {
        return 1;
    }
    tiku_trk_poseview_set_mode(fl->view, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_set_hscroll(fl->view, 0);
    tiku_workspace_activate(ws, fl->win);

    printf("Name first: the selection covers the icon and the Name\n");
    tiku_trk_poseview_select_none(fl->view);
    tiku_trk_poseview_click_at(fl->view, 0, 0, 0, 0u);
    paint_rows(fl, rows, list);
    check(has_colour(rows, (tiku_rect_t){ list.x + 3, list.y + 3, 2, 4 },
                     TIKU_C_SELECT), "IV-024",
          "the fill starts under the icon");
    check(has_colour(rows, cell_of(fl, list, TIKU_TRK_COL_NAME),
                     TIKU_C_SELECT), "IV-024", "and covers the Name cell");
    check(!has_colour(rows, cell_of(fl, list, -1), TIKU_C_SELECT), "IV-024",
          "and stops before the next column");

    printf("\nName first: a press on the name is one on the Name column\n");
    check(on_name(fl, tiku_trk_folder_window_content(fl).x + 20), "D-217",
          "a press on the icon, where a script's click-row lands, is on "
          "the name");
    check(on_name(fl, right_end(fl, TIKU_TRK_COL_NAME, 2)), "D-217",
          "a press at the right end of the Name column is");
    check(!on_name(fl, right_end(fl, TIKU_TRK_COL_NAME, -20)), "D-217",
          "a press in the next column is not");

    printf("\nName second: the selection follows it (D-217)\n");
    (void)tiku_trk_poseview_move_column_to(fl->view, TIKU_TRK_COL_NAME, 1);
    check(on_name(fl, right_end(fl, TIKU_TRK_COL_NAME, 2)), "D-217",
          "a press at the right end of the Name column is on the name");
    check(!on_name(fl, right_end(fl, -1, 2)), "D-217",
          "a press at the right end of the column now first is not");
    paint_rows(fl, rows, list);
    check(has_colour(rows, cell_of(fl, list, TIKU_TRK_COL_NAME),
                     TIKU_C_SELECT), "D-217",
          "the selection fill is in the Name cell");
    check(!has_colour(rows, cell_of(fl, list, -1), TIKU_C_SELECT), "D-217",
          "and not in the column now first");
    check(!has_colour(rows, cell_of(fl, list, TIKU_TRK_COL_NAME),
                      TIKU_C_TEXT), "D-217",
          "the name is drawn in the selected-text colour");

    printf("\nName second: the rename editor opens over it (D-217)\n");
    tiku_trk_poseview_select_none(fl->view);
    paint_rows(fl, plain, list);
    edit.renaming = 1;
    edit.rename_folder = fl;
    edit.rename_row = 0;
    tiku_field_start(&edit.rename_edit, "alpha.txt");
    paint_rows(fl, rows, list);
    draw_transients(fl, rows, list, 0, 0, 0, 4, 0);
    check(differ(rows, plain, cell_of(fl, list, TIKU_TRK_COL_NAME)) > 0,
          "D-217", "the rename editor is drawn over the Name cell");
    check(differ(rows, plain, cell_of(fl, list, -1)) == 0, "D-217",
          "and leaves the column now first alone");
    for (int width = 80; width <= 400; width += 320) {
        char long_name[180];
        tiku_rect_t name, beyond;

        memset(long_name, 'W', sizeof long_name - 1);
        long_name[sizeof long_name - 1] = '\0';
        tiku_field_stop(&edit.rename_edit);
        tiku_field_start(&edit.rename_edit, long_name);
        tiku_trk_poseview_resize_column(fl->view, TIKU_TRK_COL_NAME, width);
        name = cell_of(fl, list, TIKU_TRK_COL_NAME);
        beyond = (tiku_rect_t){name.x + name.w + 2, name.y,
                              700 - name.x - name.w - 2, name.h};
        paint_rows(fl, plain, list);
        paint_rows(fl, rows, list);
        draw_transients(fl, rows, list, 0, 0, 0, 4, 0);
        check(differ(rows, plain, beyond) == 0, "D-046",
              "rename stays inside a resized Name column");
        check(differ(rows, plain,
                     (tiku_rect_t){name.x + name.w - 8, name.y, 6, name.h})
              > 0, "D-046", "long names use the available column width");
    }
    edit.renaming = 0;
    tiku_field_stop(&edit.rename_edit);

    tiku_trk_folder_lifecycle_close(&life, fl);
    snprintf(cmd, sizeof cmd, "rm -rf %s", work);
    sh(cmd);
    tiku_surface_free(rows);
    tiku_surface_free(plain);
    tiku_store_free(st);
    tiku_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "the rows follow the Name column");
    return fails ? 1 : 0;
}
