/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_drag.c - the drag gesture: when a press becomes one, what it
 * carries, and what a drop on a given place means.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include "tiku_trk_drag.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

/** @brief A model standing in for a pose under the pointer. */
static tiku_model_t
folder(const char *path, int mutable_)
{
    tiku_model_t m;

    memset(&m, 0, sizeof m);
    snprintf(m.path, sizeof m.path, "%s", path);
    m.kind = TIKU_KIND_DIRECTORY;
    m.facts.perm = TIKU_P_READ | (mutable_ ? TIKU_P_WRITE : 0u);
    return m;
}

static tiku_model_t
plain(const char *path)
{
    tiku_model_t m = folder(path, 1);

    m.kind = TIKU_KIND_FILE;
    return m;
}

int
main(void)
{
    tiku_trk_drag_t d;

    {
        const unsigned char color[4] = { 12, 34, 56, 255 };

        tiku_trk_drag_reset(&d);
        check(tiku_trk_drag_set_payload(&d, "application/x-rgb-color",
                                        color, sizeof color) == 0 &&
              d.foreign_refs && d.payload_size == sizeof color &&
              memcmp(d.payload, color, sizeof color) == 0,
              "PVS-059", "a typed color payload survives the drag boundary");
    }

    printf("recognizing an opening double-click (PVS-010)\n");
    {
        tiku_trk_click_t c = { 0 };
        int pose_a = 1, pose_b = 2;

        check(!tiku_trk_click_press(&c, &pose_a, 100, 100, 1u, 0u,
                                    1000000),
              "PVS-010", "the first primary press only arms the pair");
        check(tiku_trk_click_press(&c, &pose_a, 105, 95, 1u, 0u,
                                   1200000),
              "PVS-010", "the same pose within six pixels and the click "
              "interval opens");
        check(!tiku_trk_click_press(&c, &pose_a, 105, 95, 1u, 0u,
                                    1250000),
              "PVS-010", "a third press starts a new pair rather than "
              "opening again");

        memset(&c, 0, sizeof c);
        (void)tiku_trk_click_press(&c, &pose_a, 100, 100, 1u, 0u, 1000000);
        check(!tiku_trk_click_press(&c, &pose_b, 100, 100, 1u, 0u, 1100000),
              "PVS-010", "the same row position in another view is not "
              "the same pose");
        check(!tiku_trk_click_press(&c, &pose_b, 106, 100, 1u, 0u, 1200000),
              "PVS-010", "six pixels is outside the strict point slop");
        check(!tiku_trk_click_press(&c, &pose_b, 106, 100, 3u, 0u, 1250000),
              "PVS-010", "a secondary press cannot complete the pair");

        memset(&c, 0, sizeof c);
        (void)tiku_trk_click_press(&c, &pose_a, 10, 10, 1u, 0u, 1000000);
        check(!tiku_trk_click_press(&c, &pose_a, 10, 10, 1u,
                                    TIKU_TRK_CLICK_CONTROL, 1100000),
              "PVS-010", "Control+primary is secondary, not an opening "
              "double-click");
        (void)tiku_trk_click_press(&c, &pose_a, 10, 10, 1u, 0u, 1200000);
        check(!tiku_trk_click_press(&c, &pose_a, 10, 10, 1u,
                                    TIKU_TRK_CLICK_EXTEND, 1300000),
              "PVS-010", "an extending click cannot open the selection");
        (void)tiku_trk_click_press(&c, &pose_a, 10, 10, 1u, 0u, 2000000);
        check(!tiku_trk_click_press(&c, &pose_a, 10, 10, 1u, 0u, 2400000),
              "PVS-010", "the interval boundary is strict too");
    }

    printf("arming a drag (PVS-032)\n");

    tiku_trk_drag_arm(&d, 100, 100, 1u, 0u, 1, NULL, "/src");
    (void)tiku_trk_drag_add(&d, "/src/one.txt");
    check(!tiku_trk_drag_active(&d), "PVS-032",
          "a press alone is not a drag");
    check(!tiku_trk_drag_moved(&d, 102, 101) && !tiku_trk_drag_active(&d),
          "PVS-032", "and neither is a shaky one: 2,1 is inside the slop");
    check(tiku_trk_drag_moved(&d, 104, 100) && tiku_trk_drag_active(&d),
          "PVS-032", "four pixels starts it");
    check(!tiku_trk_drag_moved(&d, 200, 200) && tiku_trk_drag_active(&d),
          "PVS-032", "and it starts once -- later moves are just tracking");
    check(d.x0 == 100 && d.y0 == 100, "PVS-032",
          "the drag's origin is the PRESS point, not where it started "
          "moving: the frame has to sit where the user grabbed it");

    tiku_trk_drag_arm(&d, 100, 100, 1u, 0u, 2, NULL, "/src");
    (void)tiku_trk_drag_add(&d, "/src/one.txt");
    check(!tiku_trk_drag_moved(&d, 140, 140) && !tiku_trk_drag_active(&d),
          "PVS-032", "the second click of a double-click never arms one");

    tiku_trk_drag_arm(&d, 100, 100, 1u, 0u, 1, NULL, "/src");
    check(!tiku_trk_drag_moved(&d, 140, 140), "PVS-032",
          "and a drag carrying nothing is a rubber band, not a drag");

    printf("\nwhat it carries (PVS-033)\n");
    tiku_trk_drag_arm(&d, 10, 10, 1u, 0u, 1, NULL, "/src");
    (void)tiku_trk_drag_add(&d, "/src/one.txt");
    (void)tiku_trk_drag_add(&d, "/src/two.txt");
    (void)tiku_trk_drag_moved(&d, 40, 40);
    check(d.count == 2, "PVS-033", "the whole selection travels, not the "
          "pose that happened to be under the pointer");
    check(tiku_trk_drag_carries(&d, "/src/two.txt") &&
          !tiku_trk_drag_carries(&d, "/src/three.txt"),
          "PVS-033", "and it knows what it is carrying");

    printf("\nwhat will take it (MA-016, PVS-042)\n");
    {
        tiku_model_t dir = folder("/dst", 1);
        tiku_model_t ro  = folder("/ro", 0);
        tiku_model_t f   = plain("/dst/file.txt");
        tiku_model_t self = folder("/src/one.txt", 1);

        check(tiku_trk_drag_can_drop(&d, &dir), "MA-016",
              "a writable folder is a drop target");
        check(!tiku_trk_drag_can_drop(&d, &f), "MA-016",
              "a plain file is not");
        check(!tiku_trk_drag_can_drop(&d, &ro), "MA-016",
              "and neither is a folder that will not take new entries -- "
              "said during the hover rather than after the drop");
        check(!tiku_trk_drag_can_drop(&d, &self), "PVS-042",
              "a pose being dragged is never its own target: dropping on "
              "the selection means the window behind it");
    }
    {
        tiku_model_t ln;

        memset(&ln, 0, sizeof ln);
        snprintf(ln.path, sizeof ln.path, "/dst/alias");
        ln.kind = TIKU_KIND_SYMLINK;
        snprintf(ln.type, sizeof ln.type, "application/x-vnd.Be-directory");
        ln.facts.perm = TIKU_P_READ | TIKU_P_WRITE;
        check(tiku_trk_drag_can_drop(&d, &ln), "MA-016",
              "a symlink defers to its target: a link to a writable "
              "folder takes the drop");
        ln.facts.link_broken = 1;
        check(!tiku_trk_drag_can_drop(&d, &ln), "MA-016",
              "a broken link refuses -- there is nothing to land in");
        ln.facts.link_broken = 0;
        snprintf(ln.type, sizeof ln.type, "text/plain");
        check(!tiku_trk_drag_can_drop(&d, &ln), "MA-016",
              "and a link to a plain file is no more a target than "
              "the file itself");
    }

    printf("\nwhat a drop means (PVS-037, PVS-038, FS-002)\n");
    check(tiku_trk_drag_action(&d, "/dst", 0u, 1) == TIKU_TRK_DROP_MOVE,
          "PVS-037", "same volume moves");
    check(tiku_trk_drag_action(&d, "/dst", 0u, 0) == TIKU_TRK_DROP_COPY,
          "PVS-037", "a different volume copies -- the bytes travel only "
          "when they must");
    check(tiku_trk_drag_action(&d, "/dst", TIKU_TRK_DRAG_MOD_COPY, 1) ==
          TIKU_TRK_DROP_COPY,
          "PVS-038", "Option forces a copy even within one volume");
    check(tiku_trk_drag_action(&d, "/dst", TIKU_TRK_DRAG_MOD_LINK, 1) ==
          TIKU_TRK_DROP_LINK,
          "FS-002", "Control asks for a link");
    check(tiku_trk_drag_action(&d, "/src", 0u, 1) == TIKU_TRK_DROP_NOTHING,
          "PVS-037", "and a drop back where it came from is not an "
          "operation");
    check(tiku_trk_drag_action(&d, "/src", TIKU_TRK_DRAG_MOD_COPY, 1) ==
          TIKU_TRK_DROP_COPY,
          "PVS-038", "though Option-dropping there duplicates");

    tiku_trk_drag_set_source_virtual(&d, 1);
    /* An item may carry the name a link of it is called by; the rest
     * keep their leaf. */
    tiku_trk_drag_name_at(&d, 0, "Program");
    check(strcmp(d.name[0], "Program") == 0 && d.name[1][0] == '\0',
          "PVS-033", "a dragged item may name the link a drop makes of it");
    tiku_trk_drag_name_at(&d, 0, NULL);
    check(d.name[0][0] == '\0', "PVS-033", "and may give the name up again");
    check(tiku_trk_drag_action(&d, "/dst", 0u, 1) == TIKU_TRK_DROP_LINK,
          "FS-078", "a virtual-directory drag links by default");
    check(tiku_trk_drag_action(&d, "/dst", TIKU_TRK_DRAG_MOD_COPY, 1) ==
          TIKU_TRK_DROP_COPY, "FS-078",
          "an explicit copy still snapshots the resolved entries");


    printf("\nwhere dropped icons land (PVL-027, PVS-045)\n");
    {
        tiku_trk_drag_t g;
        int x = -1, y = -1;

        /* From an ICON view: three poses in an L, dropped elsewhere. */
        tiku_trk_drag_arm(&g, 10, 10, 1u, 0u, 1, NULL, "/src");
        (void)tiku_trk_drag_add_at(&g, "/src/a", "text", 100, 100, 0);
        (void)tiku_trk_drag_add_at(&g, "/src/b", "text", 140, 100, 0);
        (void)tiku_trk_drag_add_at(&g, "/src/c", "image", 100, 160, 0);
        tiku_trk_drag_landing(&g, 0, 500, 300, &x, &y);
        check(x == 500 && y == 300, "PVS-045",
              "the item under the pointer lands at the drop point");
        tiku_trk_drag_landing(&g, 1, 500, 300, &x, &y);
        check(x == 540 && y == 300, "PVS-045",
              "and the rest keep their offsets -- what the user picked up "
              "is what they put down, in the same shape");
        tiku_trk_drag_landing(&g, 2, 500, 300, &x, &y);
        check(x == 500 && y == 360, "PVS-045",
              "in both directions");

        /* From a LIST: no positions to preserve. */
        tiku_trk_drag_arm(&g, 10, 10, 1u, 0u, 1, NULL, "/src");
        (void)tiku_trk_drag_add_at(&g, "/src/a", "text", 0, 0, 1);
        /* A row spacing UNLIKE the stack spacing, so a
         * landing that preserved it would not look like a stack. */
        (void)tiku_trk_drag_add_at(&g, "/src/b", "text", 0, 55, 1);
        tiku_trk_drag_landing(&g, 0, 500, 300, &x, &y);
        check(x == 500 && y == 300, "PVL-027",
              "dragged out of a list, the first lands at the drop point");
        tiku_trk_drag_landing(&g, 1, 500, 300, &x, &y);
        check(x == 500 && y == 320, "PVL-027",
              "and they stack in a COLUMN rather than a heap: a list has "
              "no arrangement to preserve");

        tiku_trk_drag_arm(&g, 10, 10, 1u, 0u, 1, NULL, "/foreign");
        (void)tiku_trk_drag_add_at(&g, "/foreign/a", "text", 0, 0, 1);
        (void)tiku_trk_drag_add_at(&g, "/foreign/b", "text", 0, 0, 1);
        (void)tiku_trk_drag_add_at(&g, "/foreign/c", "text", 0, 0, 1);
        (void)tiku_trk_drag_add_at(&g, "/foreign/d", "text", 0, 0, 1);
        (void)tiku_trk_drag_add_at(&g, "/foreign/e", "text", 0, 0, 1);
        tiku_trk_drag_set_foreign_refs(&g, 1);
        tiku_trk_drag_landing(&g, 4, 500, 300, &x, &y);
        check(x == 500 && y == 324, "FS-077",
              "foreign refs land in rows of four rather than a vertical stack");

        printf("\ntypes are worked out once (PVS-053)\n");
        tiku_trk_drag_arm(&g, 10, 10, 1u, 0u, 1, NULL, "/src");
        (void)tiku_trk_drag_add_at(&g, "/src/a", "text", 0, 0, 1);
        (void)tiku_trk_drag_add_at(&g, "/src/b", "text", 0, 0, 1);
        (void)tiku_trk_drag_add_at(&g, "/src/c", "image", 0, 0, 1);
        check(tiku_trk_drag_types(&g) == 2, "PVS-053",
              "the DISTINCT types are counted, not the items");
        (void)tiku_trk_drag_add_at(&g, "/src/d", "audio", 0, 0, 1);
        check(tiku_trk_drag_types(&g) == 2, "PVS-053",
              "and once asked the list is sealed -- re-entering a view "
              "with the same drag must not rebuild it, which is how a "
              "hover becomes expensive");
    }

    printf("\n%s\n", fails ? "FAILURES" : "the drag gesture holds");
    return fails ? 1 : 0;
}
