/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fontprefs.c - the interface font's preferences.
 *
 * The values only: the Fonts window owns its controls and what a click on
 * one of them means.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_font.h"
#include "tiku_fontdir.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_trk_fontprefs.h"
#include <stdio.h>
#include <string.h>

void
tiku_trk_fontprefs_default(tiku_trk_fontprefs_values_t *values)
{
    tiku_trk_deskbar_settings_t settings;

    if (values == NULL) {
        return;
    }
    /* The font defaults are the Deskbar record's defaults. */
    tiku_trk_deskbar_settings_default(&settings);
    memset(values, 0, sizeof *values);
    values->family = settings.font_family;
    values->size = settings.font_size;
}

void
tiku_trk_fontprefs_init(tiku_trk_fontprefs_t *prefs)
{
    if (prefs == NULL) {
        return;
    }
    tiku_trk_fontprefs_default(&prefs->value);
    prefs->opened = prefs->value;
}

int
tiku_trk_fontprefs_load(tiku_trk_fontprefs_t *prefs, tiku_store_t *store)
{
    tiku_trk_deskbar_prefs_t record;
    int found;

    if (prefs == NULL) {
        return 0;
    }
    tiku_trk_fontprefs_init(prefs);
    if (store == NULL) {
        return 0;
    }
    tiku_trk_deskbar_prefs_init(&record);
    found = (tiku_trk_deskbar_prefs_load(&record, store) == 0);
    if (found) {
        prefs->value.family = record.value.font_family;
        prefs->value.size = record.value.font_size;
    }
    {
        /* The dropped family's name is stored beside the record, which
         * holds only numbers. */
        char name[64];
        int n = tiku_state_read(store, TIKU_KEY_SESSION,
                                    TIKU_ATTR_UI_FONT, name,
                                    sizeof name - 1u);

        if (n > 0) {
            name[n] = '\0';
            snprintf(prefs->value.dropped, sizeof prefs->value.dropped,
                     "%s", name);
            found = 1;
        }
    }
    prefs->opened = prefs->value;
    return found ? 1 : 0;
}

int
tiku_trk_fontprefs_save(const tiku_trk_fontprefs_t *prefs,
                        tiku_store_t *store)
{
    tiku_trk_deskbar_prefs_t record;

    if (prefs == NULL || store == NULL) {
        return 0;
    }
    /* Read the record, change the two font fields, write it back: the
     * other settings in it are kept as they are. */
    tiku_trk_deskbar_prefs_init(&record);
    (void)tiku_trk_deskbar_prefs_load(&record, store);
    record.value.font_family = prefs->value.family;
    record.value.font_size = prefs->value.size;
    tiku_trk_deskbar_settings_normalize(&record.value);
    if (tiku_state_write(store, TIKU_KEY_SESSION,
                         TIKU_ATTR_UI_FONT, prefs->value.dropped,
                         strlen(prefs->value.dropped))) return 0;
    return tiku_trk_deskbar_prefs_save(&record, store) == 0;
}

void
tiku_trk_fontprefs_mark_opened(tiku_trk_fontprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->opened = prefs->value;
    }
}

/** @brief Whether two sets of values say the same thing. */
static int
same(const tiku_trk_fontprefs_values_t *a,
     const tiku_trk_fontprefs_values_t *b)
{
    if (a->size != b->size ||
        strcmp(a->dropped, b->dropped) != 0) {
        return 0;
    }
    /* While a dropped family is set, the baked family under it is not
     * compared. */
    return a->dropped[0] != '\0' || a->family == b->family;
}

int
tiku_trk_fontprefs_defaultable(const tiku_trk_fontprefs_t *prefs)
{
    tiku_trk_fontprefs_values_t fresh;

    if (prefs == NULL) {
        return 0;
    }
    tiku_trk_fontprefs_default(&fresh);
    return same(&prefs->value, &fresh) ? 0 : 1;
}

int
tiku_trk_fontprefs_revertable(const tiku_trk_fontprefs_t *prefs)
{
    if (prefs == NULL) {
        return 0;
    }
    return same(&prefs->value, &prefs->opened) ? 0 : 1;
}

void
tiku_trk_fontprefs_defaults(tiku_trk_fontprefs_t *prefs)
{
    if (prefs != NULL) {
        tiku_trk_fontprefs_default(&prefs->value);
    }
}

void
tiku_trk_fontprefs_revert(tiku_trk_fontprefs_t *prefs)
{
    if (prefs != NULL) {
        prefs->value = prefs->opened;
    }
}

int
tiku_trk_fontprefs_set_family(tiku_trk_fontprefs_t *prefs, int family)
{
    if (prefs == NULL || family < 0 ||
        family >= tiku_font_family_count() ||
        (family == prefs->value.family && prefs->value.dropped[0] == '\0')) {
        return 0;
    }
    prefs->value.family = family;
    prefs->value.dropped[0] = '\0';
    return 1;
}

int
tiku_trk_fontprefs_step_size(tiku_trk_fontprefs_t *prefs, int up)
{
    int next;

    if (prefs == NULL) {
        return 0;
    }
    next = prefs->value.size + (up ? TIKU_TRK_FONT_STEP : -TIKU_TRK_FONT_STEP);
    if (next < TIKU_TRK_FONT_MIN || next > TIKU_TRK_FONT_MAX) {
        return 0;               /* no wrap at either end */
    }
    prefs->value.size = next;
    return 1;
}

int
tiku_trk_fontprefs_family_count(void)
{
    return tiku_font_family_count();
}

const char *
tiku_trk_fontprefs_family_name(int family)
{
    return tiku_font_family_name(family);
}

/** @brief What is in the fonts folder right now. */
static int
folder(tiku_fontdir_t *out)
{
    char dir[TIKU_PATH_MAX];

    (void)tiku_fonts_dir(dir, sizeof dir);
    return tiku_fontdir_scan(out, dir);
}

int
tiku_trk_fontprefs_offer(tiku_trk_fontprefs_offer_t *out)
{
    tiku_fontdir_t dropped;
    int i;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    for (i = 0; i < tiku_font_family_count() &&
                out->count < TIKU_TRK_FONT_OFFER_MAX; i++) {
        const char *name = tiku_font_family_name(i);

        if (name == NULL) {
            break;
        }
        snprintf(out->name[out->count], sizeof out->name[0], "%s", name);
        out->dropped[out->count] = 0;
        out->count++;
    }
    (void)folder(&dropped);
    for (i = 0; i < dropped.count &&
                out->count < TIKU_TRK_FONT_OFFER_MAX; i++) {
        snprintf(out->name[out->count], sizeof out->name[0], "%s",
                 dropped.font[i].name);
        out->dropped[out->count] = 1;
        out->count++;
    }
    return out->count;
}

int
tiku_trk_fontprefs_choose(tiku_trk_fontprefs_t *prefs,
                          const tiku_trk_fontprefs_offer_t *offer, int row)
{
    if (prefs == NULL || offer == NULL || row < 0 || row >= offer->count) {
        return 0;
    }
    if (!offer->dropped[row]) {
        return tiku_trk_fontprefs_set_family(prefs, row);
    }
    if (strcmp(prefs->value.dropped, offer->name[row]) == 0) {
        return 0;
    }
    snprintf(prefs->value.dropped, sizeof prefs->value.dropped, "%s",
             offer->name[row]);
    return 1;
}

int
tiku_trk_fontprefs_chosen(const tiku_trk_fontprefs_t *prefs,
                          const tiku_trk_fontprefs_offer_t *offer)
{
    int i;

    if (prefs == NULL || offer == NULL) {
        return -1;
    }
    for (i = 0; i < offer->count; i++) {
        if (offer->dropped[i]) {
            if (strcmp(prefs->value.dropped, offer->name[i]) == 0) {
                return i;
            }
        } else if (prefs->value.dropped[0] == '\0' &&
                   prefs->value.family == i) {
            return i;
        }
    }
    return -1;
}

void
tiku_trk_fontprefs_apply(const tiku_trk_fontprefs_t *prefs)
{
    if (prefs == NULL) {
        return;
    }
    if (prefs->value.dropped[0] != '\0') {
        tiku_fontdir_t dir;
        const tiku_fontdir_entry_t *e;

        (void)folder(&dir);
        e = tiku_fontdir_find(&dir, prefs->value.dropped);
        if (e != NULL &&
            tiku_font_set_files(e->regular,
                                     (e->bold[0] != '\0') ? e->bold
                                                          : NULL)) {
            (void)tiku_font_set_size(prefs->value.size);
            return;
        }
        /* No longer in the folder: the baked family is used. */
    }
    tiku_font_use_baked();
    (void)tiku_font_set_family(prefs->value.family);
    (void)tiku_font_set_size(prefs->value.size);
}
