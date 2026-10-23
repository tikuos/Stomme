/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_score.c - the score reader, and the eight sounds as written.
 *
 * A score renders to the length its notes add up to, starts and ends at
 * silence because of the envelope, never exceeds the amplitude it asked
 * for, and refuses a line it cannot read rather than guessing.  Then
 * every baked system sound is rendered: each within its bound, none
 * clipping, and the chime the longest of them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_score.h"
#include "tiku_sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

static int16_t
peak_of(const int16_t *pcm, size_t n)
{
    int16_t peak = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        int16_t v = (int16_t)(pcm[i] < 0 ? -pcm[i] : pcm[i]);

        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

int
main(void)
{
    static int16_t buf[TIKU_SOUND_RATE * TIKU_SCORE_MAX_S];
    const char *one = "wave sine\nattack 10\nrelease 10\nnote 440 100 0.5\n";
    size_t n;
    int i, rendered;

    printf("the reader\n");
    n = tiku_score_frames(one, TIKU_SOUND_RATE);
    check(n == 4410u, "a 100 ms note at 44.1 kHz is 4410 frames");
    n = tiku_score_render(one, TIKU_SOUND_RATE, buf, sizeof buf / sizeof buf[0]);
    check(n == 4410u, "and renders to the same count");
    check(buf[0] == 0 && buf[n - 1] == 0,
          "the envelope starts and ends the note at silence");
    check(peak_of(buf, n) <= 16384 && peak_of(buf, n) > 15000,
          "the peak is the amplitude asked for and no more");
    check(tiku_score_frames("wave sine\nnote 440 100 0.5\nrest 50\n",
                            TIKU_SOUND_RATE) == 6615u,
          "a rest counts toward the length");
    check(tiku_score_frames("wave saw\nnote 440 100 0.5\n", TIKU_SOUND_RATE) == 0u,
          "a waveform it does not know refuses the score");
    check(tiku_score_frames("hum 440 100 0.5\n", TIKU_SOUND_RATE) == 0u,
          "so does a verb it does not know");
    check(tiku_score_frames("wave sine\n# nothing\n", TIKU_SOUND_RATE) == 0u,
          "and a score with no note is no score");
    check(tiku_score_frames("wave sine\nnote 440 40000 0.5\n", TIKU_SOUND_RATE)
              == TIKU_SOUND_RATE * TIKU_SCORE_MAX_S,
          "a score longer than the bound is cut at it");

    printf("the eight sounds\n");
    tiku_sound_init(NULL);
    rendered = tiku_sound_load_scores();
    check(rendered == TIKU_SOUND_EVENTS, "every event has a score that renders");
    for (i = 0; i < TIKU_SOUND_EVENTS; i++) {
        const char *text = tiku_sound_score((tiku_sound_event_t)i);
        char what[96];

        n = text ? tiku_score_render(text, TIKU_SOUND_RATE, buf,
                                     sizeof buf / sizeof buf[0]) : 0;
        snprintf(what, sizeof what, "%s: %u ms, peak %d, silent at both ends",
                 tiku_sound_name((tiku_sound_event_t)i),
                 (unsigned)(n * 1000 / TIKU_SOUND_RATE), peak_of(buf, n));
        check(n > 0 && n <= 2u * TIKU_SOUND_RATE && peak_of(buf, n) < 32000 &&
              buf[0] == 0 && buf[n - 1] == 0, what);
    }
    check(tiku_score_frames(tiku_sound_score(TIKU_SOUND_STARTUP), TIKU_SOUND_RATE) >
              tiku_score_frames(tiku_sound_score(TIKU_SOUND_WINDOW_OPEN), TIKU_SOUND_RATE),
          "the chime is longer than a tick");
    tiku_sound_free();
    printf("%s\n", fails ? "FAILURES" : "the scores read and render");
    return fails ? 1 : 0;
}
