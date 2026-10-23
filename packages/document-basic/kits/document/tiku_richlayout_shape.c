/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_shape.c - bounded shaping and owned glyph-cluster geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richlayout_private.h"

#include <math.h>
#include <string.h>

int
tiku_rl_valid_shape(const tiku_rich_shape_t *shape,
                    const uint8_t *boundaries, size_t length)
{
    size_t i, caret = 0;

    if (!shape->bytes || shape->bytes > length || shape->glyph_count > 32 ||
        shape->caret_count < 2 || shape->caret_count > 34 ||
        !isfinite(shape->advance) || shape->advance < 0 ||
        shape->advance > 1000000 || !isfinite(shape->ascent) ||
        !isfinite(shape->descent) || shape->ascent < 0 ||
        shape->descent < 0 || shape->ascent + shape->descent < 0.01 ||
        shape->ascent + shape->descent > 1000000 ||
        !(boundaries[shape->bytes] & 1) ||
        shape->carets[0].byte || shape->carets[0].x != 0)
        return 0;
    for (i = 0; i <= shape->bytes; i++) {
        if (!(boundaries[i] & 1)) continue;
        if (caret == shape->caret_count || shape->carets[caret].byte != i ||
            !isfinite(shape->carets[caret].x) ||
            shape->carets[caret].x > shape->advance ||
            (caret && shape->carets[caret].x < shape->carets[caret - 1].x))
            return 0;
        caret++;
    }
    if (caret != shape->caret_count ||
        shape->carets[caret - 1].x != shape->advance)
        return 0;
    for (i = 0; i < shape->glyph_count; i++)
        if (!isfinite(shape->glyphs[i].x) ||
            !isfinite(shape->glyphs[i].y) ||
            fabs(shape->glyphs[i].x) > 1000000 ||
            fabs(shape->glyphs[i].y) > 1000000)
            return 0;
    return 1;
}

tiku_rich_status_t
tiku_rl_marker(tiku_richlayout_t *layout)
{
    tiku_rich_shape_t shape;
    uint8_t boundaries[32];
    size_t length = strlen(layout->marker), offset = 0, i;
    tiku_rich_status_t status;

    layout->marker_glyph = layout->nglyphs;
    layout->marker_count = 0;
    layout->marker_width = 0;
    for (i = 0; i <= length; i++)
        boundaries[i] = ((unsigned char)layout->marker[i] & 0xc0) != 0x80;
    while (offset < length) {
        memset(&shape, 0, sizeof shape);
        status = layout->options.provider.shape(
            layout->options.provider.context, &layout->defaults,
            layout->marker + offset, length - offset, boundaries + offset,
            &shape);
        if (status != TIKU_RICH_OK) return status;
        if (!tiku_rl_valid_shape(&shape, boundaries + offset, length - offset))
            return TIKU_RICH_INVALID;
        if (layout->marker_count + shape.glyph_count > 32)
            return TIKU_RICH_LIMIT;
        if (!tiku_rl_grow(layout, (void **)&layout->glyphs, &layout->cglyphs,
                         layout->nglyphs + shape.glyph_count,
                         sizeof *layout->glyphs)) return layout->status;
        for (i = 0; i < shape.glyph_count; i++) {
            layout->glyphs[layout->nglyphs] = shape.glyphs[i];
            layout->glyphs[layout->nglyphs++].x += layout->marker_width;
        }
        layout->marker_count += shape.glyph_count;
        layout->marker_width += shape.advance;
        tiku_rl_warn(layout, shape.warnings);
        if (shape.ascent > layout->ascent) layout->ascent = shape.ascent;
        if (shape.descent > layout->descent) layout->descent = shape.descent;
        offset += shape.bytes;
    }
    return TIKU_RICH_OK;
}

/** @brief Classify spaces, tabs, soft line breaks and simple wrap points. */
static unsigned
classification(uint32_t scalar)
{
    if (scalar == '\t') return RL_TAB | RL_WRAP;
    if (scalar == '\n' || scalar == '\r' || scalar == 0x2028 ||
        scalar == 0x2029) return RL_BREAK;
    if (scalar == ' ') return RL_SPACE | RL_WRAP;
    if (scalar == '-' || scalar == 0x2010 || scalar == 0xfffc ||
        (scalar >= 0x2e80 && scalar <= 0x9fff) ||
        (scalar >= 0xac00 && scalar <= 0xd7a3) ||
        (scalar >= 0x20000 && scalar <= 0x323af)) return RL_WRAP;
    return 0;
}

/** @brief Limit provider input to one style and at most 4096 UTF-8 bytes. */
static size_t
input_end(tiku_richlayout_t *layout, const tiku_rich_paragraph_t *p)
{
    size_t end, i, next;
    uint32_t scalar;

    if (layout->byte < layout->shape_end) return layout->shape_end;
    while (layout->run + 1 < p->run_count &&
           layout->run_end <= layout->byte) {
        layout->run++;
        layout->run_end += p->runs[layout->run].length;
    }
    end = layout->run_end;
    if (end > layout->byte + 4096) end = layout->byte + 4096;
    while (end > layout->byte && !(layout->boundaries[end] & 1)) end--;
    if (end == layout->byte) {
        end = layout->byte + 1;
        while (end < p->length && end <= layout->byte + 4096 &&
               !(layout->boundaries[end] & 1)) end++;
        if (end > layout->byte + 4096) return 0;
        tiku_rl_warn(layout, TIKU_RICH_LAYOUT_CLUSTER_STYLE);
    }
    for (i = layout->byte; i < end; i = next) {
        next = i;
        if (tiku_rt_decode(p->text, p->length, &next, &scalar) !=
            TIKU_RICH_OK) return 0;
        if ((layout->boundaries[i] & 1) && classification(scalar)) {
            if (i != layout->byte) return layout->shape_end = i;
            while (next < end && !(layout->boundaries[next] & 1)) next++;
            return layout->shape_end = next;
        }
    }
    return layout->shape_end = end;
}

/** @brief Append a validated shape, preserving glyph and caret ownership. */
static tiku_rich_status_t
append(tiku_richlayout_t *layout, const tiku_rich_shape_t *shape,
       const tiku_rich_chars_t *chars, unsigned flags)
{
    tiku_rich_cluster_t *cluster;
    size_t i;
    double shift = chars->baseline * chars->size * -0.3;

    if (!tiku_rl_grow(layout, (void **)&layout->clusters, &layout->cclusters,
                     layout->nclusters + 1, sizeof *layout->clusters) ||
        !tiku_rl_grow(layout, (void **)&layout->glyphs, &layout->cglyphs,
                     layout->nglyphs + shape->glyph_count,
                     sizeof *layout->glyphs) ||
        !tiku_rl_grow(layout, (void **)&layout->carets, &layout->ccarets,
                     layout->ncarets + shape->caret_count,
                     sizeof *layout->carets))
        return layout->status;
    cluster = &layout->clusters[layout->nclusters++];
    cluster->paragraph =
        layout->source.root->paragraphs[layout->paragraph]->value.id;
    cluster->first = layout->byte;
    cluster->last = layout->byte + shape->bytes;
    cluster->chars = *chars;
    cluster->image = NULL;
    cluster->flags = flags;
    cluster->box.width = (flags & (RL_BREAK | RL_TAB)) ? 0 : shape->advance;
    cluster->baseline = shape->ascent - shift;
    if (cluster->baseline < 0) cluster->baseline = 0;
    cluster->box.height = shape->descent + shift;
    if (cluster->box.height < 0) cluster->box.height = 0;
    cluster->box.height += cluster->baseline;
    cluster->glyph = layout->nglyphs;
    cluster->glyph_count = (flags & (RL_TAB | RL_BREAK)) ? 0 :
                           shape->glyph_count;
    for (i = 0; i < cluster->glyph_count; i++) {
        layout->glyphs[layout->nglyphs] = shape->glyphs[i];
        layout->glyphs[layout->nglyphs++].y += shift;
    }
    cluster->caret = layout->ncarets;
    cluster->caret_count = shape->caret_count;
    for (i = 0; i < shape->caret_count; i++) {
        layout->carets[layout->ncarets] = shape->carets[i];
        if (flags & (RL_TAB | RL_BREAK))
            layout->carets[layout->ncarets].x = 0;
        layout->ncarets++;
    }
    tiku_rl_warn(layout, shape->warnings);
    layout->byte += shape->bytes;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rl_shape(tiku_richlayout_t *layout)
{
    const tiku_rich_paragraph_t *p =
        &layout->source.root->paragraphs[layout->paragraph]->value;
    tiku_rich_chars_t chars, resolved;
    tiku_rich_para_t props;
    tiku_rich_shape_t shape = {0};
    tiku_rich_status_t status;
    size_t end, next = layout->byte;
    uint32_t scalar;

    if (layout->byte == p->length) {
        tiku_rl_free(layout, layout->boundaries);
        layout->boundaries = NULL;
        tiku_rl_flow_start(layout);
        return TIKU_RICH_OK;
    }
    end = input_end(layout, p);
    if (!end) return TIKU_RICH_LIMIT;
    status = tiku_rd_resolve_value(layout->source.root, p,
        &p->runs[layout->run], &resolved, &props);
    if (status != TIKU_RICH_OK) return status;
    chars = layout->options.defaults;
    tiku_rd_chars_overlay(&chars, &resolved);
    if (p->runs[layout->run].image) {
        const tiku_rich_image_t *image = p->runs[layout->run].image;

        shape.bytes = 3;
        shape.advance = image->width;
        shape.ascent = image->height;
        shape.caret_count = 2;
        shape.carets[1].byte = 3;
        shape.carets[1].x = image->width;
        chars.baseline = 0;
        status = append(layout, &shape, &chars, RL_WRAP);
        if (status == TIKU_RICH_OK)
            layout->clusters[layout->nclusters - 1].image = image;
        return status;
    }
    status = layout->options.provider.shape(layout->options.provider.context,
        &chars, p->text + layout->byte, end - layout->byte,
        layout->boundaries + layout->byte, &shape);
    if (status != TIKU_RICH_OK) return status;
    if (!tiku_rl_valid_shape(&shape, layout->boundaries + layout->byte,
                             end - layout->byte))
        return TIKU_RICH_INVALID;
    (void)tiku_rt_decode(p->text, p->length, &next, &scalar);
    return append(layout, &shape, &chars, classification(scalar));
}
