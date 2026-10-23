/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_fragment_styles.c - destination style matching and ID remapping.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

#include <stdio.h>
#include <string.h>

/** @brief Find an exact, case-sensitive name within a style category. */
static size_t
named(const tiku_rd_root_t *root, const tiku_rd_style_t *style)
{
    size_t i;

    for (i = 0; i < root->nstyles; i++)
        if (root->styles[i].value.kind == style->value.kind &&
            !strcmp(root->styles[i].name, style->name))
            return i;
    return SIZE_MAX;
}

/** @brief Compare definitions after translating their parent references. */
static int
equivalent(const tiku_rich_style_t *a, const tiku_rich_style_t *b)
{
    return a->kind == b->kind && a->parent == b->parent &&
        tiku_rd_chars_equal(&a->chars, &b->chars) &&
        tiku_rd_props_equal(&a->paragraph, &b->paragraph);
}

/** @brief Add a deterministic suffix without truncating a UTF-8 scalar. */
static void
rename_style(tiku_rd_style_t *style, const char *name, size_t attempt)
{
    char suffix[32];
    size_t length = strlen(name), keep, n;

    snprintf(suffix, sizeof suffix, " (paste %zu)", attempt);
    n = strlen(suffix);
    keep = length < 63 - n ? length : 63 - n;
    while (!tiku_rd_boundary(name, length, keep))
        keep--;
    memset(style->name, 0, sizeof style->name);
    memcpy(style->name, name, keep);
    memcpy(style->name + keep, suffix, n + 1);
    style->value.name = style->name;
}

/** @brief Reuse a matching definition or append a conflict-renamed copy. */
static tiku_rich_status_t
map_style(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rd_style_t *from, uint64_t parent, uint64_t *next_id,
    uint64_t *mapped)
{
    tiku_rd_style_t style = *from;
    size_t index, attempt = 0;
    tiku_rich_status_t status;

    style.value.name = style.name;
    style.value.parent = parent;
    for (;;) {
        index = named(root, &style);
        if (index == SIZE_MAX) {
            status = tiku_rd_style_append(doc, root, &style, next_id);
            if (status == TIKU_RICH_OK)
                *mapped = style.value.id;
            return status;
        }
        if (equivalent(&root->styles[index].value, &style.value)) {
            *mapped = root->styles[index].value.id;
            return TIKU_RICH_OK;
        }
        rename_style(&style, from->name, ++attempt);
    }
}

/** @brief Resolve one source ancestry from its oldest unmapped parent. */
static tiku_rich_status_t
map_chain(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rd_root_t *source, size_t index, uint64_t *map,
    uint64_t *next_id)
{
    size_t chain[64], n = 0, parent;
    tiku_rich_status_t status;
    uint64_t parent_id;

    while (!map[index]) {
        if (n == 64)
            return TIKU_RICH_INVALID_STYLE;
        chain[n++] = index;
        parent_id = source->styles[index].value.parent;
        if (!parent_id)
            break;
        index = tiku_rd_style_find(source, parent_id);
        if (index == SIZE_MAX)
            return TIKU_RICH_INVALID_STYLE;
    }
    while (n) {
        index = chain[--n];
        parent = tiku_rd_style_find(source, source->styles[index].value.parent);
        status = map_style(doc, root, &source->styles[index],
            parent == SIZE_MAX ? 0 : map[parent], next_id, &map[index]);
        if (status != TIKU_RICH_OK)
            return status;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_fragment_styles(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rd_root_t *source, uint64_t *next_id, uint64_t **out)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    uint64_t *map;
    size_t i;

    *out = NULL;
    if (!source->nstyles)
        return TIKU_RICH_OK;
    map = tiku_rd_alloc(doc->memory, source->nstyles * sizeof *map, &status);
    if (!map)
        return status;
    for (i = 0; i < source->nstyles; i++) {
        status = map_chain(doc, root, source, i, map, next_id);
        if (status != TIKU_RICH_OK) {
            tiku_rd_free(map);
            return status;
        }
    }
    *out = map;
    return TIKU_RICH_OK;
}
