/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_paragraph.c - paragraph properties and named-style assignment.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

int
tiku_rd_props_equal(const tiku_rich_para_t *a, const tiku_rich_para_t *b)
{
    return a->present == b->present && a->align == b->align &&
        a->left == b->left && a->right == b->right && a->first == b->first &&
        a->before == b->before && a->after == b->after &&
        a->keep_next == b->keep_next && a->keep_lines == b->keep_lines &&
        a->break_before == b->break_before && a->outline == b->outline;
}

/** @brief Apply validated overrides to paragraph defaults. */
static tiku_rich_status_t
properties(tiku_rich_paragraph_t *to, const tiku_rich_edit_t *edit)
{
    tiku_rich_chars_t chars;
    tiku_rich_para_t props, kept;

    if ((edit->clear_chars & ~511u) || (edit->clear_props & ~63u) ||
        tiku_rd_chars(&edit->chars, &chars) != TIKU_RICH_OK ||
        tiku_rd_props(&edit->props, &props) != TIKU_RICH_OK)
        return TIKU_RICH_INVALID_STYLE;
    tiku_rd_chars_reset(&to->chars, edit->clear_chars);
    tiku_rd_chars_overlay(&to->chars, &chars);
    kept = to->paragraph;
    kept.present &= ~edit->clear_props;
    tiku_rd_props(&kept, &to->paragraph);
    tiku_rd_props_overlay(&to->paragraph, &props);
    return TIKU_RICH_OK;
}

tiku_rd_paragraph_t *
tiku_rd_paragraph_format(tiku_richdoc_t *doc, const tiku_rd_root_t *root,
    const tiku_rich_edit_t *edit, tiku_rich_status_t *status)
{
    size_t index = tiku_rd_find(root, edit->paragraph);
    const tiku_rich_paragraph_t *from;
    tiku_rich_paragraph_t to;
    tiku_rd_paragraph_t *node;

    if (index == SIZE_MAX || edit->start || edit->end) {
        *status = TIKU_RICH_INVALID_RANGE;
        return NULL;
    }
    from = &root->paragraphs[index]->value;
    to = *from;
    *status = TIKU_RICH_OK;
    if (edit->kind == TIKU_RICH_PARAGRAPH_STYLE_SET) {
        if (!tiku_rd_style_ref(root, edit->style, TIKU_RICH_PARAGRAPH_STYLE)) {
            *status = TIKU_RICH_INVALID_STYLE;
            return NULL;
        }
        to.style = edit->style;
    } else if (edit->kind == TIKU_RICH_LIST_SET) {
        *status = tiku_rd_list(&edit->list, &to.list);
        if (*status != TIKU_RICH_OK)
            return NULL;
    } else {
        *status = properties(&to, edit);
        if (*status != TIKU_RICH_OK)
            return NULL;
    }
    if (to.style == from->style &&
        tiku_rd_list_equal(&to.list, &from->list) &&
        tiku_rd_chars_equal(&to.chars, &from->chars) &&
        tiku_rd_props_equal(&to.paragraph, &from->paragraph)) {
        *status = TIKU_RICH_NO_CHANGE;
        return NULL;
    }
    node = tiku_rd_paragraph_new(doc, &to, to.length, to.run_count, status);
    if (!node)
        return NULL;
    memcpy((char *)node->value.text, from->text, from->length);
    tiku_rd_slice_runs(&node->value, from, 0, from->length, NULL);
    return node;
}
