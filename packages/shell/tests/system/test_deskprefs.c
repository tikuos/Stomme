/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskprefs.c - the values behind the Desktop preferences.
 *
 * What Defaults and Revert mean, where the icon ladder stops, and which
 * files a chooser may offer -- decided here, away from any window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_trk_deskprefs.h"

static int fails;

static void
check(int cond, const char *row, const char *what)
{
    printf("  [%s] %-9s %s\n", cond ? "PASS" : "FAIL", row, what);
    if (!cond) {
        fails++;
    }
}

/**
 * @brief Step until the ladder stops, but never for ever.
 *
 * A ladder that wrapped would climb until the machine was turned off, and
 * a test that hangs says nothing at all; bounded, it fails and says it.
 */
static void
climb(tiku_trk_deskprefs_t *prefs, int up)
{
    int guard;

    for (guard = 0; guard < TIKU_TRK_DESK_ICON_STEPS + 2; guard++) {
        if (!tiku_trk_deskprefs_step_icon(prefs, up)) {
            return;
        }
    }
}

/** @brief A small BMP, which is a picture; and a file that is not one. */
static void
plant(const char *dir)
{
    char path[512];
    FILE *f;
    unsigned char header[54];
    unsigned char row[12];

    snprintf(path, sizeof path, "%s/one.bmp", dir);
    memset(header, 0, sizeof header);
    header[0] = 'B';
    header[1] = 'M';
    header[10] = 54;
    header[14] = 40;
    header[18] = 2;             /* width 2 */
    header[22] = 2;             /* height 2 */
    header[26] = 1;
    header[28] = 24;
    memset(row, 32, sizeof row);
    f = fopen(path, "wb");
    if (f != NULL) {
        (void)fwrite(header, 1u, sizeof header, f);
        (void)fwrite(row, 1u, sizeof row, f);
        (void)fclose(f);
    }
    snprintf(path, sizeof path, "%s/notes.txt", dir);
    f = fopen(path, "w");
    if (f != NULL) {
        (void)fputs("not a picture\n", f);
        (void)fclose(f);
    }
}

int
main(void)
{
    tiku_trk_deskprefs_t prefs;
    tiku_trk_deskprefs_pictures_t found;
    tiku_store_t *store;
    const char *dir = "/tmp/trk_deskprefs";
    const char *dirs[1];

    printf("what the Desktop looks like (DEV-009)\n");

    tiku_trk_deskprefs_init(&prefs);
    check(prefs.value.icon_size == 32 && !prefs.value.have_bg, "DEV-009",
          "a Desktop nobody has dressed has no picture and small icons");
    check(!tiku_trk_deskprefs_defaultable(&prefs) &&
          !tiku_trk_deskprefs_revertable(&prefs), "DEV-009",
          "so neither Defaults nor Revert has anything to do");
    check(!prefs.value.reduce_motion, "MOTION", "motion is enabled by default");
    prefs.value.reduce_motion = 1;
    check(tiku_trk_deskprefs_defaultable(&prefs) &&
          tiku_trk_deskprefs_revertable(&prefs), "MOTION",
          "Reduce motion participates in Defaults and Revert");
    tiku_trk_deskprefs_mark_opened(&prefs);
    tiku_trk_deskprefs_defaults(&prefs);
    check(!prefs.value.reduce_motion, "MOTION", "Defaults enables motion");
    tiku_trk_deskprefs_revert(&prefs);
    check(prefs.value.reduce_motion, "MOTION",
          "Revert restores reduced motion");
    tiku_trk_deskprefs_init(&prefs);

    tiku_trk_deskprefs_set_picture(&prefs, "/tmp/whatever.bmp");
    check(tiku_trk_deskprefs_defaultable(&prefs) &&
          tiku_trk_deskprefs_revertable(&prefs), "DEV-009",
          "choosing a picture gives both of them something");
    tiku_trk_deskprefs_revert(&prefs);
    check(!prefs.value.have_bg, "DEV-009",
          "and Revert puts back what the window opened on");

    check(tiku_trk_deskprefs_step_icon(&prefs, 1) &&
          prefs.value.icon_size == 40, "DEV-009",
          "the icon size climbs its ladder a rung at a time");
    climb(&prefs, 1);
    check(prefs.value.icon_size == 128, "DEV-009",
          "which stops at the top rather than wrapping to the bottom");
    climb(&prefs, 0);
    check(prefs.value.icon_size == 32, "DEV-009",
          "and the same at the foot");

    /* A record on a node, read back as the window would read it. */
    store = tiku_store_memory_open();
    tiku_trk_deskprefs_set_picture(&prefs, "/tmp/whatever.bmp");
    tiku_trk_deskprefs_set_mode(&prefs, TIKU_TRK_BG_TILED);
    check(tiku_trk_deskprefs_save(&prefs, store, "/desk"), "DEV-009",
          "what was chosen is written onto the Desktop itself");
    {
        tiku_trk_deskprefs_t again;

        (void)tiku_trk_deskprefs_load(&again, store, "/desk", 64);
        check(again.value.have_bg &&
              strcmp(again.value.bg.path, "/tmp/whatever.bmp") == 0 &&
              again.value.bg.mode == TIKU_TRK_BG_TILED, "DEV-009",
              "and read back as the picture and the way it is laid");
        check(again.value.icon_size == 64, "DEV-009",
              "while the icon size comes from the view, which owns it");
        check(!tiku_trk_deskprefs_revertable(&again), "DEV-009",
              "a window that has just opened has nothing to revert");
    }

    /* Taking the picture away leaves the mode, so choosing another
     * picture does not start from scratch. */
    tiku_trk_deskprefs_clear_picture(&prefs);
    check(!prefs.value.have_bg && prefs.value.bg.mode == TIKU_TRK_BG_TILED,
          "DEV-009", "removing the picture keeps the way one would be laid");
    check(tiku_trk_deskprefs_save(&prefs, store, "/desk"), "DEV-009",
          "the picture-free choice can be saved");
    {
        tiku_trk_deskprefs_t again;

        (void)tiku_trk_deskprefs_load(&again, store, "/desk", 32);
        check(!again.value.have_bg &&
              again.value.bg.mode == TIKU_TRK_BG_TILED, "DEV-009",
              "and reopening keeps its layout mode for the next picture");
    }
    {
        tiku_trk_bg_t shown;

        check(!tiku_trk_bg_load_picture(store, "/desk", &shown), "DEV-009",
              "a record with no picture gives the folder nothing to draw");
        memset(&shown, 0, sizeof shown);
        shown.mode = TIKU_TRK_BG_TILED;
        snprintf(shown.path, sizeof shown.path, "%s", "/tmp/wall.png");
        (void)tiku_trk_bg_save(store, "/pictured", &shown);
        memset(&shown, 0, sizeof shown);
        check(tiku_trk_bg_load_picture(store, "/pictured", &shown) &&
              strcmp(shown.path, "/tmp/wall.png") == 0, "DEV-009",
              "a record that names a picture is drawn");
    }

    (void)system("rm -rf /tmp/trk_deskprefs && mkdir -p /tmp/trk_deskprefs");
    plant(dir);
    dirs[0] = dir;
    check(tiku_trk_deskprefs_pictures(&found, dirs, 1) == 1, "DEV-009",
          "the chooser offers the pictures in a directory");
    check(found.count == 1 &&
          strstr(found.picture[0], "one.bmp") != NULL, "DEV-009",
          "by what the file HOLDS, so a text file is not offered as one");
    check(tiku_trk_deskprefs_pictures(&found, dirs, 0) == 0, "DEV-009",
          "and a chooser given nowhere to look offers nothing");

    tiku_store_free(store);
    (void)system("rm -rf /tmp/trk_deskprefs");
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe Desktop's own settings hold\n");
    return 0;
}
