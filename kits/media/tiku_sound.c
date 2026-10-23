/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_sound.c - the event table, one clip in flight, a chunk per turn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"

#include <string.h>
#include <time.h>

#define CHUNK_FRAMES   2205u        /* fifty milliseconds at 44.1 kHz */
#define IDLE_CLOSE_S   2            /* an idle stream is let go after this */

typedef struct {
    const int16_t *pcm;
    size_t         frames;
} clip_t;

static struct {
    const tiku_sound_backend_t *backend;
    int      opened;
    clip_t   clip[TIKU_SOUND_EVENTS];
    int      enabled[TIKU_SOUND_EVENTS];
    unsigned count[TIKU_SOUND_EVENTS];
    int      volume;                    /* 0..100 */
    const clip_t *playing;              /* the clip in flight, or NULL */
    size_t   at;                        /* frames of it already written */
    time_t   last_write;                /* for the idle close */
    int16_t  chunk[CHUNK_FRAMES];
} sound;

static int (*opener)(void *ctx);
static void *opener_ctx;
static int opening;

void
tiku_sound_set_opener(int (*fn)(void *ctx), void *ctx)
{
    opener = fn;
    opener_ctx = ctx;
}

int
tiku_sound_open_now(void)
{
    if (sound.backend == NULL || sound.backend->open == NULL) {
        return -1;
    }
    return sound.backend->open(sound.backend->ctx, TIKU_SOUND_RATE);
}

void
tiku_sound_opened(int ok)
{
    opening = 0;
    if (ok) {
        sound.opened = 1;
    } else {
        sound.playing = NULL;
    }
}

static const char *const NAMES[TIKU_SOUND_EVENTS] = {
    "startup", "beep", "window-open", "window-close", "window-minimize",
    "window-zoom", "window-activate", "shutdown",
};

void
tiku_sound_init(const tiku_sound_backend_t *backend)
{
    int i;

    memset(&sound, 0, sizeof sound);
    sound.backend = backend;
    sound.volume = 100;
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        sound.enabled[i] = 1;
    }
}

void
tiku_sound_set_clip(tiku_sound_event_t event, const int16_t *pcm,
                    size_t frames)
{
    if (event < 0 || event >= TIKU_SOUND_EVENTS) {
        return;
    }
    if (sound.playing == &sound.clip[event]) {
        sound.playing = NULL;
    }
    sound.clip[event].pcm = pcm;
    sound.clip[event].frames = (pcm != NULL) ? frames : 0u;
}

void
tiku_sound_set_enabled(tiku_sound_event_t event, int on)
{
    if (event >= 0 && event < TIKU_SOUND_EVENTS) {
        sound.enabled[event] = (on != 0);
    }
}

void
tiku_sound_set_volume(int volume)
{
    sound.volume = (volume < 0) ? 0 : (volume > 100) ? 100 : volume;
}

void
tiku_sound_play(tiku_sound_event_t event)
{
    if (event < 0 || event >= TIKU_SOUND_EVENTS) {
        return;
    }
    sound.count[event]++;
    if (!sound.enabled[event] || sound.clip[event].pcm == NULL ||
        sound.backend == NULL || sound.volume == 0) {
        return;
    }
    sound.playing = &sound.clip[event];
    sound.at = 0;
}

/**
 * @brief Whether the backend is open, opening it when needed.
 *
 * With an opener installed it is asked, and 0 is returned until it reports
 * through tiku_sound_opened(); without one the backend is opened here, and
 * a refusal drops the clip in flight.
 */
static int
ensure_open(void)
{
    if (sound.opened) {
        return 1;
    }
    if (opener != NULL) {
        /* The opener opens the device elsewhere and reports through
         * tiku_sound_opened(); until then nothing is written and the clip
         * waits. */
        if (!opening && opener(opener_ctx)) {
            opening = 1;
        }
        return 0;
    }
    if (sound.backend->open != NULL &&
        sound.backend->open(sound.backend->ctx, TIKU_SOUND_RATE) != 0) {
        sound.playing = NULL;
        return 0;
    }
    sound.opened = 1;
    return 1;
}

void
tiku_sound_pump(void)
{
    size_t n;
    int room, took;
    size_t i;

    if (sound.backend == NULL) {
        return;
    }
    if (sound.playing == NULL) {
        if (sound.opened && sound.backend->close != NULL &&
            time(NULL) - sound.last_write > IDLE_CLOSE_S) {
            sound.backend->close(sound.backend->ctx);
            sound.opened = 0;
        }
        return;
    }
    if (!ensure_open()) {
        return;
    }
    room = (sound.backend->room != NULL)
               ? sound.backend->room(sound.backend->ctx) : -1;
    if (room == 0) {
        return;                     /* the card is still busy with the last */
    }
    n = sound.playing->frames - sound.at;
    if (n > CHUNK_FRAMES) {
        n = CHUNK_FRAMES;
    }
    if (room > 0 && (size_t)room < n) {
        n = (size_t)room;
    }
    /* The volume is applied as each chunk is written; clips are kept at
     * full scale. */
    for (i = 0; i < n; i++) {
        sound.chunk[i] = (int16_t)((int)sound.playing->pcm[sound.at + i] *
                                   sound.volume / 100);
    }
    took = sound.backend->write(sound.backend->ctx, sound.chunk, n);
    sound.last_write = time(NULL);
    if (took < 0) {
        sound.playing = NULL;
        return;
    }
    sound.at += (size_t)took;
    if (sound.at >= sound.playing->frames) {
        sound.playing = NULL;
    }
}

int
tiku_sound_enabled(tiku_sound_event_t event)
{
    return (event >= 0 && event < TIKU_SOUND_EVENTS) ? sound.enabled[event]
                                                     : 0;
}

int
tiku_sound_volume(void)
{
    return sound.volume;
}

size_t
tiku_sound_frames(tiku_sound_event_t event)
{
    return (event >= 0 && event < TIKU_SOUND_EVENTS)
               ? sound.clip[event].frames : 0u;
}

int
tiku_sound_playing(void)
{
    return sound.playing != NULL;
}

unsigned
tiku_sound_count(tiku_sound_event_t event)
{
    return (event >= 0 && event < TIKU_SOUND_EVENTS) ? sound.count[event] : 0u;
}

const char *
tiku_sound_name(tiku_sound_event_t event)
{
    return (event >= 0 && event < TIKU_SOUND_EVENTS) ? NAMES[event] : "";
}

void
tiku_sound_free(void)
{
    if (sound.opened && sound.backend != NULL && sound.backend->close != NULL) {
        sound.backend->close(sound.backend->ctx);
    }
    memset(&sound, 0, sizeof sound);
    sound.volume = 100;
    opening = 0;
}
