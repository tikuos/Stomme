/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_writer_image.c - pictures written into the file.
 *
 * A JPEG is already what PDF's DCTDecode reads, so it goes in byte for
 * byte, its size and colours read from its frame header.  Anything else
 * the picture reader knows is decoded to pixels and goes in deflated,
 * with its alpha as a soft mask beside it when any pixel is not opaque.
 * Each picture is written once, when given, and drawn as often as asked.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_pdf_writer_private.h"
#include "tiku_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief What a JPEG's headers say: its size, its colours, whether an
 *         Adobe marker says its CMYK is stored inverted. */
typedef struct {
    int w, h, comps, adobe;
} jpeg_t;

/** @brief Whether @p b is a JPEG this can pass through: its frame header
 *         read into @p j, 8-bit, one, three or four colours. */
static int
jpeg_info(const unsigned char *b, size_t n, jpeg_t *j)
{
    size_t p = 2;

    memset(j, 0, sizeof *j);
    if (n < 4 || b[0] != 0xff || b[1] != 0xd8) {
        return 0;
    }
    while (p + 4 <= n) {
        unsigned m, len;

        if (b[p] != 0xff) {
            return 0;
        }
        m = b[p + 1];
        if (m == 0xff) {
            p++;                    /* fill bytes before a marker */
            continue;
        }
        len = (unsigned)b[p + 2] << 8 | b[p + 3];
        if (m == 0xda || p + 2 + len > n) {
            break;
        }
        if (m == 0xee && len >= 12 && memcmp(b + p + 4, "Adobe", 5) == 0) {
            j->adobe = 1;
        }
        if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc &&
            len >= 8) {
            j->h = b[p + 5] << 8 | b[p + 6];
            j->w = b[p + 7] << 8 | b[p + 8];
            j->comps = b[p + 9];
            if (b[p + 4] != 8) {
                return 0;
            }
        }
        p += 2 + len;
    }
    return j->w > 0 && j->h > 0 &&
           (j->comps == 1 || j->comps == 3 || j->comps == 4);
}

/** @brief A picture's name, for the object @p obj written for it.
 *  @return its number, or -1. */
static int
image_res(tiku_pdf_writer_t *w, int obj)
{
    return pw_resource(w, 'I', w->nimage++, obj);
}

int
tiku_pdf_writer_image_sampled(tiku_pdf_writer_t *w, const void *bytes,
                              size_t len, int *pw, int *ph, int interpolate)
{
    tiku_image_t im;
    jpeg_t j;
    char dict[200];
    int obj, r;

    if (w == NULL || bytes == NULL || len == 0 ||
        (interpolate != 0 && interpolate != 1)) {
        return -1;
    }
    if (jpeg_info(bytes, len, &j)) {
        if ((obj = pw_reserve(w)) == 0 || (r = image_res(w, obj)) < 0) {
            return -1;
        }
        snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d "
                 "/Height %d /ColorSpace /Device%s /BitsPerComponent 8 "
                 "/Filter /DCTDecode %s", j.w, j.h, j.comps == 1 ? "Gray" :
                 j.comps == 3 ? "RGB" : "CMYK", j.comps == 4 && j.adobe ?
                 "/Decode [1 0 1 0 1 0 1 0] " : "");
        if (interpolate)
            strcat(dict, "/Interpolate true ");
        pw_stream(w, obj, dict, bytes, len, 0);
        if (pw != NULL) {
            *pw = j.w;
        }
        if (ph != NULL) {
            *ph = j.h;
        }
        return r;
    }
    if (tiku_image_decode(bytes, len, &im) != TIKU_IMG_OK) {
        return -1;
    }
    r = tiku_pdf_writer_pixels_sampled(w, im.px, im.w, im.h, interpolate);
    if (pw != NULL) {
        *pw = im.w;
    }
    if (ph != NULL) {
        *ph = im.h;
    }
    tiku_image_free(&im);
    return r;
}

int
tiku_pdf_writer_pixels_sampled(tiku_pdf_writer_t *w, const uint32_t *px,
                               int width, int height, int interpolate)
{
    size_t n = (size_t)width * (size_t)height, i;
    unsigned char *rgb, *a;
    char dict[200];
    int obj, mask = 0, alpha = 0;

    if (w == NULL || px == NULL || width <= 0 || height <= 0 ||
        (interpolate != 0 && interpolate != 1) || n > 64u << 20 ||
        (rgb = malloc(n * 3)) == NULL) {
        return -1;
    }
    if ((a = malloc(n)) == NULL) {
        free(rgb);
        return -1;
    }
    for (i = 0; i < n; i++) {
        rgb[3 * i] = (unsigned char)(px[i] >> 16);
        rgb[3 * i + 1] = (unsigned char)(px[i] >> 8);
        rgb[3 * i + 2] = (unsigned char)px[i];
        a[i] = (unsigned char)(px[i] >> 24);
        alpha |= a[i] != 0xff;
    }
    if (alpha && (mask = pw_reserve(w)) != 0) {
        snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d "
                 "/Height %d /ColorSpace /DeviceGray /BitsPerComponent 8 ",
                 width, height);
        pw_stream(w, mask, dict, a, n, 1);
    }
    obj = pw_reserve(w);
    snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d "
             "/Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 ", width,
             height);
    if (mask) {
        snprintf(dict + strlen(dict), sizeof dict - strlen(dict),
                 "/SMask %d 0 R ", mask);
    }
    if (interpolate)
        strcat(dict, "/Interpolate true ");
    if (obj != 0) {
        pw_stream(w, obj, dict, rgb, n * 3, 1);
    }
    free(rgb);
    free(a);
    return obj != 0 ? image_res(w, obj) : -1;
}

int
tiku_pdf_writer_image(tiku_pdf_writer_t *writer, const void *bytes,
                      size_t length, int *width, int *height)
{
    return tiku_pdf_writer_image_sampled(writer, bytes, length, width, height,
                                         0);
}
int
tiku_pdf_writer_pixels(tiku_pdf_writer_t *writer, const uint32_t *pixels,
                       int width, int height)
{
    return tiku_pdf_writer_pixels_sampled(writer, pixels, width, height, 0);
}
