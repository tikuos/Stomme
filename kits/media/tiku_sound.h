/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound.h - system sounds: the moments a shell may sound, and how.
 *
 * A table of named events -- the startup chime, the beep, a window
 * opening -- each with a clip, an on/off and one shared volume.  A shell
 * raises an event, and tiku_sound_pump() hands the clip's PCM to a backend
 * a chunk per turn.  With no backend every event is silent and still
 * counted (tiku_sound_count()).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SOUND_H_
#define TIKU_SOUND_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The moments a shell may sound.  Their order is the table's. */
typedef enum {
    TIKU_SOUND_STARTUP,         /* the boot screen's first stage */
    TIKU_SOUND_BEEP,            /* an alert opened */
    TIKU_SOUND_WINDOW_OPEN,
    TIKU_SOUND_WINDOW_CLOSE,
    TIKU_SOUND_WINDOW_MINIMIZE,
    TIKU_SOUND_WINDOW_ZOOM,
    TIKU_SOUND_WINDOW_ACTIVATE,
    TIKU_SOUND_SHUTDOWN,        /* the closing screen began */
    TIKU_SOUND_EVENTS
} tiku_sound_event_t;

/** @brief The rate every clip is rendered at and every backend opened at. */
#define TIKU_SOUND_RATE 44100u

/**
 * @brief Where the PCM goes: mono, signed 16-bit, at TIKU_SOUND_RATE.
 *
 * open() is called when a clip first needs writing, and close() once
 * nothing has been written for two seconds.  room() says how many frames
 * write() would take without blocking, or -1 when the backend cannot
 * tell; the kit writes chunks of at most fifty milliseconds either way.
 */
typedef struct tiku_sound_backend {
    const char *name;
    int  (*open)(void *ctx, unsigned rate);
    int  (*room)(void *ctx);
    int  (*write)(void *ctx, const int16_t *pcm, size_t frames);
    void (*close)(void *ctx);
    void *ctx;
} tiku_sound_backend_t;

/**
 * @brief Start with @p backend, or silent when it is NULL.
 *
 * Every event begins enabled with no clip and the volume at 100; a play
 * before a clip is set is counted and nothing else.
 */
void tiku_sound_init(const tiku_sound_backend_t *backend);

/** @brief Give @p event its PCM: @p frames mono samples, borrowed.  A
 *         clip of @p event in flight stops. */
void tiku_sound_set_clip(tiku_sound_event_t event, const int16_t *pcm,
                         size_t frames);

/** @brief Whether @p event makes a sound at all. */
void tiku_sound_set_enabled(tiku_sound_event_t event, int on);

/** @brief One volume for every event, 0 to 100, linear in amplitude. */
void tiku_sound_set_volume(int volume);

/**
 * @brief Raise @p event.
 *
 * Counted whether or not it sounds.  When it sounds, its clip replaces
 * whatever was playing; a shell that beeps twice hears the second.
 */
void tiku_sound_play(tiku_sound_event_t event);

/**
 * @brief Hand the backend the next chunk, from the shell's turn.
 *
 * Blocks no longer than the backend's write of one chunk, at most fifty
 * milliseconds of audio.  Closes an idle backend.
 */
void tiku_sound_pump(void);

/**
 * @brief Install an opener, asked when the device is closed and a clip is
 *        waiting; it returns nonzero when it takes the opening.
 *
 * Connecting to the sound server can take seconds, so a shell with workers
 * opens the device on a worker.
 */
void tiku_sound_set_opener(int (*fn)(void *ctx), void *ctx);

/** @brief Open the backend now, at TIKU_SOUND_RATE.
 *  @return The backend's open() result, or -1 with no backend.
 *  @note An opener calls this where it likes, a worker included. */
int  tiku_sound_open_now(void);

/** @brief Report the opener's result: the device is open when @p ok, and
 *         otherwise the waiting clip is dropped.
 *  @note Call it on the loop. */
void tiku_sound_opened(int ok);

/** @brief Whether a clip is still being handed out. */
int tiku_sound_playing(void);

/** @brief Whether @p event is enabled. */
int tiku_sound_enabled(tiku_sound_event_t event);

/** @brief The volume in force, 0 to 100. */
int tiku_sound_volume(void);

/** @brief How many times @p event has been raised since init. */
unsigned tiku_sound_count(tiku_sound_event_t event);

/** @brief The event's name, for a log or a preferences row. */
const char *tiku_sound_name(tiku_sound_event_t event);

/** @brief Close the backend and forget the clips. */
void tiku_sound_free(void);

/**
 * @brief Render every baked score and give each event its clip.
 * @return how many of the events now have a clip.
 */
int tiku_sound_load_scores(void);

/** @brief The score text baked for @p event, or NULL. */
const char *tiku_sound_score(tiku_sound_event_t event);

/** @brief Give @p event its baked score again.  @return 1 when it has
 *         one; without one the event's clip is cleared. */
int tiku_sound_load_score(tiku_sound_event_t event);

/**
 * @brief Give @p event the sound in @p path instead: a .score is rendered,
 *        a .wav is read (tiku_wav.h).  @return 1 when the clip changed;
 *        0 leaves whatever it had.
 */
int tiku_sound_load_file(tiku_sound_event_t event, const char *path);

/** @brief How many frames @p event's clip has, 0 for none. */
size_t tiku_sound_frames(tiku_sound_event_t event);

/**
 * @brief Whatever plays sound on the host this was built for.
 *
 * Defined once per host, the Makefile choosing the file: the PulseAudio
 * simple API opened with dlopen, or a CoreAudio audio queue.  A backend
 * that cannot open a device prints one line on stderr and plays silence.
 *
 * @return the backend, or NULL where this host has nothing to play on.
 */
const tiku_sound_backend_t *tiku_sound_host_backend(void);

#endif /* TIKU_SOUND_H_ */
