/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_wav.c - the RIFF walk and the resample behind tiku_wav_decode(),
 * and tiku_wav_read() giving it a file's bytes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned channels, rate, bits;
} format_t;

/** @brief A little-endian field of @p n bytes at @p p. */
static uint32_t
le(const unsigned char *p, int n)
{
    uint32_t v = 0;
    int i;

    for (i = n - 1; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}

/** @brief The most bytes of a WAV file tiku_wav_read() reads. */
#define WAV_FILE_MAX ((long)64 << 20)

/**
 * @brief Walk the @p len bytes @p b for their "fmt " and "data" chunks.
 * @return the data chunk within @p b, its length in @p bytes (cut to what
 *         @p b holds), or NULL.
 */
static const unsigned char *
chunks(const unsigned char *b, size_t len, format_t *fmt, size_t *bytes)
{
    size_t at = 12;

    if (len < 12 || memcmp(b, "RIFF", 4) != 0 ||
        memcmp(b + 8, "WAVE", 4) != 0) {
        return NULL;
    }
    memset(fmt, 0, sizeof *fmt);
    while (len - at >= 8) {
        uint32_t size = le(b + at + 4, 4);
        const unsigned char *body = b + at + 8;
        size_t left = len - at - 8;

        if (memcmp(b + at, "fmt ", 4) == 0) {
            if (size < 16 || left < 16 || le(body, 2) != 1) {
                return NULL;               /* not plain PCM */
            }
            fmt->channels = le(body + 2, 2);
            fmt->rate = le(body + 4, 4);
            fmt->bits = le(body + 14, 2);
        } else if (memcmp(b + at, "data", 4) == 0) {
            if (fmt->rate == 0 || size == 0) {
                return NULL;
            }
            *bytes = size < left ? size : left;
            return body;
        }
        if ((size_t)size + (size & 1) > left) {
            return NULL;
        }
        at += 8 + (size_t)size + (size & 1);
    }
    return NULL;
}

/** @brief Frame @p i of @p data as one signed 16-bit value, averaged. */
static int16_t
frame(const unsigned char *data, const format_t *fmt, size_t i)
{
    long sum = 0;
    unsigned c;
    size_t width = fmt->bits / 8;

    for (c = 0; c < fmt->channels; c++) {
        const unsigned char *p = data + (i * fmt->channels + c) * width;

        sum += (fmt->bits == 8) ? ((int)p[0] - 128) * 256
                                : (int16_t)le(p, 2);
    }
    return (int16_t)(sum / (long)fmt->channels);
}

int16_t *
tiku_wav_decode(const void *wav, size_t len, unsigned rate, unsigned max_s,
                size_t *frames)
{
    format_t fmt;
    const unsigned char *data;
    int16_t *out;
    size_t bytes = 0, in, n, i;

    if (frames != NULL) {
        *frames = 0;
    }
    if (wav == NULL || rate == 0 || frames == NULL ||
        (data = chunks(wav, len, &fmt, &bytes)) == NULL ||
        (fmt.bits != 8 && fmt.bits != 16) || fmt.channels < 1 ||
        fmt.channels > 2) {
        return NULL;
    }
    in = bytes / (fmt.channels * (fmt.bits / 8));
    n = (size_t)((double)in * rate / fmt.rate);
    if (n > (size_t)rate * max_s) {
        n = (size_t)rate * max_s;
    }
    out = (n > 0) ? malloc(n * sizeof *out) : NULL;
    if (out == NULL) {
        return NULL;
    }
    /* Each output frame takes the nearest source frame, unfiltered. */
    for (i = 0; i < n; i++) {
        size_t src = (size_t)((double)i * fmt.rate / rate);

        out[i] = frame(data, &fmt, src < in ? src : in - 1);
    }
    *frames = n;
    return out;
}

int16_t *
tiku_wav_read(const char *path, unsigned rate, unsigned max_s, size_t *frames)
{
    FILE *f = path != NULL ? fopen(path, "rb") : NULL;
    unsigned char *b = NULL;
    int16_t *out = NULL;
    long len;

    if (frames != NULL) {
        *frames = 0;
    }
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 &&
        len <= WAV_FILE_MAX && fseek(f, 0, SEEK_SET) == 0 &&
        (b = malloc((size_t)len)) != NULL &&
        fread(b, 1, (size_t)len, f) == (size_t)len) {
        out = tiku_wav_decode(b, (size_t)len, rate, max_s, frames);
    }
    free(b);
    fclose(f);
    return out;
}
