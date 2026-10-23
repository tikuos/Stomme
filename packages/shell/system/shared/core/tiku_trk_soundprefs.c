/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_soundprefs.c - the sound settings, read and written by name.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_state.h"
#include "tiku_trk_soundprefs.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUME_KEY "sound.volume"

const char *
tiku_trk_soundprefs_key(tiku_sound_event_t event)
{
    /* One slot per event: the names are handed out by pointer and two
     * may be in hand at once, as when a table is being drawn. */
    static char keys[TIKU_SOUND_EVENTS][32];

    if (event < 0 || event >= TIKU_SOUND_EVENTS) {
        return "";
    }
    snprintf(keys[event], sizeof keys[event], "sound.%s",
             tiku_sound_name(event));
    return keys[event];
}

const char *
tiku_trk_soundprefs_clip_key(tiku_sound_event_t event)
{
    static char keys[TIKU_SOUND_EVENTS][40];

    if (event < 0 || event >= TIKU_SOUND_EVENTS) {
        return "";
    }
    snprintf(keys[event], sizeof keys[event], "sound.%s.clip",
             tiku_sound_name(event));
    return keys[event];
}

const char *
tiku_trk_soundprefs_clip(tiku_trk_prefs_t *prefs, tiku_sound_event_t event)
{
    return tiku_trk_prefs_text(prefs, tiku_trk_soundprefs_clip_key(event));
}

void
tiku_trk_soundprefs_set_clip(tiku_trk_prefs_t *prefs,
                             tiku_sound_event_t event, const char *name)
{
    tiku_trk_prefs_set_text(prefs, tiku_trk_soundprefs_clip_key(event), name);
}

/** @brief Whether @p name is a .score or .wav file, and not hidden. */
static int
playable(const char *name)
{
    const char *dot = strrchr(name, '.');

    return name[0] != '.' && dot != NULL &&
           (strcmp(dot, ".score") == 0 || strcmp(dot, ".wav") == 0);
}

/** @brief strcmp over two offer names, for qsort. */
static int
by_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int
tiku_trk_soundprefs_offer(tiku_trk_soundprefs_offer_t *out)
{
    char dir[1024];
    DIR *d;
    struct dirent *e;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    d = opendir(tiku_sounds_dir(dir, sizeof dir));
    if (d == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && out->count < TIKU_TRK_SOUND_OFFER_MAX) {
        if (playable(e->d_name) && strlen(e->d_name) < sizeof out->name[0]) {
            snprintf(out->name[out->count++], sizeof out->name[0], "%s",
                     e->d_name);
        }
    }
    closedir(d);
    qsort(out->name, (size_t)out->count, sizeof out->name[0], by_name);
    return out->count;
}

int
tiku_trk_soundprefs_cycle_clip(tiku_trk_prefs_t *prefs,
                               const tiku_trk_soundprefs_offer_t *offer,
                               tiku_sound_event_t event)
{
    const char *now = tiku_trk_soundprefs_clip(prefs, event);
    int i;

    if (offer == NULL || offer->count == 0) {
        return 0;
    }
    /* A name no longer on offer is treated as the baked one, so the
     * first press after a file went missing lands on a real choice. */
    for (i = 0; i < offer->count; i++) {
        if (strcmp(offer->name[i], now) == 0) {
            break;
        }
    }
    tiku_trk_soundprefs_set_clip(prefs, event,
                                 (i + 1 < offer->count) ? offer->name[i + 1]
                                 : (i == offer->count)  ? offer->name[0]
                                                        : "");
    return 1;
}

int
tiku_trk_soundprefs_enabled(tiku_trk_prefs_t *prefs, tiku_sound_event_t event)
{
    return tiku_trk_prefs_get(prefs, tiku_trk_soundprefs_key(event)) != 0;
}

void
tiku_trk_soundprefs_set_enabled(tiku_trk_prefs_t *prefs,
                                tiku_sound_event_t event, int on)
{
    tiku_trk_prefs_set(prefs, tiku_trk_soundprefs_key(event), on ? 1 : 0);
}

int
tiku_trk_soundprefs_volume(tiku_trk_prefs_t *prefs)
{
    int32_t v = tiku_trk_prefs_get(prefs, VOLUME_KEY);

    return (v < 0) ? 0 : (v > 100) ? 100 : (int)v;
}

int
tiku_trk_soundprefs_step_volume(tiku_trk_prefs_t *prefs, int up)
{
    int v = tiku_trk_soundprefs_volume(prefs);
    int to = up ? v + TIKU_TRK_SOUND_VOLUME_STEP
                : v - TIKU_TRK_SOUND_VOLUME_STEP;

    if (to < 0 || to > 100) {
        return 0;
    }
    tiku_trk_prefs_set(prefs, VOLUME_KEY, to);
    return 1;
}

int
tiku_trk_soundprefs_defaultable(tiku_trk_prefs_t *prefs)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        const tiku_trk_pref_t *pr =
            tiku_trk_prefs_find(prefs, tiku_trk_soundprefs_key(i));

        if (pr != NULL && pr->value != pr->def) {
            return 1;
        }
        if (tiku_trk_soundprefs_clip(prefs, i)[0] != '\0') {
            return 1;
        }
    }
    {
        const tiku_trk_pref_t *pr = tiku_trk_prefs_find(prefs, VOLUME_KEY);

        return pr != NULL && pr->value != pr->def;
    }
}

void
tiku_trk_soundprefs_defaults(tiku_trk_prefs_t *prefs)
{
    int i;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        tiku_trk_pref_t *pr =
            tiku_trk_prefs_find(prefs, tiku_trk_soundprefs_key(i));

        if (pr != NULL) {
            pr->value = pr->def;
        }
        tiku_trk_soundprefs_set_clip(prefs, i, "");
    }
    {
        tiku_trk_pref_t *pr = tiku_trk_prefs_find(prefs, VOLUME_KEY);

        if (pr != NULL) {
            pr->value = pr->def;
        }
    }
}

/**
 * @brief Give @p event the file its setting names, or the baked score
 *        when it names none or one that cannot be read.
 */
static void
load(tiku_trk_prefs_t *prefs, tiku_sound_event_t event, const char *dir)
{
    const char *name = tiku_trk_soundprefs_clip(prefs, event);
    char path[1200];

    if (name[0] != '\0') {
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if (tiku_sound_load_file(event, path)) {
            return;
        }
    }
    (void)tiku_sound_load_score(event);
}

void
tiku_trk_soundprefs_apply(tiku_trk_prefs_t *prefs)
{
    char dir[1024];
    int i;

    (void)tiku_sounds_dir(dir, sizeof dir);
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        tiku_sound_set_enabled(i, tiku_trk_soundprefs_enabled(prefs, i));
        load(prefs, i, dir);
    }
    tiku_sound_set_volume(tiku_trk_soundprefs_volume(prefs));
}
