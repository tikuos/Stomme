/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_background.c - the picture behind a folder: record, and placement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include "tiku_gfx.h"
#include "tiku_font.h"

#include "tiku_trk_background.h"

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
    tiku_trk_bg_t bg;
    char rec[600];
    int x, y, w, h;

    printf("the record a node carries (CW-020)\n");
    memset(&bg, 0, sizeof bg);
    bg.mode = TIKU_TRK_BG_AT_OFFSET;
    bg.x = 120;
    bg.y = 340;
    snprintf(bg.path, sizeof bg.path, "%s", "/tmp/pic tures/wall.png");
    check(tiku_trk_bg_format(&bg, rec, sizeof rec) > 0, "CW-020",
          "a background writes down as one line");
    memset(&bg, 0, sizeof bg);
    check(tiku_trk_bg_parse(rec, &bg) == 1 &&
          bg.mode == TIKU_TRK_BG_AT_OFFSET && bg.x == 120 && bg.y == 340 &&
          strcmp(bg.path, "/tmp/pic tures/wall.png") == 0, "CW-020",
          "and reads back whole -- including a path with a space in it, "
          "which is why the path is last and takes the rest of the line");
    check(tiku_trk_bg_parse("nonsense", &bg) == 0, "CW-020",
          "a record that is not one of ours is refused rather than read as "
          "a picture at 0,0");
    check(tiku_trk_bg_parse("", &bg) == 0, "CW-020",
          "and so is an empty one");
    check(tiku_trk_bg_parse("9 0 0 /tmp/wall.png", &bg) == 0, "CW-020",
          "a mode number nothing lays out is refused too, rather than "
          "falling through to a picture at the corner");

    printf("\nwhere the picture goes (AW-016)\n");
    memset(&bg, 0, sizeof bg);
    bg.mode = TIKU_TRK_BG_SCALED;
    check(tiku_trk_bg_place(&bg, 400, 300, 64, 64, 0, &x, &y, &w, &h) &&
          x == 0 && y == 0 && w == 400 && h == 300, "AW-016",
          "scaled covers the view whatever the picture's own size");
    check(!tiku_trk_bg_place(&bg, 400, 300, 64, 64, 1, &x, &y, &w, &h),
          "AW-016", "and is drawn once");

    bg.mode = TIKU_TRK_BG_CENTERED;
    check(tiku_trk_bg_place(&bg, 400, 300, 100, 50, 0, &x, &y, &w, &h) &&
          x == 150 && y == 125 && w == 100 && h == 50, "AW-016",
          "centred keeps its own size and sits in the middle");
    check(tiku_trk_bg_place(&bg, 100, 100, 300, 300, 0, &x, &y, &w, &h) &&
          x == -100 && y == -100, "AW-016",
          "a picture bigger than the view is centred over it rather than "
          "shrunk: the caller clips, so the middle of it is what shows");

    bg.mode = TIKU_TRK_BG_AT_OFFSET;
    bg.x = 30;
    bg.y = 40;
    check(tiku_trk_bg_place(&bg, 400, 300, 100, 50, 0, &x, &y, &w, &h) &&
          x == 30 && y == 40 && w == 100 && h == 50, "AW-016",
          "at-offset puts its corner where it was told");

    bg.mode = TIKU_TRK_BG_TILED;
    {
        int i = 0, seen = 0, covered_x = 0, covered_y = 0;

        while (tiku_trk_bg_place(&bg, 400, 300, 150, 100, i, &x, &y, &w, &h)) {
            if (x + w > covered_x) { covered_x = x + w; }
            if (y + h > covered_y) { covered_y = y + h; }
            seen++;
            i++;
            if (i > 100) { break; }
        }
        check(seen == 9, "AW-016",
              "tiled walks a grid that covers the view: three across and "
              "three down for a 150x100 picture in 400x300");
        check(covered_x >= 400 && covered_y >= 300, "AW-016",
              "and the last row and column run past the edge rather than "
              "stopping short of it, which would leave a bare strip");
    }

    printf("\nthe theme is a table, not a constant\n");
    {
        /* The Appearance Manager lesson: swap the table and every
         * semantic colour follows; hand back NULL and the R5 default
         * returns.  Layout is untouched throughout -- only the ink. */
        tiku_theme_t inverted = *tiku_theme();
        tiku_surface_t *s2 = tiku_surface_new(8, 8,
                                                        TIKU_C_PANEL);

        check(TIKU_C_PANEL == TIKU_RGB(216, 216, 216),
              "THEME", "the default table IS the R5 look");
        inverted.panel = TIKU_RGB(20, 20, 20);
        tiku_theme_set(&inverted);
        check(TIKU_C_PANEL == TIKU_RGB(20, 20, 20),
              "THEME", "a swapped table recolours the semantic names");
        if (s2 != NULL) {
            tiku_fill(s2, (tiku_rect_t){ 0, 0, 8, 8 },
                           TIKU_C_PANEL);
            check(s2->px[0] == TIKU_RGB(20, 20, 20), "THEME",
                  "and what is painted through them follows the table");
            tiku_surface_free(s2);
        }
        tiku_theme_set(NULL);
        check(TIKU_C_PANEL == TIKU_RGB(216, 216, 216),
              "THEME", "NULL restores the default rather than crashing");
    }
    {
        /* The metric-reference rule (Mac HIG ch1): every face the toolkit
         * offers must fit the plain face's metrics, so layout computed
         * against the reference survives any display face. */
        const tiku_font_t *plain = tiku_font_plain();
        const tiku_font_t *bold = tiku_font_bold();

        check(bold->height <= plain->height + 2 &&
              bold->height >= plain->height - 2, "METRIC",
              "the bold face lives within the reference metrics");
    }
    {
        /* And so does every family the picker offers: a person choosing
         * Serif is choosing letters, not a new layout, so each family is
         * baked at the same sizes and inside the same two pixels. */
        int family, px, worst = 0;

        check(tiku_font_family_count() >= 2 &&
              tiku_font_family_name(0) != NULL &&
              tiku_font_family_name(tiku_font_family_count())
                  == NULL, "METRIC",
              "the toolkit offers named families, and says where they end");
        for (px = 10; px <= 16; px += 2) {
            int reference, d;

            (void)tiku_font_set_family(0);
            (void)tiku_font_set_size(px);
            reference = tiku_font_plain()->height;
            for (family = 0; family < tiku_font_family_count();
                 family++) {
                (void)tiku_font_set_family(family);
                d = tiku_font_plain()->height - reference;
                worst = (d < 0 ? -d : d) > worst ? (d < 0 ? -d : d) : worst;
                d = tiku_font_bold()->height - reference;
                worst = (d < 0 ? -d : d) > worst ? (d < 0 ? -d : d) : worst;
            }
        }
        check(worst <= 2, "METRIC",
              "every family, both weights, all four sizes, within them too");

        (void)tiku_font_set_family(1);
        check(tiku_font_set_size(16) == 16 &&
              tiku_font_family() == 1, "METRIC",
              "and a chosen family outlives a change of size");
        check(tiku_font_set_family(tiku_font_family_count()) == 1 &&
              tiku_font_set_family(-1) == 1, "METRIC",
              "asking for a family that is not there keeps the one in force");
        (void)tiku_font_set_family(0);
        (void)tiku_font_set_size(12);
    }

    printf("\n%s\n", fails ? "FAILURES" : "the background holds");
    return fails ? 1 : 0;
}
