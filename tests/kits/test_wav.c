/*
 * KintsugiTracker v0.01 -- the file manager for KintsugiOS.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * test_wav.c - a WAV file becomes a clip at the kit's rate.
 *
 * A stereo 16-bit file at half the rate reads back mono at twice the
 * frames with the channels averaged; an 8-bit file is scaled up; a
 * file that is not WAV, or not PCM, is refused rather than guessed at;
 * bytes in memory decode as the file reads, cut short anywhere safely.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Write a little-endian field of @p n bytes. */
static void
put(FILE *f, uint32_t v, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        fputc((int)((v >> (8 * i)) & 0xffu), f);
    }
}

/** @brief Write a PCM WAV of @p frames of @p body at @p path. */
static void
wav(const char *path, unsigned channels, unsigned rate, unsigned bits,
    const unsigned char *body, size_t bytes, unsigned format)
{
    FILE *f = fopen(path, "wb");

    fputs("RIFF", f);
    put(f, (uint32_t)(36 + bytes), 4);
    fputs("WAVEfmt ", f);
    put(f, 16, 4);
    put(f, format, 2);
    put(f, channels, 2);
    put(f, rate, 4);
    put(f, rate * channels * bits / 8, 4);
    put(f, channels * bits / 8, 2);
    put(f, bits, 2);
    fputs("data", f);
    put(f, (uint32_t)bytes, 4);
    fwrite(body, 1, bytes, f);
    fclose(f);
}

int
main(void)
{
    char path[128];
    /* Two stereo frames: (1000, 3000) and (-2000, -4000). */
    static const unsigned char stereo[] = { 0xe8, 0x03, 0xb8, 0x0b,
                                            0x30, 0xf8, 0x60, 0xf0 };
    static const unsigned char eight[] = { 128, 255, 0 };
    int16_t *pcm;
    size_t n = 0;

    snprintf(path, sizeof path, "/tmp/trk_wav_%d.wav", (int)getpid());
    printf("a stereo file at half the rate\n");
    wav(path, 2, 22050, 16, stereo, sizeof stereo, 1);
    pcm = tiku_wav_read(path, 44100, 3, &n);
    check(pcm != NULL && n == 4, "two frames at 22050 are four at 44100");
    check(pcm != NULL && pcm[0] == 2000 && pcm[1] == 2000 && pcm[2] == -3000,
          "each an average of the two channels, held across the step");
    free(pcm);

    printf("an 8-bit file\n");
    wav(path, 1, 44100, 8, eight, sizeof eight, 1);
    pcm = tiku_wav_read(path, 44100, 3, &n);
    check(pcm != NULL && n == 3 && pcm[0] == 0 && pcm[1] == 32512 &&
              pcm[2] == -32768,
          "unsigned bytes become signed 16-bit around silence");
    free(pcm);

    printf("bytes in memory\n");
    wav(path, 2, 22050, 16, stereo, sizeof stereo, 1);
    {
        unsigned char b[128];
        FILE *f = fopen(path, "rb");
        size_t len = f != NULL ? fread(b, 1, sizeof b, f) : 0;
        size_t cut;

        if (f != NULL) {
            fclose(f);
        }
        pcm = tiku_wav_decode(b, len, 44100, 3, &n);
        check(pcm != NULL && n == 4 && pcm[0] == 2000 && pcm[2] == -3000,
              "decoded as the file reads");
        free(pcm);
        pcm = tiku_wav_decode(b, len - 4, 44100, 3, &n);
        check(pcm != NULL && n == 2, "its data cut short: what is there");
        free(pcm);
        for (cut = 0; cut < 44; cut++) {
            if ((pcm = tiku_wav_decode(b, cut, 44100, 3, &n)) != NULL) {
                break;
            }
        }
        check(cut == 44, "its header cut anywhere: refused");
        free(pcm);
        b[16] = 200;                    /* the fmt chunk past the bytes */
        check(tiku_wav_decode(b, len, 44100, 3, &n) == NULL,
              "a chunk past the end: refused");
    }

    printf("refusals\n");
    wav(path, 1, 44100, 16, stereo, sizeof stereo, 3);
    pcm = tiku_wav_read(path, 44100, 3, &n);
    check(pcm == NULL && n == 0, "a float file is not PCM and is refused");
    {
        FILE *f = fopen(path, "wb");

        fputs("wave sine\nnote 440 100 0.5\n", f);
        fclose(f);
    }
    pcm = tiku_wav_read(path, 44100, 3, &n);
    check(pcm == NULL, "so is a file that is not RIFF at all");
    check(tiku_wav_read("/nonexistent/x.wav", 44100, 3, &n) == NULL,
          "and one that is not there");
    unlink(path);
    printf("%s\n", fails ? "FAILURES" : "a WAV becomes a clip");
    return fails ? 1 : 0;
}
