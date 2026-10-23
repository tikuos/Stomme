/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_navmenu.c - walking a folder tree from a menu.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tiku_trk_navmenu.h"
#include "tiku_trk_launch.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static void
put(const char *dir, const char *name)
{
    char p[700];
    FILE *f;

    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "w");
    if (f != NULL) { fputs("x", f); (void)fclose(f); }
}

/** @brief Index of @p name in a level, or -1. */
static int
find(tiku_trk_nav_level_t *lv, const char *name)
{
    int i;

    for (i = 0; i < lv->n; i++) {
        if (strcmp(lv->item[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int
main(void)
{
    tiku_backend_t *b = tiku_backend_posix_open();
    tiku_trk_nav_t nav;
    tiku_trk_nav_level_t *lv;
    tiku_model_t m;
    char root[] = "/tmp/trk_nav";
    char deep[600], deeper[700];
    int i;

    (void)system("rm -rf /tmp/trk_nav /tmp/trk_nav_big");
    (void)mkdir(root, 0755);
    snprintf(deep, sizeof deep, "%s/sub", root);
    (void)mkdir(deep, 0755);
    snprintf(deeper, sizeof deeper, "%s/inner", deep);
    (void)mkdir(deeper, 0755);
    put(root, "afile.txt");
    put(root, ".hidden");
    put(deep, "leaf.txt");
    {   /* A Trash inside the folder: hidden for being the Trash, not for
         * its name, which is why the check runs with dot files SHOWN. */
        char tr[700];

        snprintf(tr, sizeof tr, "%s/.Trash", root);
        (void)mkdir(tr, 0755);
    }

    printf("who gets a navigation menu (CW-045)\n");
    memset(&m, 0, sizeof m);
    m.kind = TIKU_KIND_DIRECTORY;
    m.facts.perm = TIKU_P_READ | TIKU_P_WRITE;
    check(tiku_trk_nav_wanted(&m), "CW-045", "a folder does");
    m.kind = TIKU_KIND_FILE;
    check(!tiku_trk_nav_wanted(&m), "CW-045",
          "a file does not -- an empty submenu on every row is worse than "
          "none at all");

    printf("\nwhat a level lists (CW-056, CW-057)\n");
    tiku_trk_nav_open(&nav, b, root);
    tiku_trk_nav_fill_all(&nav, 0);
    lv = tiku_trk_nav_top(&nav);
    check(lv != NULL && lv->n == 3, "CW-057",
          "the folder's entries are listed, dot file included by default");
    check(find(lv, ".Trash") == -1, "CW-057",
          "but never the Trash, even with dot files shown: it is hidden "
          "for being the Trash rather than for its name");
    check(find(lv, "sub") == 0, "CW-056",
          "folders sort first, so the things you can walk into are "
          "together at the top");
    i = find(lv, "afile.txt");
    check(i >= 0 && !lv->item[i].enabled, "CW-056",
          "a plain file is listed but not choosable: this menu exists to "
          "reach a folder, and a live file row would be a second way to "
          "open things that behaves differently");
    check(lv->item[0].enabled && lv->item[0].is_container, "CW-056",
          "while a folder is both");

    tiku_trk_nav_open(&nav, b, root);
    nav.hide_dotfiles = 1;
    tiku_trk_nav_fill_all(&nav, 0);
    lv = tiku_trk_nav_top(&nav);
    check(find(lv, ".hidden") == -1 && lv->n == 2, "CW-057",
          "with the setting on the menu hides what the window hides -- one "
          "that showed a dot file the window does not would be a second, "
          "disagreeing view of one folder");

    printf("\nfilling a little at a time (CW-055)\n");
    {
        char big[700];
        tiku_trk_nav_t slow;
        int steps = 0;

        /* Outside the tree the walk uses: a folder created for one
         * check must not change what another check lists. */
        snprintf(big, sizeof big, "/tmp/trk_nav_big");
        (void)mkdir(big, 0755);
        for (i = 0; i < 45; i++) {
            char nm[32];

            snprintf(nm, sizeof nm, "f%02d.txt", i);
            put(big, nm);
        }
        tiku_trk_nav_open(&slow, b, big);
        check(tiku_trk_nav_fill(&slow, 0) == 1, "CW-055",
              "a big folder does not finish in one call");
        check(tiku_trk_nav_top(&slow)->n == TIKU_TRK_NAV_CHUNK, "CW-055",
              "it takes a chunk at a time, so the menu answers the mouse "
              "between steps rather than freezing on a huge folder");
        while (tiku_trk_nav_fill(&slow, 0) && steps++ < 100) {
            /* keep asking */
        }
        check(tiku_trk_nav_top(&slow)->n == 45, "CW-055",
              "and every entry arrives in the end");
        check(steps > 0 && steps < 100, "CW-055",
              "in a bounded number of steps");
    }

    printf("\nwalking in and out (CW-056)\n");
    tiku_trk_nav_open(&nav, b, root);
    tiku_trk_nav_fill_all(&nav, 0);
    check(nav.depth == 1, "CW-056", "a walk starts at one level");
    check(!tiku_trk_nav_move(&nav, TIKU_TRK_NAV_UP), "CW-056",
          "nothing is highlighted, so Up does nothing");
    check(tiku_trk_nav_move(&nav, TIKU_TRK_NAV_DOWN) &&
          tiku_trk_nav_top(&nav)->sel == 0, "CW-056",
          "Down takes the first row");
    check(tiku_trk_nav_move(&nav, TIKU_TRK_NAV_IN) && nav.depth == 2,
          "CW-056", "and Right walks into it");
    {   /* A file has nothing to walk into, and pretending otherwise would
         * open an empty level over a leaf. */
        int at = find(tiku_trk_nav_top(&nav), "leaf.txt");
        int was = nav.depth;

        tiku_trk_nav_fill_all(&nav, 1);
        at = find(tiku_trk_nav_top(&nav), "leaf.txt");
        check(at >= 0 && !tiku_trk_nav_enter(&nav, at) && nav.depth == was,
              "CW-056", "while walking into a FILE does nothing at all");
    }
    tiku_trk_nav_fill_all(&nav, 1);
    lv = tiku_trk_nav_top(&nav);
    check(strcmp(lv->path, deep) == 0 && lv->n == 2, "CW-056",
          "the new level is that folder's own contents");
    check(tiku_trk_nav_move(&nav, TIKU_TRK_NAV_OUT) && nav.depth == 1,
          "CW-056", "Left comes back out");
    check(!tiku_trk_nav_move(&nav, TIKU_TRK_NAV_OUT), "CW-056",
          "and out of the first level there is nowhere to go -- that level "
          "IS the menu");
    {
        int guard = 0;

        while (tiku_trk_nav_move(&nav, TIKU_TRK_NAV_DOWN) && guard++ < 50) {
            /* to the end */
        }
        check(guard < 50, "CW-056",
              "Down stops at the last row rather than wrapping: a menu is "
              "a list with ends, and wrapping reads as a missed key");
    }

    printf("\nre-opening re-reads (CW-055)\n");
    tiku_trk_nav_open(&nav, b, root);
    tiku_trk_nav_fill_all(&nav, 0);
    lv = tiku_trk_nav_top(&nav);
    i = find(lv, "sub");
    (void)tiku_trk_nav_enter(&nav, i);
    tiku_trk_nav_fill_all(&nav, 1);
    check(tiku_trk_nav_top(&nav)->n == 2, "CW-055", "the level lists two");
    put(deep, "arrived.txt");
    (void)tiku_trk_nav_leave(&nav);
    (void)tiku_trk_nav_enter(&nav, i);
    tiku_trk_nav_fill_all(&nav, 1);
    check(tiku_trk_nav_top(&nav)->n == 3, "CW-055",
          "and re-opening it RE-READS the folder: a menu is not a "
          "snapshot, and one showing last time's contents would be lying");

    {   /* CW-057: a menu on the root lists volumes, as the window does. */
        tiku_trk_nav_t rn;
        int k, vols = 0, dirs = 0;

        tiku_trk_nav_open(&rn, b, "/");
        tiku_trk_nav_fill_all(&rn, 0);
        for (k = 0; k < rn.level[0].n; k++) {
            if (strcmp(rn.level[0].item[k].path, "/usr") == 0 ||
                strcmp(rn.level[0].item[k].path, "/etc") == 0) {
                dirs++;
            }
            if (strcmp(rn.level[0].item[k].path, "/") == 0) {
                vols++;
            }
        }
        check(vols == 1 && dirs == 0, "CW-057",
              "a menu on the root lists volumes -- the boot volume once, "
              "and never /usr or /etc as rows");
    }

    /* --- what a SPRING menu enables (PVS-052) --------------------------- */
    printf("\nwhat a spring menu enables (PVS-052)\n");
    {
        char sdir[] = "/tmp/trk_nav_spring";
        tiku_trk_apps_t reg;
        tiku_trk_nav_t sn;
        const char *types[1] = { "text/plain" };
        char appfile[600];
        int i;

        (void)system("rm -rf /tmp/trk_nav_spring");
        (void)mkdir(sdir, 0755);
        (void)mkdir("/tmp/trk_nav_spring/folder", 0755);
        put(sdir, "plain.txt");
        put(sdir, "editor");
        (void)symlink("/nowhere-such", "/tmp/trk_nav_spring/dangler");
        snprintf(appfile, sizeof appfile, "%s/editor", sdir);
        tiku_trk_launch_clear(&reg);
        (void)tiku_trk_launch_add(&reg, "Editor", appfile, "text/plain");

        tiku_trk_nav_open(&sn, b, sdir);
        tiku_trk_nav_fill_all(&sn, 0);
        tiku_trk_nav_spring_states(&sn,
            (const struct tiku_trk_apps *)&reg, types, 1);
        lv = &sn.level[0];
        i = find(lv, "folder");
        check(i >= 0 && lv->item[i].enabled, "PVS-052",
              "a container is always enabled");
        i = find(lv, "editor");
        check(i >= 0 && lv->item[i].enabled, "PVS-052",
              "an application claiming a dragged type is enabled");
        i = find(lv, "plain.txt");
        check(i >= 0 && !lv->item[i].enabled, "PVS-052",
              "a file nothing claims is greyed");
        i = find(lv, "dangler");
        check(i < 0 || !lv->item[i].enabled, "PVS-052",
              "a broken link is never a landing");
        {
            const char *none[1] = { "image/png" };

            tiku_trk_nav_spring_states(&sn,
                (const struct tiku_trk_apps *)&reg, none, 1);
            i = find(lv, "editor");
            check(i >= 0 && !lv->item[i].enabled, "PVS-052",
                  "and the same application greys when the drag carries "
                  "nothing it claims");
        }
        (void)system("rm -rf /tmp/trk_nav_spring");
    }

    (void)system("rm -rf /tmp/trk_nav /tmp/trk_nav_big");
    tiku_backend_close(b);
    printf("\n%s\n", fails ? "FAILURES" : "the navigation menu holds");
    return fails ? 1 : 0;
}
