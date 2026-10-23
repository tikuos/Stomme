/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_score.c - the score reader and the oscillator behind it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_score.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WAVE_SINE, WAVE_TRIANGLE, WAVE_SQUARE };

typedef struct {
    int    wave;
    double attack_s, release_s;
} voice_t;

/** @brief One waveform's value at @p phase in [0, 1), in [-1, 1]. */
static double
sample(int wave, double phase)
{
    switch (wave) {
    case WAVE_TRIANGLE:
        return (phase < 0.5) ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
    case WAVE_SQUARE:
        /* Softened: a hard edge at 44.1 kHz aliases into a buzz. */
        return (phase < 0.5) ? 0.8 : -0.8;
    default:
        return sin(2.0 * 3.14159265358979 * phase);
    }
}

/**
 * @brief Walk the score, writing each note and rest into @p out, at most
 *        @p max frames; with @p out NULL nothing is written.
 * @return The frames the score spans, at most TIKU_SCORE_MAX_S of them, or
 *         0 for a score with no notes or a line it cannot read.
 */
static size_t
walk(const char *text, unsigned rate, int16_t *out, size_t max)
{
    voice_t v = { WAVE_SINE, 0.005, 0.030 };
    size_t at = 0;
    size_t limit = (size_t)rate * TIKU_SCORE_MAX_S;
    const char *p = text;
    int notes = 0;

    if (max > limit) {
        max = limit;
    }
    while (*p != '\0') {
        char word[16];
        double a = 0, b = 0, c = 0;
        int n;
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[96];

        if (len >= sizeof line) {
            return 0;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        p += len + (eol ? 1 : 0);
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        n = sscanf(line, "%15s %lf %lf %lf", word, &a, &b, &c);
        if (n < 1) {
            continue;
        }
        if (strcmp(word, "wave") == 0) {
            char name[16];

            if (sscanf(line, "%*s %15s", name) != 1) {
                return 0;
            }
            v.wave = strcmp(name, "triangle") == 0 ? WAVE_TRIANGLE
                   : strcmp(name, "square") == 0   ? WAVE_SQUARE
                   : strcmp(name, "sine") == 0     ? WAVE_SINE : -1;
            if (v.wave < 0) {
                return 0;
            }
        } else if (strcmp(word, "attack") == 0 && n >= 2) {
            v.attack_s = a / 1000.0;
        } else if (strcmp(word, "release") == 0 && n >= 2) {
            v.release_s = a / 1000.0;
        } else if (strcmp(word, "rest") == 0 && n >= 2) {
            size_t frames = (size_t)(a / 1000.0 * rate);
            size_t i;

            for (i = 0; i < frames && out != NULL && at + i < max; i++) {
                out[at + i] = 0;
            }
            at += frames;
        } else if (strcmp(word, "note") == 0 && n >= 4) {
            size_t frames = (size_t)(b / 1000.0 * rate);
            size_t i;
            double attack = v.attack_s * rate, release = v.release_s * rate;

            if (c < 0.0) { c = 0.0; }
            if (c > 1.0) { c = 1.0; }
            notes++;
            for (i = 0; i < frames && out != NULL && at + i < max; i++) {
                double env = 1.0;
                double phase = fmod(a * (double)i / rate, 1.0);

                if (attack > 0 && (double)i < attack) {
                    env = (double)i / attack;
                }
                if (release > 0 && (double)(frames - 1 - i) < release) {
                    env = (double)(frames - 1 - i) / release;
                    if (attack > 0 && (double)i < attack &&
                        (double)i / attack < env) {
                        env = (double)i / attack;
                    }
                }
                out[at + i] = (int16_t)(32767.0 * c * env *
                                        sample(v.wave, phase));
            }
            at += frames;
        } else {
            return 0;
        }
    }
    if (notes == 0) {
        return 0;
    }
    return at > limit ? limit : at;
}

size_t
tiku_score_frames(const char *text, unsigned rate)
{
    return (text != NULL && rate > 0) ? walk(text, rate, NULL, 0) : 0;
}

size_t
tiku_score_render(const char *text, unsigned rate, int16_t *out, size_t max)
{
    size_t n;

    if (text == NULL || out == NULL || rate == 0 || max == 0) {
        return 0;
    }
    n = walk(text, rate, out, max);
    return n > max ? max : n;
}
