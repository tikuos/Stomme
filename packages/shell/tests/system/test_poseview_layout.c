/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_poseview_layout.c - the icon-mode extent after placement and a
 * folder switch, nudges across a mode change, the placement hint in a
 * scrolled view, list rows under a filter, sorting on the text columns,
 * and entry positions under a damaged view-state record.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tiku_trk_poseview.h"
#include "tiku_state.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static char work[256];

static void
sh(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (system(cmd) != 0) { /* the caller checks the effect */ }
}

/** @brief A label measure: eight pixels a character. */
static int
measure(const char *text, void *ctx)
{
    (void)ctx;
    return (text != NULL) ? (int)strlen(text) * 8 : 0;
}

/** @brief A view on @p dir in icon mode at 32 px with a 12 px font. */
static tiku_trk_poseview_t *
icon_view(tiku_backend_t *b, const char *dir)
{
    tiku_trk_poseview_t *v = tiku_trk_poseview_new(b, dir);

    tiku_trk_poseview_set_measure(v, measure, NULL);
    tiku_trk_poseview_set_font_metrics(v, 12, 9);
    (void)tiku_trk_poseview_reload(v);
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(v, 32);
    return v;
}

/** @brief Whether every placed pose's rect lies inside the extent. */
static int
extent_covers(tiku_trk_poseview_t *v)
{
    int ex = 0, ey = 0, ew = 0, eh = 0, i, n;

    tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        if (px < ex || py < ey || px + pw > ex + ew || py + ph > ey + eh) {
            return 0;
        }
    }
    return 1;
}

/** @brief The pose named @p name, or NULL. */
static tiku_trk_pose_t *
named(tiku_trk_poseview_t *v, const char *name)
{
    int i, n = tiku_trk_poseview_count(v);

    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);

        if (p != NULL && strcmp(p->model.name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

int
main(void)
{
    tiku_backend_t *b;
    char big[300], small[300], list[300], perm[300];
    int i;

    snprintf(work, sizeof work, "/tmp/trk-poselayout-%d", (int)getpid());
    sh("rm -rf %s && mkdir -p %s", work, work);
    /* The state store's sidecar lives under the work directory. */
    (void)setenv("HOME", work, 1);
    (void)setenv("XDG_DATA_HOME", work, 1);
    snprintf(big, sizeof big, "%s/big", work);
    snprintf(small, sizeof small, "%s/small", work);
    snprintf(list, sizeof list, "%s/list", work);
    snprintf(perm, sizeof perm, "%s/perm", work);
    sh("mkdir -p %s %s %s %s", big, small, list, perm);
    for (i = 0; i < 60; i++) {
        sh("touch %s/f%02d", big, i);
    }
    sh("touch %s/only", small);
    sh("cd %s && touch alpha beta delta gamma omega sigma", list);
    b = tiku_backend_posix_open();

    printf("the extent follows auto-placement and a folder switch "
           "(PVL-050)\n");
    {
        tiku_trk_poseview_t *v = icon_view(b, big);
        int eh0 = 0, eh1 = 0, gx, gy, ox, oy;

        tiku_trk_poseview_set_viewport(v, 400, 300);
        (void)tiku_trk_poseview_place_unplaced(v, 400);
        tiku_trk_poseview_extent(v, NULL, NULL, NULL, &eh0);
        check(extent_covers(v), "PVL-050",
              "the box built after placement holds every icon");
        /* Laid out again one icon to a row: the icons run far below the
         * box measured above. */
        tiku_trk_poseview_cleanup(v, 70);
        check(extent_covers(v), "PVL-050",
              "icons auto-placed after the box was measured are inside "
              "it");
        tiku_trk_poseview_extent(v, NULL, NULL, NULL, &eh1);
        check(eh1 > eh0, "PVL-050",
              "so the scroll range reaches the lowest of them");
        (void)tiku_trk_poseview_switch_dir(v, small);
        (void)tiku_trk_poseview_place_unplaced(v, 400);
        tiku_trk_poseview_extent(v, NULL, NULL, NULL, &eh1);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(tiku_trk_poseview_count(v) == 1 && eh1 <= gy + 2 * oy,
              "PVL-050",
              "a switch to a one-file folder leaves a box one cell tall, "
              "not the last folder's");
        tiku_trk_poseview_free(v);
    }

    printf("\na nudge off the grid carries across a mode change "
           "(PVL-011)\n");
    {
        tiku_trk_poseview_t *v = icon_view(b, list);
        tiku_trk_pose_t *p0 = named(v, "alpha");
        tiku_trk_pose_t *p1 = named(v, "beta");
        int gx, gy, ox, oy, x, y;

        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        /* Both in cell (1, 1): one 4 px up and left of it, one 4 px down
         * and right. */
        tiku_trk_poseview_move_pose(v, p0, ox + gx - 4, oy + gy - 4, 0);
        tiku_trk_poseview_move_pose(v, p1, ox + 3 * gx + 4,
                                    oy + gy + 4, 0);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        tiku_trk_poseview_pose_location(v, p0, &x, &y);
        check(x > ox && y > oy, "PVL-011",
              "an icon nudged up and left stays in its own cell, not one "
              "cell up and left");
        check(x < ox + gx && y < oy + gy, "PVL-011",
              "and keeps the nudge up and left of the cell's corner");
        tiku_trk_poseview_pose_location(v, p1, &x, &y);
        check(x > ox + 3 * gx && y > oy + gy, "PVL-011",
              "an icon nudged down and right keeps its nudge too");
        check(x < ox + 4 * gx && y < oy + 2 * gy, "PVL-011",
              "within the same cell");
        tiku_trk_poseview_free(v);
    }

    printf("\nthe placement hint starts at the visible top-left "
           "(PVL-017)\n");
    {
        tiku_trk_poseview_t *v = icon_view(b, big);
        int gx, gy, ox, oy, x = 0, y = 0, hx = 0, hy = 0;

        tiku_trk_poseview_set_viewport(v, 200, 150);
        (void)tiku_trk_poseview_place_unplaced(v, 200);
        tiku_trk_poseview_set_origin(v, 0, 300);
        tiku_trk_poseview_origin(v, &x, &y);
        check(y == 300, "PVL-017", "the view scrolls down 300 px");
        tiku_trk_poseview_set_viewport(v, 210, 150);
        tiku_trk_poseview_origin(v, &x, &y);
        tiku_trk_poseview_hint(v, &hx, &hy);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(hy >= y && hy + gy <= y + 150, "PVL-017",
              "after a resize the hint is inside the scrolled view, not "
              "at the folder's top");
        check((hy - oy) % gy == 0 && (hx - ox) % gx == 0, "PVL-017",
              "and on the grid");
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_hint(v, &hx, &hy);
        check(hy >= y && hy + gy <= y + 150, "PVL-017",
              "a reload while scrolled starts there too");
        (void)tiku_trk_poseview_place_unplaced(v, 210);
        {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, 0);
            int px = 0, py = 0;

            tiku_trk_poseview_pose_location(v, p, &px, &py);
            check(py >= y && py + gy <= y + 150, "PVL-017",
                  "and the first icon placed lands in sight");
        }
        (void)tiku_trk_poseview_switch_dir(v, small);
        tiku_trk_poseview_hint(v, &hx, &hy);
        check(hx == ox && hy == oy, "PVL-017",
              "a switch to another folder places from that folder's "
              "top-left, not from the last folder's scroll");
        tiku_trk_poseview_free(v);
    }

    printf("\na band in a filtered list sweeps the rows shown "
           "(PVS-026)\n");
    {
        tiku_trk_poseview_t *v = tiku_trk_poseview_new(b, list);
        tiku_trk_pose_t *first;
        int rh, hits, x, y, w, h;

        tiku_trk_poseview_set_measure(v, measure, NULL);
        tiku_trk_poseview_set_row_height(v, 20);
        tiku_trk_poseview_set_viewport(v, 300, 400);
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_set_filtering(v, 1);
        tiku_trk_poseview_type_ahead(v, 'm');
        check(tiku_trk_poseview_count(v) == 3, "PVS-026",
              "the filter shows gamma, omega and sigma");
        rh = tiku_trk_poseview_row_height(v);
        first = tiku_trk_poseview_at(v, 0);
        tiku_trk_poseview_pose_rect(v, first, &x, &y, &w, &h);
        check(y == 0, "PVS-026",
              "the first row shown is at the top of the list");
        hits = tiku_trk_poseview_select_rect(v, 10, 2, 60, rh - 2, 0u);
        check(hits == 1 && first->selected, "PVS-026",
              "a band over the first row selects the pose drawn there");
        tiku_trk_poseview_free(v);
    }

    printf("\nthe text columns sort (TS-001)\n");
    {
        tiku_trk_poseview_t *v;
        tiku_store_t *store;
        int field;

        sh("cd %s && touch a b c && chmod 755 a && chmod 644 b && "
           "chmod 600 c", perm);
        v = tiku_trk_poseview_new(b, perm);
        (void)tiku_trk_poseview_reload(v);
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_PERM);
        check(strcmp(tiku_trk_poseview_at(v, 0)->model.name, "c") == 0 &&
              strcmp(tiku_trk_poseview_at(v, 2)->model.name, "a") == 0,
              "TS-001",
              "Permissions orders -rw------- before -rwxr-xr-x, not by "
              "name");
        store = b->ops->state_store(b);
        field = tiku_trk_poseview_attr_column(v, "test:rank", "Rank", 60);
        {
            char path[400];
            static const char *names[] = { "a", "b", "c" };
            static const char *ranks[] = { "3", "1", "2" };

            for (i = 0; i < 3; i++) {
                snprintf(path, sizeof path, "%s/%s", perm, names[i]);
                (void)tiku_state_write(store, path, "test:rank", ranks[i],
                                       strlen(ranks[i]));
            }
        }
        (void)tiku_trk_poseview_toggle_column(v, field);
        tiku_trk_poseview_sort_by(v, field);
        check(field >= 0 &&
              strcmp(tiku_trk_poseview_at(v, 0)->model.name, "b") == 0 &&
              strcmp(tiku_trk_poseview_at(v, 1)->model.name, "c") == 0 &&
              strcmp(tiku_trk_poseview_at(v, 2)->model.name, "a") == 0,
              "TS-001", "an attribute column orders by its values");
        tiku_trk_poseview_free(v);
    }

    printf("\nan unknown mode word keeps the entries' records (MA-063)\n");
    {
        tiku_trk_poseview_t *v = icon_view(b, list);
        tiku_store_t *store = b->ops->state_store(b);
        const char *bad = "sideways 64 0 0 0 0 0 0 -1 0 0";
        tiku_trk_pose_t *p;
        int at, x = 0, y = 0;

        p = named(v, "alpha");
        tiku_trk_poseview_move_pose(v, p, 300, 200, 0);
        p = named(v, "beta");
        at = -1;
        for (i = 0; i < tiku_trk_poseview_count(v); i++) {
            if (tiku_trk_poseview_at(v, i) == p) {
                at = i;
            }
        }
        (void)tiku_trk_poseview_set_invisible(v, at, 1);
        (void)tiku_trk_poseview_save_state(v);
        tiku_trk_poseview_free(v);
        (void)tiku_state_write(store, list, TIKU_ATTR_VIEW_STATE, bad,
                               strlen(bad));
        v = tiku_trk_poseview_new(b, list);
        (void)tiku_trk_poseview_reload(v);
        (void)tiku_trk_poseview_restore_state(v);
        check(tiku_trk_poseview_mode(v) == TIKU_TRK_MODE_LIST &&
              tiku_trk_poseview_icon_size(v) == 32, "MA-063",
              "the view-state record is discarded");
        p = named(v, "alpha");
        if (p != NULL) {
            x = p->loc_x;
            y = p->loc_y;
        }
        check(p != NULL && p->placed && x == 300 && y == 200, "MA-063",
              "an entry's saved position is restored all the same");
        check(named(v, "beta") == NULL && tiku_trk_poseview_count(v) == 5,
              "MA-063", "and an entry saved invisible stays hidden");
        tiku_trk_poseview_free(v);
    }

    tiku_backend_close(b);
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "the pose layout holds");
    return fails ? 1 : 0;
}
