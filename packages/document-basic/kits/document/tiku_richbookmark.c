/*
 * TikuDesktop Document Kit.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_richbookmark.c - checked named locations and local link resolution.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"
#include <string.h>

tiku_rich_status_t
tiku_rd_bookmarks_validate(const tiku_rich_source_t *source)
{
    const tiku_rich_bookmark_source_t *b;
    const tiku_rich_paragraph_source_t *p;
    size_t i, j;

    if (source->bookmark_count > TIKU_RICH_BOOKMARKS_MAX) return TIKU_RICH_LIMIT;
    if (source->bookmark_count && !source->bookmarks) return TIKU_RICH_INVALID;
    for (i = 0; i < source->bookmark_count; i++) {
        b = &source->bookmarks[i];
        if (!tiku_rd_name(b->name) || b->after > 1) return TIKU_RICH_INVALID;
        for (j = 0; j < i; j++)
            if (!strcmp(b->name, source->bookmarks[j].name))
                return TIKU_RICH_INVALID;
        if (b->paragraph >= source->paragraph_count)
            return TIKU_RICH_INVALID_RANGE;
        p = &source->paragraphs[b->paragraph];
        if (!tiku_rd_boundary(p->text, p->length, b->byte))
            return TIKU_RICH_INVALID_RANGE;
    }
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_rd_bookmarks_build(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rich_source_t *source)
{
    tiku_rich_status_t status = TIKU_RICH_OK;
    tiku_rd_bookmarks_t *marks;
    const tiku_rich_bookmark_source_t *s;
    size_t i;

    if (!source->bookmark_count) return status;
    marks = tiku_rd_alloc(doc->memory, sizeof *marks +
        source->bookmark_count * sizeof *marks->items, &status);
    if (!marks) return status;
    atomic_init(&marks->refs, 1);
    marks->count = source->bookmark_count;
    for (i = 0; i < marks->count; i++) {
        s = &source->bookmarks[i];
        strcpy(marks->items[i].name, s->name);
        marks->items[i].position = (tiku_rich_pos_t){root->body,
            root->paragraphs[s->paragraph]->value.id, s->byte,
            s->after ? TIKU_RICH_AFTER : TIKU_RICH_BEFORE};
    }
    root->bookmarks = marks;
    return status;
}

void
tiku_rd_bookmarks_drop(tiku_rd_bookmarks_t *marks)
{
    if (marks && atomic_fetch_sub_explicit(&marks->refs, 1,
                                          memory_order_acq_rel) == 1)
        tiku_rd_free(marks);
}

size_t
tiku_richsnapshot_bookmarks(const tiku_richsnapshot_t *snapshot)
{
    return snapshot && snapshot->root->bookmarks ?
           snapshot->root->bookmarks->count : 0;
}

tiku_rich_status_t
tiku_richsnapshot_bookmark(const tiku_richsnapshot_t *snapshot, size_t index,
    tiku_rich_bookmark_t *out)
{
    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot) return TIKU_RICH_INVALID;
    if (index >= tiku_richsnapshot_bookmarks(snapshot))
        return TIKU_RICH_INVALID_RANGE;
    *out = snapshot->root->bookmarks->items[index];
    return TIKU_RICH_OK;
}

tiku_rich_status_t
tiku_richsnapshot_find_bookmark(const tiku_richsnapshot_t *snapshot,
    const char *name, tiku_rich_pos_t *out)
{
    size_t i;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot || !name || !tiku_rd_name(name)) return TIKU_RICH_INVALID;
    for (i = 0; i < tiku_richsnapshot_bookmarks(snapshot); i++)
        if (!strcmp(name, snapshot->root->bookmarks->items[i].name)) {
            *out = snapshot->root->bookmarks->items[i].position;
            return TIKU_RICH_OK;
        }
    return TIKU_RICH_NOT_FOUND;
}

/** @brief Convert a validated hexadecimal digit to its unsigned value. */
static unsigned
hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

tiku_rich_status_t
tiku_richsnapshot_resolve_link(const tiku_richsnapshot_t *snapshot,
    const char *target, tiku_rich_pos_t *out)
{
    char name[64];
    size_t i = 1, length = 0;
    tiku_rich_status_t status;

    if (!out) return TIKU_RICH_INVALID;
    memset(out, 0, sizeof *out);
    if (!snapshot || !target || !*target) return TIKU_RICH_INVALID;
    status = tiku_rich_link_validate(target);
    if (status != TIKU_RICH_OK) return status;
    if (*target != '#') return TIKU_RICH_UNSUPPORTED;
    while (target[i]) {
        if (length == sizeof name - 1) return TIKU_RICH_LIMIT;
        if (target[i] == '%') {
            name[length++] = (char)((hex(target[i + 1]) << 4) | hex(target[i + 2]));
            i += 3;
        } else name[length++] = target[i++];
    }
    name[length] = 0;
    return tiku_richsnapshot_find_bookmark(snapshot, name, out);
}
