/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richmarkup_image.c - bounded embedded PNG import without resource I/O.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richmarkup_private.h"
#include "tiku_image.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/** @brief Decode one base64 alphabet character; padding is handled separately. */
static int
digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

/** @brief Decode padded base64 with whitespace, rejecting trailing or pad bits. */
static unsigned char *
base64(tiku_rimp_t *r, const char *text, size_t *length, size_t *capacity)
{
    size_t n = strlen(text), at = 0;
    unsigned char *out;
    int group[4], count = 0, ended = 0;

    *capacity = n / 4 * 3 + 3;
    if (*capacity > TIKU_IMAGE_BYTES_MAX) {
        r->status = TIKU_RICHIO_LIMIT;
        return NULL;
    }
    out = tiku_rimp_allocate(r, *capacity);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        if (!(i % 4096) && !tiku_rimp_active(r)) break;
        if (strchr(" \t\r\n\f", text[i])) continue;
        if (ended) { r->status = TIKU_RICHIO_DAMAGED; break; }
        group[count++] = text[i] == '=' ? -2 : digit((unsigned char)text[i]);
        if (count != 4) continue;
        if (group[0] < 0 || group[1] < 0 || group[2] == -1 || group[3] == -1 ||
            (group[2] == -2 && (group[3] != -2 || (group[1] & 15))) ||
            (group[2] >= 0 && group[3] == -2 && (group[2] & 3))) {
            r->status = TIKU_RICHIO_DAMAGED;
            break;
        }
        out[at++] = (unsigned char)((group[0] << 2) | (group[1] >> 4));
        if (group[2] >= 0) {
            out[at++] = (unsigned char)((group[1] << 4) | (group[2] >> 2));
            if (group[3] >= 0)
                out[at++] = (unsigned char)((group[2] << 6) | group[3]);
        }
        ended = group[3] == -2;
        count = 0;
    }
    if (r->status == TIKU_RICHIO_OK && (count || !at))
        r->status = TIKU_RICHIO_DAMAGED;
    if (!tiku_rimp_active(r)) {
        free(out); r->used -= *capacity;
        return NULL;
    }
    *length = at;
    return out;
}

/** @brief Read an unsigned big-endian PNG field from four available bytes. */
static uint32_t
big32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

/** @brief Check dimensions and reserve enough headroom for PNG decoder peaks.
 *  @note The bound covers two IDAT copies, inflated rows, ARGB pixels and
 *        two scanlines. Unsupported transparency must not become opaque. */
static int
png_budget(tiku_rimp_t *r, const unsigned char *data, size_t length)
{
    static const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    uint32_t width, height;
    size_t peak;

    if (length < 33 || memcmp(data, signature, sizeof signature) ||
        big32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4)) {
        r->status = TIKU_RICHIO_DAMAGED;
        return 0;
    }
    width = big32(data + 16); height = big32(data + 20);
    if (!width || !height || width > 30000 || height > 30000 ||
        width > TIKU_RICH_IMAGE_PIXELS_MAX / height) {
        r->status = TIKU_RICHIO_LIMIT;
        return 0;
    }
    for (size_t at = 8; at < length;) {
        if (!tiku_rimp_active(r)) return 0;
        if (length - at < 12 || big32(data + at) > length - at - 12) {
            r->status = TIKU_RICHIO_DAMAGED;
            return 0;
        }
        if (!memcmp(data + at + 4, "tRNS", 4) && data[25] != 3) return 0;
        at += 12 + (size_t)big32(data + at);
    }
    peak = length * 2 + (size_t)width * height * 12 +
           (size_t)height * 12 + (size_t)width * 8 + 128;
    if (peak > r->options.memory_bytes - r->used) {
        r->status = TIKU_RICHIO_LIMIT;
        return 0;
    }
    return 1;
}

/** @brief Convert an unsigned HTML pixel dimension to document points. */
static float
dimension(tiku_rimp_t *r, const tiku_rm_tag_t *tag, const char *name)
{
    const tiku_rm_attr_t *a = tiku_rm_attr(tag, name);
    unsigned value = 0;

    if (!a) return 0;
    if (!a->length || a->length > 5) goto unsupported;
    for (size_t i = 0; i < a->length; i++) {
        unsigned d = (unsigned char)a->value[i] - '0';

        if (d > 9) goto unsupported;
        value = value * 10 + d;
    }
    if (value >= 2 && value <= 19200) return (float)(value * .75);
unsupported:
    r->notices |= TIKU_RICHMARKUP_STYLE;
    return 0;
}

int
tiku_rm_picture(tiku_rimp_t *r, const tiku_rm_tag_t *tag,
                const tiku_rm_style_t *style, const char *alt)
{
    const tiku_rm_attr_t *src = tiku_rm_attr(tag, "src");
    char *url;
    unsigned char *png;
    size_t length = 0, capacity = 0;
    tiku_image_t decoded = {0};
    tiku_rich_image_t image = {0};
    tiku_image_err_t status;
    int result = 0;

    if (!src || src->length < 22 || strlen(alt) > 1023 ||
        strncasecmp(src->value, "data:image/png;base64,", 22)) return 0;
    url = tiku_rm_decode(r, src->value, src->length);
    if (!url) return 0;
    png = base64(r, url + 22, &length, &capacity);
    if (!png) return 0;
    if (png_budget(r, png, length)) {
        status = tiku_image_decode(png, length, &decoded);
        if (status == TIKU_IMG_NO_MEMORY) r->status = TIKU_RICHIO_NO_MEMORY;
        else if (status != TIKU_IMG_OK && status != TIKU_IMG_UNSUPPORTED)
            r->status = TIKU_RICHIO_DAMAGED;
    }
    free(png); r->used -= capacity;
    if (!decoded.px || !tiku_rimp_active(r)) goto done;
    image = (tiku_rich_image_t){(unsigned)decoded.w, (unsigned)decoded.h,
        decoded.px, dimension(r, tag, "width"),
        dimension(r, tag, "height"), alt};
    if (style->image_width) image.width = style->image_width;
    if (style->image_height) image.height = style->image_height;
    if (!image.width && !image.height) {
        image.width = (float)fmax(1, decoded.w * .75);
        image.height = (float)fmax(1, decoded.h * .75);
        if (decoded.w == 1 || decoded.h == 1)
            r->notices |= TIKU_RICHMARKUP_STYLE;
    } else if (!image.width) image.width = image.height * decoded.w / decoded.h;
    else if (!image.height) image.height = image.width * decoded.h / decoded.w;
    if (tiku_rich_image_validate(&image) != TIKU_RICH_OK) {
        r->status = TIKU_RICHIO_LIMIT;
        goto done;
    }
    result = tiku_rimp_picture(r, &image, &style->chars, style->link);
    if (result) decoded.px = NULL;
done:
    tiku_image_free(&decoded);
    return result;
}
