/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs.c - the Deskbar's preferences: defaults, ranges,
 * the stored record and its older layouts, and the dock picker.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_font.h"
#include "tiku_trk_deskbar_prefs.h"
#include <stdint.h>
#include <string.h>

#define PREF_VERSION 6
#define PREF_FIELDS  23
#define PREF_FIELDS_V5 22
#define PREF_FIELDS_V4 21
#define PREF_FIELDS_V3 20
#define PREF_FIELDS_V2 17
#define PREF_FIELDS_V1 16

/** @brief Whether two settings agree in every field the file keeps. */
static int
same(const tiku_trk_deskbar_settings_t *a,
     const tiku_trk_deskbar_settings_t *b)
{
    int i;

    if (a->sort_apps != b->sort_apps ||
        a->tracker_first != b->tracker_first ||
        a->show_expander != b->show_expander ||
        a->expand_new_apps != b->expand_new_apps ||
        a->hide_app_names != b->hide_app_names ||
        a->icon_size != b->icon_size ||
        a->always_on_top != b->always_on_top ||
        a->auto_raise != b->auto_raise || a->auto_hide != b->auto_hide ||
        a->show_clock != b->show_clock ||
        a->show_seconds != b->show_seconds ||
        a->show_day_of_week != b->show_day_of_week ||
        a->show_timezone != b->show_timezone ||
        a->font_size != b->font_size ||
        a->font_family != b->font_family ||
        a->show_replicants != b->show_replicants) {
        return 0;
    }
    for (i = 0; i < TIKU_TRK_DB_RECENT_KINDS; i++) {
        if (a->recent_enabled[i] != b->recent_enabled[i] ||
            a->recent_count[i] != b->recent_count[i]) {
            return 0;
        }
    }
    return 1;
}

void
tiku_trk_deskbar_settings_default(tiku_trk_deskbar_settings_t *settings)
{
    int i;

    memset(settings, 0, sizeof *settings);
    settings->tracker_first = 1;
    settings->show_expander = 1;
    settings->expand_new_apps = 1;
    settings->icon_size = TIKU_TRK_DB_ICON_MIN;
    for (i = 0; i < TIKU_TRK_DB_RECENT_KINDS; i++) {
        settings->recent_enabled[i] = 1;
        settings->recent_count[i] = 10;
    }
    settings->always_on_top = 1;
    settings->show_clock = 1;
    settings->font_size = 12;
    settings->show_replicants = 1;
}

void
tiku_trk_deskbar_settings_normalize(tiku_trk_deskbar_settings_t *settings)
{
    int i;

    if (settings == NULL) { return; }
    if (settings->icon_size < TIKU_TRK_DB_ICON_MIN) {
        settings->icon_size = TIKU_TRK_DB_ICON_MIN;
    }
    if (settings->icon_size > TIKU_TRK_DB_ICON_MAX) {
        settings->icon_size = TIKU_TRK_DB_ICON_MAX;
    }
    for (i = 0; i < TIKU_TRK_DB_RECENT_KINDS; i++) {
        if (settings->recent_count[i] < 0) {
            settings->recent_count[i] = 0;
        }
        if (settings->recent_count[i] > TIKU_TRK_DB_RECENT_LIMIT) {
            settings->recent_count[i] = TIKU_TRK_DB_RECENT_LIMIT;
        }
        settings->recent_enabled[i] = !!settings->recent_enabled[i];
    }
    settings->sort_apps = !!settings->sort_apps;
    settings->tracker_first = !!settings->tracker_first;
    settings->show_expander = !!settings->show_expander;
    settings->expand_new_apps = !!settings->expand_new_apps;
    settings->hide_app_names = !!settings->hide_app_names;
    settings->always_on_top = !!settings->always_on_top;
    settings->auto_raise = settings->always_on_top ? 0
                                                   : !!settings->auto_raise;
    settings->auto_hide = !!settings->auto_hide;
    settings->show_clock = !!settings->show_clock;
    settings->show_seconds = !!settings->show_seconds;
    settings->show_day_of_week = !!settings->show_day_of_week;
    settings->show_timezone = !!settings->show_timezone;
    settings->show_replicants = !!settings->show_replicants;
    if (settings->font_size < 10) { settings->font_size = 10; }
    if (settings->font_size > 16) { settings->font_size = 16; }
    settings->font_size &= ~1;      /* the baked faces are even-sized */
    /* A family number outside 0 .. tiku_font_family_count() - 1 falls
     * back to the first family. */
    if (settings->font_family < 0 ||
        settings->font_family >= tiku_font_family_count()) {
        settings->font_family = 0;
    }
}

void
tiku_trk_deskbar_prefs_init(tiku_trk_deskbar_prefs_t *prefs)
{
    tiku_trk_deskbar_settings_default(&prefs->value);
    prefs->opened = prefs->value;
}

void
tiku_trk_deskbar_prefs_mark_opened(tiku_trk_deskbar_prefs_t *prefs)
{
    prefs->opened = prefs->value;
}

int
tiku_trk_deskbar_prefs_defaultable(const tiku_trk_deskbar_prefs_t *prefs)
{
    tiku_trk_deskbar_settings_t defaults;

    tiku_trk_deskbar_settings_default(&defaults);
    return !same(&prefs->value, &defaults);
}

int
tiku_trk_deskbar_prefs_revertable(const tiku_trk_deskbar_prefs_t *prefs)
{
    return !same(&prefs->value, &prefs->opened);
}

void
tiku_trk_deskbar_prefs_defaults(tiku_trk_deskbar_prefs_t *prefs)
{
    tiku_trk_deskbar_settings_default(&prefs->value);
}

void
tiku_trk_deskbar_prefs_revert(tiku_trk_deskbar_prefs_t *prefs)
{
    prefs->value = prefs->opened;
}

int
tiku_trk_deskbar_prefs_recent_enabled(const tiku_trk_deskbar_prefs_t *prefs,
                                      tiku_trk_db_recent_t which)
{
    return prefs != NULL && which >= 0 &&
           which < TIKU_TRK_DB_RECENT_KINDS &&
           prefs->value.recent_enabled[which] &&
           prefs->value.recent_count[which] > 0;
}

int
tiku_trk_deskbar_prefs_expanders_enabled(
    const tiku_trk_deskbar_prefs_t *prefs, tiku_trk_db_dock_t dock)
{
    (void)prefs;
    return dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           dock == TIKU_TRK_DB_RIGHT_EXPANDED;
}

int
tiku_trk_deskbar_prefs_auto_raise_enabled(
    const tiku_trk_deskbar_prefs_t *prefs)
{
    return prefs != NULL && !prefs->value.always_on_top;
}

/** @brief The settings as the stored record, version first. */
static void
pack(const tiku_trk_deskbar_settings_t *s, int32_t v[PREF_FIELDS])
{
    int at = 0, i;

    v[at++] = PREF_VERSION;
    v[at++] = s->sort_apps;
    v[at++] = s->tracker_first;
    v[at++] = s->show_expander;
    v[at++] = s->expand_new_apps;
    v[at++] = s->hide_app_names;
    v[at++] = s->icon_size;
    for (i = 0; i < 3; i++) { v[at++] = s->recent_enabled[i]; }
    for (i = 0; i < 3; i++) { v[at++] = s->recent_count[i]; }
    v[at++] = s->always_on_top;
    v[at++] = s->auto_raise;
    v[at++] = s->auto_hide;
    v[at++] = s->show_clock;
    v[at++] = s->show_seconds;
    v[at++] = s->show_day_of_week;
    v[at++] = s->show_timezone;
    v[at++] = s->font_size;
    v[at++] = s->font_family;
    v[at++] = s->show_replicants;
}

/**
 * @brief The settings from a stored record, the fields an older version
 *        lacks filled with their defaults, then normalised.
 */
static void
unpack(tiku_trk_deskbar_settings_t *s, const int32_t v[PREF_FIELDS])
{
    int at = 1, i;

    s->sort_apps = v[at++];
    s->tracker_first = v[at++];
    s->show_expander = v[at++];
    s->expand_new_apps = v[at++];
    s->hide_app_names = v[at++];
    s->icon_size = v[at++];
    for (i = 0; i < 3; i++) { s->recent_enabled[i] = v[at++]; }
    for (i = 0; i < 3; i++) { s->recent_count[i] = v[at++]; }
    s->always_on_top = v[at++];
    s->auto_raise = v[at++];
    s->auto_hide = v[at++];
    s->show_clock = (v[0] >= 2) ? v[at++] : 1;
    s->show_seconds = (v[0] >= 3) ? v[at++] : 0;
    s->show_day_of_week = (v[0] >= 3) ? v[at++] : 0;
    s->show_timezone = (v[0] >= 3) ? v[at++] : 0;
    s->font_size = (v[0] >= 4) ? v[at++] : 12;
    s->font_family = (v[0] >= 5) ? v[at++] : 0;
    s->show_replicants = (v[0] >= 6) ? v[at++] : 1;
    tiku_trk_deskbar_settings_normalize(s);
}

int
tiku_trk_deskbar_prefs_load(tiku_trk_deskbar_prefs_t *prefs,
                            tiku_store_t *store)
{
    int32_t record[PREF_FIELDS];
    int n;

    if (prefs == NULL || store == NULL) {
        return -1;
    }
    n = tiku_state_read(store, TIKU_KEY_SESSION,
                            TIKU_ATTR_DESKBAR_PREFS, record,
                            sizeof record);
    if (n != (int)sizeof record) {
        n = tiku_state_read(store, TIKU_KEY_SESSION,
                                TIKU_ATTR_DESKBAR_PREFS, record,
                                PREF_FIELDS_V1 * sizeof *record);
    }
    if (n != (int)sizeof record &&
        n != PREF_FIELDS_V5 * (int)sizeof *record &&
        n != PREF_FIELDS_V4 * (int)sizeof *record &&
        n != PREF_FIELDS_V3 * (int)sizeof *record &&
        n != PREF_FIELDS_V2 * (int)sizeof *record &&
        n != PREF_FIELDS_V1 * (int)sizeof *record) {
        return -1;
    }
    if (record[0] < 1 || record[0] > PREF_VERSION) {
        return -1;
    }
    unpack(&prefs->value, record);
    prefs->opened = prefs->value;
    return 0;
}

int
tiku_trk_deskbar_prefs_save(const tiku_trk_deskbar_prefs_t *prefs,
                            tiku_store_t *store)
{
    int32_t record[PREF_FIELDS];

    if (prefs == NULL || store == NULL) { return -1; }
    pack(&prefs->value, record);
    return tiku_state_write(store, TIKU_KEY_SESSION,
                                TIKU_ATTR_DESKBAR_PREFS,
                                record, sizeof record);
}

tiku_trk_db_dock_t
tiku_trk_deskbar_prefs_dock_at(int x, int y, int width, int height,
                               tiku_trk_db_dock_t current)
{
    if (width <= 0 || height <= 0 || x < 0 || y < 0 ||
        x >= width || y >= height) {
        return current;
    }
    if (x < width / 3) {
        return (y >= height / 3 && y < 2 * height / 3)
                   ? TIKU_TRK_DB_LEFT_EXPANDED : TIKU_TRK_DB_LEFT_MINI;
    }
    if (x >= 2 * width / 3) {
        return (y >= height / 3 && y < 2 * height / 3)
                   ? TIKU_TRK_DB_RIGHT_EXPANDED : TIKU_TRK_DB_RIGHT_MINI;
    }
    return (y < height / 2) ? TIKU_TRK_DB_TOP : TIKU_TRK_DB_BOTTOM;
}

int
tiku_trk_deskbar_prefs_dock_key(const char *key, tiku_trk_db_dock_t *dock)
{
    if (key == NULL || dock == NULL) { return 0; }
    if (strcmp(key, "8") == 0 || strcmp(key, "Up") == 0) {
        *dock = TIKU_TRK_DB_TOP;
    } else if (strcmp(key, "2") == 0 || strcmp(key, "Down") == 0) {
        *dock = TIKU_TRK_DB_BOTTOM;
    } else if (strcmp(key, "7") == 0 || strcmp(key, "Home") == 0) {
        *dock = TIKU_TRK_DB_LEFT_MINI;
    } else if (strcmp(key, "9") == 0 || strcmp(key, "PageUp") == 0) {
        *dock = TIKU_TRK_DB_RIGHT_MINI;
    } else if (strcmp(key, "1") == 0 || strcmp(key, "End") == 0) {
        *dock = TIKU_TRK_DB_LEFT_EXPANDED;
    } else if (strcmp(key, "3") == 0 || strcmp(key, "PageDown") == 0) {
        *dock = TIKU_TRK_DB_RIGHT_EXPANDED;
    } else {
        return 0;
    }
    return 1;
}

tiku_rect_t
tiku_trk_deskbar_prefs_frame(int saved, int x, int y, int screen_w,
                             int screen_h, int width, int height)
{
    tiku_rect_t frame = { (screen_w - width) / 2,
                               (screen_h - height) / 2, width, height };

    if (saved && x >= 0 && y >= 0 && x + width <= screen_w &&
        y + height <= screen_h) {
        frame.x = x;
        frame.y = y;
    }
    if (frame.x < 0) { frame.x = 0; }
    if (frame.y < 0) { frame.y = 0; }
    return frame;
}
