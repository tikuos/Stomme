/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richfont.c - bounded native font loading and retained face lifetime.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richfont_private.h"
#include "tiku_fontset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Read a capped font file and retain its parsed copy. */
static tiku_rich_status_t
open_face(const tiku_richfont_source_t *source, tiku_rf_face_t *out,
          size_t *remaining)
{
    FILE *file;
    long length;
    unsigned char *data;
    const char *family;
    tiku_rich_status_t status = TIKU_RICH_OK;

    if (!source->path || source->bold > 1 || source->italic > 1 ||
        (source->family && strlen(source->family) >= sizeof out->family))
        return TIKU_RICH_INVALID;
    file = fopen(source->path, "rb");
    if (!file) return TIKU_RICH_NOT_FOUND;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 1 ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return TIKU_RICH_INVALID;
    }
    if ((unsigned long)length > (32u << 20) || (size_t)length > *remaining) {
        fclose(file);
        return TIKU_RICH_LIMIT;
    }
    data = malloc((size_t)length);
    if (!data) status = TIKU_RICH_NO_MEMORY;
    else if (fread(data, 1, (size_t)length, file) != (size_t)length ||
             fgetc(file) != EOF || ferror(file)) status = TIKU_RICH_INVALID;
    if (fclose(file) && status == TIKU_RICH_OK) status = TIKU_RICH_INVALID;
    if (status == TIKU_RICH_OK) {
        out->ttf = tiku_ttf_open_mem_face(data, (size_t)length, source->index);
        if (!out->ttf) status = TIKU_RICH_INVALID;
    }
    free(data);
    if (status != TIKU_RICH_OK) return status;
    *remaining -= (size_t)length;
    family = source->family ? source->family : tiku_ttf_family(out->ttf);
    snprintf(out->family, sizeof out->family, "%s", family);
    out->bold = source->bold;
    out->italic = source->italic;
    out->index = source->index;
    out->upem = tiku_ttf_upem(out->ttf);
    tiku_ttf_line(out->ttf, &out->ascent, &out->height);
    if (out->ascent < 0 || out->height <= out->ascent ||
        out->height > out->upem * 4) {
        out->ascent = out->upem;
        out->height = out->upem + out->upem / 4;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richfont_open(const tiku_richfont_source_t *sources, size_t count,
                   tiku_richfont_t **out)
{
    tiku_richfont_t *fonts;
    size_t remaining = (size_t)128 << 20;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!sources || !count || count > 32) return TIKU_RICH_INVALID;
    fonts = calloc(1, sizeof *fonts);
    if (!fonts) return TIKU_RICH_NO_MEMORY;
    fonts->refs = 1;
    while (fonts->count < count) {
        status = open_face(&sources[fonts->count], &fonts->face[fonts->count],
                           &remaining);
        if (status != TIKU_RICH_OK) {
            tiku_richfont_release(fonts);
            return status;
        }
        fonts->count++;
    }
    *out = fonts;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richfont_system(const char *family, tiku_richfont_t **out)
{
    tiku_fontset_face_t found[4];
    tiku_richfont_source_t sources[4];
    tiku_rich_status_t status;
    unsigned i, warnings = 0;

    if (!out) return TIKU_RICH_INVALID;
    *out = NULL;
    if (!family || !*family) family = "sans-serif";
    for (i = 0; i < 4; i++) {
        if (tiku_fontset_find(family, (i & 1) ? 700 : 400, !!(i & 2),
                              &found[i])) return TIKU_RICH_NOT_FOUND;
        sources[i] = (tiku_richfont_source_t){found[i].path,
            (unsigned)found[i].index, found[i].family,
            found[i].weight >= 600, !!found[i].italic};
        if (!found[i].exact || found[i].bold_synth || found[i].italic_synth)
            warnings |= TIKU_RICH_LAYOUT_FALLBACK;
    }
    status = tiku_richfont_open(sources, 4, out);
    if (status == TIKU_RICH_OK) (*out)->warnings = warnings;
    return status;
}

/** @brief Retain the font provider for one additional layout. */
static void
retain(void *context)
{
    ((tiku_richfont_t *)context)->refs++;
}

/** @brief Release a layout's font provider reference. */
static void
release(void *context)
{
    tiku_richfont_release(context);
}

tiku_rich_provider_t
tiku_richfont_provider(tiku_richfont_t *fonts)
{
    return fonts ? (tiku_rich_provider_t){fonts, tiku_rf_shape, retain,
        release, 1} : (tiku_rich_provider_t){0};
}

void
tiku_richfont_release(tiku_richfont_t *fonts)
{
    size_t i;

    if (!fonts || --fonts->refs) return;
    for (i = 0; i < fonts->count; i++) tiku_ttf_close(fonts->face[i].ttf);
    free(fonts);
}

tiku_rich_status_t
tiku_richfont_raster(tiku_richfont_t *fonts, uint32_t font, uint32_t glyph,
                     int size, tiku_ttf_glyph_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!fonts || !font || font > fonts->count || size < 1 || size > 512 ||
        glyph >= (unsigned)tiku_ttf_glyphs(fonts->face[font - 1].ttf))
        return TIKU_RICH_INVALID;
    return tiku_ttf_render_glyph(fonts->face[font - 1].ttf, glyph, size, out) ?
        TIKU_RICH_OK : TIKU_RICH_NO_MEMORY;
}
