#include "tiku_shell_product.h"
/* TikuTracker's list rows against the columns as placed: a rename arms
 * from a press inside the Name column wherever it sits and however far
 * the list is scrolled, the selection and the editor follow the Name
 * column, and a script's narration leaves an owed frame owed.
 * SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../../system/tracker/tiku_trk_app_private.h"

/* Defined in tiku_trk_app_events.c, which declares it for its own use. */
int browser_press_list(app_t *app, const tiku_event_t *ev, browser_loop_t *lp);

static int fails;

static void check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        fails++;
}

/** @brief The column showing @p field, or NULL. */
static const tiku_trk_column_t *column(app_t *a, int field)
{
    const tiku_trk_column_t *c;
    int n = tiku_trk_poseview_columns(a->view, &c);
    for (int i = 0; i < n; i++)
        if (c[i].field == field)
            return &c[i];
    return NULL;
}

/** @brief The first column that is not the Name column. */
static const tiku_trk_column_t *other_column(app_t *a)
{
    const tiku_trk_column_t *c;
    int n = tiku_trk_poseview_columns(a->view, &c);
    for (int i = 0; i < n; i++)
        if (c[i].field != TIKU_TRK_COL_NAME)
            return &c[i];
    return NULL;
}

/** @brief Whether a slow second press at @p x on row 0 arms a rename. */
static int press_arms(app_t *a, int x)
{
    browser_loop_t lp;
    tiku_rect_t list = browser_list_rect(a);
    tiku_event_t ev;
    tiku_trk_pose_t *p;

    memset(&lp, 0, sizeof lp);
    memset(&ev, 0, sizeof ev);
    ev.type = TIKU_EVENT_POINTER_DOWN;
    ev.button = 1u;
    ev.x = x;
    ev.y = list.y + 2 + a->row_h / 2;
    tiku_trk_poseview_cancel_edit_wait(a->view);
    tiku_trk_poseview_select_none(a->view);
    tiku_trk_poseview_click_at(a->view, 0, 0, 0, 0u);
    p = tiku_trk_poseview_at(a->view, 0);
    p->sel_time -= 4 * TIKU_TRK_CLICK_SPEED_US;
    (void)browser_press_list(a, &ev, &lp);
    return tiku_trk_poseview_edit_armed(a->view);
}

/** @brief Whether any pixel of @p r on the frame is @p c. */
static int has_colour(const tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c)
{
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            if (tiku_peek(s, x, y) == c)
                return 1;
    return 0;
}

/** @brief How many pixels of @p r differ between two frames. */
static int differ(const tiku_surface_t *s, const tiku_surface_t *t,
                  tiku_rect_t r)
{
    int n = 0;
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            n += tiku_peek(s, x, y) != tiku_peek(t, x, y);
    return n;
}

/** @brief Row 0's cell under column @p c, inset from its edges. */
static tiku_rect_t cell_of(app_t *a, const tiku_trk_column_t *c)
{
    tiku_rect_t list = browser_list_rect(a);
    return (tiku_rect_t){ browser_col_x(a, c) + 2, list.y + 3,
                          c->width - 4, a->row_h - 2 };
}

int main(void)
{
    static app_t app;
    app_t *a = &app;
    char dir[] = "/tmp/tiku-tracker-columns-XXXXXX";
    char file[sizeof dir + 16];
    FILE *f;

    if (mkdtemp(dir) == NULL)
        return 1;
    snprintf(file, sizeof file, "%s/alpha.txt", dir);
    f = fopen(file, "w");
    if (f == NULL)
        return 1;
    (void)fputs("x", f);
    (void)fclose(f);

    (void)tiku_strings_init(NULL, NULL);
    tiku_trk_cwmenu_init(&a->menus);
    a->bar_open = -1;
    a->select_next = -1;
    a->active = 1;
    a->surf = tiku_surface_new(WIN_W, WIN_H, TIKU_C_PANEL);
    a->backend = tiku_backend_posix_open();
    a->view = tiku_trk_poseview_new(a->backend, dir);
    if (a->surf == NULL || a->backend == NULL || a->view == NULL)
        return 1;
    a->row_h = tiku_font_plain()->height + 5;
    tiku_trk_poseview_set_measure(a->view, browser_measure_text,
                                  (void *)tiku_font_plain());
    tiku_trk_poseview_set_mode(a->view, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_allow_editing(a->view, 1);
    snprintf(a->title, sizeof a->title, "columns");

    printf("a rename arms from the Name column as placed\n");
    {
        const tiku_trk_column_t *name = column(a, TIKU_TRK_COL_NAME);
        int nx = browser_col_x(a, name);

        const tiku_trk_column_t *other = other_column(a);

        check(press_arms(a, nx + name->width - 2),
              "a press at the right end of the Name column arms");
        check(!press_arms(a, browser_col_x(a, other) + 4),
              "a press in the next column does not");
        tiku_trk_poseview_set_hscroll(a->view, 60);
        check(press_arms(a, browser_col_x(a, name) + name->width - 2),
              "scrolled sideways, a press in the Name column arms");
        check(!press_arms(a, browser_col_x(a, other) + 4),
              "and a press in the next column does not");
        tiku_trk_poseview_set_hscroll(a->view, 0);
        (void)tiku_trk_poseview_move_column_to(a->view, TIKU_TRK_COL_NAME,
                                               1);
        name = column(a, TIKU_TRK_COL_NAME);
        other = other_column(a);
        check(press_arms(a, browser_col_x(a, name) + name->width - 2),
              "with Name second, a press at its right end arms");
        check(!press_arms(a, browser_col_x(a, other) + other->width - 2),
              "and a press at the right end of the column now first "
              "does not");
    }

    printf("the selection and the editor follow the Name column\n");
    {
        const tiku_trk_column_t *name = column(a, TIKU_TRK_COL_NAME);
        const tiku_trk_column_t *first = other_column(a);
        tiku_surface_t *plain;

        tiku_trk_poseview_cancel_edit_wait(a->view);
        tiku_trk_poseview_select_none(a->view);
        tiku_trk_poseview_click_at(a->view, 0, 0, 0, 0u);
        browser_draw(a);
        check(has_colour(a->surf, cell_of(a, name), TIKU_C_SELECT),
              "the selection fill is in the Name cell");
        check(!has_colour(a->surf, cell_of(a, first), TIKU_C_SELECT),
              "and not in the column now first");
        check(!has_colour(a->surf, cell_of(a, name), TIKU_C_TEXT),
              "the name is drawn in the selected-text colour");

        tiku_trk_poseview_select_none(a->view);
        browser_draw(a);
        plain = tiku_grab(a->surf, (tiku_rect_t){ 0, 0, WIN_W, WIN_H });
        a->editing = 1;
        snprintf(a->edit_path, sizeof a->edit_path, "%s",
                 tiku_trk_poseview_at(a->view, 0)->model.path);
        tiku_field_start(&a->ed, "alpha.txt");
        browser_draw(a);
        check(differ(a->surf, plain, cell_of(a, name)) > 0,
              "the rename editor opens over the Name cell");
        check(differ(a->surf, plain, cell_of(a, first)) == 0,
              "and leaves the column now first alone");
        a->editing = 0;
        tiku_surface_free(plain);
    }

    printf("a narration leaves an owed frame owed\n");
    {
        char said[4096];

        a->dirty = 1;
        (void)browser_script_narrate(a, said, sizeof said);
        check(a->dirty == 1, "the dirty flag survives the narration");
    }

    tiku_trk_poseview_free(a->view);
    tiku_backend_close(a->backend);
    tiku_surface_free(a->surf);
    (void)unlink(file);
    (void)rmdir(dir);
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
