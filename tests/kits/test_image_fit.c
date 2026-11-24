/*
 * TikuOS Translation Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_image_fit.c - images decoded no larger than a bound: JPEG planes
 * reduced by block means, a one-scan JPEG converted a row of MCUs at a
 * time, PNG reduced after decoding, sizes and refusals.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_image.h"
#include "tiku_jpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The apps package's image fixtures: under the package in this tree, and
 * beside the kit fixtures in a product's composed view of it. */
#if __has_include("../fixtures/image_fixtures.inc")
#include "../fixtures/image_fixtures.inc"
#else
#include "../../packages/apps/tests/fixtures/image_fixtures.inc"
#endif
#include "../fixtures/jpeg_bands.inc"

#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)

/** @brief Channel @p shift of @p p. */
static int channel(uint32_t p, int shift) { return (int)(p >> shift) & 255; }

/** @brief The luma of @p p, in thousandths of a level. */
static int luma(uint32_t p)
{
    return 299 * channel(p, 16) + 587 * channel(p, 8) + 114 * channel(p, 0);
}

/**
 * @brief The largest difference, over every pixel, between the luma of
 *        @p small and the mean luma of the @p k by @p k pixels of @p full it
 *        covers, in levels; with @p rgb, between each channel and the
 *        channel's mean instead.
 */
static int mean_error(const tiku_image_t *full, const tiku_image_t *small, int k, int rgb)
{
    int worst = 0;
    for (int y = 0; y < small->h; y++)
        for (int x = 0; x < small->w; x++)
            for (int s = 0; s <= (rgb ? 16 : 0); s += 8) {
                long sum = 0, mine = rgb ? channel(small->px[y * small->w + x], s) * 1000L :
                                           luma(small->px[y * small->w + x]);
                for (int dy = 0; dy < k; dy++)
                    for (int dx = 0; dx < k; dx++) {
                        uint32_t p = full->px[(y * k + dy) * full->w + x * k + dx];
                        sum += rgb ? channel(p, s) * 1000L : luma(p);
                    }
                int e = (int)((labs(mine - sum / (k * k)) + 500) / 1000);
                worst = e > worst ? e : worst;
            }
    return worst;
}

/** @brief @p data decoded within @p max_w by @p max_h: @p w by @p h pixels. */
static tiku_image_t fit(const unsigned char *data, size_t len, int max_w, int max_h, int w, int h)
{
    tiku_image_t im = {0};
    CHECK(tiku_image_decode_fit(data, len, max_w, max_h, &im) == TIKU_IMG_OK);
    CHECK(im.w == w && im.h == h && im.px != NULL);
    return im;
}

int main(void)
{
    int fw, fh;
    tiku_image_t full = {0}, im;

    tiku_image_fit_size(1640, 1232, 728, 340, &fw, &fh);
    CHECK(fw == 452 && fh == 340);
    tiku_image_fit_size(1640, 1232, 1000, 1000, &fw, &fh);
    CHECK(fw == 1000 && fh == 751);
    tiku_image_fit_size(640, 480, 0, 0, &fw, &fh);
    CHECK(fw == 640 && fh == 480);
    tiku_image_fit_size(4000, 1, 10, 10, &fw, &fh);
    CHECK(fw == 10 && fh == 1);

    /* No bound, or one the picture fits, is the whole decode: the reference
     * decoder's points, within the 9 levels its smoothed chroma differs by
     * from the nearest chroma sample this decoder takes. */
    CHECK(tiku_image_decode(jpeg_420, sizeof jpeg_420, &full) == TIKU_IMG_OK);
    CHECK(full.w == 16 && full.h == 16);
    for (size_t i = 0; i < sizeof jpeg_expect / sizeof jpeg_expect[0]; i++) {
        uint32_t p = full.px[jpeg_expect[i].y * 16 + jpeg_expect[i].x];
        CHECK(abs(channel(p, 16) - jpeg_expect[i].r) <= 9 && abs(channel(p, 8) - jpeg_expect[i].g) <= 9 &&
              abs(channel(p, 0) - jpeg_expect[i].b) <= 9);
    }
    im = fit(jpeg_420, sizeof jpeg_420, 0, 0, 16, 16);
    CHECK(!memcmp(im.px, full.px, 16 * 16 * 4)); tiku_image_free(&im);
    im = fit(row_grey, sizeof row_grey, 0, 0, 23, 1);
    for (int x = 0; x < 23; x++)
        CHECK(im.px[x] == (0xff000000u | row_grey_levels[x] * 0x010101u));
    tiku_image_free(&im);
    im = fit(jpeg_420, sizeof jpeg_420, 16, 40, 16, 16);
    CHECK(!memcmp(im.px, full.px, 16 * 16 * 4)); tiku_image_free(&im);

    /* Halved, quartered and an eighth: each pixel's luma the mean of the
     * block it covers.  Its colour is the mean over a block twice as wide,
     * the chroma planes being half the size: near the block's own at a half
     * and a quarter; at an eighth this 16-pixel picture has one colour. */
    static const int ks[] = {2, 4, 8}, bounds[][2] = {{8, 8}, {4, 4}, {2, 3}};
    for (int i = 0; i < 3; i++) {
        int k = ks[i];
        im = fit(jpeg_420, sizeof jpeg_420, bounds[i][0], bounds[i][1], 16 / k, 16 / k);
        printf("1/%d: luma %d, colour %d\n", k, mean_error(&full, &im, k, 0), mean_error(&full, &im, k, 1));
        CHECK(mean_error(&full, &im, k, 0) <= 2 && (k == 8 || mean_error(&full, &im, k, 1) <= 48));
        tiku_image_free(&im);
    }
    im = fit(jpeg_420, sizeof jpeg_420, 16, 8, 8, 8);
    CHECK(mean_error(&full, &im, 2, 0) <= 2); tiku_image_free(&im);
    im = fit(jpeg_420, sizeof jpeg_420, 1, 1, 1, 1); tiku_image_free(&im);

    /* Between the halvings, the plane sample under each pixel: the whole
     * decode's, unreduced. */
    im = fit(jpeg_420, sizeof jpeg_420, 12, 12, 12, 12);
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 12; x++)
            CHECK(im.px[y * 12 + x] == full.px[(y * 16 / 12) * 16 + x * 16 / 12]);
    tiku_image_free(&im);
    im = fit(jpeg_420, sizeof jpeg_420, 6, 6, 6, 6);
    tiku_image_t half = fit(jpeg_420, sizeof jpeg_420, 8, 8, 8, 8);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
            CHECK(im.px[y * 6 + x] == half.px[(y * 8 / 6) * 8 + x * 8 / 6]);
    tiku_image_free(&im);

    /* Progressive scans are reduced as they are drawn at the end. */
    im = fit(jpeg_420_progressive, sizeof jpeg_420_progressive, 8, 8, 8, 8);
    printf("progressive 1/2: luma %d\n", mean_error(&full, &im, 2, 0));
    CHECK(mean_error(&full, &im, 2, 0) <= 3);
    tiku_image_free(&im); tiku_image_free(&half);

    /* A sequential file is converted a row of MCUs at a time as its one
     * scan is read; its progressive twin from whole planes at the end.  The
     * two agree whole and at each bound, across rows of MCUs, short last
     * rows and restarts. */
    static const struct { const unsigned char *seq, *prog; size_t ns, np; } twins[] = {
        {bands_420_seq, bands_420_prog, sizeof bands_420_seq, sizeof bands_420_prog},
        {bands_grey_seq, bands_grey_prog, sizeof bands_grey_seq, sizeof bands_grey_prog},
        {bands_grey2_seq, bands_grey2_prog, sizeof bands_grey2_seq, sizeof bands_grey2_prog},
    };
    static const int twin_bounds[][2] = {{0, 0}, {30, 30}, {19, 15}, {10, 10}, {5, 4}, {36, 3}};
    for (size_t t = 0; t < sizeof twins / sizeof twins[0]; t++)
        for (size_t b = 0; b < sizeof twin_bounds / sizeof twin_bounds[0]; b++) {
            tiku_image_t seq = {0}, prog = {0};
            int side = t == 2 ? 21 : 37, w, h;
            tiku_image_fit_size(side, t == 2 ? 21 : 29, twin_bounds[b][0], twin_bounds[b][1], &w, &h);
            seq = fit(twins[t].seq, twins[t].ns, twin_bounds[b][0], twin_bounds[b][1], w, h);
            prog = fit(twins[t].prog, twins[t].np, twin_bounds[b][0], twin_bounds[b][1], w, h);
            CHECK(!memcmp(seq.px, prog.px, (size_t)w * h * 4));
            tiku_image_free(&seq); tiku_image_free(&prog);
        }
    for (size_t n = 1; n < sizeof bands_420_seq; n++)
        CHECK(tiku_image_decode_fit(bands_420_seq, n, 10, 10, &im) != TIKU_IMG_OK && im.px == NULL);

    /* A PNG is decoded whole and each kept pixel copies one. */
    tiku_image_t png = {0};
    CHECK(tiku_image_decode(png_palette, sizeof png_palette, &png) == TIKU_IMG_OK && png.w == 8 && png.h == 4);
    im = fit(png_palette, sizeof png_palette, 4, 4, 4, 2);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 4; x++)
            CHECK(im.px[y * 4 + x] == png.px[(y * 2) * 8 + x * 2]);
    tiku_image_free(&im); tiku_image_free(&png);

    /* A cut file is refused at any bound, and the image kept. */
    im = (tiku_image_t){0};
    for (size_t n = 1; n < sizeof jpeg_420; n++)
        CHECK(tiku_image_decode_fit(jpeg_420, n, 4, 4, &im) != TIKU_IMG_OK && im.px == NULL);
    tiku_image_free(&full);
    puts("PASS image fit: decodes within a bound keep the aspect, reduce JPEG blocks by their means");
    return 0;
}
