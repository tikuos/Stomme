/*
 * TikuDesktop Translation Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richio_image.c - bounded inline-image XML pixels and metadata.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richio_private.h"
#include <stdlib.h>
#include <string.h>

void
tiku_rio_write_image(tiku_rio_writer_t *w, const tiku_rich_image_t *image)
{
    static const char hex[] = "0123456789abcdef";
    size_t count = (size_t)image->pixel_width * image->pixel_height;
    char digits[8];
    size_t reference = w->image_refs ? w->image_refs[w->image_at] : 0;

    w->image_at++;
    tiku_rio_literal(w, "<image");
    tiku_rio_uint(w, "pixel-width", image->pixel_width);
    tiku_rio_uint(w, "pixel-height", image->pixel_height);
    tiku_rio_write_float(w, "width-f32", image->width);
    tiku_rio_write_float(w, "height-f32", image->height);
    tiku_rio_literal(w, " alt=\"");
    tiku_rio_escape(w, image->alt, strlen(image->alt));
    tiku_rio_literal(w, "\"");
    if (reference) {
        tiku_rio_uint(w, "ref", reference);
        tiku_rio_literal(w, "/>");
        return;
    }
    tiku_rio_literal(w, ">");
    for (size_t i = 0; i < count && w->status == TIKU_RICHIO_OK; i++) {
        if (!(i % 4096) && w->options.cancel &&
            w->options.cancel(w->options.ctx)) {
            w->status = TIKU_RICHIO_CANCELLED;
            break;
        }
        for (unsigned j = 0; j < 8; j++)
            digits[j] = hex[(image->pixels[i] >> (28 - j * 4)) & 15];
        tiku_rio_put(w, digits, sizeof digits);
    }
    tiku_rio_literal(w, "</image>");
}

/** @brief Decode exactly eight hexadecimal digits per pixel. */
static void
pixels(tiku_rio_reader_t *r, const tiku_xml_node_t *node, uint32_t *out,
       size_t count)
{
    size_t at = 0;
    unsigned digit, nibble = 0;
    uint32_t value = 0;

    for (node = node->child; node && r->status == TIKU_RICHIO_OK;
         node = node->next) {
        if (node->name) { r->status = TIKU_RICHIO_UNSUPPORTED; return; }
        for (const unsigned char *p = (const unsigned char *)node->text;
             *p; p++) {
            if (!(at % 4096) && !nibble && r->options.cancel &&
                r->options.cancel(r->options.ctx)) {
                r->status = TIKU_RICHIO_CANCELLED;
                return;
            }
            digit = *p >= '0' && *p <= '9' ? *p - '0' :
                *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : 16;
            if (digit == 16 || at == count) {
                r->status = TIKU_RICHIO_DAMAGED;
                return;
            }
            value = (value << 4) | digit;
            if (++nibble == 8) { out[at++] = value; nibble = value = 0; }
        }
    }
    if (at != count || nibble) r->status = TIKU_RICHIO_DAMAGED;
}

const tiku_rich_image_t *
tiku_rio_read_image(tiku_rio_reader_t *r, const tiku_xml_node_t *node)
{
    static const char *const attrs[] = {"pixel-width", "pixel-height",
        "width-f32", "height-f32", "alt", "ref"};
    tiku_rio_image_t *slot;
    size_t count;
    uint32_t *data;
    const char *reference = tiku_xml_attr(node, "ref");

    if (!r->image_feature) { r->status = TIKU_RICHIO_UNSUPPORTED; return NULL; }
    if (reference && !r->reference_feature) {
        r->status = TIKU_RICHIO_UNSUPPORTED;
        return NULL;
    }
    if (!tiku_rio_element(r, node, "image", attrs, reference ? 6 : 5)) return NULL;
    if (r->image_at == r->nimages) {
        r->status = TIKU_RICHIO_DAMAGED;
        return NULL;
    }
    slot = &r->images[r->image_at++];
    slot->image.pixel_width = (unsigned)tiku_rio_number(r, node,
        "pixel-width", TIKU_RICH_IMAGE_PIXELS_MAX);
    slot->image.pixel_height = (unsigned)tiku_rio_number(r, node,
        "pixel-height", TIKU_RICH_IMAGE_PIXELS_MAX);
    slot->image.width = tiku_rio_read_float(r, node, "width-f32");
    slot->image.height = tiku_rio_read_float(r, node, "height-f32");
    slot->image.alt = slot->alt;
    tiku_rio_string(r, tiku_xml_attr(node, "alt"), slot->alt, sizeof slot->alt);
    if (r->status != TIKU_RICHIO_OK) return NULL;
    if (!slot->image.pixel_height || !slot->image.pixel_width ||
        slot->image.pixel_width > TIKU_RICH_IMAGE_PIXELS_MAX /
                                   slot->image.pixel_height) {
        r->status = TIKU_RICHIO_LIMIT;
        return NULL;
    }
    count = (size_t)slot->image.pixel_width * slot->image.pixel_height;
    if (reference) {
        size_t index = tiku_rio_number(r, node, "ref", r->image_at - 1);
        const tiku_rich_image_t *source;

        if (r->status != TIKU_RICHIO_OK) return NULL;
        if (!index || node->child) {
            r->status = TIKU_RICHIO_DAMAGED;
            return NULL;
        }
        source = &r->images[index - 1].image;
        if (source->pixel_width != slot->image.pixel_width ||
            source->pixel_height != slot->image.pixel_height || !source->pixels) {
            r->status = TIKU_RICHIO_DAMAGED;
            return NULL;
        }
        slot->image.pixels = source->pixels;
        slot->borrowed = 1;
        return &slot->image;
    }
    if (count > (r->options.memory_bytes / 2 - r->used) / sizeof *data) {
        r->status = TIKU_RICHIO_LIMIT;
        return NULL;
    }
    data = calloc(count, sizeof *data);
    if (!data) { r->status = TIKU_RICHIO_NO_MEMORY; return NULL; }
    r->used += count * sizeof *data;
    slot->image.pixels = data;
    pixels(r, node, data, count);
    return &slot->image;
}
