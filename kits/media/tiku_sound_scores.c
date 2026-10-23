/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound_scores.c - the system sounds: the baked scores rendered into
 * clips, and a .score or .wav file in place of one.
 *
 * The scores are the text files under assets/sounds, baked into
 * tiku_scores_default.h at build time, so the sounds ship in the binary and
 * stay text in the tree.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"
#include "tiku_score.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_scores_default.h"
#include "tiku_wav.h"

#define SCORE_FILE_MAX 4096

static int16_t *rendered[TIKU_SOUND_EVENTS];

/** @brief The baked score text for @p name, or NULL. */
static const char *
score_text(const char *name)
{
    size_t i;

    for (i = 0; i < sizeof TIKU_SCORES / sizeof TIKU_SCORES[0]; i++) {
        if (strcmp(TIKU_SCORES[i].name, name) == 0) {
            return TIKU_SCORES[i].text;
        }
    }
    return NULL;
}

/** @brief Hand @p event the buffer @p pcm, freeing the one before. */
static void
take(tiku_sound_event_t event, int16_t *pcm, size_t frames)
{
    if (tiku_sound_playing()) {
        /* The clip in flight may be the one being replaced. */
        tiku_sound_set_clip(event, NULL, 0);
    }
    free(rendered[event]);
    rendered[event] = pcm;
    tiku_sound_set_clip(event, pcm, frames);
}

int
tiku_sound_load_score(tiku_sound_event_t event)
{
    const char *text;
    int16_t *pcm;
    size_t frames;

    if (event < 0 || event >= TIKU_SOUND_EVENTS) {
        return 0;
    }
    text = score_text(tiku_sound_name(event));
    frames = (text != NULL) ? tiku_score_frames(text, TIKU_SOUND_RATE) : 0;
    if (frames == 0) {
        take(event, NULL, 0);
        return 0;
    }
    pcm = malloc(frames * sizeof *pcm);
    if (pcm == NULL) {
        return 0;
    }
    frames = tiku_score_render(text, TIKU_SOUND_RATE, pcm, frames);
    take(event, pcm, frames);
    return 1;
}

int
tiku_sound_load_scores(void)
{
    int i, loaded = 0;

    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        loaded += tiku_sound_load_score((tiku_sound_event_t)i);
    }
    return loaded;
}

/** @brief The first SCORE_FILE_MAX bytes of @p path as text, malloc'd, or
 *         NULL. */
static char *
slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *text;
    size_t n;

    if (f == NULL) {
        return NULL;
    }
    text = malloc(SCORE_FILE_MAX + 1);
    if (text == NULL) {
        fclose(f);
        return NULL;
    }
    n = fread(text, 1, SCORE_FILE_MAX, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

int
tiku_sound_load_file(tiku_sound_event_t event, const char *path)
{
    const char *dot;
    int16_t *pcm = NULL;
    size_t frames = 0;

    if (event < 0 || event >= TIKU_SOUND_EVENTS || path == NULL) {
        return 0;
    }
    dot = strrchr(path, '.');
    if (dot != NULL && strcmp(dot, ".wav") == 0) {
        pcm = tiku_wav_read(path, TIKU_SOUND_RATE, TIKU_SCORE_MAX_S, &frames);
    } else if (dot != NULL && strcmp(dot, ".score") == 0) {
        char *text = slurp(path);

        frames = (text != NULL) ? tiku_score_frames(text, TIKU_SOUND_RATE) : 0;
        pcm = (frames > 0) ? malloc(frames * sizeof *pcm) : NULL;
        if (pcm != NULL) {
            frames = tiku_score_render(text, TIKU_SOUND_RATE, pcm, frames);
        }
        free(text);
    }
    if (pcm == NULL || frames == 0) {
        free(pcm);
        return 0;
    }
    take(event, pcm, frames);
    return 1;
}

const char *
tiku_sound_score(tiku_sound_event_t event)
{
    return score_text(tiku_sound_name(event));
}
