/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_deskprefs.c - the values behind the Desktop preferences.
 *
 * What Defaults and Revert mean, where the icon ladder stops, and which
 * files a chooser may offer, and whether the window fits the panel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tiku_font.h"
#include "tiku_iconpaint.h"
#include "tiku_trk_desktop_prefs_window.h"

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

/** @brief The title tab and the bottom of the window fit the Korvo panel. */
static void panel_window(void)
{
    tiku_surface_t *panel = tiku_surface_new(800, 480, 0);
    tiku_workspace_t *ws = tiku_workspace_new(panel);
    tiku_trk_desktop_view_t view = {0};
    tiku_trk_desktop_prefs_window_t prefs;
    tiku_window_t *hit = NULL;
    int dirty = 0;

    view.screen_w = 800;
    view.screen_h = 480;
    view.top_inset = 21;
    view.deskbar = tiku_trk_deskbar_new(NULL);
    tiku_font_set_size(16);
    tiku_trk_deskbar_set_dock(view.deskbar, TIKU_TRK_DB_RIGHT_EXPANDED);
    tiku_trk_deskbar_draw(view.deskbar, panel);
    tiku_trk_desktop_prefs_window_init(&prefs, ws, &view, NULL, 800, 480, &dirty);
    tiku_trk_desktop_prefs_window_open(&prefs);
    tiku_workspace_draw(ws);
    tiku_rect_t f = prefs.window->frame;
    printf("  800x480 frame (%d,%d) %dx%d, menu ends at %d\n",
           f.x, f.y, f.w, f.h, view.top_inset);
    check(f.y >= view.top_inset && f.y + f.h <= 480, "PANEL",
          "the tab and bottom actions are inside the usable display");
    int x = f.x + 12, y = f.y + tiku_window_tab_h() / 2;
    check(y >= view.top_inset && tiku_workspace_hit(ws, x, y, &hit) == TIKU_HIT_CLOSE &&
          hit == prefs.window, "PANEL", "the displayed close box receives the pointer");
    for (int size = 10; size <= 16; size += 2) {
        tiku_font_set_size(size);
        tiku_workspace_draw(ws);
        f = prefs.window->frame;
        tiku_trk_rect_t area = tiku_trk_desktop_view_area(&view);
        check(f.x >= area.x && f.x + f.w <= area.x + area.w &&
              f.y >= area.y && f.y + f.h <= area.y + area.h, "PANEL",
              "the refitted window stays clear of both bars after a font change");
    }
    tiku_trk_desktop_prefs_window_destroy(&prefs);
    tiku_trk_deskbar_free(view.deskbar);
    tiku_workspace_free(ws);
    tiku_surface_free(panel);
    tiku_font_set_size(12);
}

/** @brief Compare a damaged wallpaper tile or scaled picture with direct
 *  nearest-neighbour sampling. The view shares its parent's row stride. */
static int
picture_matches(int scale, int width, int height, int opaque)
{
    const tiku_rgb_t ground = TIKU_RGB(29, 31, 37);
    uint32_t pixels[12];
    tiku_image_t image = { 4, 3, pixels };
    tiku_rgb_t canvas[22 * 16];
    tiku_surface_t parent = { .px = canvas, .w = 11, .h = 8, .scale = scale };
    tiku_rect_t dst = { -1, -1, width, height }, clip = { 0, 1, 6, 4 };
    tiku_surface_t view;

    for (size_t i = 0; i < sizeof canvas / sizeof *canvas; i++) canvas[i] = ground;
    for (unsigned i = 0; i < 12; i++)
        pixels[i] = ((i % 3u) << 24) | TIKU_RGB(11u * i, i, 97);
    tiku_clip_damage(&parent, &(tiku_rect_t){3, 2, 5, 3});
    if (tiku_surface_view(&view, &parent, (tiku_rect_t){2, 1, 8, 6})) return 0;
    if (opaque) {
        for (unsigned i = 0; i < 12; i++) pixels[i] &= 0xffffffu;
        tiku_paint_rgb_image(&view, pixels, 4, 3, dst, clip);
    } else {
        tiku_paint_image(&view, &image, dst, clip);
    }
    for (int y = 0; y < 8 * scale; y++) {
        for (int x = 0; x < 11 * scale; x++) {
            int vx = x / scale - 2, vy = y / scale - 1;
            tiku_rgb_t want = ground;
            if (vx >= 1 && vx < 6 && vy >= 1 && vy < 4 && vx + 1 < width && vy + 1 < height) {
                uint32_t p = pixels[(vy + 1) * 3 / height * 4 + (vx + 1) * 4 / width];
                if (opaque || p >> 24) {
                    want = p & 0xffffffu;
                    if (tiku_theme_achromatic()) want = tiku_grey(want);
                }
            }
            if (canvas[y * 11 * scale + x] != want) return 0;
        }
    }
    return 1;
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

    const tiku_theme_t *original = tiku_theme();
    for (int mono = 0; mono <= 1; mono++) {
        if (mono) tiku_theme_set(tiku_theme_mono());
        for (int scale = 1; scale <= 2; scale++) {
            for (int opaque = 0; opaque <= 1; opaque++) {
                check(picture_matches(scale, 4, 3, opaque) &&
                      picture_matches(scale, 4, 6, opaque) &&
                      picture_matches(scale, 7, 6, opaque) &&
                      picture_matches(scale, 3, 6, opaque), "PICTURE",
                      "clipped RGB and ARGB wallpapers match direct sampling");
            }
        }
    }
    tiku_theme_set(original);
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
    panel_window();
    (void)system("rm -rf /tmp/trk_deskprefs");
    if (fails > 0) {
        printf("\nFAILURES\n");
        return 1;
    }
    printf("\nthe Desktop's own settings hold\n");
    return 0;
}
