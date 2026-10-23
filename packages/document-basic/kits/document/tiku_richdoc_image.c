/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_image.c - retained inline images and shared immutable pixels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <math.h>
#include <string.h>

/** @brief Pixel storage shared by size and alternate-text revisions. */
typedef struct {
    atomic_size_t refs;
    uint32_t pixels[];
} pixels_t;
/** @brief An image value with owned metadata and one retained pixel buffer. */
typedef struct {
    tiku_rich_image_t value;
    atomic_size_t refs;
    pixels_t *data;
    char alt[1024];
} image_t;

tiku_rich_status_t
tiku_rich_image_validate(const tiku_rich_image_t *image)
{
    size_t length = 0;

    if (!image || !image->pixels || !image->pixel_width ||
        !image->pixel_height || !isfinite(image->width) ||
        !isfinite(image->height) || image->width < 1 || image->width > 14400 ||
        image->height < 1 || image->height > 14400)
        return TIKU_RICH_INVALID;
    if (image->pixel_width > TIKU_RICH_IMAGE_PIXELS_MAX /
                              image->pixel_height)
        return TIKU_RICH_LIMIT;
    if (image->alt) {
        while (length < 1024 && image->alt[length]) length++;
        if (length == 1024) return TIKU_RICH_LIMIT;
        if (tiku_rd_utf8(image->alt, length, 1) != TIKU_RICH_OK)
            return TIKU_RICH_INVALID_TEXT;
    }
    return TIKU_RICH_OK;
}

const tiku_rich_image_t *
tiku_rd_image_new(tiku_rd_memory_t *memory, const tiku_rich_image_t *source,
    const tiku_rich_image_t *owned, tiku_rich_status_t *status)
{
    image_t *image;
    size_t bytes;

    *status = tiku_rich_image_validate(source);
    if (*status != TIKU_RICH_OK) return NULL;
    image = tiku_rd_alloc(memory, sizeof *image, status);
    if (!image) return NULL;
    atomic_init(&image->refs, 1);
    image->value = *source;
    image->value.alt = image->alt;
    if (source->alt) strcpy(image->alt, source->alt);
    if (owned && source->pixels == owned->pixels &&
        source->pixel_width == owned->pixel_width &&
        source->pixel_height == owned->pixel_height) {
        image->data = ((const image_t *)owned)->data;
        atomic_fetch_add_explicit(&image->data->refs, 1, memory_order_relaxed);
    } else {
        bytes = (size_t)source->pixel_width * source->pixel_height * 4;
        image->data = tiku_rd_alloc(memory, sizeof *image->data + bytes,
                                    status);
        if (!image->data) { tiku_rd_free(image); return NULL; }
        atomic_init(&image->data->refs, 1);
        memcpy(image->data->pixels, source->pixels, bytes);
    }
    image->value.pixels = image->data->pixels;
    return &image->value;
}

void
tiku_rd_image_retain(const tiku_rich_image_t *image)
{
    if (image) atomic_fetch_add_explicit(&((image_t *)image)->refs, 1,
                                         memory_order_relaxed);
}

void
tiku_rd_image_drop(const tiku_rich_image_t *value)
{
    image_t *image = (image_t *)value;

    if (!image || atomic_fetch_sub_explicit(&image->refs, 1,
                                            memory_order_acq_rel) != 1)
        return;
    if (atomic_fetch_sub_explicit(&image->data->refs, 1,
                                  memory_order_acq_rel) == 1)
        tiku_rd_free(image->data);
    tiku_rd_free(image);
}

void
tiku_rd_image_account(const tiku_rich_image_t *value, int add, size_t *bytes)
{
    const image_t *image = (const image_t *)value;

    if (tiku_rd_account(image, add, bytes))
        tiku_rd_account(image->data, add, bytes);
}
