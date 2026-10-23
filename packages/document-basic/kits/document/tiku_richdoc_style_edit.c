/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_style_edit.c - undoable named-style definitions and removal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <string.h>

/** @brief Validate the complete candidate graph, including descendants. */
static tiku_rich_status_t
graph_valid(tiku_richdoc_t *doc, const tiku_rd_root_t *root)
{
    size_t i, at, depth, *parents;
    tiku_rich_status_t status;

    parents = tiku_rd_alloc(doc->memory, root->nstyles * sizeof *parents,
                            &status);
    if (!parents)
        return status;
    for (i = 0; i < root->nstyles; i++) {
        parents[i] = tiku_rd_style_find(root, root->styles[i].value.parent);
        if (root->styles[i].value.parent &&
            (parents[i] == SIZE_MAX || root->styles[parents[i]].value.kind !=
                                       root->styles[i].value.kind)) {
            status = TIKU_RICH_INVALID_STYLE;
            goto done;
        }
    }
    for (i = 0; i < root->nstyles; i++) {
        at = i;
        depth = 1;
        while ((at = parents[at]) != SIZE_MAX) {
            if (++depth > 64) {
                status = TIKU_RICH_INVALID_STYLE;
                goto done;
            }
        }
    }
    status = TIKU_RICH_OK;
done:
    tiku_rd_free(parents);
    return status;
}

/** @brief Whether a named style has paragraph, run or descendant references. */
static int
referenced(const tiku_rd_root_t *root, uint64_t id)
{
    size_t i, j;
    const tiku_rich_paragraph_t *p;

    for (i = 0; i < root->nstyles; i++)
        if (root->styles[i].value.parent == id)
            return 1;
    for (i = 0; i < root->count; i++) {
        p = &root->paragraphs[i]->value;
        if (p->style == id)
            return 1;
        for (j = 0; j < p->run_count; j++)
            if (p->runs[j].style == id)
                return 1;
    }
    return 0;
}

/** @brief Copy and validate an editing definition with unique category/name. */
static tiku_rich_status_t
definition(const tiku_rd_root_t *root, const tiku_rich_edit_t *edit,
             tiku_rd_style_t *out)
{
    const tiku_rich_style_spec_t *spec = edit->definition;
    size_t i;

    if (!spec)
        return TIKU_RICH_INVALID;
    if (!tiku_rd_name(spec->name) ||
        (spec->kind != TIKU_RICH_PARAGRAPH_STYLE &&
         spec->kind != TIKU_RICH_CHARACTER_STYLE) ||
        !tiku_rd_style_ref(root, spec->parent, spec->kind) ||
        tiku_rd_chars(&spec->chars, &out->value.chars) != TIKU_RICH_OK ||
        tiku_rd_props(&spec->paragraph, &out->value.paragraph) !=
            TIKU_RICH_OK ||
        (spec->kind == TIKU_RICH_CHARACTER_STYLE && spec->paragraph.present))
        return TIKU_RICH_INVALID_STYLE;
    for (i = 0; i < root->nstyles; i++) {
        const tiku_rich_style_t *s = &root->styles[i].value;

        if (s->id != edit->style && s->kind == spec->kind &&
            !strcmp(s->name, spec->name))
            return TIKU_RICH_INVALID_STYLE;
        if (s->id == edit->style && s->kind != spec->kind)
            return TIKU_RICH_INVALID_STYLE;
    }
    memcpy(out->name, spec->name, strlen(spec->name) + 1);
    out->value.name = out->name;
    out->value.parent = spec->parent;
    out->value.kind = spec->kind;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_style_append(tiku_richdoc_t *doc, tiku_rd_root_t *root,
               tiku_rd_style_t *style, uint64_t *next_id)
{
    tiku_rich_status_t status;
    tiku_rd_style_t *styles;
    size_t i;

    if (!*next_id || root->nstyles >= doc->limits.styles ||
        root->nstyles >= SIZE_MAX / sizeof *styles)
        return TIKU_RICH_LIMIT;
    styles = tiku_rd_alloc(doc->memory,
        (root->nstyles + 1) * sizeof *styles, &status);
    if (!styles)
        return status;
    if (root->nstyles)
        memcpy(styles, root->styles, root->nstyles * sizeof *styles);
    style->value.id = *next_id;
    *next_id = *next_id == UINT64_MAX ? 0 : *next_id + 1;
    styles[root->nstyles++] = *style;
    for (i = 0; i < root->nstyles; i++)
        styles[i].value.name = styles[i].name;
    tiku_rd_free(root->styles);
    root->styles = styles;
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_style_edit(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                    const tiku_rich_edit_t *edit, uint64_t *next_id)
{
    size_t index = tiku_rd_style_find(root, edit->style), i;
    tiku_rd_style_t value = {0};
    tiku_rich_style_t *old;
    tiku_rich_status_t status;

    if (edit->style && index == SIZE_MAX)
        return TIKU_RICH_INVALID_STYLE;
    if (edit->kind == TIKU_RICH_STYLE_REMOVE) {
        if (!edit->style || referenced(root, edit->style))
            return TIKU_RICH_INVALID_STYLE;
        memmove(root->styles + index, root->styles + index + 1,
                (root->nstyles - index - 1) * sizeof *root->styles);
        root->nstyles--;
        for (i = index; i < root->nstyles; i++)
            root->styles[i].value.name = root->styles[i].name;
        return TIKU_RICH_OK;
    }
    status = definition(root, edit, &value);
    if (status != TIKU_RICH_OK)
        return status;
    if (!edit->style)
        status = tiku_rd_style_append(doc, root, &value, next_id);
    else {
        old = &root->styles[index].value;
        if (!strcmp(old->name, value.name) &&
            old->parent == value.value.parent &&
            tiku_rd_chars_equal(&old->chars, &value.value.chars) &&
            tiku_rd_props_equal(&old->paragraph, &value.value.paragraph))
            return TIKU_RICH_NO_CHANGE;
        value.value.id = edit->style;
        root->styles[index] = value;
        root->styles[index].value.name = root->styles[index].name;
    }
    if (status != TIKU_RICH_OK)
        return status;
    return graph_valid(doc, root);
}
