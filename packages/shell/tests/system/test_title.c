/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_title.c - column geometry and the title strip's tracking state.
 *
 * The rules in this area are almost all thresholds, and a threshold is only
 * a rule if something checks the wrong side of it.  Each case here sits one
 * step either side of the boundary it is about.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_poseview.h"
#include "tiku_trk_titleview.h"
#include "tiku_state.h"
#include "tiku_trk_task.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief Six characters wide per character, so a width is predictable. */
static int
fixed_measure(const char *text, void *ctx)
{
    (void)ctx;
    return 6 * (int)strlen(text);
}

static void
make_tree(const char *dir)
{
    static const char *names[] = { "a", "bb", "ccc", "dddd" };
    char p[256];
    int i;

    (void)mkdir(dir, 0755);
    for (i = 0; i < 4; i++) {
        FILE *f;

        snprintf(p, sizeof p, "%s/%s.txt", dir, names[i]);
        f = fopen(p, "w");
        if (f != NULL) { (void)fputc('x', f); (void)fclose(f); }
    }
}

static void
rm_tree(const char *dir)
{
    static const char *names[] = { "a", "bb", "ccc", "dddd" };
    char p[256];
    int i;

    for (i = 0; i < 4; i++) {
        snprintf(p, sizeof p, "%s/%s.txt", dir, names[i]);
        (void)unlink(p);
    }
    (void)rmdir(dir);
}

/** @brief The width of the column showing @p field. */
static int
width_of(tiku_trk_poseview_t *v, int field)
{
    const tiku_trk_column_t *c;
    int n = tiku_trk_poseview_columns(v, &c), i;

    for (i = 0; i < n; i++) {
        if (c[i].field == field) { return c[i].width; }
    }
    return -1;
}

static int
offset_of(tiku_trk_poseview_t *v, int field)
{
    const tiku_trk_column_t *c;
    int n = tiku_trk_poseview_columns(v, &c), i;

    for (i = 0; i < n; i++) {
        if (c[i].field == field) { return c[i].offset; }
    }
    return -1;
}

static int
index_of(tiku_trk_poseview_t *v, int field)
{
    const tiku_trk_column_t *c;
    int n = tiku_trk_poseview_columns(v, &c), i;

    for (i = 0; i < n; i++) {
        if (c[i].field == field) { return i; }
    }
    return -1;
}

/** @brief Put the columns in a known order: Name, Kind, Value, Cap. */
static void
order_as(tiku_trk_poseview_t *v)
{
    static const int want[] = { TIKU_TRK_COL_NAME, TIKU_TRK_COL_KIND,
                                TIKU_TRK_COL_VALUE, TIKU_TRK_COL_CAP };
    int i;

    for (i = 3; i >= 1; i--) {
        /* Placed from the right, so each move lands the column at the
         * index it wants without disturbing the ones already placed. */
        if (index_of(v, want[i]) != i && index_of(v, want[i]) >= 0) {
            const tiku_trk_column_t *c;
            int n = tiku_trk_poseview_columns(v, &c);

            if (i < n) {
                (void)tiku_trk_poseview_move_column(v, want[i], c[i].field);
            }
        }
    }
}

int
main(void)
{
    const char *dir = "/tmp/trk_title_test";
    tiku_backend_t *b;
    tiku_trk_poseview_t *v;
    tiku_trk_titleview_t t;
    int64_t now = tiku_trk_now();

    make_tree(dir);
    b = tiku_backend_posix_open();
    v = tiku_trk_poseview_new(b, dir);
    if (v == NULL) { printf("cannot open view\n"); return 1; }
    tiku_trk_poseview_set_measure(v, fixed_measure, NULL);
    tiku_trk_titleview_init(&t, v, 18);

    printf("column geometry\n");
    {
        int start = tiku_trk_poseview_start_offset(v);
        int l, r;

        check(offset_of(v, TIKU_TRK_COL_NAME) == start, "PVL-033",
              "the first column starts where the icon strip ends");
        check(offset_of(v, TIKU_TRK_COL_KIND) ==
              start + width_of(v, TIKU_TRK_COL_NAME) + TIKU_TRK_COL_MARGIN,
              "PVL-033",
              "and each next one is a width plus both margins along");

        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &l, &r);
        check(l == offset_of(v, TIKU_TRK_COL_KIND) -
                   TIKU_TRK_COL_LEFT_MARGIN,
              "TS-017", "a title starts 11px LEFT of its data");
        check(r == offset_of(v, TIKU_TRK_COL_KIND) +
                   width_of(v, TIKU_TRK_COL_KIND) +
                   TIKU_TRK_COL_RIGHT_MARGIN,
              "TS-017", "and ends 5px past its right edge");
    }

    printf("\nhit tests\n");
    {
        int r;

        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        check(tiku_trk_titleview_edge_at(&t, r) == TIKU_TRK_COL_NAME,
              "TS-012", "the divider itself is in the resize band");
        check(tiku_trk_titleview_edge_at(&t, r - TIKU_TRK_EDGE_SIZE) ==
              TIKU_TRK_COL_NAME,
              "TS-012", "and so is six pixels inside it");
        check(tiku_trk_titleview_edge_at(&t, r + TIKU_TRK_EDGE_SIZE) ==
              TIKU_TRK_COL_NAME,
              "TS-012",
              "and six pixels PAST it -- the band straddles the edge, so "
              "the divider belongs to the column on its left");
        check(tiku_trk_titleview_edge_at(&t, r - TIKU_TRK_EDGE_SIZE - 1) < 0,
              "TS-012", "one pixel further in is not the band any more");
        check(tiku_trk_titleview_title_at(&t, r + 40) == TIKU_TRK_COL_KIND,
              "TS-007", "a point past the divider is the next title");
        check(tiku_trk_titleview_title_at(&t, -50) < 0,
              "TS-007", "and a point left of every title is none of them");
    }

    printf("\nresizing (TS-010)\n");
    {
        int r, was;
        unsigned f;

        was = width_of(v, TIKU_TRK_COL_NAME);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        f = tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now);
        check((f & TIKU_TRK_TT_LINE) != 0u &&
              tiku_trk_titleview_tracking(&t) == TIKU_TRK_TRACK_RESIZE,
              "TS-011", "grabbing a divider starts a resize and a guide line");
        (void)tiku_trk_titleview_mouse_moved(&t, r + 30, 9, now + 1000);
        check(width_of(v, TIKU_TRK_COL_NAME) == was + 30, "TS-010",
              "the column follows the pointer exactly, live");
        check(offset_of(v, TIKU_TRK_COL_KIND) ==
              offset_of(v, TIKU_TRK_COL_NAME) + was + 30 +
              TIKU_TRK_COL_MARGIN,
              "PVL-036", "and everything to its right slides along with it");
        (void)tiku_trk_titleview_mouse_moved(&t, r - 400, 9, now + 2000);
        check(width_of(v, TIKU_TRK_COL_NAME) == TIKU_TRK_COL_MIN_WIDTH,
              "TS-010", "dragging past the floor stops at 20, not below");
        f = tiku_trk_titleview_mouse_up(&t, r - 400, 9, 0, now + 3000);
        check((f & TIKU_TRK_TT_NO_LINE) != 0u &&
              tiku_trk_titleview_tracking(&t) == TIKU_TRK_TRACK_NONE,
              "TS-011", "releasing erases the guide line and ends tracking");
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_NAME, 5);
        check(width_of(v, TIKU_TRK_COL_NAME) == TIKU_TRK_COL_MIN_WIDTH,
              "TS-010",
              "the floor belongs to the column, not to the gesture: any "
              "caller asking for less gets 20");
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_NAME, was);
    }

    printf("\nthe click test (TS-016)\n");
    {
        int l, r, was_reverse;
        unsigned f;

        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &l, &r);
        /* A press and a release inside the window, with no movement. */
        (void)tiku_trk_titleview_mouse_down(&t, (l + r) / 2, 9, 1, 1, now);
        f = tiku_trk_titleview_mouse_up(&t, (l + r) / 2, 9, 0, now + 1000);
        check((f & TIKU_TRK_TT_SORTED) != 0u &&
              tiku_trk_poseview_sort_field(v) == TIKU_TRK_COL_KIND,
              "TS-016", "a short, still press is a click and sorts");

        /* The same gesture, held past the deadline. */
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_NAME);
        (void)tiku_trk_titleview_mouse_down(&t, (l + r) / 2, 9, 1, 1, now);
        f = tiku_trk_titleview_mouse_up(&t, (l + r) / 2, 9, 0,
                                        now + TIKU_TRK_CLICK_SPEED_US + 1);
        check((f & TIKU_TRK_TT_SORTED) == 0u &&
              tiku_trk_poseview_sort_field(v) == TIKU_TRK_COL_NAME,
              "TS-016",
              "held past the double-click window it is a drag, not a click, "
              "even though it never moved");

        /* And one that moved a single pixel is still a click. */
        (void)tiku_trk_titleview_mouse_down(&t, (l + r) / 2, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_moved(&t, (l + r) / 2 + 1, 10,
                                             now + 1000);
        f = tiku_trk_titleview_mouse_up(&t, (l + r) / 2 + 1, 10, 0,
                                        now + 2000);
        check((f & TIKU_TRK_TT_SORTED) != 0u, "TS-016",
              "one pixel of shake is inside the dead zone and still a click");

        /* Two pixels is not. */
        was_reverse = tiku_trk_poseview_sort_reverse(v);
        (void)tiku_trk_titleview_mouse_down(&t, (l + r) / 2, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_moved(&t, (l + r) / 2 + 2, 9,
                                             now + 1000);
        f = tiku_trk_titleview_mouse_up(&t, (l + r) / 2 + 2, 9, 0,
                                        now + 2000);
        check((f & TIKU_TRK_TT_SORTED) == 0u &&
              tiku_trk_poseview_sort_reverse(v) == was_reverse,
              "TS-016", "two pixels is a drag");

        /* The dead zone is only in force until the deadline: one pixel
         * after it is a real move, and on a divider that means a resize. */
        {
            int rr, w0;

            /* Clear of the previous gesture, or this press would be read
             * as the second half of a double-click on a divider. */
            now += 20 * TIKU_TRK_CLICK_SPEED_US;
            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, NULL, &rr);
            w0 = width_of(v, TIKU_TRK_COL_KIND);
            (void)tiku_trk_titleview_mouse_down(&t, rr, 9, 1, 1, now);
            (void)tiku_trk_titleview_mouse_moved(&t, rr + 1, 9, now + 1000);
            check(width_of(v, TIKU_TRK_COL_KIND) == w0, "TS-016",
                  "one pixel inside the click window resizes nothing");
            (void)tiku_trk_titleview_mouse_moved(&t, rr + 1, 9,
                                                 now + TIKU_TRK_CLICK_SPEED_US
                                                 + 1);
            check(width_of(v, TIKU_TRK_COL_KIND) == w0 + 1, "TS-016",
                  "the same pixel after it does, because the dead zone "
                  "lapses with the deadline rather than lasting the drag");
            (void)tiku_trk_titleview_mouse_up(&t, rr + 1, 9, 0,
                                              now + TIKU_TRK_CLICK_SPEED_US +
                                              2000);
            (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_KIND, w0);
            now += 20 * TIKU_TRK_CLICK_SPEED_US;
        }

        /* After the deadline the dead zone is gone entirely. */
        (void)tiku_trk_titleview_mouse_down(&t, (l + r) / 2, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_moved(&t, (l + r) / 2, 9,
                                             now + TIKU_TRK_CLICK_SPEED_US +
                                             1);
        f = tiku_trk_titleview_mouse_up(&t, (l + r) / 2, 9, 0,
                                        now + TIKU_TRK_CLICK_SPEED_US + 2);
        check((f & TIKU_TRK_TT_SORTED) == 0u, "TS-016",
              "past the deadline even a motionless move counts as movement");
    }

    printf("\nreordering (TS-007)\n");
    {
        int nl, nr, kl, kr;

        tiku_trk_poseview_reflow_columns(v);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, &nl, &nr);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &kl, &kr);
        check(index_of(v, TIKU_TRK_COL_NAME) == 0 &&
              index_of(v, TIKU_TRK_COL_KIND) == 1, "TS-007",
              "Name is first and Kind second to begin with");

        /* Drag Name right, far enough that Kind is clear of it. */
        (void)tiku_trk_titleview_mouse_down(&t, (nl + nr) / 2, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_moved(&t, kr - 1, 9, now + 1000);
        check(index_of(v, TIKU_TRK_COL_KIND) == 0 &&
              index_of(v, TIKU_TRK_COL_NAME) == 1, "TS-007",
              "dragging a title over its neighbour swaps them");

        /* And the swap does not immediately undo itself on the next move. */
        (void)tiku_trk_titleview_mouse_moved(&t, kr, 9, now + 2000);
        check(index_of(v, TIKU_TRK_COL_KIND) == 0, "TS-007",
              "a further move the same way does not swap them back -- the "
              "anti-oscillation term is what stops the pair flickering");
        (void)tiku_trk_titleview_mouse_up(&t, kr, 9, 0,
                                          now + TIKU_TRK_CLICK_SPEED_US + 1);
        (void)tiku_trk_poseview_move_column(v, TIKU_TRK_COL_NAME,
                                            TIKU_TRK_COL_KIND);
        check(index_of(v, TIKU_TRK_COL_NAME) == 0, "PVL-038",
              "and a column can be put back where it was");

        /* A drag that has slipped BELOW the strip still follows the
         * pointer, but must not re-order anything: reordering is a gesture
         * on the strip. */
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
        {
            int al, ar, bl, br, was;

            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, &al, &ar);
            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &bl, &br);
            was = index_of(v, TIKU_TRK_COL_NAME);
            (void)tiku_trk_titleview_mouse_down(&t, (al + ar) / 2, 9, 1, 1,
                                                now);
            (void)tiku_trk_titleview_mouse_moved(&t, br - 1, 24,
                                                 now + TIKU_TRK_CLICK_SPEED_US
                                                 + 1);
            check(index_of(v, TIKU_TRK_COL_NAME) == was, "TS-007",
                  "six pixels below the strip the columns stay put");
            (void)tiku_trk_titleview_mouse_moved(&t, br - 1, 9,
                                                 now + TIKU_TRK_CLICK_SPEED_US
                                                 + 2000);
            check(index_of(v, TIKU_TRK_COL_NAME) != was, "TS-007",
                  "and back on the strip the same position swaps them");
            (void)tiku_trk_titleview_mouse_up(&t, br - 1, 9, 0,
                                              now + TIKU_TRK_CLICK_SPEED_US +
                                              3000);
            order_as(v);
            now += 20 * TIKU_TRK_CLICK_SPEED_US;
        }

        /* Dragging a narrow column LEFT over a much wider one: the pointer
         * is inside the neighbour but far past its left edge, so the swap
         * is refused.  Taking it would put the dragged title under a
         * pointer that is no longer over it, and the next motion would
         * swap it straight back. */
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
        {
            int cl, cr, nl2, nr2, was = index_of(v, TIKU_TRK_COL_CAP);

            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_CAP, &cl, &cr);
            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, &nl2, &nr2);
            (void)tiku_trk_titleview_mouse_down(&t, (cl + cr) / 2, 9, 1, 1,
                                                now);
            (void)tiku_trk_titleview_mouse_moved(&t, nr2 - 20, 9,
                                                 now + TIKU_TRK_CLICK_SPEED_US
                                                 + 1);
            check(index_of(v, TIKU_TRK_COL_CAP) == was, "TS-007",
                  "a leftward drag that has not reached within one title "
                  "width of the neighbour's left edge does NOT swap");
            (void)tiku_trk_titleview_mouse_moved(&t, nl2 + 4, 9,
                                                 now + TIKU_TRK_CLICK_SPEED_US
                                                 + 2000);
            check(index_of(v, TIKU_TRK_COL_CAP) < was, "TS-007",
                  "and once it has, it does");
            (void)tiku_trk_titleview_mouse_up(&t, nl2 + 4, 9, 0,
                                              now + TIKU_TRK_CLICK_SPEED_US +
                                              3000);
            now += 20 * TIKU_TRK_CLICK_SPEED_US;
        }
    }

    printf("\nremoval and return (TS-008, TS-009)\n");
    {
        int kl, kr, before = 0;
        const tiku_trk_column_t *cols;

        before = tiku_trk_poseview_columns(v, &cols);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &kl, &kr);
        (void)tiku_trk_titleview_mouse_down(&t, (kl + kr) / 2, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_moved(&t, (kl + kr) / 2, 25,
                                             now + 1000);
        check(tiku_trk_poseview_columns(v, &cols) == before, "TS-008",
              "seven pixels below the strip is still inside the band");
        (void)tiku_trk_titleview_mouse_moved(&t, (kl + kr) / 2, 40,
                                             now + 2000);
        check(tiku_trk_poseview_columns(v, &cols) == before - 1 &&
              !tiku_trk_poseview_has_column(v, TIKU_TRK_COL_KIND),
              "TS-008", "past ten pixels the column is torn out");

        /* Back inside the UNGROWN strip and it returns. */
        (void)tiku_trk_titleview_mouse_moved(&t, (kl + kr) / 2, 22,
                                             now + 3000);
        check(!tiku_trk_poseview_has_column(v, TIKU_TRK_COL_KIND), "TS-009",
              "coming back only as far as the grown band does not restore "
              "it -- leaving and returning use different edges");
        (void)tiku_trk_titleview_mouse_moved(&t, (kl + kr) / 2, 9,
                                             now + 4000);
        check(tiku_trk_poseview_has_column(v, TIKU_TRK_COL_KIND), "TS-009",
              "back inside the strip itself and it is restored");
        /* And it lands after the header it was dropped on.  Dropped before
         * it, the end of the row would be unreachable by dragging. */
        {
            int drop_on, mid2;

            (void)tiku_trk_titleview_mouse_up(&t, (kl + kr) / 2, 9, 0,
                                              now + 5000);
            now += 20 * TIKU_TRK_CLICK_SPEED_US;
            /* Take Kind out again, then drop it on a KNOWN header. */
            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &kl, &kr);
            (void)tiku_trk_titleview_mouse_down(&t, (kl + kr) / 2, 9, 1, 1,
                                                now);
            (void)tiku_trk_titleview_mouse_moved(&t, (kl + kr) / 2, 40,
                                                 now + 1000);
            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_VALUE, &kl, &kr);
            mid2 = (kl + kr) / 2;
            drop_on = tiku_trk_titleview_title_at(&t, mid2);
            (void)tiku_trk_titleview_mouse_moved(&t, mid2, 9, now + 2000);
            check(drop_on == TIKU_TRK_COL_VALUE &&
                  index_of(v, TIKU_TRK_COL_KIND) ==
                  index_of(v, TIKU_TRK_COL_VALUE) + 1, "TS-009",
                  "it lands immediately AFTER the header under the pointer, "
                  "not before it");
        }
        (void)tiku_trk_titleview_mouse_up(&t, (kl + kr) / 2, 9, 0,
                                          now + TIKU_TRK_CLICK_SPEED_US + 1);

        /* A column dragged out and back keeps the width it had, and the
         * outline stays under the pointer rather than jumping a column
         * width away: the drag origin is rebased on the way out and back. */
        {
            int off_before = 0, off_after = 0, mid;

            (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_VALUE, &kl, &kr);
            mid = (kl + kr) / 2;
            (void)tiku_trk_titleview_mouse_down(&t, mid, 9, 1, 1, now);
            (void)tiku_trk_titleview_mouse_moved(&t, mid + 3, 9,
                                                 now + TIKU_TRK_CLICK_SPEED_US
                                                 + 1);
            {   /* Where the outline is DRAWN is the title's own left plus
                 * the reported offset; that is what has to stay under the
                 * pointer, not the offset itself. */
                int l0 = 0, l1 = 0;

                (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_VALUE, &l0,
                                                NULL);
                (void)tiku_trk_titleview_dragged(&t, &off_before);
                off_before += l0;
                (void)tiku_trk_titleview_mouse_moved(&t, mid + 3, 60,
                                                     now +
                                                     TIKU_TRK_CLICK_SPEED_US
                                                     + 2000);
                (void)tiku_trk_titleview_mouse_moved(&t, mid + 3, 9,
                                                     now +
                                                     TIKU_TRK_CLICK_SPEED_US
                                                     + 3000);
                (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_VALUE, &l1,
                                                NULL);
                (void)tiku_trk_titleview_dragged(&t, &off_after);
                off_after += l1;
            }
            check(off_after - off_before < 4 && off_before - off_after < 4,
                  "TS-008",
                  "a column dragged out and back is still outlined under "
                  "the pointer, wherever re-insertion put the column");
            (void)tiku_trk_titleview_mouse_up(&t, mid + 3, 9, 0,
                                              now + TIKU_TRK_CLICK_SPEED_US +
                                              4000);
            now += 20 * TIKU_TRK_CLICK_SPEED_US;
        }

        /* Released outside, the removal stands. */
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_KIND, &kl, &kr);
        (void)tiku_trk_titleview_mouse_down(&t, (kl + kr) / 2, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_moved(&t, (kl + kr) / 2, 60,
                                             now + 1000);
        (void)tiku_trk_titleview_mouse_up(&t, (kl + kr) / 2, 60, 0,
                                          now + TIKU_TRK_CLICK_SPEED_US + 1);
        check(!tiku_trk_poseview_has_column(v, TIKU_TRK_COL_KIND), "TS-008",
              "letting go outside the strip leaves the column removed");
        (void)tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_KIND);
    }

    printf("\nfit to widest (PVL-037, TS-012)\n");
    {
        int r;
        unsigned f;

        /* The widest name here is "dddd.txt" -- 8 characters at 6px. */
        check(tiku_trk_poseview_column_widest(v, TIKU_TRK_COL_NAME) == 48,
              "PVL-037", "the widest value decides the width");
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_NAME, 300);
        check(tiku_trk_poseview_fit_column(v, TIKU_TRK_COL_NAME) &&
              width_of(v, TIKU_TRK_COL_NAME) == 48, "PVL-037",
              "fitting shrinks an over-wide column to it");
        check(tiku_trk_poseview_fit_column(v, TIKU_TRK_COL_CAP) &&
              width_of(v, TIKU_TRK_COL_CAP) == TIKU_TRK_COL_MIN_WIDTH,
              "PVL-037",
              "a column whose values are all narrower than the floor "
              "shrinks to the floor");
        check(!tiku_trk_poseview_fit_column(v, TIKU_TRK_COL_CAP), "TS-012",
              "and asking again refuses, so the press falls through into a "
              "drag rather than being swallowed for nothing");

        /* The middle button forces a fit with no prior click, so the clock
         * is moved well clear of the last gesture first. */
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_NAME, 300);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        f = tiku_trk_titleview_mouse_down(&t, r, 9, 3, 1, now);
        check(width_of(v, TIKU_TRK_COL_NAME) == 48 &&
              tiku_trk_titleview_tracking(&t) == TIKU_TRK_TRACK_NONE &&
              (f & TIKU_TRK_TT_REDRAW) != 0u,
              "PVL-037",
              "the middle button fits at once and starts no drag");
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 1000);

        /* A first primary press does not fit; a second inside the window
         * does.  The clock is moved well past the previous gesture so this
         * is a fresh one and not the tail of the last. */
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_NAME, 300);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now);
        check(width_of(v, TIKU_TRK_COL_NAME) == 300, "TS-012",
              "one click on a divider only starts a resize");
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 1000);
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now + 2000);
        check(width_of(v, TIKU_TRK_COL_NAME) == 48, "PVL-037",
              "a second one inside the double-click window fits it");
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 3000);

        /* A double-click on an already-fitted divider is inert: it must
         * not leave a live resize armed on the column just fitted. */
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 1000);
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now + 2000);
        check(tiku_trk_titleview_tracking(&t) == TIKU_TRK_TRACK_NONE,
              "TS-012",
              "fitting a column that is already fitted still consumes the "
              "press rather than arming a resize on it");
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 3000);

        /* Too late, and it is two separate resizes. */
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_NAME, 300);
        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now);
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 1000);
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1,
                                            now + 2 * TIKU_TRK_CLICK_SPEED_US);
        check(width_of(v, TIKU_TRK_COL_NAME) == 300, "TS-012",
              "and a second click too late is just another resize");
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0,
                                          now + 2 * TIKU_TRK_CLICK_SPEED_US +
                                          1000);
        now += 20 * TIKU_TRK_CLICK_SPEED_US;
    }

    printf("\nan inactive window (TS-015)\n");
    {
        unsigned f;
        int l, r, was = tiku_trk_poseview_sort_field(v);

        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_CAP, &l, &r);
        f = tiku_trk_titleview_mouse_down(&t, (l + r) / 2, 9, 1, 0, now);
        check((f & TIKU_TRK_TT_ACTIVATE) != 0u &&
              tiku_trk_titleview_tracking(&t) == TIKU_TRK_TRACK_NONE &&
              tiku_trk_poseview_sort_field(v) == was,
              "TS-015",
              "a title click in an inactive window only activates it");
    }

    printf("\nthe cursor (TS-013)\n");
    {
        int r;

        (void)tiku_trk_titleview_bounds(&t, TIKU_TRK_COL_NAME, NULL, &r);
        check((tiku_trk_titleview_mouse_moved(&t, r, 9, now) &
               TIKU_TRK_TT_CURSOR) != 0u, "TS-013",
              "over a divider the caller is told to show the resize cursor");
        check((tiku_trk_titleview_mouse_moved(&t, r - 40, 9, now) &
               TIKU_TRK_TT_CURSOR) == 0u, "TS-013",
              "and away from one it is not");
        (void)tiku_trk_titleview_mouse_down(&t, r, 9, 1, 1, now);
        check((tiku_trk_titleview_mouse_moved(&t, r, 9, now + 1000) &
               TIKU_TRK_TT_CURSOR) == 0u, "TS-013",
              "while tracking the cursor is left alone");
        (void)tiku_trk_titleview_mouse_up(&t, r, 9, 0, now + 2000);
    }

    printf("\nthe saved layout (PVL-033, TS-031, TS-032)\n");
    {
        int saved;

        /* A known order, whatever the earlier gestures left behind: Name,
         * Kind, Value, Cap. */
        order_as(v);
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_KIND, 111);
        (void)tiku_trk_poseview_move_column(v, TIKU_TRK_COL_CAP,
                                            TIKU_TRK_COL_KIND);
        check(index_of(v, TIKU_TRK_COL_CAP) == 1, "PVL-033",
              "Cap moved to second place");
        saved = tiku_trk_poseview_save_columns(v);
        check(saved == 4, "PVL-033", "four columns written");

        /* Scramble, then restore. */
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_KIND, 40);
        (void)tiku_trk_poseview_move_column(v, TIKU_TRK_COL_CAP,
                                            TIKU_TRK_COL_VALUE);
        check(tiku_trk_poseview_restore_columns(v) == 4, "PVL-033",
              "and four read back");
        check(width_of(v, TIKU_TRK_COL_KIND) == 111, "PVL-033",
              "the width came back");
        check(index_of(v, TIKU_TRK_COL_CAP) == 1, "PVL-033",
              "and so did the order");
        check(offset_of(v, TIKU_TRK_COL_NAME) ==
              tiku_trk_poseview_start_offset(v), "PVL-033",
              "positions are re-derived from the start, never restored");

        /* At a bigger font the same layout comes back proportionally. */
        tiku_trk_poseview_set_font_size(v, 24);
        (void)tiku_trk_poseview_restore_columns(v);
        check(width_of(v, TIKU_TRK_COL_KIND) == 222, "TS-031",
              "a layout saved at 12pt comes back doubled at 24pt");
        tiku_trk_poseview_set_font_size(v, TIKU_TRK_FONT_REF);
        (void)tiku_trk_poseview_restore_columns(v);
        check(width_of(v, TIKU_TRK_COL_KIND) == 111, "TS-031",
              "and back to itself at 12pt again");
    }

    printf("\na sort key whose column left (MA-067)\n");
    {
        /* Cap sits first, so "the first column" and "the Name column" are
         * different answers and the fallback can be told apart. */
        order_as(v);
        (void)tiku_trk_poseview_move_column(v, TIKU_TRK_COL_CAP,
                                            TIKU_TRK_COL_NAME);
        check(index_of(v, TIKU_TRK_COL_CAP) == 0, "MA-067",
              "Cap leads the strip");
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_VALUE);
        tiku_trk_poseview_set_secondary(v, TIKU_TRK_COL_KIND);
        (void)tiku_trk_poseview_save_columns(v);
        (void)tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_VALUE);
        (void)tiku_trk_poseview_save_columns(v);
        (void)tiku_trk_poseview_restore_columns(v);
        check(tiku_trk_poseview_sort_field(v) == TIKU_TRK_COL_CAP,
              "MA-067",
              "a primary sort whose column did not come back falls to the "
              "column that leads the strip, which is the one the reader is "
              "looking at, not whichever column happens to be called Name");
        tiku_trk_poseview_sort_by(v, TIKU_TRK_COL_CAP);
        tiku_trk_poseview_set_secondary(v, TIKU_TRK_COL_CAP);
        check(tiku_trk_poseview_sort_secondary(v) < 0, "MA-067",
              "and a tie-break equal to the key it breaks is no tie-break "
              "at all");
        order_as(v);
        (void)tiku_trk_poseview_save_columns(v);
    }

    printf("\nwidest is measured unbounded (TS-012)\n");
    {
        /* A Modified column's text is width-dependent -- it degrades
         * through four formats -- which is exactly the case where asking a
         * cell how wide it wants to be at its CURRENT width answers itself. */
        (void)tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_DATE);
        (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_DATE, 200);
        {
            int wide = tiku_trk_poseview_column_widest(v, TIKU_TRK_COL_DATE);

            (void)tiku_trk_poseview_resize_column(v, TIKU_TRK_COL_DATE, 40);
            check(tiku_trk_poseview_column_widest(v, TIKU_TRK_COL_DATE) ==
                  wide, "TS-012",
                  "a narrowed column still reports the width its FULL value "
                  "wants, not the width of the shortened form it is "
                  "currently drawing");
        }
        (void)tiku_trk_poseview_toggle_column(v, TIKU_TRK_COL_DATE);
    }

    printf("\ncorrupt saved data (TS-030, TS-032)\n");
    {
        /* Six int32s and the attribute name a column may carry; forge one
         * by hand and check that the view refuses it rather than adopting
         * nonsense. */
        struct { int32_t k, ver, field, off, w, align;
                 char attr[TIKU_TRK_ATTR_NAME_MAX]; } rec[2];
        tiku_store_t *store = b->ops->state_store(b);
        int was_w = width_of(v, TIKU_TRK_COL_KIND);

        memset(rec, 0, sizeof rec);
        /* The version is the one this build writes: the record grew a
         * name when columns learned to show an attribute, and a
         * record of the older shape is refused by design. */
        rec[0].k = 0x54434f4c; rec[0].ver = 2;
        rec[0].field = TIKU_TRK_COL_NAME; rec[0].off = 22; rec[0].w = 100;
        rec[1] = rec[0];
        rec[1].field = TIKU_TRK_COL_KIND; rec[1].off = 138;
        rec[1].w = 99999;                       /* absurd */
        (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                   TIKU_ATTR_COLUMNS, rec, sizeof rec);
        check(tiku_trk_poseview_restore_columns(v) == 0 &&
              width_of(v, TIKU_TRK_COL_KIND) == was_w, "TS-032",
              "an out-of-range width is rejected, not clamped -- a layout "
              "the user never chose is worse than the one they have");

        rec[1].w = 100;
        rec[1].align = 7;                       /* not an alignment */
        (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                   TIKU_ATTR_COLUMNS, rec, sizeof rec);
        check(tiku_trk_poseview_restore_columns(v) == 0, "TS-032",
              "and so is an alignment that is not one of the three");

        rec[1].align = 0;
        (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                   TIKU_ATTR_COLUMNS, rec,
                                   sizeof rec - 3u);
        check(tiku_trk_poseview_restore_columns(v) == 0, "TS-032",
              "a truncated record is not a layout at all");

        /* Two records for one column: a listing cannot show the same
         * column twice, and the name column cannot be removed, so a layout
         * like that could never be undone. */
        rec[1].field = TIKU_TRK_COL_NAME;
        rec[1].w = 100;
        (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                   TIKU_ATTR_COLUMNS, rec, sizeof rec);
        check(tiku_trk_poseview_restore_columns(v) == 0, "TS-032",
              "two records for one column are refused");
        rec[1].field = TIKU_TRK_COL_KIND;

        /* A width that is legal as stored and absurd once scaled: the bound
         * belongs to the number that becomes a column. */
        rec[1].w = 9000;
        (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                   TIKU_ATTR_COLUMNS, rec, sizeof rec);
        tiku_trk_poseview_set_font_size(v, 24);
        check(tiku_trk_poseview_restore_columns(v) == 0, "TS-032",
              "a width inside the bound as stored but outside it once "
              "scaled is still refused");
        tiku_trk_poseview_set_font_size(v, TIKU_TRK_FONT_REF);
        rec[1].w = 100;

        /* Records stored out of order, with offsets that say otherwise:
         * the offsets win, which is the whole of PVL-033. */
        rec[0].field = TIKU_TRK_COL_KIND; rec[0].off = 500; rec[0].w = 90;
        rec[1].field = TIKU_TRK_COL_NAME; rec[1].off = 22;  rec[1].w = 100;
        rec[1].align = 0;
        (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                   TIKU_ATTR_COLUMNS, rec, sizeof rec);
        check(tiku_trk_poseview_restore_columns(v) == 2 &&
              index_of(v, TIKU_TRK_COL_NAME) == 0, "PVL-033",
              "the saved offsets decide the order even when the records "
              "are stored the other way round");
        rec[0].field = TIKU_TRK_COL_NAME; rec[0].off = 22;  rec[0].w = 100;
        rec[1].field = TIKU_TRK_COL_KIND; rec[1].off = 138;

        /* The same records with every scalar byte-swapped, as a host of the
         * other endianness would have written them. */
        {
            struct { int32_t k, ver, field, off, w, align;
                     char attr[TIKU_TRK_ATTR_NAME_MAX]; } sw[2];
            int i;
            unsigned char *p;
            size_t j;

            rec[1].w = 77;
            memcpy(sw, rec, sizeof rec);
            p = (unsigned char *)sw;
            /* Each record's six scalars, at that record's own offset: the
             * name after them is bytes and is not turned. */
            for (i = 0; i < 2; i++) {
                for (j = 0; j < 6u * sizeof(int32_t); j += 4u) {
                    size_t at = (size_t)i * sizeof sw[0] + j;
                    unsigned char a = p[at], bb = p[at + 1];

                    p[at] = p[at + 3];
                    p[at + 1] = p[at + 2];
                    p[at + 2] = bb;
                    p[at + 3] = a;
                }
            }
            (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                       TIKU_ATTR_COLUMNS, sw, sizeof sw);
            check(tiku_trk_poseview_restore_columns(v) == 2 &&
                  width_of(v, TIKU_TRK_COL_KIND) == 77, "TS-030",
                  "a layout written by the other byte order is read, not "
                  "refused -- the stamp in the record says which way round "
                  "it is");
            /* One record each way is not a layout any host wrote. */
            memcpy(sw, rec, sizeof rec[0]);
            (void)tiku_state_write(store, tiku_trk_poseview_path(v),
                                       TIKU_ATTR_COLUMNS, sw, sizeof sw);
            check(tiku_trk_poseview_restore_columns(v) == 0, "TS-030",
                  "and a blob whose records disagree about their own byte "
                  "order is refused rather than half-read");
        }
    }

    tiku_trk_poseview_free(v);
    tiku_backend_close(b);
    rm_tree(dir);

    printf("\n%s\n", fails ? "FAILURES" : "column rules match the source");
    return fails ? 1 : 0;
}
