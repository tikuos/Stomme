/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound_pulse.c - the host backend over the PulseAudio simple API.
 *
 * libpulse-simple is opened at run time with dlopen, so the build needs no
 * header and no link line; PipeWire serves the same library through
 * pipewire-pulse.  The prototypes below are the API's own.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define PA_SAMPLE_S16LE    3            /* pa_sample_format_t */
#define PA_STREAM_PLAYBACK 1            /* pa_stream_direction_t */
#define LATENCY_FULL_US    200000u      /* past this, room() answers 0    */
#define CHUNK_FRAMES       2205         /* fifty milliseconds */

typedef struct {
    int      format;
    uint32_t rate;
    uint8_t  channels;
} pa_sample_spec_t;

typedef void *(*pa_new_fn)(const char *server, const char *name, int dir,
                           const char *dev, const char *stream_name,
                           const pa_sample_spec_t *ss, const void *map,
                           const void *attr, int *error);
typedef int (*pa_write_fn)(void *s, const void *data, size_t bytes,
                           int *error);
typedef uint64_t (*pa_latency_fn)(void *s, int *error);
typedef void (*pa_free_fn)(void *s);
typedef const char *(*pa_strerror_fn)(int error);

static struct {
    void          *lib;
    pa_new_fn      pa_new;
    pa_write_fn    pa_write;
    pa_latency_fn  pa_latency;
    pa_free_fn     pa_free;
    pa_strerror_fn pa_strerror;
    void          *stream;
    int            said;                /* the one line on stderr, once */
} pulse;

/**
 * @brief Load the library and its entry points: pa_simple_new, _write and
 *        _free are required, pa_simple_get_latency and pa_strerror not.
 * @return 1, or 0 when the library or a required entry point is missing.
 */
static int
load(void)
{
    if (pulse.lib != NULL) {
        return 1;
    }
    pulse.lib = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
    if (pulse.lib == NULL) {
        return 0;
    }
    pulse.pa_new = (pa_new_fn)dlsym(pulse.lib, "pa_simple_new");
    pulse.pa_write = (pa_write_fn)dlsym(pulse.lib, "pa_simple_write");
    pulse.pa_latency = (pa_latency_fn)dlsym(pulse.lib, "pa_simple_get_latency");
    pulse.pa_free = (pa_free_fn)dlsym(pulse.lib, "pa_simple_free");
    pulse.pa_strerror = (pa_strerror_fn)dlsym(pulse.lib, "pa_strerror");
    if (pulse.pa_new == NULL || pulse.pa_write == NULL ||
        pulse.pa_free == NULL) {
        dlclose(pulse.lib);
        pulse.lib = NULL;
        return 0;
    }
    return 1;
}

/** @brief Open a mono playback stream at @p rate; with no server, one line
 *         on stderr.  @return 0, or -1. */
static int
pulse_open(void *ctx, unsigned rate)
{
    pa_sample_spec_t ss;
    int error = 0;

    (void)ctx;
    if (!load()) {
        return -1;
    }
    ss.format = PA_SAMPLE_S16LE;
    ss.rate = rate;
    ss.channels = 1;
    pulse.stream = pulse.pa_new(NULL, "TikuDesktop", PA_STREAM_PLAYBACK, NULL,
                                "system sounds", &ss, NULL, NULL, &error);
    if (pulse.stream == NULL) {
        if (!pulse.said) {
            fprintf(stderr, "sound: no server (%s); running silent\n",
                    pulse.pa_strerror != NULL ? pulse.pa_strerror(error)
                                              : "pa_simple_new failed");
            pulse.said = 1;
        }
        return -1;
    }
    return 0;
}

/** @brief CHUNK_FRAMES while the server holds up to LATENCY_FULL_US of
 *         audio, 0 while it holds more; -1 when that cannot be asked. */
static int
pulse_room(void *ctx)
{
    int error = 0;

    (void)ctx;
    if (pulse.stream == NULL || pulse.pa_latency == NULL) {
        return -1;
    }
    return (pulse.pa_latency(pulse.stream, &error) > LATENCY_FULL_US)
               ? 0 : CHUNK_FRAMES;
}

/** @brief Write @p frames samples, blocking until the server takes them.
 *  @return @p frames, or -1. */
static int
pulse_write(void *ctx, const int16_t *pcm, size_t frames)
{
    int error = 0;

    (void)ctx;
    if (pulse.stream == NULL) {
        return -1;
    }
    if (pulse.pa_write(pulse.stream, pcm, frames * sizeof *pcm, &error) < 0) {
        return -1;
    }
    return (int)frames;
}

/** @brief Free the stream. */
static void
pulse_close(void *ctx)
{
    (void)ctx;
    if (pulse.stream != NULL) {
        pulse.pa_free(pulse.stream);
        pulse.stream = NULL;
    }
}

static const tiku_sound_backend_t PULSE = {
    "pulse", pulse_open, pulse_room, pulse_write, pulse_close, NULL
};

const tiku_sound_backend_t *
tiku_sound_host_backend(void)
{
    if (!load()) {
        if (!pulse.said) {
            fprintf(stderr, "sound: no libpulse-simple; running silent\n");
            pulse.said = 1;
        }
        return NULL;
    }
    return &PULSE;
}
