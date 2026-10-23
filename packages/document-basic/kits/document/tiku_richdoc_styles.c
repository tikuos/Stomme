/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_styles.c - explicit properties and named style resolution.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <math.h>
#include <string.h>

void
tiku_rd_chars_overlay(tiku_rich_chars_t *to, const tiku_rich_chars_t *from)
{
    unsigned p = from->present;

    if (p & TIKU_RICH_BOLD) to->bold = from->bold;
    if (p & TIKU_RICH_ITALIC) to->italic = from->italic;
    if (p & TIKU_RICH_UNDERLINE) to->underline = from->underline;
    if (p & TIKU_RICH_STRIKE) to->strike = from->strike;
    if (p & TIKU_RICH_SIZE) to->size = from->size;
    if (p & TIKU_RICH_FONT) {
        memset(to->font, 0, sizeof to->font);
        memcpy(to->font, from->font, strlen(from->font));
    }
    if (p & TIKU_RICH_FOREGROUND) to->foreground = from->foreground;
    if (p & TIKU_RICH_HIGHLIGHT) to->highlight = from->highlight;
    if (p & TIKU_RICH_BASELINE) to->baseline = from->baseline;
    to->present |= p;
}

void
tiku_rd_props_overlay(tiku_rich_para_t *to, const tiku_rich_para_t *from)
{
    unsigned p = from->present;

    if (p & TIKU_RICH_ALIGNMENT) to->align = from->align;
    if (p & TIKU_RICH_INDENT) {
        to->left = from->left;
        to->right = from->right;
        to->first = from->first;
    }
    if (p & TIKU_RICH_SPACING) {
        to->before = from->before;
        to->after = from->after;
    }
    if (p & TIKU_RICH_KEEP) {
        to->keep_next = from->keep_next;
        to->keep_lines = from->keep_lines;
    }
    if (p & TIKU_RICH_PAGE_BREAK) to->break_before = from->break_before;
    if (p & TIKU_RICH_OUTLINE) to->outline = from->outline;
    to->present |= p;
}

tiku_rich_status_t
tiku_rd_chars(const tiku_rich_chars_t *in, tiku_rich_chars_t *out)
{
    unsigned p = in->present;

    memset(out, 0, sizeof *out);
    if (p & ~511u)
        return TIKU_RICH_INVALID_STYLE;
    if (((p & TIKU_RICH_BOLD) && in->bold > 1) ||
        ((p & TIKU_RICH_ITALIC) && in->italic > 1) ||
        ((p & TIKU_RICH_UNDERLINE) && in->underline > 1) ||
        ((p & TIKU_RICH_STRIKE) && in->strike > 1))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_SIZE) &&
        (!isfinite(in->size) || in->size < 1 || in->size > 4096))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_FONT) && !tiku_rd_name(in->font))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_BASELINE) &&
        (in->baseline < -1 || in->baseline > 1))
        return TIKU_RICH_INVALID_STYLE;
    tiku_rd_chars_overlay(out, in);
    return TIKU_RICH_OK;
}

/** @brief Whether a document length is finite and within its bounds. */
static int
length_ok(float value, float low)
{
    return isfinite(value) && value >= low && value <= 1000000;
}

tiku_rich_status_t
tiku_rd_props(const tiku_rich_para_t *in, tiku_rich_para_t *out)
{
    unsigned p = in->present;

    memset(out, 0, sizeof *out);
    if (p & ~63u)
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_ALIGNMENT) &&
        (in->align < TIKU_RICH_LEFT || in->align > TIKU_RICH_JUSTIFY))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_INDENT) &&
        (!length_ok(in->left, -1000000) ||
         !length_ok(in->right, -1000000) ||
         !length_ok(in->first, -1000000)))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_SPACING) &&
        (!length_ok(in->before, 0) || !length_ok(in->after, 0)))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_KEEP) &&
        (in->keep_next > 1 || in->keep_lines > 1))
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_PAGE_BREAK) && in->break_before > 1)
        return TIKU_RICH_INVALID_STYLE;
    if ((p & TIKU_RICH_OUTLINE) && in->outline > 6)
        return TIKU_RICH_INVALID_STYLE;
    tiku_rd_props_overlay(out, in);
    return TIKU_RICH_OK;
}

size_t
tiku_rd_style_find(const tiku_rd_root_t *root, uint64_t id)
{
    size_t i;

    for (i = 0; i < root->nstyles; i++)
        if (root->styles[i].value.id == id)
            return i;
    return SIZE_MAX;
}

int
tiku_rd_style_ref(const tiku_rd_root_t *root, uint64_t id,
                   tiku_rich_style_kind_t kind)
{
    size_t index = tiku_rd_style_find(root, id);

    return !id || (index != SIZE_MAX && root->styles[index].value.kind == kind);
}

/** @brief Apply an inheritance chain from its oldest ancestor. */
static tiku_rich_status_t
inherit(const tiku_rd_root_t *root, uint64_t id, tiku_rich_chars_t *chars,
         tiku_rich_para_t *props)
{
    const tiku_rich_style_t *chain[64], *style;
    size_t n = 0, index;

    while (id) {
        index = tiku_rd_style_find(root, id);
        if (index == SIZE_MAX || n == 64)
            return TIKU_RICH_INVALID_STYLE;
        style = &root->styles[index].value;
        chain[n++] = style;
        id = style->parent;
    }
    while (n) {
        style = chain[--n];
        tiku_rd_chars_overlay(chars, &style->chars);
        tiku_rd_props_overlay(props, &style->paragraph);
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_resolve(const tiku_rd_root_t *root, size_t paragraph, size_t run,
                 tiku_rich_chars_t *chars, tiku_rich_para_t *props)
{
    const tiku_rich_paragraph_t *p;

    if (paragraph >= root->count)
        return TIKU_RICH_INVALID_RANGE;
    p = &root->paragraphs[paragraph]->value;
    if ((p->run_count && run >= p->run_count) || (!p->run_count && run))
        return TIKU_RICH_INVALID_RANGE;
    return tiku_rd_resolve_value(root, p,
        p->run_count ? &p->runs[run] : NULL, chars, props);
}

void
tiku_rd_chars_reset(tiku_rich_chars_t *chars, unsigned mask)
{
    tiku_rich_chars_t kept = *chars;

    kept.present &= ~mask;
    tiku_rd_chars(&kept, chars);
}

tiku_rich_status_t
tiku_rd_resolve_value(const tiku_rd_root_t *root,
    const tiku_rich_paragraph_t *p, const tiku_rich_run_t *run,
    tiku_rich_chars_t *chars, tiku_rich_para_t *props)
{
    tiku_rich_status_t status;

    memset(chars, 0, sizeof *chars);
    memset(props, 0, sizeof *props);
    status = inherit(root, p->style, chars, props);
    if (status != TIKU_RICH_OK)
        return status;
    tiku_rd_props_overlay(props, &p->paragraph);
    if (run) {
        status = inherit(root, run->style, chars, props);
        if (status != TIKU_RICH_OK)
            return status;
    }
    tiku_rd_chars_overlay(chars, &p->chars);
    if (run) {
        tiku_rd_chars_reset(chars, run->reset);
        tiku_rd_chars_overlay(chars, &run->chars);
    }
    return TIKU_RICH_OK;
}
