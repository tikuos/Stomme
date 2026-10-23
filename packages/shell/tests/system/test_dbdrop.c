#include "tiku_shell_product.h"
/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_dbdrop.c - dropping files on the Deskbar.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tiku_trk_deskbar.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(b);
    tiku_rect_t f;
    tiku_model_t m;
    const char *txt[1] = { "text/plain" };
    const char *mix[2] = { "text/plain", "image/png" };
    int which = -99, tracker, editor;

    if (db == NULL) { return 1; }
    tracker = tiku_trk_deskbar_add_team(db, TIKU_SHELL_TRACKER_NAME);
    editor  = tiku_trk_deskbar_add_team(db, "Editor");
    (void)tiku_trk_deskbar_add_team(db, "Player");
    (void)tiku_trk_deskbar_team_type(db, editor, "text/plain");
    {   /* The frame is settled by a draw, so the bar is laid out once
         * into a surface nobody looks at: hit-testing a bar that has never
         * been drawn would be testing a rectangle of zeroes. */
        tiku_surface_t *sfc =
            tiku_surface_new(1000, 700, TIKU_C_BACKDROP);

        tiku_trk_deskbar_draw(db, sfc);
        tiku_surface_free(sfc);
    }
    f = tiku_trk_deskbar_frame(db);

    printf("what a drag is over (DB-009, DB-035)\n");
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 4, &which) ==
          TIKU_TRK_DB_DROP_LEAF, "DB-009",
          "the logo takes a drop: links go in the Deskbar folder");
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 42, &which) ==
          TIKU_TRK_DB_DROP_TEAM && which == 0, "DB-035",
          "the first application row is a target, and says which app");
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 63, &which) ==
          TIKU_TRK_DB_DROP_TEAM && which == 1, "DB-035",
          "and so is the second");
    /* The unpainted gutter between two rows belongs to neither of them,
     * the same way a menu's seam does: a drop there is a drop on the bar. */
    check(tiku_trk_deskbar_drop_at(db, f.x + 10, f.y + 58, &which) !=
          TIKU_TRK_DB_DROP_TEAM, "DB-035",
          "the seam between two application rows is not either of them");
    check(tiku_trk_deskbar_drop_at(db, f.x - 40, f.y + 42, &which) ==
          TIKU_TRK_DB_DROP_NONE, "DB-035",
          "while beside the bar is not a Deskbar drop at all");

    printf("\nwhat an application will take (DB-035)\n");
    check(tiku_trk_deskbar_team_accepts(db, tracker, txt, 1, 0), "DB-035",
          "Tracker opens anything: it is what is asked when nothing else "
          "claims a file");
    check(tiku_trk_deskbar_team_accepts(db, editor, txt, 1, 0), "DB-035",
          "an application takes a type it declares");
    check(!tiku_trk_deskbar_team_accepts(db, 2, txt, 1, 0), "DB-035",
          "and refuses one it does not -- a drop that appears to work and "
          "does nothing is worse than one that is plainly unavailable");
    check(tiku_trk_deskbar_team_accepts(db, 2, txt, 1, 1), "DB-035",
          "unless Ctrl forces it");
    check(!tiku_trk_deskbar_team_accepts(db, editor, mix, 2, 0), "DB-035",
          "EVERY type must be one it opens: a mixed selection it can only "
          "half read is a drop that half works");
    check(!tiku_trk_deskbar_team_accepts(db, editor, NULL, 0, 0), "DB-035",
          "and knowing nothing about the types is a refusal, not a guess");

    printf("\nthe menu while a drag is in flight (DB-008)\n");
    check(tiku_trk_deskbar_item_enabled(db, 1), "DB-008",
          "a fixed item is available ordinarily");
    tiku_trk_deskbar_set_dragging(db, 1);
    check(!tiku_trk_deskbar_item_enabled(db, 1), "DB-008",
          "and goes dead while dragging: About and Power off are not drop "
          "targets, and leaving them live invites a drop that does "
          "something unrelated");
    check(tiku_trk_deskbar_item_enabled(db, 0), "DB-008",
          "while folders and recents stay live -- those are exactly the "
          "targets the menu exists to offer during a drag");
    tiku_trk_deskbar_set_dragging(db, 0);
    check(tiku_trk_deskbar_item_enabled(db, 1), "DB-008",
          "and it comes back when the drag ends");

    printf("\nthe menu's own entries (DB-009)\n");
    {
        char into[256], seen[8][256];
        int y2, nseen = 0, lo = -1, k, all_paths = 1;

        /*
         * The rows that take a drop are the ones the Deskbar FOLDER put
         * there.  Opening the menu and counting is not enough: with no
         * fixed rows for /sys, /dev and /data, the folder has to have
         * something in it for there to be anything to drop on, which is
         * the honest arrangement and needs saying out loud here.
         */
        (void)system("rm -rf /tmp/trk_dbdrop && mkdir -p "
                     "/tmp/trk_dbdrop/one /tmp/trk_dbdrop/two && "
                     "touch /tmp/trk_dbdrop/a.txt");
        tiku_trk_deskbar_set_menu_dir(db, "/tmp/trk_dbdrop_missing",
                                      "/tmp/trk_dbdrop");
        tiku_trk_deskbar_open_menu(db);
        for (y2 = 0; y2 < 400; y2++) {
            if (tiku_trk_deskbar_menu_drop_at(db, f.x - 20, y2, into,
                                              sizeof into) ==
                    TIKU_TRK_DB_DROP_MENU_DIR) {
                int dup = 0;

                if (lo < 0) { lo = y2; }
                for (k = 0; k < nseen; k++) {
                    if (strcmp(seen[k], into) == 0) { dup = 1; }
                }
                if (!dup && nseen < 8) {
                    snprintf(seen[nseen], sizeof seen[0], "%s", into);
                    if (into[0] != '/') { all_paths = 0; }
                    nseen++;
                }
            }
        }
        check(nseen == 2 && all_paths, "DB-009",
              "exactly the folder's own directories take a drop, and only "
              "entries that name a path -- About and Shut down are not "
              "places to put a file, and neither is the plain file beside "
              "them");
        tiku_trk_deskbar_close_menu(db);
        check(lo >= 0 &&
              tiku_trk_deskbar_menu_drop_at(db, f.x - 20, lo, into,
                                            sizeof into) ==
                  TIKU_TRK_DB_DROP_NONE, "DB-009",
              "and with the menu closed the same point takes nothing: a "
              "menu that is not there is not a target");
        (void)system("rm -rf /tmp/trk_dbdrop");
    }

    printf("\nthe highlight under a hovering drag (DB-035)\n");
    check(tiku_trk_deskbar_drop_team(db) == -1, "DB-035",
          "nothing is lit to begin with");
    tiku_trk_deskbar_set_dragging(db, 1);
    tiku_trk_deskbar_set_drop_team(db, 1);
    check(tiku_trk_deskbar_drop_team(db) == 1, "DB-035",
          "a hover lights the row the drop would be handed to, so the "
          "promise is visible before it is made");
    tiku_trk_deskbar_set_dragging(db, 0);
    check(tiku_trk_deskbar_drop_team(db) == -1, "DB-035",
          "and the drag ending takes it away -- there is nothing left for "
          "it to be promising");

    printf("\nwhat a drop launches (DB-066, DB-075)\n");
    memset(&m, 0, sizeof m);
    m.kind = TIKU_KIND_FILE;
    check(tiku_trk_deskbar_launchable(&m), "DB-066", "a file is launched");
    m.kind = TIKU_KIND_DIRECTORY;
    check(!tiku_trk_deskbar_launchable(&m), "DB-066",
          "a directory is SILENTLY ignored -- launching one has no meaning "
          "and complaining about an accident would be noise");
    m.kind = TIKU_KIND_VOLUME;
    check(!tiku_trk_deskbar_launchable(&m), "DB-075",
          "and neither is a volume, for the same reason");

    printf("\nwhat the leaf menu is made of (DB-002, DB-003, DB-004, "
           "DB-074)\n");
    {
        char recent[3][TIKU_PATH_MAX];
        int i, dirs = 0, recents = 0, disabled = 0, count;
        const tiku_trk_db_item_t *it;

        (void)system("rm -rf /tmp/trk_dbmenu && mkdir -p "
                     "/tmp/trk_dbmenu/tools && touch /tmp/trk_dbmenu/notes");
        tiku_trk_deskbar_set_menu_dir(db, "/tmp/trk_dbmenu_missing",
                                      "/tmp/trk_dbmenu");
        tiku_trk_deskbar_open_menu(db);
        count = tiku_trk_deskbar_leaf_count(db);
        for (i = 0; i < count; i++) {
            it = tiku_trk_deskbar_leaf_item(db, i);
            if (it->expand == TIKU_TRK_DB_EXPAND_DIR) { dirs++; }
            if (it->expand == TIKU_TRK_DB_EXPAND_RECENT) { recents++; }
            if (it->kind == TIKU_TRK_DB_ITEM_DISABLED) { disabled++; }
        }
        check(dirs == 1 && disabled == 0, "DB-003",
              "with no folder of the user's own the menu shows the system "
              "copy rather than nothing");
        check(recents == 0, "DB-074",
              "a recents list with nothing in it is not offered at all: "
              "an empty submenu is a promise of nothing");
        snprintf(recent[0], sizeof recent[0], "/tmp/trk_dbmenu");
        tiku_trk_deskbar_set_recent(db, TIKU_TRK_DB_RECENT_FOLDERS,
                                    (const char (*)[TIKU_PATH_MAX])recent,
                                    1);
        tiku_trk_deskbar_open_menu(db);
        recents = 0;
        count = tiku_trk_deskbar_leaf_count(db);
        for (i = 0; i < count; i++) {
            it = tiku_trk_deskbar_leaf_item(db, i);
            if (it->expand == TIKU_TRK_DB_EXPAND_RECENT) { recents++; }
        }
        check(recents == 1, "DB-002",
              "and one with something in it is");
        (void)system("rm -rf /tmp/trk_dbmenu/tools /tmp/trk_dbmenu/notes");
        tiku_trk_deskbar_open_menu(db);
        disabled = 0;
        dirs = 0;
        count = tiku_trk_deskbar_leaf_count(db);
        {
            int seams = 0, prev_sep = 0;

            for (i = 0; i < count; i++) {
                it = tiku_trk_deskbar_leaf_item(db, i);
                if (it->kind == TIKU_TRK_DB_ITEM_DISABLED) { disabled++; }
                if (it->expand == TIKU_TRK_DB_EXPAND_DIR) { dirs++; }
                if (it->kind == TIKU_TRK_DB_ITEM_SEP) {
                    if (prev_sep) { seams++; }
                    prev_sep = 1;
                } else {
                    prev_sep = 0;
                }
            }
            check(disabled == 0 && dirs == 0, "DB-004",
                  "an empty Deskbar folder puts NOTHING in the menu -- and "
                  "the folder is read again on every open, so emptying it "
                  "took effect without a restart (DB-005)");
            check(seams == 0, "DB-004",
                  "and the rule that would have set those items off goes "
                  "with them: the menu does not close on two lines in a "
                  "row where the folder's items would have been");
        }
        (void)system("rm -rf /tmp/trk_dbmenu");
    }

    printf("\nwhat a drag is over on a horizontal bar (D-061)\n");
    {
        static const tiku_trk_db_dock_t docks[2] = {
            TIKU_TRK_DB_TOP, TIKU_TRK_DB_BOTTOM
        };
        int d;

        for (d = 0; d < 2; d++) {
            tiku_surface_t *sfc =
                tiku_surface_new(1000, 700, TIKU_C_BACKDROP);
            int seen[8], nseen = 0, x, mid, k;

            tiku_trk_deskbar_set_dock(db, docks[d]);
            tiku_trk_deskbar_draw(db, sfc);
            tiku_surface_free(sfc);
            f = tiku_trk_deskbar_frame(db);
            mid = f.y + f.h / 2;
            check(tiku_trk_deskbar_drop_at(db, f.x + 10, mid, &which) ==
                  TIKU_TRK_DB_DROP_LEAF,
                  "D-061", d == 0 ? "top dock: the leaf button takes links"
                                  : "bottom dock: the leaf button takes "
                                    "links");
            for (x = f.x; x < f.x + f.w; x++) {
                if (tiku_trk_deskbar_drop_at(db, x, mid, &which) ==
                        TIKU_TRK_DB_DROP_TEAM &&
                    (nseen == 0 || seen[nseen - 1] != which) && nseen < 8) {
                    seen[nseen++] = which;
                }
            }
            k = (nseen == 3 && seen[0] == 0 && seen[1] == 1 &&
                 seen[2] == 2);
            check(k, "D-061",
                  d == 0 ? "top dock: each application button is a target, "
                           "left to right"
                         : "bottom dock: each application button is a "
                           "target, left to right");
        }
    }

    tiku_trk_deskbar_free(db);
    tiku_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "Deskbar drops hold");
    return fails ? 1 : 0;
}
