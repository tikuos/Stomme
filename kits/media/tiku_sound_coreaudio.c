/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_sound_coreaudio.c - the host backend over CoreAudio's audio queue.
 *
 * The CoreAudio counterpart of tiku_sound_pulse.c, behind the same backend
 * struct; the Makefile compiles one of the two.  An audio queue plays the
 * buffers it is given and hands each back on a thread of its own, so the
 * backend is three buffers and which of them are free.  Opening an output
 * can fail on a machine with no device or session: one line on stderr,
 * then silence.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_sound.h"

#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define BUFFERS       3             /* one with the card, one going, one free */
#define CHUNK_FRAMES  2205          /* fifty milliseconds at 44.1 kHz */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static struct {
    AudioQueueRef       queue;
    AudioQueueBufferRef buffer[BUFFERS];
    /* Which buffers are free to fill.  The card hands one back on a
     * thread of its own, so this is guarded by lock. */
    int                 mine[BUFFERS];
    int                 said;       /* the one line on stderr, once */
} audio;

/** @brief Print one line on stderr, the first time only. */
static void
say_once(const char *what)
{
    if (!audio.said) {
        fprintf(stderr, "sound: %s; running silent\n", what);
        audio.said = 1;
    }
}

/** @brief The card has finished with a buffer: it is free again. */
static void
played(void *ctx, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    int i;

    (void)ctx;
    (void)queue;
    (void)pthread_mutex_lock(&lock);
    for (i = 0; i < BUFFERS; i++) {
        if (audio.buffer[i] == buffer) {
            audio.mine[i] = 1;
            break;
        }
    }
    (void)pthread_mutex_unlock(&lock);
}

/** @brief Let go of the queue and its buffers, however far open() got. */
static void
tear_down(void)
{
    if (audio.queue != NULL) {
        (void)AudioQueueDispose(audio.queue, true);
    }
    audio.queue = NULL;
    memset(audio.buffer, 0, sizeof audio.buffer);
    memset(audio.mine, 0, sizeof audio.mine);
}

/**
 * @brief Open an output queue at @p rate with three buffers, and start it
 *        primed with silence.
 * @return 0, or -1 with one line on stderr.
 */
static int
coreaudio_open(void *ctx, unsigned rate)
{
    AudioStreamBasicDescription fmt;
    int i;

    (void)ctx;
    if (audio.queue != NULL) {
        return 0;
    }
    memset(&fmt, 0, sizeof fmt);
    fmt.mSampleRate       = (Float64)rate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger |
                            kLinearPCMFormatFlagIsPacked;
    fmt.mFramesPerPacket  = 1;
    fmt.mChannelsPerFrame = 1;              /* mono, as the kit renders */
    fmt.mBitsPerChannel   = 16;
    fmt.mBytesPerFrame    = sizeof(int16_t);
    fmt.mBytesPerPacket   = sizeof(int16_t);
    if (AudioQueueNewOutput(&fmt, played, NULL, NULL, NULL, 0,
                            &audio.queue) != noErr) {
        audio.queue = NULL;
        say_once("no output device");
        return -1;
    }
    for (i = 0; i < BUFFERS; i++) {
        if (AudioQueueAllocateBuffer(audio.queue,
                                     CHUNK_FRAMES * sizeof(int16_t),
                                     &audio.buffer[i]) != noErr) {
            tear_down();
            say_once("no room for a buffer");
            return -1;
        }
        audio.mine[i] = 1;
    }
    /*
     * Primed with one buffer of silence before it starts: a queue started
     * empty waits for an IO cycle that never comes.  The first clip starts
     * fifty milliseconds late.
     */
    audio.buffer[0]->mAudioDataByteSize = CHUNK_FRAMES * sizeof(int16_t);
    memset(audio.buffer[0]->mAudioData, 0,
           CHUNK_FRAMES * sizeof(int16_t));
    if (AudioQueueEnqueueBuffer(audio.queue, audio.buffer[0], 0,
                                NULL) != noErr) {
        tear_down();
        say_once("the output would take nothing");
        return -1;
    }
    audio.mine[0] = 0;
    /*
     * Starting a device can take a moment, and on a machine that will not
     * start one this does not return.  A shell with workers runs open() on
     * one (tiku_sound_set_opener()).
     */
    if (AudioQueueStart(audio.queue, NULL) != noErr) {
        tear_down();
        say_once("the output would not start");
        return -1;
    }
    return 0;
}

/** @brief A chunk for every free buffer; -1 when the queue is not open. */
static int
coreaudio_room(void *ctx)
{
    int i, n = 0;

    (void)ctx;
    if (audio.queue == NULL) {
        return -1;
    }
    (void)pthread_mutex_lock(&lock);
    for (i = 0; i < BUFFERS; i++) {
        n += audio.mine[i] ? 1 : 0;
    }
    (void)pthread_mutex_unlock(&lock);
    return n * CHUNK_FRAMES;
}

/** @brief Fill a free buffer with up to one chunk and queue it.
 *  @return Frames taken, 0 when every buffer is with the card, or -1. */
static int
coreaudio_write(void *ctx, const int16_t *pcm, size_t frames)
{
    int i, slot = -1;
    size_t bytes;

    (void)ctx;
    if (audio.queue == NULL) {
        return -1;
    }
    if (frames > (size_t)CHUNK_FRAMES) {
        frames = (size_t)CHUNK_FRAMES;
    }
    (void)pthread_mutex_lock(&lock);
    for (i = 0; i < BUFFERS; i++) {
        if (audio.mine[i]) {
            audio.mine[i] = 0;
            slot = i;
            break;
        }
    }
    (void)pthread_mutex_unlock(&lock);
    if (slot < 0) {
        return 0;               /* every one is with the card; next turn */
    }
    bytes = frames * sizeof *pcm;
    memcpy(audio.buffer[slot]->mAudioData, pcm, bytes);
    audio.buffer[slot]->mAudioDataByteSize = (UInt32)bytes;
    if (AudioQueueEnqueueBuffer(audio.queue, audio.buffer[slot], 0,
                                NULL) != noErr) {
        (void)pthread_mutex_lock(&lock);
        audio.mine[slot] = 1;
        (void)pthread_mutex_unlock(&lock);
        return -1;
    }
    return (int)frames;
}

/** @brief Stop the queue and free it. */
static void
coreaudio_close(void *ctx)
{
    (void)ctx;
    if (audio.queue == NULL) {
        return;
    }
    /* Stopped at once, not played out: the kit closes only after two
     * seconds with nothing written. */
    (void)AudioQueueStop(audio.queue, true);
    tear_down();
}

static const tiku_sound_backend_t COREAUDIO = {
    "coreaudio", coreaudio_open, coreaudio_room, coreaudio_write,
    coreaudio_close, NULL
};

const tiku_sound_backend_t *
tiku_sound_host_backend(void)
{
    return &COREAUDIO;
}
