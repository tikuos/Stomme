/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_background.c - the record a background is kept in, and where the
 * picture it names goes.
 *
 * Placement is arithmetic over the view and the picture: the drawing floor
 * blits what this works out, and knows nothing about modes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_background.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
tiku_trk_bg_parse(const char *rec, tiku_trk_bg_t *out)
{
    int mode = 0, x = 0, y = 0, used = 0;

    if (rec == NULL || out == NULL || rec[0] == '\0') {
        return 0;
    }
    if (sscanf(rec, "%d %d %d %n", &mode, &x, &y, &used) < 3 || used <= 0) {
        return 0;
    }
    if (mode < TIKU_TRK_BG_AT_OFFSET || mode > TIKU_TRK_BG_TILED) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->mode = (tiku_trk_bg_mode_t)mode;
    out->x = x;
    out->y = y;
    /* The path takes the rest of the line, so a name with a space in it
     * survives the round trip. */
    if (rec[used] != '\0') {
        snprintf(out->path, sizeof out->path, "%s", rec + used);
    }
    return 1;
}

int
tiku_trk_bg_format(const tiku_trk_bg_t *bg, char *out, size_t max)
{
    if (bg == NULL || out == NULL || max == 0u) {
        return 0;
    }
    snprintf(out, max, "%d %d %d %s", (int)bg->mode, bg->x, bg->y, bg->path);
    return (int)strlen(out);
}

int
tiku_trk_bg_have(tiku_store_t *store, const char *node)
{
    char rec[TIKU_PATH_MAX + 64];

    if (store == NULL || node == NULL) {
        return 0;
    }
    return tiku_state_read(store, node, TIKU_TRK_ATTR_BACKGROUND, rec,
                               sizeof rec - 1) > 0;
}

int
tiku_trk_bg_load(tiku_store_t *store, const char *node,
                 tiku_trk_bg_t *out)
{
    char rec[TIKU_PATH_MAX + 64];
    int n;

    if (store == NULL || node == NULL || out == NULL) {
        return 0;
    }
    n = tiku_state_read(store, node, TIKU_TRK_ATTR_BACKGROUND, rec,
                            sizeof rec - 1);
    if (n <= 0) {
        return 0;
    }
    rec[n] = '\0';
    return tiku_trk_bg_parse(rec, out);
}

int
tiku_trk_bg_load_picture(tiku_store_t *store, const char *node,
                         tiku_trk_bg_t *out)
{
    tiku_trk_bg_t bg;

    if (out == NULL || !tiku_trk_bg_load(store, node, &bg) ||
        bg.path[0] == '\0') {
        return 0;
    }
    *out = bg;
    return 1;
}

int
tiku_trk_bg_save(tiku_store_t *store, const char *node,
                 const tiku_trk_bg_t *bg)
{
    char rec[TIKU_PATH_MAX + 64];
    int n;

    if (store == NULL || node == NULL || bg == NULL) {
        return 0;
    }
    n = tiku_trk_bg_format(bg, rec, sizeof rec);
    if (n <= 0) {
        return 0;
    }
    return tiku_state_write(store, node, TIKU_TRK_ATTR_BACKGROUND, rec,
                                (size_t)n) == 0;
}

int
tiku_trk_bg_place(const tiku_trk_bg_t *bg, int view_w, int view_h,
                  int img_w, int img_h, int i, int *dx, int *dy,
                  int *dw, int *dh)
{
    if (bg == NULL || img_w <= 0 || img_h <= 0 || i < 0 ||
        dx == NULL || dy == NULL || dw == NULL || dh == NULL) {
        return 0;
    }
    switch (bg->mode) {
    case TIKU_TRK_BG_SCALED:
        if (i > 0) {
            return 0;
        }
        *dx = 0;
        *dy = 0;
        *dw = view_w;
        *dh = view_h;
        return 1;
    case TIKU_TRK_BG_CENTERED:
        if (i > 0) {
            return 0;
        }
        /* Its own size, over the middle: a picture larger than the view
         * shows its middle and is not shrunk; SCALED is the mode that
         * fits it. */
        *dx = (view_w - img_w) / 2;
        *dy = (view_h - img_h) / 2;
        *dw = img_w;
        *dh = img_h;
        return 1;
    case TIKU_TRK_BG_TILED: {
        int cols = (view_w + img_w - 1) / img_w;
        int rows = (view_h + img_h - 1) / img_h;

        if (cols < 1) { cols = 1; }
        if (rows < 1) { rows = 1; }
        if (i >= cols * rows) {
            return 0;
        }
        *dx = (i % cols) * img_w;
        *dy = (i / cols) * img_h;
        *dw = img_w;
        *dh = img_h;
        return 1;
    }
    case TIKU_TRK_BG_AT_OFFSET:
    default:
        if (i > 0) {
            return 0;
        }
        *dx = bg->x;
        *dy = bg->y;
        *dw = img_w;
        *dh = img_h;
        return 1;
    }
}

int
tiku_trk_bg_install_default(tiku_store_t *store, const char *node,
                            int screen_w, int screen_h)
{
    const char *home = getenv("HOME");
    tiku_trk_bg_t bg;

    if (store == NULL || node == NULL || home == NULL || home[0] == '\0') {
        return 0;
    }

    if (tiku_trk_bg_have(store, node)) {
        return 0;               /* set before, or cleared                 */
    }
    memset(&bg, 0, sizeof bg);
    snprintf(bg.path, sizeof bg.path, "%s%s", home,
             TIKU_TRK_BG_DEFAULT_FILE);
    if (access(bg.path, R_OK) != 0) {
        return 0;               /* no picture to install                  */
    }
    /* The golden ratio across and nine tenths down, which is where the
     * source puts its logo. */
    bg.mode = TIKU_TRK_BG_AT_OFFSET;
    bg.x = (int)((double)screen_w * 0.618);
    bg.y = (int)((double)screen_h * 0.9);
    return tiku_trk_bg_save(store, node, &bg);
}
