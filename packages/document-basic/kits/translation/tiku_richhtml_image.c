/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richhtml_image.c - self-contained HTML pictures with bounded PNG data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richhtml_private.h"
#include "tiku_image.h"
#include <string.h>

/** @brief Base64 grouping state shared across PNG callback boundaries. */
typedef struct {
    tiku_rio_writer_t *writer; /**< bounded HTML sink */
    unsigned char bytes[3];   /**< incomplete input group */
    unsigned count;           /**< number of buffered PNG bytes */
} base64_t;

/** @brief Emit one complete or padded base64 quartet. */
static void
quartet(base64_t *b)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[4];

    out[0] = alphabet[b->bytes[0] >> 2];
    out[1] = alphabet[((b->bytes[0] & 3) << 4) | (b->bytes[1] >> 4)];
    out[2] = b->count > 1 ?
        alphabet[((b->bytes[1] & 15) << 2) | (b->bytes[2] >> 6)] : '=';
    out[3] = b->count > 2 ? alphabet[b->bytes[2] & 63] : '=';
    tiku_rio_put(b->writer, out, sizeof out);
    memset(b->bytes, 0, sizeof b->bytes);
    b->count = 0;
}

/** @brief Append PNG bytes as base64, polling cancellation at each callback. */
static int
emit(void *context, const void *input, size_t length)
{
    base64_t *b = context;
    tiku_rio_writer_t *w = b->writer;
    const unsigned char *bytes = input;

    if (w->status == TIKU_RICHIO_OK && w->options.cancel &&
        w->options.cancel(w->options.ctx)) w->status = TIKU_RICHIO_CANCELLED;
    for (size_t i = 0; i < length && w->status == TIKU_RICHIO_OK; i++) {
        b->bytes[b->count++] = bytes[i];
        if (b->count == 3) quartet(b);
    }
    return w->status != TIKU_RICHIO_OK;
}

void
tiku_rhtml_image(tiku_rio_writer_t *w, const tiku_rich_image_t *image)
{
    size_t bytes = tiku_png_rgba_size(image->pixel_width, image->pixel_height);
    base64_t b = {.writer = w};

    if (w->status != TIKU_RICHIO_OK) return;
    if (!bytes) { w->status = TIKU_RICHIO_DAMAGED; return; }
    tiku_rio_literal(w, "<img alt=\"");
    tiku_rhtml_escape(w, image->alt, strlen(image->alt));
    tiku_rio_literal(w, "\" style=\"width:");
    tiku_rhtml_number(w, image->width);
    tiku_rio_literal(w, "pt;height:");
    tiku_rhtml_number(w, image->height);
    tiku_rio_literal(w, "pt\" src=\"data:image/png;base64,");
    if (!w->data) tiku_rio_put(w, NULL, (bytes + 2) / 3 * 4);
    else if (tiku_png_rgba_stream(image->pixels, image->pixel_width,
        image->pixel_height, emit, &b) && w->status == TIKU_RICHIO_OK)
        w->status = TIKU_RICHIO_DAMAGED;
    if (b.count) quartet(&b);
    tiku_rio_literal(w, "\">");
}
