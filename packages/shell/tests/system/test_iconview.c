/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_iconview.c - the icon viewport: cells, the extent, origins, what a
 * click lands on, and where an arrow goes.
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

/** @brief A label measure: eight pixels a character, no font needed. */
static int
measure(const char *text, void *ctx)
{
    (void)ctx;
    return (text != NULL) ? (int)strlen(text) * 8 : 0;
}

/** @brief An icon mask with a transparent left half, so sides differ. */
static int
half_mask(const tiku_model_t *m, int size, int px, int py, void *ctx)
{
    (void)m;
    (void)py;
    (void)ctx;
    return (px >= size / 2);
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_trk_poseview_t *v;
    int gx, gy, ox, oy, x, y, w, h;

    snprintf(work, sizeof work, "/tmp/trk-iconview-%d", (int)getpid());
    sh("rm -rf %s && mkdir -p %s", work, work);
    sh("cd %s && touch alpha beta gamma delta epsilon", work);

    v = tiku_trk_poseview_new(b, work);
    tiku_trk_poseview_set_measure(v, measure, NULL);
    (void)tiku_trk_poseview_reload(v);
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_set_icon_size(v, 32);

    printf("the cell is as tall as what goes in it (PVL-007)\n");
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    {
        int without = gy;

        tiku_trk_poseview_set_font_metrics(v, 12, 9);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(gy == 32 + 12 + 1, "PVL-007",
              "icon mode: the art plus the label's height, since the name "
              "is drawn UNDER the icon");
        check(gy != without, "PVL-007",
              "which is not what a cell derived from the art alone gives");
        tiku_trk_poseview_set_font_metrics(v, 24, 18);
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(gy == 32 + 24 + 1, "PVL-007",
              "a bigger face makes a taller cell -- a height fixed to the "
              "icon clips the name at one font and floats it at another");
        check(tiku_trk_poseview_row_height(v) == (int)(24 * 1.65f + 0.999f),
              "PVL-007", "and a list row is the font's own height, scaled");
        tiku_trk_poseview_set_font_metrics(v, 12, 9);
    }
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    check(gy == 16, "PVL-007",
          "mini mode: the art's height while the art is the taller, since "
          "there the two sit side by side rather than stacked");
    tiku_trk_poseview_set_font_metrics(v, 24, 18);
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    check(gy == 25, "PVL-007",
          "and the LABEL's when the label is -- a row shorter than the "
          "text in it clips every name");
    tiku_trk_poseview_set_font_metrics(v, 12, 9);
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);

    printf("\nwhat a click lands on (PVS-019, IV-026)\n");
    (void)tiku_trk_poseview_place_unplaced(v, 400);
    {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, 0);
        int ix, iy, iw, ih, lx, ly, lw, lh, px, py, pw, ph, cx, cy;

        tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih,
                                     &lx, &ly, &lw, &lh);
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        check(ly >= iy + ih, "PVS-019",
              "the label sits below the icon, not over it");
        check(px <= ix && px + pw >= ix + iw, "PVS-019",
              "and the rect a band sweeps is their union");
        check(tiku_trk_poseview_pose_at(v, ix + iw / 2, iy + 2) == p,
              "PVS-019", "a point on the icon hits the pose");
        check(tiku_trk_poseview_pose_at(v, lx + 2, ly + 2) == p, "PVS-019",
              "so does a point on the name, where the name is DRAWN");
        check(lw < iw * 3, "PVS-019",
              "the fixture's names are shorter than their cells, which is "
              "what the next check needs");
        tiku_trk_poseview_pose_location(v, p, &cx, &cy);
        check(tiku_trk_poseview_pose_at(v, cx + 1, ly + 2) != p,
              "PVS-019",
              "but the space BESIDE the name hits nothing, and neither "
              "does the empty bottom of the cell: in an icon mode a cell "
              "is mostly empty, and treating the cell as the pose is what "
              "makes the gap between two icons undraggable");
        check(tiku_trk_poseview_pose_at(v, ix + iw / 2,
                                        ly + lh + 2) != p, "PVS-019",
              "nor does the space under it");
        tiku_trk_poseview_set_icon_hit(v, half_mask, NULL);
        check(tiku_trk_poseview_pose_at(v, ix + iw - 2, iy + 2) == p,
              "IV-026", "with a mask, an opaque pixel still hits");
        check(tiku_trk_poseview_pose_at(v, ix + 1, iy + 2) != p, "IV-026",
              "and a TRANSPARENT one does not -- an icon is not a "
              "rectangle, and arming from its clear corner is what makes "
              "the gap between two icons impossible to sweep through");
        tiku_trk_poseview_set_icon_hit(v, NULL, NULL);
        check(tiku_trk_poseview_pose_at(v, ix + 1, iy + 2) == p, "IV-026",
              "without one the square answers, as it did before");
    }

    printf("\nShift follows the icon arrangement (PVS-005)\n");
    {
        tiku_trk_poseview_t *sv = tiku_trk_poseview_new(b, work);
        tiku_trk_pose_t *p0, *p1, *p2, *p4;
        int old1x, old1y, old4x, old4y;
        int x0, y0, x2, y2;

        tiku_trk_poseview_set_measure(sv, measure, NULL);
        tiku_trk_poseview_set_font_metrics(sv, 12, 9);
        tiku_trk_poseview_set_mode(sv, TIKU_TRK_MODE_ICON);
        (void)tiku_trk_poseview_place_unplaced(sv, 400);
        p0 = tiku_trk_poseview_at(sv, 0);
        p1 = tiku_trk_poseview_at(sv, 1);
        p2 = tiku_trk_poseview_at(sv, 2);
        p4 = tiku_trk_poseview_at(sv, 4);
        tiku_trk_poseview_pose_location(sv, p1, &old1x, &old1y);
        tiku_trk_poseview_pose_location(sv, p4, &old4x, &old4y);
        /* Swap two existing slots, so the geometry and name order disagree
         * without changing the view's overall extent. */
        tiku_trk_poseview_move_pose(sv, p1, old4x, old4y, 0);
        tiku_trk_poseview_move_pose(sv, p4, old1x, old1y, 0);
        tiku_trk_poseview_pose_location(sv, p0, &x0, &y0);
        tiku_trk_poseview_pose_location(sv, p2, &x2, &y2);
        tiku_trk_poseview_select_none(sv);
        tiku_trk_poseview_click_at(sv, 0, x0 + 2, y0 + 2, 0u);
        tiku_trk_poseview_click_at(sv, 2, x2 + 20, y2 + 20,
                                   TIKU_TRK_MOD_SHIFT);
        check(p0->selected && p2->selected && p4->selected && !p1->selected,
              "PVS-005",
              "Shift selects the rectangle between arranged icons, not the "
              "name-sorted index range between them");
        tiku_trk_poseview_click_at(sv, 2, x2 + 20, y2 + 20,
            TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
        check(!p0->selected && !p2->selected && !p4->selected &&
                  !p1->selected,
              "PVS-005", "Shift+Command applies the same spatial rectangle");
        tiku_trk_poseview_free(sv);
    }

    printf("\nthe scrollable area (PVL-049)\n");
    tiku_trk_poseview_extent(v, &x, &y, &w, &h);
    {
        int i, n = tiku_trk_poseview_count(v), lox = 0, hix = 0, hiy = 0;

        for (i = 0; i < n; i++) {
            tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
            int px, py, pw, ph;

            tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
            if (i == 0 || px < lox)         { lox = px; }
            if (i == 0 || px + pw > hix)    { hix = px + pw; }
            if (i == 0 || py + ph > hiy)    { hiy = py + ph; }
        }
        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        check(x == 0 && y == 0 && lox >= 0, "PVL-049",
              "the window's own corner is in it whatever the icons do: a "
              "box that started at the first icon would scroll the view "
              "down to meet a drop made low in the window");
        check(w == hix + ox, "PVL-049",
              "and it reaches past the furthest icon by the layout's own "
              "margin, so an icon at the far edge is not flush to it");
        check(h >= hiy - y, "PVL-049", "in both directions");
        {
            int px, py, nx, ny, nw, nh;

            tiku_trk_poseview_pose_location(v, tiku_trk_poseview_at(v, 0),
                                            &px, &py);
            tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 0), -200,
                                        -100, 0);
            tiku_trk_poseview_extent(v, &nx, &ny, &nw, &nh);
            check(nx <= -200 && ny <= -100, "PVL-049",
                  "content that really is outside that corner extends it "
                  "the other way, or an icon dragged above the top could "
                  "never be scrolled back to");
            tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 0), px,
                                        py, 0);
        }
    }
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_extent(v, &x, &y, &w, &h);
    check(h == tiku_trk_poseview_count(v) * tiku_trk_poseview_row_height(v),
          "PVL-049",
          "in list mode it is the rows, which is a different question with "
          "the same name");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);

    printf("\nthe box is grown, not walked (PVL-050)\n");
    {
        int w0, h0, w1, h1;

        tiku_trk_poseview_extent(v, &x, &y, &w0, &h0);
        tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 0), 900, 900,
                                    0);
        tiku_trk_poseview_extent(v, &x, &y, &w1, &h1);
        check(w1 > w0 && h1 > h0, "PVL-050",
              "moving an icon out grows the box");
        tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 0), 10, 10,
                                    0);
        tiku_trk_poseview_extent(v, &x, &y, &w1, &h1);
        check(w1 > w0 || h1 > h0, "PVL-050",
              "moving it back does NOT shrink it: a running box cannot "
              "shrink, and pretending otherwise costs a walk per move");
        {   /* A removal is the one thing that pays for the walk. */
            sh("rm -f %s/alpha", work);
            (void)tiku_trk_poseview_poll(v, 0, NULL, -1, 1);
            tiku_trk_poseview_extent(v, &x, &y, &w1, &h1);
            check(w1 <= w0 + 1 && h1 <= h0 + 1, "PVL-050",
                  "and a removal recomputes it, which is what keeps a "
                  "deleted icon from leaving empty space to scroll into");
        }
    }

    printf("\none origin per mode (PVL-009)\n");
    /* A window smaller than the content, or there is nothing to scroll and
     * every origin clamps to the same place. */
    tiku_trk_poseview_set_viewport(v, 60, 40);
    tiku_trk_poseview_set_origin(v, 40, 30);
    tiku_trk_poseview_origin(v, &x, &y);
    check(x == 40 && y == 30, "PVL-009", "an icon view scrolls in pixels");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
    tiku_trk_poseview_origin(v, &x, &y);
    check(x == 0 && y == 0, "PVL-009",
          "a list has no pixel origin of its own -- it counts rows");
    tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
    tiku_trk_poseview_origin(v, &x, &y);
    check(x == 40 && y == 30, "PVL-009",
          "and coming back returns to where the icons were, which one "
          "shared origin could not do");
    {   /* And the two ICON modes do not share one either. */
        int ix0, iy0, mx, my, ix1, iy1;

        tiku_trk_poseview_origin(v, &ix0, &iy0);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
        tiku_trk_poseview_set_viewport(v, 60, 40);
        tiku_trk_poseview_set_origin(v, ix0 + 37, iy0 + 21);
        tiku_trk_poseview_origin(v, &mx, &my);
        check(mx != ix0 || my != iy0, "PVL-009",
              "the mini view is put somewhere else");
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_viewport(v, 60, 40);
        tiku_trk_poseview_origin(v, &ix1, &iy1);
        check(ix1 == ix0 && iy1 == iy0, "PVL-009",
              "and the icon view is where it was left, not where the mini "
              "view was put: their cells are different shapes, so the same "
              "pixel is a different place");
    }
    tiku_trk_poseview_set_origin(v, -500, -500);
    tiku_trk_poseview_origin(v, &x, &y);
    {
        int ex, ey, ew, eh;

        tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
        check(x >= ex && y >= ey, "PVL-009",
              "an origin before the content is clamped to it");
        tiku_trk_poseview_set_origin(v, 100000, 100000);
        tiku_trk_poseview_origin(v, &x, &y);
        check(x <= ex + ew && y <= ey + eh, "PVL-009",
              "and one past the end is too: rows of blank space with "
              "nothing to scroll back to is how a folder opens looking "
              "empty");
    }

    printf("\nHome, End and the pages (PVK-010, PVK-011)\n");
    tiku_trk_poseview_select_none(v);
    tiku_trk_poseview_click(v, 0, 0u);
    tiku_trk_poseview_set_origin(v, 0, 0);
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_END, 0u);
    tiku_trk_poseview_origin(v, &x, &y);
    check(y > 0 && tiku_trk_poseview_selection_count(v) == 1, "PVK-010",
          "End moves the view and leaves the selection alone: there is no "
          "last ROW to select in a two-dimensional arrangement");
    (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_HOME, 0u);
    tiku_trk_poseview_origin(v, &x, &y);
    {
        int ex, ey, ew, eh;

        tiku_trk_poseview_extent(v, &ex, &ey, &ew, &eh);
        check(y == ey, "PVK-010", "and Home goes back to the top");
    }
    {
        int before, after;

        tiku_trk_poseview_origin(v, &x, &before);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_PGDN, 0u);
        tiku_trk_poseview_origin(v, &x, &after);
        check(after - before == 40, "PVK-011",
              "a page is exactly what the WINDOW shows -- forty pixels of "
              "a forty-pixel window -- not a count of rows a layout "
              "without rows does not have");
    }

    printf("\nwhat is on screen (PVK-011, PVL-062)\n");
    {
        int first = -1, last = -1, px, py, pw, ph, n;

        /* Parked on one pose's own corner, because the corner of the
         * BOUNDING BOX need not contain any pose at all: the extreme x can
         * come from one icon and the extreme y from another. */
        tiku_trk_poseview_pose_rect(v, tiku_trk_poseview_at(v, 0), &px, &py,
                                    &pw, &ph);
        tiku_trk_poseview_set_origin(v, px, py);
        check(tiku_trk_poseview_visible_range(v, &first, &last) &&
              first == 0, "PVL-062",
              "the view can say which poses are in the window");
        n = tiku_trk_poseview_count(v);
        check(last < n - 1, "PVL-062",
              "and that the ones past the bottom are NOT -- which is the "
              "whole point: a renderer that cannot ask walks every pose in "
              "the folder on every frame");
        tiku_trk_poseview_set_origin(v, px, py + 100000);
        {
            int f2 = -1, l2 = -1;

            (void)tiku_trk_poseview_visible_range(v, &f2, &l2);
            check(f2 != first, "PVL-062",
                  "the answer follows the origin");
        }
        tiku_trk_poseview_set_origin(v, px, py);
    }

    printf("\nkeeping the middle while the size changes (PVL-010)\n");
    {
        int y0, y1;

        tiku_trk_poseview_set_icon_size(v, 32);
        tiku_trk_poseview_set_origin(v, 0, 200);
        tiku_trk_poseview_origin(v, &x, &y0);
        (void)tiku_trk_poseview_step_icon_size(v, 1);
        tiku_trk_poseview_origin(v, &x, &y1);
        check(y1 > y0, "PVL-010",
              "growing the icons moves the origin down with the content: "
              "the pixels all moved, and an origin that did not would jump "
              "the view to the top-left");
    }

    printf("\nstraightening versus rearranging (PVL-021, PVL-024)\n");
    {
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);
        tiku_trk_pose_t *p1 = tiku_trk_poseview_at(v, 1);
        int gx, gy, ox, oy, x0, y0, x1, y1;

        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        /* Put two icons out of order and off the grid. */
        tiku_trk_poseview_move_pose(v, p0, ox + 3 * gx + 5, oy + 2, 0);
        tiku_trk_poseview_move_pose(v, p1, ox + 3, oy + 4, 0);
        check(tiku_trk_poseview_snap_to_grid(v) > 0, "PVL-021",
              "Clean up moves what is off the grid");
        tiku_trk_poseview_pose_location(v, p0, &x0, &y0);
        tiku_trk_poseview_pose_location(v, p1, &x1, &y1);
        check(((x0 - ox) % gx) == 0 && ((y0 - oy) % gy) == 0, "PVL-021",
              "onto a grid point");
        check(x0 > x1, "PVL-021",
              "and it does NOT reorder them: the icon the user put on the "
              "right is still on the right, because straightening an "
              "arrangement and replacing it are two different commands");
        check(tiku_trk_poseview_snap_to_grid(v) == 0, "PVL-021",
              "a second Clean up moves nothing, since everything is "
              "already on a point");
        {   /* And it is an ICON-mode command. */
            int lx, ly;

            tiku_trk_poseview_pose_location(v, p1, &lx, &ly);
            tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
            p1->loc_x += 7;     /* off any grid */
            check(tiku_trk_poseview_snap_to_grid(v) == 0, "PVL-021",
                  "in list mode it does nothing at all: there is no grid "
                  "to snap to, and laying out against one would clear an "
                  "arrangement made in icon mode");
            p1->loc_x -= 7;
            tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
            (void)lx; (void)ly;
        }
        tiku_trk_poseview_cleanup(v, 400);
        tiku_trk_poseview_pose_location(v, p0, &x0, &y0);
        tiku_trk_poseview_pose_location(v, p1, &x1, &y1);
        check(x0 < x1 || y0 < y1, "PVL-024",
              "Arrange by is the other command: it lays them out in SORT "
              "order, so the first by name comes first");
    }

    printf("\nwhere an arrow goes (PVK-007, PVK-008)\n");
    {
        tiku_trk_pose_t *a, *bb, *cc;

        /* A deliberate arrangement: b to the right of a, c below a. */
        a = tiku_trk_poseview_at(v, 0);
        bb = tiku_trk_poseview_at(v, 1);
        cc = tiku_trk_poseview_at(v, 2);
        tiku_trk_poseview_move_pose(v, a, 100, 100, 0);
        tiku_trk_poseview_move_pose(v, bb, 200, 100, 0);
        tiku_trk_poseview_move_pose(v, cc, 100, 200, 0);
        tiku_trk_poseview_select_none(v);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0u);
        {
            int i, n = tiku_trk_poseview_count(v), sel = -1, topmost = -1;
            int besty = 0, bestx = 0;

            for (i = 0; i < n; i++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
                int px, py, pw, ph;

                if (p->selected) { sel = i; }
                tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
                if (topmost < 0 || py < besty ||
                    (py == besty && px < bestx)) {
                    topmost = i;
                    besty = py;
                    bestx = px;
                }
            }
            check(sel == topmost, "PVK-008",
                  "the first arrow with nothing selected picks the "
                  "upper-left-most pose, whichever key it was");
        }
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, a->index, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_RIGHT, 0u);
        check(tiku_trk_poseview_at(v, 1)->selected, "PVK-007",
              "Right from an icon picks the one to its RIGHT, not the next "
              "in sort order -- a walk by index jumps across the window");
        /* One NEARER than the target, but the wrong way. */
        tiku_trk_poseview_move_pose(v, tiku_trk_poseview_at(v, 3), 100, 70,
                                    0);
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, a->index, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_DOWN, 0u);
        check(tiku_trk_poseview_at(v, 2)->selected, "PVK-007",
              "and Down picks the one below it, not the one ABOVE it that "
              "happens to be nearer -- a direction key is a half-plane, "
              "not a distance");
        tiku_trk_poseview_select_none(v);
        tiku_trk_poseview_click(v, bb->index, 0u);
        (void)tiku_trk_poseview_key_nav(v, TIKU_TRK_KEY_RIGHT, 0u);
        check(tiku_trk_poseview_at(v, 1)->selected, "PVK-007",
              "with nothing that way the selection stays where it is "
              "rather than wrapping to the other side of the window");
    }

    printf("\nwhat the folder remembers (MA-061)\n");
    {
        int ox, oy;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_SIZE);
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_SIZE);   /* reversed */
        tiku_trk_poseview_set_viewport(v, 60, 40);
        tiku_trk_poseview_set_origin(v, 30, 20);
        tiku_trk_poseview_origin(v, &ox, &oy);
        (void)tiku_trk_poseview_save_state(v);
        {
            tiku_trk_poseview_t *again = tiku_trk_poseview_new(b, work);
            int rx = -1, ry = -1;

            tiku_trk_poseview_set_measure(again, measure, NULL);
            tiku_trk_poseview_set_font_metrics(again, 12, 9);
            (void)tiku_trk_poseview_reload(again);
            (void)tiku_trk_poseview_restore_state(again);
            check(tiku_trk_poseview_mode(again) == TIKU_TRK_MODE_ICON,
                  "MA-061", "the mode comes back");
            check(tiku_trk_poseview_sort_field(again) ==
                      tiku_trk_poseview_sort_field(v) &&
                  tiku_trk_poseview_sort_reverse(again) ==
                      tiku_trk_poseview_sort_reverse(v),
                  "MA-061",
                  "and so do the SORT keys: a folder arranged by one "
                  "column and closed came back arranged by name, which is "
                  "the arrangement lost in the one place a user would look "
                  "for it");
            tiku_trk_poseview_origin(again, &rx, &ry);
            check(rx == ox && ry == oy, "MA-061",
                  "and so does where the icon view was SCROLLED to, which "
                  "the record had no room for -- a folder left half way "
                  "down reopened at the top");
            tiku_trk_poseview_set_mode(again, TIKU_TRK_MODE_LIST);
            check(tiku_trk_poseview_scroll(again) >= 0, "MA-061",
                  "the list's row and the icon view's pixels are different "
                  "numbers, kept apart");
            tiku_trk_poseview_free(again);
        }
    }

    printf("\nthe icon mode a list detour left behind (MA-061)\n");
    {
        tiku_trk_poseview_t *again, *truth;
        int ax, ay, tx, ty;

        /* Arranged in MINI, then parked in list mode and saved: the
         * positions on disk are mini-geometry, and only the record can
         * say so. */
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_MINI);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_LIST);
        (void)tiku_trk_poseview_save_state(v);
        again = tiku_trk_poseview_new(b, work);
        tiku_trk_poseview_set_measure(again, measure, NULL);
        tiku_trk_poseview_set_font_metrics(again, 12, 9);
        (void)tiku_trk_poseview_reload(again);
        (void)tiku_trk_poseview_restore_state(again);
        check(tiku_trk_poseview_last_icon_mode(again) == TIKU_TRK_MODE_MINI,
              "MA-061", "the record says WHICH icon mode the folder left, "
              "not merely that it sits in list mode now");
        /* The ground truth: go back through mini first, then to icon --
         * the live path that maps the grid. */
        truth = tiku_trk_poseview_new(b, work);
        tiku_trk_poseview_set_measure(truth, measure, NULL);
        tiku_trk_poseview_set_font_metrics(truth, 12, 9);
        (void)tiku_trk_poseview_reload(truth);
        (void)tiku_trk_poseview_restore_state(truth);
        tiku_trk_poseview_set_mode(truth, TIKU_TRK_MODE_MINI);
        tiku_trk_poseview_set_mode(truth, TIKU_TRK_MODE_ICON);
        /* The claim: straight from the restored list into icon mode lands
         * the poses exactly where the detour through mini would have. */
        tiku_trk_poseview_set_mode(again, TIKU_TRK_MODE_ICON);
        ax = tiku_trk_poseview_at(again, 0)->loc_x;
        ay = tiku_trk_poseview_at(again, 0)->loc_y;
        tx = tiku_trk_poseview_at(truth, 0)->loc_x;
        ty = tiku_trk_poseview_at(truth, 0)->loc_y;
        check(ax == tx && ay == ty, "MA-061",
              "so leaving list straight for icon maps the stored "
              "mini-geometry instead of taking it for icon-geometry");
        tiku_trk_poseview_free(again);
        tiku_trk_poseview_free(truth);
    }

    printf("\nthree state slots that never collide (MA-064)\n");
    {
        struct tiku_store *store = b->ops->state_store(b);
        tiku_trk_poseview_t *desk, *folder, *desk2, *root;
        char buf[96];

        /* The Desktop's arrangement and a folder window on the same
         * directory are different records: rearranging one must not
         * disturb the other. */
        desk = tiku_trk_poseview_new(b, work);
        tiku_trk_poseview_set_state_slot(desk, TIKU_KEY_DESKTOP);
        tiku_trk_poseview_set_mode(desk, TIKU_TRK_MODE_ICON);
        (void)tiku_trk_poseview_save_state(desk);
        folder = tiku_trk_poseview_new(b, work);
        tiku_trk_poseview_set_mode(folder, TIKU_TRK_MODE_MINI);
        (void)tiku_trk_poseview_save_state(folder);
        desk2 = tiku_trk_poseview_new(b, work);
        tiku_trk_poseview_set_state_slot(desk2, TIKU_KEY_DESKTOP);
        (void)tiku_trk_poseview_restore_state(desk2);
        check(tiku_trk_poseview_mode(desk2) == TIKU_TRK_MODE_ICON,
              "MA-064", "the Desktop slot survives a folder window "
              "arranging the same directory its own way");
        /* And the Disks window: the root's state is redirected onto the
         * Desktop directory under its own attribute name, exactly as the
         * original redirects it -- the root itself is nowhere the store
         * can write. */
        {
            char deskdir[TIKU_PATH_MAX];

            (void)setenv("HOME", work, 1);
            /* Wherever this Tracker's Desktop is; the root's state is
             * redirected onto it, not onto the login session's. */
            (void)tiku_desktop_dir(deskdir, sizeof deskdir);
            sh("mkdir -p '%s'", deskdir);
            root = tiku_trk_poseview_new(b, "/");
            tiku_trk_poseview_set_mode(root, TIKU_TRK_MODE_MINI);
            (void)tiku_trk_poseview_save_state(root);
            check(tiku_state_read(store, deskdir,
                                      TIKU_ATTR_DISKS_VIEW_STATE, buf,
                                      sizeof buf) > 0,
                  "MA-064", "the root window writes the Disks slot on the "
                  "Desktop directory, colliding with no folder's own");
        }
        tiku_trk_poseview_free(desk);
        tiku_trk_poseview_free(folder);
        tiku_trk_poseview_free(desk2);
        tiku_trk_poseview_free(root);
    }

    printf("\nsaved state that is out of range (MA-063)\n");
    {
        struct tiku_store *store = b->ops->state_store(b);
        const char *bad;
        tiku_trk_poseview_t *again;

        bad = "icon 999 0 0 0 0 0 0 -1 0";
        (void)tiku_state_write(store, work, TIKU_ATTR_VIEW_STATE,
                                   bad, strlen(bad));
        again = tiku_trk_poseview_new(b, work);
        (void)tiku_trk_poseview_restore_state(again);
        check(tiku_trk_poseview_icon_size(again) == 128, "MA-063",
              "an icon size past the top of the ladder is pulled back to "
              "the largest there is, not thrown away for the default: the "
              "folder was left as large as it could be asked for");
        tiku_trk_poseview_free(again);

        bad = "icon 4 0 0 0 0 0 0 -1 0";
        (void)tiku_state_write(store, work, TIKU_ATTR_VIEW_STATE,
                                   bad, strlen(bad));
        again = tiku_trk_poseview_new(b, work);
        (void)tiku_trk_poseview_restore_state(again);
        check(tiku_trk_poseview_icon_size(again) == 16, "MA-063",
              "and one below the bottom comes up to the smallest");

        bad = "list -7 0 0 0 0 0 0 -1 0";
        (void)tiku_state_write(store, work, TIKU_ATTR_VIEW_STATE,
                                   bad, strlen(bad));
        tiku_trk_poseview_free(again);
        again = tiku_trk_poseview_new(b, work);
        (void)tiku_trk_poseview_restore_state(again);
        check(tiku_trk_poseview_icon_size(again) == 16, "MA-063",
              "a negative size is a size below the bottom, not a number to "
              "obey: an icon cannot be drawn -7 across");
        tiku_trk_poseview_free(again);

        bad = "sideways 64 9 0 0 0 0 3 -1 1";
        (void)tiku_state_write(store, work, TIKU_ATTR_VIEW_STATE,
                                   bad, strlen(bad));
        again = tiku_trk_poseview_new(b, work);
        (void)tiku_trk_poseview_reload(again);
        (void)tiku_trk_poseview_restore_state(again);
        check(tiku_trk_poseview_mode(again) == TIKU_TRK_MODE_LIST &&
              tiku_trk_poseview_icon_size(again) == 32 &&
              tiku_trk_poseview_scroll(again) == 0 &&
              tiku_trk_poseview_sort_field(again) == TIKU_TRK_COL_NAME,
              "MA-063",
              "a mode nothing recognises discards the WHOLE record rather "
              "than the mode alone: the numbers beside it were written by "
              "whatever wrote the name, and a size and a sort key taken "
              "from a record that garbled are not more trustworthy than "
              "the name was");
        tiku_trk_poseview_free(again);
        (void)tiku_state_forget(store, work);
    }

    printf("\nwhat a sweep leaves behind (PVS-029)\n");
    {
        tiku_trk_pose_t *p0 = tiku_trk_poseview_at(v, 0);
        int i, sel = 0, px, py, pw, ph;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_select_none(v);
        /* Clicked by hand, so the pivot is a real one, then swept far away
         * from it. */
        tiku_trk_poseview_click(v, 0, 0u);
        tiku_trk_poseview_pose_rect(v, p0, &px, &py, &pw, &ph);
        /* Command held, so the sweep keeps what is already selected and
         * toggles on top of it -- and sweeping ACROSS the clicked pose
         * toggles the pivot itself off. */
        tiku_trk_poseview_set_band(v, 1);
        (void)tiku_trk_poseview_select_rect(v, px, py, px + pw, py + ph,
                                            TIKU_TRK_MOD_CMD);
        check(p0->selected == 0, "PVS-029",
              "a sweep back over the pose it was anchored on takes it out "
              "of the selection");
        tiku_trk_poseview_set_band(v, 0);
        {
            tiku_trk_pose_t *p3 = tiku_trk_poseview_at(v, 3);

            /* Earlier size tests leave these icons overlapping.
             * Isolate the next click so this check is about the dropped pivot,
             * not PVS-005 correctly catching another intersecting icon. */
            tiku_trk_poseview_move_pose(v, p3, px + pw + 100,
                                        py + ph + 100, 0);
        }
        tiku_trk_poseview_click(v, 3, TIKU_TRK_MOD_SHIFT);
        for (i = 0; i < tiku_trk_poseview_count(v); i++) {
            tiku_trk_pose_t *q = tiku_trk_poseview_at(v, i);

            if (q != NULL && q->selected) {
                sel++;
            }
        }
        check(sel == 1, "PVS-029",
              "so the release drops the pivot with it: the shift-click "
              "after a sweep selects one pose rather than extending from "
              "a pose the sweep had already let go of");
    }

    printf("\nwhere the next icon lands (PVL-017)\n");
    {
        int hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;

        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        tiku_trk_poseview_set_viewport(v, 400, 300);
        tiku_trk_poseview_hint(v, &hx0, &hy0);
        tiku_trk_poseview_set_viewport(v, 240, 180);
        tiku_trk_poseview_hint(v, &hx1, &hy1);
        check(hx1 == hx0 && hy1 == hy0, "PVL-017",
              "a resize starts the placement hint over: it is a point in "
              "the WINDOW, and a window of another size has a different "
              "top-left to place from");
        {
            int hx2 = 0, hy2 = 0;

            /* Placing icons ADVANCES the hint; a reload must put it back,
             * or the first new icon lands where the last listing left
             * off. */
            (void)tiku_trk_poseview_reload(v);
            (void)tiku_trk_poseview_place_unplaced(v, 400);
            tiku_trk_poseview_hint(v, &hx2, &hy2);
            check(hx2 != hx0 || hy2 != hy0, "PVL-017",
                  "placing icons moves the hint along, which is what makes "
                  "the reset matter at all");
            (void)tiku_trk_poseview_reload(v);
            tiku_trk_poseview_hint(v, &hx1, &hy1);
            check(hx1 == hx0 && hy1 == hy0, "PVL-017",
                  "and a reload puts it back -- a fresh set of poses "
                  "places from the top-left rather than from wherever the "
                  "last one finished");
        }
    }

    printf("\na name too long for its cell (IV-047)\n");
    {
        char big[120];
        tiku_trk_pose_t *p;
        int ix, iy, iw, ih, lx, ly, lw, lh, budget;

        memset(big, 'w', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        memcpy(big, "start", 5);
        memcpy(big + sizeof big - 4, "end", 3);
        sh("touch %s/%s", work, big);
        tiku_trk_poseview_set_mode(v, TIKU_TRK_MODE_ICON);
        (void)tiku_trk_poseview_reload(v);
        p = NULL;
        {
            int i, n = tiku_trk_poseview_count(v);

            for (i = 0; i < n; i++) {
                if (strcmp(tiku_trk_poseview_at(v, i)->model.name, big) == 0) {
                    p = tiku_trk_poseview_at(v, i);
                }
            }
        }
        budget = 30 * measure("M", NULL);
        check(p != NULL, "IV-047", "the long name is listed at all");
        if (p != NULL) {
            char fit[256];

            tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih,
                                         &lx, &ly, &lw, &lh);
            check(lw <= budget, "IV-047",
                  "the label a pose OWNS is bounded by thirty characters' "
                  "worth of room: measured from the whole name, the "
                  "selection box and the hit area run off across the icons "
                  "beside it");
            (void)tiku_trk_poseview_fit_label(v, p, fit, sizeof fit);
            check(strncmp(fit, "start", 5) == 0 &&
                  strstr(fit, "end") != NULL &&
                  strstr(fit, "\xe2\x80\xa6") != NULL,
                  "IV-047",
                  "and it is the MIDDLE that goes: a column of names "
                  "sharing a prefix is unreadable when every one of them "
                  "is cut at the same point");
            check(measure(fit, NULL) <= budget, "IV-047",
                  "what is drawn fits the same budget the box was given, "
                  "so the text cannot overrun its own highlight");
        }
        sh("rm -f %s/%s", work, big);
        (void)tiku_trk_poseview_reload(v);
    }

    tiku_trk_poseview_free(v);
    tiku_backend_close(b);
    sh("rm -rf %s", work);
    printf("\n%s\n", fails ? "FAILURES" : "the icon viewport holds");
    return fails ? 1 : 0;
}
