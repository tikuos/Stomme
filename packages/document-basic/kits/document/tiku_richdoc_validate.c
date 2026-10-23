/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_validate.c - bounds, style graphs and run coverage on import.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Whether a source index names the requested category, or is zero. */
static int
style_ref(const tiku_rich_source_t *source, size_t index,
           tiku_rich_style_kind_t kind)
{
    return !index || (index <= source->style_count &&
                      source->styles[index - 1].kind == kind);
}

/** @brief Validate style values, unique names and bounded inheritance. */
static tiku_rich_status_t
styles_valid(const tiku_rich_source_t *source)
{
    size_t i, j, at, depth;
    const tiku_rich_style_source_t *style;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;

    for (i = 0; i < source->style_count; i++) {
        style = &source->styles[i];
        if (!tiku_rd_name(style->name) ||
            (style->kind != TIKU_RICH_PARAGRAPH_STYLE &&
             style->kind != TIKU_RICH_CHARACTER_STYLE) ||
            !style_ref(source, style->parent, style->kind) ||
            tiku_rd_chars(&style->chars, &chars) != TIKU_RICH_OK ||
            tiku_rd_props(&style->paragraph, &props) != TIKU_RICH_OK ||
            (style->kind == TIKU_RICH_CHARACTER_STYLE &&
             style->paragraph.present))
            return TIKU_RICH_INVALID_STYLE;
        for (j = 0; j < i; j++)
            if (source->styles[j].kind == style->kind &&
                !strcmp(source->styles[j].name, style->name))
                return TIKU_RICH_INVALID_STYLE;
        at = i + 1;
        depth = 0;
        while (at) {
            if (at > source->style_count || ++depth > 64)
                return TIKU_RICH_INVALID_STYLE;
            at = source->styles[at - 1].parent;
        }
    }
    return TIKU_RICH_OK;
}

/** @brief Validate one paragraph's references, bytes and complete runs. */
static tiku_rich_status_t
paragraph_valid(const tiku_rich_source_t *source,
                 const tiku_rich_paragraph_source_t *p)
{
    tiku_rich_status_t status;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    size_t i, offset = 0;
    tiku_rich_list_t list;

    if (p->run_count && !p->runs)
        return TIKU_RICH_INVALID;
    status = tiku_rd_utf8(p->text, p->length, p->run_count ? 2 : 1);
    if (status != TIKU_RICH_OK)
        return status;
    if (!style_ref(source, p->style, TIKU_RICH_PARAGRAPH_STYLE) ||
        tiku_rd_chars(&p->chars, &chars) != TIKU_RICH_OK ||
        tiku_rd_props(&p->paragraph, &props) != TIKU_RICH_OK ||
        tiku_rd_list(&p->list, &list) != TIKU_RICH_OK)
        return TIKU_RICH_INVALID_STYLE;
    for (i = 0; i < p->run_count; i++) {
        const tiku_rich_run_source_t *run = &p->runs[i];

        if (!run->length || run->length > p->length - offset)
            return TIKU_RICH_INVALID_RANGE;
        if (!tiku_rd_boundary(p->text, p->length, offset + run->length))
            return TIKU_RICH_INVALID_RANGE;
        if (run->image) {
            if (run->length != 3 ||
                memcmp(p->text + offset, "\xef\xbf\xbc", 3))
                return TIKU_RICH_INVALID_RANGE;
            status = tiku_rich_image_validate(run->image);
        } else status = tiku_rd_utf8(p->text + offset, run->length, 1);
        if (status != TIKU_RICH_OK) return status;
        offset += run->length;
        if (!tiku_rd_boundary(p->text, p->length, offset))
            return TIKU_RICH_INVALID_RANGE;
        if (!style_ref(source, run->style, TIKU_RICH_CHARACTER_STYLE) ||
            (run->reset & ~511u) ||
            tiku_rd_chars(&run->chars, &chars) != TIKU_RICH_OK)
            return TIKU_RICH_INVALID_STYLE;
        status = tiku_rich_link_validate(run->link);
        if (status != TIKU_RICH_OK)
            return status;
    }
    return p->run_count && offset != p->length
           ? TIKU_RICH_INVALID_RANGE : TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_validate(const tiku_rich_limits_t *limits,
                   const tiku_rich_source_t *source)
{
    size_t i, bytes = 0, runs = 0;
    size_t count = source->paragraph_count ? source->paragraph_count : 1;
    tiku_rich_status_t status;

    if ((source->style_count && !source->styles) ||
        (source->paragraph_count && !source->paragraphs))
        return TIKU_RICH_INVALID;
    if (count > limits->paragraphs || source->style_count > limits->styles ||
        count > SIZE_MAX / sizeof(tiku_rd_paragraph_t *) ||
        source->style_count > SIZE_MAX / sizeof(tiku_rd_style_t) ||
        count > limits->memory_bytes / sizeof(tiku_rd_paragraph_t) ||
        source->style_count > limits->memory_bytes / sizeof(tiku_rd_style_t))
        return TIKU_RICH_LIMIT;
    {
        tiku_rich_page_setup_t page;

        status = tiku_rich_page_setup(&source->page, &page);
        if (status != TIKU_RICH_OK) return status;
    }
    status = tiku_rd_blocks_validate(source);
    if (status != TIKU_RICH_OK) return status;
    status = styles_valid(source);
    if (status != TIKU_RICH_OK)
        return status;
    for (i = 0; i < source->paragraph_count; i++) {
        const tiku_rich_paragraph_source_t *p = &source->paragraphs[i];
        size_t nr = p->run_count ? p->run_count : (p->length != 0);

        if (p->length == SIZE_MAX || p->length > limits->text_bytes - bytes ||
            p->length >= limits->memory_bytes ||
            nr > limits->runs - runs ||
            nr > SIZE_MAX / sizeof(tiku_rich_run_t) ||
            nr > limits->memory_bytes / sizeof(tiku_rich_run_t))
            return TIKU_RICH_LIMIT;
        bytes += p->length;
        runs += nr;
        status = paragraph_valid(source, p);
        if (status != TIKU_RICH_OK)
            return status;
    }
    return tiku_rd_bookmarks_validate(source);
}
