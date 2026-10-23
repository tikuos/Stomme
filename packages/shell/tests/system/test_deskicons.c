/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskicons.c - the three things the Desktop does that a window does not.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_trk_deskicons.h"
#include "tiku_trk_poseview.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) { fails++; }
}

static tiku_trk_rect_t
R(int x, int y, int w, int h)
{
    tiku_trk_rect_t r;

    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

int
main(void)
{
    tiku_trk_rect_t screen = R(0, 0, 1000, 700);
    tiku_trk_rect_t bar = R(880, 0, 120, 240);
    tiku_trk_rect_t other[4];

    printf("the Desktop is never a list (PVL-008)\n");
    check(tiku_trk_deskicons_mode(TIKU_TRK_MODE_LIST) == TIKU_TRK_MODE_ICON,
          "PVL-008",
          "a saved list-mode state is coerced back to icons -- honouring "
          "it would leave a Desktop nothing can be put on");
    check(tiku_trk_deskicons_mode(TIKU_TRK_MODE_MINI) == TIKU_TRK_MODE_MINI,
          "PVL-008", "while the other icon modes are left alone");

    printf("\nwhere an icon may sit (PVL-018)\n");
    check(tiku_trk_deskicons_valid(R(100, 100, 40, 40), screen, bar, NULL, 0),
          "PVL-018", "the middle of the screen is fine");
    check(!tiku_trk_deskicons_valid(R(980, 300, 40, 40), screen, bar,
                                    NULL, 0),
          "PVL-018", "half off the right edge is not -- an icon that cannot "
          "be picked up is worse than one in an awkward place");
    check(!tiku_trk_deskicons_valid(R(-4, 100, 40, 40), screen, bar,
                                    NULL, 0),
          "PVL-018", "and neither is half off the left");
    check(!tiku_trk_deskicons_valid(R(900, 100, 40, 40), screen, bar,
                                    NULL, 0),
          "PVL-018", "on top of the Deskbar is refused");
    /* Clear of the bar itself but inside its margin: the only shape that
     * tells the margin apart from the bar. */
    check(!tiku_trk_deskicons_valid(R(874, 100, 4, 40), screen, bar,
                                    NULL, 0),
          "PVL-018", "and so is within ten pixels of it, which is the "
          "margin that stops an icon looking stuck to the strip");
    check(tiku_trk_deskicons_valid(R(864, 100, 4, 40), screen, bar,
                                   NULL, 0),
          "PVL-018", "while a pixel past the margin is fine");
    other[0] = R(300, 300, 60, 60);
    check(!tiku_trk_deskicons_valid(R(356, 300, 40, 40), screen, bar,
                                    other, 1),
          "PVL-018", "a replicant gets a smaller margin of its own");
    /* Past the replicant's five but inside the Deskbar's ten: the shape
     * that tells the two margins apart. */
    check(tiku_trk_deskicons_valid(R(368, 300, 40, 40), screen, bar,
                                   other, 1),
          "PVL-018", "cleared by eight pixels, which is past the five a "
          "replicant gets and inside the ten the Deskbar gets");

    printf("\nwhen the screen changes (PVL-030)\n");
    {
        tiku_trk_deskpos_set_t s;
        tiku_trk_rect_t r;
        int x = -1, y = -1;

        memset(&s, 0, sizeof s);
        tiku_trk_deskicons_remember(&s, 1920, 1080, 100, 200);
        tiku_trk_deskicons_remember(&s, 1280, 720, 40, 60);
        check(tiku_trk_deskicons_recall(&s, 1280, 720, &x, &y) &&
              x == 40 && y == 60, "PVL-030",
              "an icon returns to where it sat on THAT screen size");
        check(tiku_trk_deskicons_recall(&s, 1920, 1080, &x, &y) &&
              x == 100, "PVL-030",
              "and the other size keeps its own position -- plugging a "
              "monitor back in should not inherit the other one's layout");
        check(!tiku_trk_deskicons_recall(&s, 800, 600, &x, &y), "PVL-030",
              "a size never seen has nothing recorded");
        tiku_trk_deskicons_remember(&s, 1280, 720, 41, 61);
        check(s.n == 2 && tiku_trk_deskicons_recall(&s, 1280, 720, &x, NULL)
              && x == 41, "PVL-030",
              "moving an icon updates that size rather than adding a "
              "second record for it");

        r = R(980, 690, 40, 40);
        check(tiku_trk_deskicons_pull_back(&r, screen) &&
              r.x == 960 && r.y == 660, "PVL-030",
              "an icon that no longer fits is pulled back on screen");
        r = R(100, 100, 40, 40);
        check(!tiku_trk_deskicons_pull_back(&r, screen), "PVL-030",
              "and one that fits is left where it is");
        r = R(-50, -50, 2000, 40);
        (void)tiku_trk_deskicons_pull_back(&r, screen);
        check(r.x == 0 && r.y == 0, "PVL-030",
              "something wider than the screen ends against the near "
              "corner rather than off the far one");

        r = R(480, 330, 40, 40);
        tiku_trk_deskicons_reflow(&r, R(0, 0, 1000, 700),
                                  R(0, 0, 2000, 1400));
        check(r.x == 980 && r.y == 680, "AW-020",
              "a centered icon stays centered when the Desktop grows");
        r = R(960, 660, 40, 40);
        tiku_trk_deskicons_reflow(&r, R(0, 0, 1000, 700),
                                  R(0, 0, 2000, 1400));
        check(r.x == 1960 && r.y == 1360, "AW-020",
              "an icon at the old far corner reaches the new far corner");
        r = R(826, 330, 40, 40);
        tiku_trk_deskicons_reflow(&r, R(0, 0, 866, 700),
                                  R(0, 0, 1866, 1400));
        check(r.x == 1826 && r.y == 680, "AW-020",
              "reflow uses the area beside Deskbar rather than hiding "
              "icons beneath it");

        other[0] = R(0, 0, 40, 40);
        r = R(0, 0, 40, 40);
        check(tiku_trk_deskicons_find_free(&r, R(0, 0, 140, 90),
                                           other, 1, 50, 50) &&
              r.x == 50 && r.y == 0, "AW-020",
              "icons collapsed onto one old edge take the nearest free "
              "slot after reflow");
        other[1] = R(50, 0, 40, 40);
        other[2] = R(0, 50, 40, 40);
        other[3] = R(50, 50, 40, 40);
        r = R(0, 0, 40, 40);
        check(!tiku_trk_deskicons_find_free(&r, R(0, 0, 90, 90),
                                            other, 4, 50, 50), "AW-020",
              "a full grid reports that it has no collision-free slot");
    }

    printf("\nthe union the Desktop shows (PVN-051, PVN-058)\n");
    {
        tiku_backend_t *b = tiku_backend_posix_open();
        tiku_volumes_t vs;
        tiku_vdir_t src;

        memset(&vs, 0, sizeof vs);
        vs.n = 2;
        snprintf(vs.v[0].mount, sizeof vs.v[0].mount, "/tmp/trk_di_a");
        snprintf(vs.v[1].mount, sizeof vs.v[1].mount, "/tmp/trk_di_b");
        (void)system("rm -rf /tmp/trk_di_a /tmp/trk_di_b");
        (void)mkdir("/tmp/trk_di_a", 0755);
        (void)mkdir("/tmp/trk_di_b", 0755);
        (void)mkdir("/tmp/trk_di_a/Desktop", 0755);
        tiku_trk_deskicons_sources(&vs, b, NULL, &src);
        check(src.nsrc == 1, "PVN-051",
              "only volumes that HAVE a Desktop folder contribute");
        (void)mkdir("/tmp/trk_di_b/Desktop", 0755);
        tiku_trk_deskicons_sources(&vs, b, NULL, &src);
        check(src.nsrc == 2, "PVN-051",
              "and a second disk's Desktop items appear beside the first's "
              "-- the Desktop is a union, not a directory");

        {
            /* The home Desktop contributes ahead of the mount points, so a
             * name it holds is the one that shows. */
            tiku_vdir_t withhome;
            char resolved[TIKU_PATH_MAX];

            (void)mkdir("/tmp/trk_di_home", 0755);
            (void)system("touch /tmp/trk_di_a/Desktop/shared "
                         "/tmp/trk_di_home/shared");
            tiku_trk_deskicons_sources(&vs, b, "/tmp/trk_di_home",
                                       &withhome);
            check(withhome.nsrc == 3, "PVN-051",
                  "the user's own Desktop folder contributes as well as "
                  "the volumes'");
            check(tiku_vdir_resolve(&withhome, b, "shared", resolved,
                                        sizeof resolved) == 0 &&
                  strcmp(resolved, "/tmp/trk_di_home/shared") == 0,
                  "PVN-051",
                  "and it goes FIRST, so a name it shares with a volume's "
                  "Desktop shows the user's copy");
            (void)system("rm -rf /tmp/trk_di_home");
        }

        check(tiku_trk_deskicons_show_disks(1, 0) &&
              tiku_trk_deskicons_show_disks(0, 1), "PVN-058",
              "either setting alone puts the root on the Desktop");
        check(!tiku_trk_deskicons_show_disks(0, 0), "PVN-058",
              "and with neither, it is not there");

        (void)system("rm -rf /tmp/trk_di_a /tmp/trk_di_b");
        tiku_backend_close(b);
    }

    printf("\n%s\n", fails ? "FAILURES" : "the Desktop's own rules hold");
    return fails ? 1 : 0;
}
