/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskprefs.c - what the Desktop looks like.
 *
 * The values only: where the controls sit and what a click on one means
 * belong to the window, tiku_trk_desktop_prefs_window.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_image.h"
#include "tiku_trk_deskprefs.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

static const int kSteps[TIKU_TRK_DESK_ICON_STEPS] = {
    32, 40, 48, 64, 96, 128
};

int
tiku_trk_deskprefs_icon_step(int i)
{
    if (i < 0 || i >= TIKU_TRK_DESK_ICON_STEPS) {
        return 0;
    }
    return kSteps[i];
}

void
tiku_trk_deskprefs_default(tiku_trk_deskprefs_values_t *values)
{
    if (values == NULL) {
        return;
    }
    memset(values, 0, sizeof *values);
    /* No picture, scaled, and the smallest icons. */
    values->bg.mode = TIKU_TRK_BG_SCALED;
    values->icon_size = 32;
}

void
tiku_trk_deskprefs_init(tiku_trk_deskprefs_t *prefs)
{
    if (prefs == NULL) {
        return;
    }
    tiku_trk_deskprefs_default(&prefs->value);
    prefs->opened = prefs->value;
}

int
tiku_trk_deskprefs_load(tiku_trk_deskprefs_t *prefs,
                        tiku_store_t *store, const char *node,
                        int icon_size)
{
    int found;

    if (prefs == NULL) {
        return 0;
    }
    tiku_trk_deskprefs_init(prefs);
    if (icon_size > 0) {
        prefs->value.icon_size = icon_size;
    }
    found = (store != NULL && node != NULL &&
             tiku_trk_bg_load(store, node, &prefs->value.bg));
    prefs->value.have_bg = found && prefs->value.bg.path[0] != '\0';
    if (!prefs->value.have_bg) {
        /* A record with no picture in it still names a mode, so only the
         * path is cleared and the mode is kept. */
        prefs->value.bg.path[0] = '\0';
    }
    prefs->opened = prefs->value;
    return found ? 1 : 0;
}

int
tiku_trk_deskprefs_save(const tiku_trk_deskprefs_t *prefs,
                        tiku_store_t *store, const char *node)
{
    tiku_trk_bg_t out;

    if (prefs == NULL || store == NULL || node == NULL) {
        return 0;
    }
    out = prefs->value.bg;
    if (!prefs->value.have_bg) {
        out.path[0] = '\0';
    }
    return tiku_trk_bg_save(store, node, &out);
}

void
tiku_trk_deskprefs_mark_opened(tiku_trk_deskprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->opened = prefs->value;
    }
}

/** @brief Whether two sets of values say the same thing. */
static int
same(const tiku_trk_deskprefs_values_t *a,
     const tiku_trk_deskprefs_values_t *b)
{
    if (a->reduce_motion != b->reduce_motion ||
        a->icon_size != b->icon_size || a->have_bg != b->have_bg ||
        a->bg.mode != b->bg.mode) {
        return 0;
    }
    /* The path counts only while a picture is shown: two Desktops showing
     * no picture are the same, whatever path each last held. */
    if (a->have_bg && strcmp(a->bg.path, b->bg.path) != 0) {
        return 0;
    }
    return 1;
}

int
tiku_trk_deskprefs_defaultable(const tiku_trk_deskprefs_t *prefs)
{
    tiku_trk_deskprefs_values_t fresh;

    if (prefs == NULL) {
        return 0;
    }
    tiku_trk_deskprefs_default(&fresh);
    return same(&prefs->value, &fresh) ? 0 : 1;
}

int
tiku_trk_deskprefs_revertable(const tiku_trk_deskprefs_t *prefs)
{
    if (prefs == NULL) {
        return 0;
    }
    return same(&prefs->value, &prefs->opened) ? 0 : 1;
}

void
tiku_trk_deskprefs_defaults(tiku_trk_deskprefs_t *prefs)
{
    if (prefs != NULL) {
        tiku_trk_deskprefs_default(&prefs->value);
    }
}

void
tiku_trk_deskprefs_revert(tiku_trk_deskprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->value = prefs->opened;
    }
}

void
tiku_trk_deskprefs_set_picture(tiku_trk_deskprefs_t *prefs,
                               const char *path)
{
    if (prefs == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    snprintf(prefs->value.bg.path, sizeof prefs->value.bg.path, "%s", path);
    prefs->value.have_bg = 1;
}

void
tiku_trk_deskprefs_clear_picture(tiku_trk_deskprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->value.have_bg = 0;
        prefs->value.bg.path[0] = '\0';
    }
}

void
tiku_trk_deskprefs_set_mode(tiku_trk_deskprefs_t *prefs,
                            tiku_trk_bg_mode_t mode)
{
    if (prefs != NULL) {
        prefs->value.bg.mode = mode;
    }
}

int
tiku_trk_deskprefs_step_icon(tiku_trk_deskprefs_t *prefs, int up)
{
    int i;

    if (prefs == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_TRK_DESK_ICON_STEPS; i++) {
        if (kSteps[i] == prefs->value.icon_size) {
            break;
        }
    }
    if (i >= TIKU_TRK_DESK_ICON_STEPS) {
        i = 0;                  /* a size not in kSteps counts as kSteps[0] */
    }
    i += up ? 1 : -1;
    if (i < 0 || i >= TIKU_TRK_DESK_ICON_STEPS) {
        return 0;               /* the ends do not wrap */
    }
    prefs->value.icon_size = kSteps[i];
    return 1;
}

int
tiku_trk_deskprefs_pictures(tiku_trk_deskprefs_pictures_t *out,
                            const char *const *dirs, int n)
{
    int d;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    for (d = 0; d < n && dirs != NULL; d++) {
        struct dirent *entry;
        DIR *dir;

        if (dirs[d] == NULL || dirs[d][0] == '\0') {
            continue;
        }
        dir = opendir(dirs[d]);
        if (dir == NULL) {
            continue;
        }
        while ((entry = readdir(dir)) != NULL &&
               out->count < TIKU_TRK_DESK_PICTURES) {
            char path[TIKU_PATH_MAX];

            if (entry->d_name[0] == '.') {
                continue;
            }
            if ((size_t)snprintf(path, sizeof path, "%s/%s", dirs[d],
                                 entry->d_name) >= sizeof path) {
                continue;
            }
            if (!tiku_image_is_image(path)) {
                continue;
            }
            snprintf(out->picture[out->count], TIKU_PATH_MAX, "%s",
                     path);
            out->count++;
        }
        (void)closedir(dir);
    }
    /* Sorted by path, since a directory returns its entries in no fixed
     * order. */
    {
        int i, j;

        for (i = 1; i < out->count; i++) {
            char hold[TIKU_PATH_MAX];

            snprintf(hold, sizeof hold, "%s", out->picture[i]);
            for (j = i; j > 0 &&
                        strcmp(out->picture[j - 1], hold) > 0; j--) {
                snprintf(out->picture[j], TIKU_PATH_MAX, "%s",
                         out->picture[j - 1]);
            }
            snprintf(out->picture[j], TIKU_PATH_MAX, "%s", hold);
        }
    }
    return out->count;
}
