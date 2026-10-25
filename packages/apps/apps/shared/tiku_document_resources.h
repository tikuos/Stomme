/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_document_resources.h - native rich-document observations and checked dispatch.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DOCUMENT_RESOURCES_H_
#define TIKU_DOCUMENT_RESOURCES_H_
#include "tiku_app_resources.h"
#include "tiku_richview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define DR_MEMBER tiku_app_resource_member
#define DR_TEXT(o, k, s)                                                      \
    DR_MEMBER(o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, s))
#define DR_INT(o, k, n)                                                       \
    DR_MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, n))
#define DR_BOOL(o, k, n)                                                      \
    DR_MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, !!(n)))
#define DR_COUNTER(o, k, n) DR_MEMBER(o, k, tiku_app_resource_counter_value(n))
/** @brief Borrowed owner-thread model; hooks preserve application restrictions. */
typedef struct {
    tiku_richdoc_t *doc;
    tiku_richview_t *view;
    const char *path, *type;
    uint32_t window;
    int dirty, busy, read_only;
    uint64_t revision;
    void *context;
    tiku_rich_status_t (*execute)(void *, uint64_t, const tiku_richcmd_t *);
    tiku_resource_error_t (*save)(void *);
} dr_document_t;
/** @brief Native command errors translated without applying fallback edits. */
static inline tiku_resource_error_t dr_status(tiku_rich_status_t status)
{
    switch (status) {
    case TIKU_RICH_OK:
    case TIKU_RICH_NO_CHANGE:
        return 0;
    case TIKU_RICH_READ_ONLY:
        return TIKU_RESOURCE_READ_ONLY;
    case TIKU_RICH_STALE:
        return TIKU_RESOURCE_STALE;
    case TIKU_RICH_BUSY:
    case TIKU_RICH_PENDING:
        return TIKU_RESOURCE_BUSY;
    case TIKU_RICH_NO_MEMORY:
        return TIKU_RESOURCE_NO_MEMORY;
    case TIKU_RICH_LIMIT:
        return TIKU_RESOURCE_LIMIT;
    case TIKU_RICH_UNSUPPORTED:
        return TIKU_RESOURCE_UNSUPPORTED;
    default:
        return TIKU_RESOURCE_VALUE;
    }
}
/** @brief Recognize a current owner-local document path and return its suffix. */
static inline const char *dr_suffix(tiku_app_resources_t *owner,
                                    const dr_document_t *d, const char *path)
{
    const char *id =
        tiku_app_resources_document(owner, tiku_richdoc_identity(d->doc));
    return id && !strncmp(path, "documents/", 10) && strlen(path) > 42 &&
                   !strncmp(path + 10, id, 32) && path[42] == '/'
               ? path + 43
               : NULL;
}
/** @brief Copy text and cell selection identities, including native view restrictions. */
static inline tiku_msg_t *dr_selection(const dr_document_t *d)
{
    tiku_rich_pos_t a = {0}, b = {0};
    tiku_richsession_t *session = tiku_richview_session(d->view);
    if (!session || tiku_richsession_selection(session, &a, &b))
        return NULL;
    tiku_msg_t *value = tiku_app_resource_object(0);
    int bad = 0;
    bad |= DR_INT(value,"anchor_affinity",a.affinity);
    bad |= DR_INT(value,"caret_affinity",b.affinity);
    bad |= DR_COUNTER(value, "anchor_body", a.body);
    bad |= DR_COUNTER(value, "anchor_paragraph", a.paragraph);
    bad |= DR_COUNTER(value, "anchor_byte", a.byte);
    bad |= DR_COUNTER(value, "caret_body", b.body);
    bad |= DR_COUNTER(value, "caret_paragraph", b.paragraph);
    bad |= DR_COUNTER(value, "caret_byte", b.byte);
    bad |= DR_COUNTER(value, "generation", tiku_richdoc_generation(d->doc));
    bad |= DR_COUNTER(value, "document", tiku_richdoc_identity(d->doc));
    bad |= DR_COUNTER(value, "session", tiku_richsession_id(session));
    bad |= DR_COUNTER(value, "revision", d->revision);
    bad |= DR_BOOL(value, "read_only",
                   d->read_only || tiku_richsession_read_only(session));
    tiku_rich_cells_t cells = {0};
    int held = tiku_richview_cells(d->view, &cells) == TIKU_RICH_OK;
    bad |= DR_BOOL(value, "cells", held);
    if (held) {
        tiku_msg_t *v = tiku_app_resource_object(0);
        bad |= DR_COUNTER(v, "table", cells.table);
        bad |= DR_INT(v, "row", cells.row);
        bad |= DR_INT(v, "column", cells.column);
        bad |= DR_INT(v, "rows", cells.rows);
        bad |= DR_INT(v, "columns", cells.columns);
        bad |= DR_MEMBER(value, "rectangle", v);
    }
    if (bad) {
        tiku_msg_free(value);
        return NULL;
    }
    return value;
}
/** @brief Produce a token for the current document, selection and controller state. */
static inline int dr_token(const dr_document_t *d, char out[65])
{
    tiku_msg_t *value = dr_selection(d);
    int error = !value || tiku_app_resource_token(value, out);
    tiku_msg_free(value);
    return error;
}
/** @brief Find an implemented native rich command by its stable name. */
static inline const tiku_richcmd_info_t *dr_command(const char *name)
{
    for (size_t i = 0; i < tiku_richcmd_count(); i++) {
        const tiku_richcmd_info_t *info = tiku_richcmd_at(i);
        if (!strcmp(name, info->key))
            return info;
    }
    return NULL;
}
/** @brief Capture immutable public session facts or native command metadata. */
static inline tiku_resource_error_t dr_capture(tiku_app_resources_t *owner,
                                               const dr_document_t *d,
                                               const char *path,
                                               tiku_resource_snapshot_t *out)
{
    const char *name = dr_suffix(owner, d, path);
    if (!name)
        return TIKU_RESOURCE_UNSUPPORTED;
    tiku_msg_t *value = NULL;
    int bad = 0;
    if (!strcmp(name, "path"))
        value = tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                           d->path ? d->path : "");
    else if (!strcmp(name, "type"))
        value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, d->type);
    else if (!strcmp(name, "dirty"))
        value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, d->dirty);
    else if (!strcmp(name, "selection")) {
        value = dr_selection(d);
        char token[65];
        if (!value || dr_token(d, token))
            bad = 1;
        else
            bad |= DR_TEXT(value, "context", token);
    } else if (!strcmp(name, "identity")) {
        value = tiku_app_resource_object(0);
        bad |= DR_TEXT(
            value, "id",
            tiku_app_resources_document(owner, tiku_richdoc_identity(d->doc)));
        bad |=
            DR_COUNTER(value, "generation", tiku_richdoc_generation(d->doc));
        bad |= DR_INT(value, "window", d->window);
        bad |= DR_BOOL(value, "read_only",
                       d->read_only || tiku_richsession_read_only(
                                           tiku_richview_session(d->view)));
        bad |= DR_BOOL(value, "busy", d->busy);
    } else if (!strcmp(name, "commands") || !strcmp(name, "objects")) {
        value = tiku_app_resource_object(0);
        size_t count = tiku_richcmd_count() + 4;
        if (!strcmp(name, "objects")) {
            tiku_richsnapshot_t *snapshot = NULL;
            if (tiku_richdoc_snapshot(d->doc, &snapshot)) {
                tiku_msg_free(value);
                return TIKU_RESOURCE_UNAVAILABLE;
            }
            count = tiku_richsnapshot_blocks(snapshot);
            tiku_richsnapshot_release(snapshot);
        }
        bad |= DR_INT(value, "count", count);
        bad |= DR_TEXT(value, "scope",
                       !strcmp(name, "objects") ? "snapshot_blocks"
                                                : "native_richview");
    } else if (!strcmp(name, "view")) {
        value = tiku_app_resource_object(0);
        bad |= DR_TEXT(value, "scope", "application_view");
        bad |= DR_INT(value, "window", d->window);
    } else if (!strncmp(name, "commands/", 9)) {
        value = tiku_app_resource_object(0);
        const tiku_richcmd_info_t *info = dr_command(name + 9);
        tiku_richcmd_state_t query = {0};
        if (info) {
            tiku_rich_status_t s =
                tiku_richview_query(d->view, info->kind, &query);
            bad |= DR_BOOL(value, "enabled",
                           !s && query.enabled && !d->read_only && !d->busy);
            bad |= DR_INT(value, "parameter", info->parameter);
            bad |= DR_TEXT(value, "reason", query.reason ? query.reason : "");
        }
        bad |= DR_TEXT(value, "command", name + 9);
        bad |= DR_TEXT(value, "required", "context");
    } else
        return TIKU_RESOURCE_UNSUPPORTED;
    if (bad) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, value, "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Enumerate document observations and native command action schemas. */
static inline int dr_catalog(tiku_app_resources_t *owner,
                             const dr_document_t *d, size_t index,
                             tiku_resource_initial_t *out)
{
    static const char *const base[] = {"identity", "path",      "type",
                                       "dirty",    "selection", "view",
                                       "commands", "objects"};
    static const char *const extra[] = {"save", "select", "read-text",
                                        "read-objects"};
    size_t count = tiku_richcmd_count();
    if (index >= 8 + count + 4)
        return 0;
    const char *id =
        tiku_app_resources_document(owner, tiku_richdoc_identity(d->doc));
    if (!id)
        return -1;
    const char *key = index < 8           ? base[index]
                      : index < 8 + count ? tiku_richcmd_at(index - 8)->key
                                          : extra[index - 8 - count];
    const char *path = tiku_app_resources_path(
        owner, "documents/%s/%s%s", id, index < 8 ? "" : "commands/", key);
    tiku_resource_kind_t kind = index >= 8   ? TIKU_RESOURCE_ACTION
                                : index >= 5 ? TIKU_RESOURCE_COLLECTION
                                             : TIKU_RESOURCE_STATE;
    tiku_resource_type_t type = index == 1 || index == 2 ? TIKU_RESOURCE_TEXT
                                : index == 3 ? TIKU_RESOURCE_BOOLEAN
                                             : TIKU_RESOURCE_OBJECT;
    out->spec =
        (tiku_resource_spec_t){.path = path,
                               .kind = kind,
                               .schema_version = 1,
                               .schema = {.type = type, .text_max = 4096},
                               .persistence = "none"};
    return path && !dr_capture(owner, d, path, &out->initial) ? 1 : -1;
}
/** @brief Accept a subset of unique documented payload fields. */
static inline int dr_keys(const tiku_msg_t *object, const char *const *keys,
                          size_t count)
{
    const char *type = tiku_msg_find_string(object, "type", 0);
    uint64_t seen = 0;
    if (!type || strcmp(type, "object") || count > 64)
        return 0;
    for (int i = 0; i < tiku_msg_count(object, "member"); i++) {
        tiku_msg_t *member = tiku_msg_find_message(object, "member", i);
        const char *name = tiku_msg_find_string(member, "key", 0);
        size_t k;
        for (k = 0; k < count; k++)
            if (name && !strcmp(name, keys[k]))
                break;
        tiku_msg_free(member);
        if (k == count || (seen & (UINT64_C(1) << k)))
            return 0;
        seen |= UINT64_C(1) << k;
    }
    return 1;
}
/** @brief Read a finite number from a typed numeric value. */
static inline int dr_number(const tiku_msg_t *object, const char *key,
                            double low, double high, double *out)
{
    tiku_msg_t *v = tiku_app_resource_find(object, key);
    const char *type = tiku_msg_find_string(v, "type", 0);
    double n = 0;
    int64_t i;
    int ok =
        type &&
        ((!strcmp(type, "number") &&
          tiku_msg_find_double(v, "value", 0, &n)) ||
         (!strcmp(type, "integer") && tiku_msg_find_int64(v, "value", 0, &i) &&
          (n = (double)i, 1))) &&
        isfinite(n) && n >= low && n <= high;
    if (ok)
        *out = n;
    tiku_msg_free(v);
    return ok;
}
/** @brief Whether an optional field is present. */
static inline int dr_has(const tiku_msg_t *object, const char *key)
{
    tiku_msg_t *value = tiku_app_resource_find(object, key);
    int found = value != NULL;
    tiku_msg_free(value);
    return found;
}
/** @brief Decode present character properties and a validated clear mask. */
static inline int dr_chars(const tiku_msg_t *v, tiku_rich_chars_t *chars,
                           unsigned *clear)
{
    static const char *const keys[] = {
        "bold", "italic",     "underline", "strike",   "size",
        "font", "foreground", "highlight", "baseline", "clear"};
    if (!dr_keys(v, keys, 10))
        return 0;
    unsigned char *booleans[] = {&chars->bold, &chars->italic,
                                 &chars->underline, &chars->strike};
    for (size_t i = 0; i < 4; i++)
        if (dr_has(v, keys[i])) {
            int64_t n;
            if (!tiku_app_resource_integer(v, keys[i], 0, 1, &n))
                return 0;
            *booleans[i] = (unsigned char)n;
            chars->present |= 1u << i;
        }
    double size;
    if (dr_has(v, "size")) {
        if (!dr_number(v, "size", 1, 4096, &size))
            return 0;
        chars->size = (float)size;
        chars->present |= TIKU_RICH_SIZE;
    }
    if (dr_has(v, "font")) {
        if (!tiku_app_resource_text(v, "font", chars->font,
                                    sizeof chars->font))
            return 0;
        chars->present |= TIKU_RICH_FONT;
    }
    for (size_t i = 6; i < 9; i++)
        if (dr_has(v, keys[i])) {
            int64_t n;
            if (!tiku_app_resource_integer(v, keys[i], i == 8 ? -1 : 0,
                                           i == 8 ? 1 : UINT32_MAX, &n))
                return 0;
            if (i == 6)
                chars->foreground = (uint32_t)n;
            else if (i == 7)
                chars->highlight = (uint32_t)n;
            else
                chars->baseline = (int)n;
            chars->present |= 1u << i;
        }
    if (dr_has(v, "clear")) {
        int64_t n;
        if (!tiku_app_resource_integer(v, "clear", 0, 511, &n))
            return 0;
        *clear = (unsigned)n;
    }
    return 1;
}
/** @brief Decode public paragraph alignment, indents and spacing properties. */
static inline int dr_para(const tiku_msg_t *v, tiku_rich_para_t *p)
{
    static const char *const keys[] = {
        "align", "left",      "right",      "first",        "before",
        "after", "keep_next", "keep_lines", "break_before", "outline"};
    if (!dr_keys(v, keys, 10))
        return 0;
    if (dr_has(v, "align")) {
        int64_t n;
        if (!tiku_app_resource_integer(v, "align", 0, 3, &n))
            return 0;
        p->align = n;
        p->present |= TIKU_RICH_ALIGNMENT;
    }
    float *dims[] = {&p->left, &p->right, &p->first, &p->before, &p->after};
    for (size_t i = 0; i < 5; i++)
        if (dr_has(v, keys[i + 1])) {
            double n;
            if (!dr_number(v, keys[i + 1], -14400, 14400, &n))
                return 0;
            *dims[i] = (float)n;
            p->present |= i < 3 ? TIKU_RICH_INDENT : TIKU_RICH_SPACING;
        }
    for (size_t i = 6; i < 10; i++)
        if (dr_has(v, keys[i])) {
            int64_t n;
            if (!tiku_app_resource_integer(v, keys[i], 0, i == 9 ? 6 : 1, &n))
                return 0;
            if (i == 6)
                p->keep_next = n;
            else if (i == 7)
                p->keep_lines = n;
            else if (i == 8)
                p->break_before = n;
            else
                p->outline = n;
            p->present |= i < 8    ? TIKU_RICH_KEEP
                          : i == 8 ? TIKU_RICH_PAGE_BREAK
                                   : TIKU_RICH_OUTLINE;
        }
    return 1;
}
/** @brief Decode native table geometry, leaving validation to the document engine. */
static inline int dr_table(const tiku_msg_t *v, tiku_rich_table_t *table)
{
    static const char *const keys[] = {"rows",       "columns", "width",
                                       "padding",    "border",  "color",
                                       "header_rows"};
    int64_t n;
    if (!dr_keys(v, keys, 7) ||
        !tiku_app_resource_integer(v, "rows", 1, 256, &n))
        return 0;
    table->rows = n;
    if (!tiku_app_resource_integer(v, "columns", 1, 64, &n))
        return 0;
    table->columns = n;
    float *dims[] = {&table->width, &table->padding, &table->border};
    for (size_t i = 0; i < 3; i++)
        if (dr_has(v, keys[i + 2])) {
            double x;
            if (!dr_number(v, keys[i + 2], 0,
                           i == 0   ? 14400
                           : i == 1 ? 72
                                    : 12,
                           &x))
                return 0;
            *dims[i] = (float)x;
        }
    if (dr_has(v, "color")) {
        if (!tiku_app_resource_integer(v, "color", 0, UINT32_MAX, &n))
            return 0;
        table->color = n;
    }
    if (dr_has(v, "header_rows")) {
        if (!tiku_app_resource_integer(v, "header_rows", 0, table->rows, &n))
            return 0;
        table->header_rows = n;
    }
    return !tiku_rich_table_validate(table);
}
/** @brief Decode a canonical hexadecimal digit, rejecting punctuation and uppercase. */
static inline int dr_hex(char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
}
/** @brief Decode bounded inline ARGB pixels without a file or host-resource handle. */
static inline int dr_image(const tiku_msg_t *value, tiku_rich_image_t *image,
                           uint32_t **pixels, char alt[1024])
{
    const char *keys[] = {"pixel_width", "pixel_height", "width",
                          "height",      "alt",          "pixels"};
    int64_t w, h;
    double width, height;
    if (!tiku_app_resource_keys(value, keys, 6) ||
        !tiku_app_resource_integer(value, "pixel_width", 1, 4096, &w) ||
        !tiku_app_resource_integer(value, "pixel_height", 1, 4096, &h) ||
        w * h > 16384 || !dr_number(value, "width", 1, 14400, &width) ||
        !dr_number(value, "height", 1, 14400, &height) ||
        !tiku_app_resource_text(value, "alt", alt, 1024))
        return 0;
    tiku_msg_t *chunks = tiku_app_resource_find(value, "pixels");
    const char *type = tiku_msg_find_string(chunks, "type", 0);
    if (!type || strcmp(type, "array")) {
        tiku_msg_free(chunks);
        return 0;
    }
    size_t count = (size_t)(w * h), at = 0;
    *pixels = calloc(count, sizeof **pixels);
    if (!*pixels) {
        tiku_msg_free(chunks);
        return 0;
    }
    int valid = 1;
    for (int i = 0; i < tiku_msg_count(chunks, "item") && valid; i++) {
        tiku_msg_t *item = tiku_msg_find_message(chunks, "item", i);
        const char *kind = tiku_msg_find_string(item, "type", 0),
                   *text = tiku_msg_find_string(item, "value", 0);
        size_t n = text ? strlen(text) : 0;
        if (!kind || strcmp(kind, "text") || !text || n % 8 ||
            n / 8 > count - at)
            valid = 0;
        if (valid)
            for (size_t k = 0; k < n; k++) {
                int digit = dr_hex(text[k]);
                if (digit < 0) {
                    valid = 0;
                    break;
                }
                (*pixels)[at + k / 8] =
                    ((*pixels)[at + k / 8] << 4) | (unsigned)digit;
            }
        at += n / 8;
        tiku_msg_free(item);
    }
    tiku_msg_free(chunks);
    if (!valid || at != count)
        return 0;
    *image = (tiku_rich_image_t){.pixel_width = w,
                                 .pixel_height = h,
                                 .pixels = *pixels,
                                 .width = width,
                                 .height = height,
                                 .alt = alt};
    return !tiku_rich_image_validate(image);
}
/** @brief Decode live document-local source endpoints for an owned native fragment. */
static inline int dr_fragment(const dr_document_t *d, const tiku_msg_t *value,
                              tiku_richfragment_t **out)
{
    const char *keys[] = {"first_body", "first_paragraph", "first_byte",
                          "last_body",  "last_paragraph",  "last_byte"};
    uint64_t n[6];
    if (!tiku_app_resource_keys(value, keys, 6))
        return 0;
    for (size_t i = 0; i < 6; i++)
        if (!tiku_app_resource_counter(value, keys[i], &n[i]))
            return 0;
    tiku_richsnapshot_t *snapshot = NULL;
    if (tiku_richdoc_snapshot(d->doc, &snapshot))
        return 0;
    tiku_rich_status_t status = tiku_richfragment_create(
        snapshot,
        (tiku_rich_pos_t){.body = n[0], .paragraph = n[1], .byte = n[2]},
        (tiku_rich_pos_t){.body = n[3], .paragraph = n[4], .byte = n[5]}, out);
    tiku_richsnapshot_release(snapshot);
    return !status;
}
/** @brief Decode a complete named style through public literal property types. */
static inline int dr_definition(const tiku_msg_t *value,
                                tiku_richcmd_t *command)
{
    const char *keys[] = {"id",     "name",  "kind",
                          "parent", "chars", "paragraph"};
    uint64_t id, parent;
    int64_t kind;
    if (!tiku_app_resource_keys(value, keys, 6) ||
        !tiku_app_resource_counter(value, "id", &id) ||
        !tiku_app_resource_counter(value, "parent", &parent) ||
        !tiku_app_resource_integer(value, "kind", 0, 1, &kind) ||
        !tiku_app_resource_text(value, "name",
                                command->value.definition.value.name,
                                sizeof command->value.definition.value.name))
        return 0;
    tiku_msg_t *chars = tiku_app_resource_find(value, "chars"),
               *para = tiku_app_resource_find(value, "paragraph");
    unsigned clear = 0;
    int valid =
        dr_chars(chars, &command->value.definition.value.chars, &clear) &&
        !clear && dr_para(para, &command->value.definition.value.paragraph);
    tiku_msg_free(chars);
    tiku_msg_free(para);
    command->value.definition.style = id;
    command->value.definition.value.parent = parent;
    command->value.definition.value.kind = kind;
    return valid;
}
/** @brief Copy one bounded paragraph slice without flattening the whole document. */
static inline tiku_resource_error_t dr_read(const dr_document_t *d,
                                            const tiku_msg_t *input,
                                            tiku_msg_t **result, int objects)
{
    const char *object_keys[] = {"context", "offset", "limit"},
               *text_keys[] = {"context", "offset", "byte", "limit"};
    int64_t offset, limit, byte = 0;
    if (!tiku_app_resource_keys(input, objects ? object_keys : text_keys,
                                objects ? 3 : 4) ||
        !tiku_app_resource_integer(input, "offset", 0, INT32_MAX, &offset) ||
        !tiku_app_resource_integer(input, "limit", 1, objects ? 32 : 4096,
                                   &limit) ||
        (!objects &&
         !tiku_app_resource_integer(input, "byte", 0, INT32_MAX, &byte)))
        return TIKU_RESOURCE_VALUE;
    tiku_richsnapshot_t *snapshot = NULL;
    tiku_rich_status_t status = tiku_richdoc_snapshot(d->doc, &snapshot);
    if (status)
        return dr_status(status);
    tiku_msg_t *out = tiku_app_resource_object(0),
               *rows = tiku_app_resource_object(1);
    int bad = 0;
    size_t count = objects ? tiku_richsnapshot_blocks(snapshot)
                           : tiku_richsnapshot_paragraphs(snapshot),
           next = (size_t)offset, next_byte = 0;
    if (next > count || (next == count && byte)) {
        tiku_richsnapshot_release(snapshot);
        tiku_msg_free(out);
        tiku_msg_free(rows);
        return TIKU_RESOURCE_VALUE;
    }
    if (objects)
        for (int64_t i = 0; i < limit && next < count; i++, next++) {
            tiku_rich_block_t block;
            status = tiku_richsnapshot_block(snapshot, next, &block);
            if (status)
                break;
            char row[80];
            snprintf(row, sizeof row, "%llu:%llu:%d",
                     (unsigned long long)block.id,
                     (unsigned long long)block.parent, (int)block.kind);
            bad |= tiku_app_resource_item(
                rows, tiku_resource_value_string(TIKU_RESOURCE_TEXT, row));
        }
    else if (next < count) {
        tiku_rich_paragraph_t p;
        status = tiku_richsnapshot_paragraph(snapshot, next, &p);
        if (!status) {
            if ((size_t)byte > p.length ||
                ((size_t)byte < p.length &&
                 ((unsigned char)p.text[byte] & 0xc0) == 0x80))
                status = TIKU_RICH_INVALID_RANGE;
            else {
                size_t end = (size_t)byte + (size_t)limit;
                if (end > p.length)
                    end = p.length;
                while (end > (size_t)byte && end < p.length &&
                       ((unsigned char)p.text[end] & 0xc0) == 0x80)
                    end--;
                if (end == (size_t)byte && end < p.length)
                    status = TIKU_RICH_LIMIT;
                else {
                    char text[4097];
                    size_t bytes = end - (size_t)byte;
                    if (bytes)
                        memcpy(text, p.text + byte, bytes);
                    text[bytes] = 0;
                    bad |= DR_TEXT(out, "text", text);
                    bad |= DR_COUNTER(out, "paragraph", p.id);
                    bad |= DR_INT(out, "bytes", bytes);
                    if (end == p.length)
                        next++;
                    else
                        next_byte = end;
                }
            }
        }
    }
    bad |= DR_TEXT(out,"object_encoding","id:parent:kind");
    bad |= DR_MEMBER(out, "objects", rows);
    bad |= DR_INT(out, "next", next);
    bad |= DR_INT(out, "next_byte", next_byte);
    bad |= DR_BOOL(out, "eof", next == count);
    tiku_richsnapshot_release(snapshot);
    if (status || bad) {
        tiku_msg_free(out);
        return status ? dr_status(status) : TIKU_RESOURCE_NO_MEMORY;
    }
    *result = out;
    return 0;
}
/** @brief Execute a native rich command against a current context and typed payload. */
static inline tiku_resource_error_t
dr_change(tiku_app_resources_t *owner, dr_document_t *d, const char *path,
          const tiku_msg_t *input, int invoke, tiku_resource_snapshot_t *out,
          tiku_resource_effect_t *effect)
{
    const char *name = dr_suffix(owner, d, path);
    if (!name || strncmp(name, "commands/", 9) || !invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    name += 9;
    char expected[65], actual[65];
    if (!tiku_app_resource_text(input, "context", expected, sizeof expected))
        return TIKU_RESOURCE_VALUE;
    if (dr_token(d, actual))
        return TIKU_RESOURCE_UNAVAILABLE;
    if (strcmp(expected, actual))
        return TIKU_RESOURCE_STALE;
    if (d->busy)
        return TIKU_RESOURCE_BUSY;
    tiku_msg_t *result = NULL;
    tiku_resource_error_t error = 0;
    int read = !strcmp(name, "read-text") || !strcmp(name, "read-objects");
    if (read)
        error = dr_read(d, input, &result, !strcmp(name, "read-objects"));
    else if (!strcmp(name, "save")) {
        const char *keys[] = {"context"};
        if (!tiku_app_resource_keys(input, keys, 1))
            return TIKU_RESOURCE_VALUE;
        error = d->save ? d->save(d->context) : TIKU_RESOURCE_UNSUPPORTED;
    } else if (!strcmp(name, "select")) {
        const char *keys[] = {"context",     "anchor_body", "anchor_paragraph",
                              "anchor_byte", "caret_body",  "caret_paragraph",
                              "caret_byte"};
        uint64_t n[6];
        if (!tiku_app_resource_keys(input, keys, 7))
            return TIKU_RESOURCE_VALUE;
        for (size_t i = 0; i < 6; i++)
            if (!tiku_app_resource_counter(input, keys[i + 1], &n[i]))
                return TIKU_RESOURCE_VALUE;
        error = dr_status(tiku_richsession_select(
            tiku_richview_session(d->view), tiku_richdoc_generation(d->doc),
            (tiku_rich_pos_t){.body = n[0], .paragraph = n[1], .byte = n[2]},
            (tiku_rich_pos_t){.body = n[3], .paragraph = n[4], .byte = n[5]}));
    } else {
        if (d->read_only)
            return TIKU_RESOURCE_READ_ONLY;
        const tiku_richcmd_info_t *info = dr_command(name);
        if (!info)
            return TIKU_RESOURCE_UNSUPPORTED;
        tiku_richcmd_t command = {.kind = info->kind};
        const char *plain[] = {"context"}, *with[] = {"context", "payload"};
        if (!tiku_app_resource_keys(input, info->parameter ? with : plain,
                                    info->parameter ? 2 : 1))
            return TIKU_RESOURCE_VALUE;
        tiku_msg_t *payload = tiku_app_resource_find(input, "payload");
        char text[4097], alt[1024];
        int64_t n = 0;
        int valid = 1;
        uint64_t id = 0;
        uint32_t *pixels = NULL;
        tiku_richfragment_t *fragment = NULL;
        tiku_rich_image_t image = {0};
        switch (info->parameter) {
        case TIKU_RICHCMD_NO_PARAMETER:
            break;
        case TIKU_RICHCMD_TEXT_PARAMETER:
        case TIKU_RICHCMD_LINK_PARAMETER:
            valid =
                tiku_app_resource_text(input, "payload", text, sizeof text);
            if (info->parameter == TIKU_RICHCMD_TEXT_PARAMETER) {
                command.value.text.data = text;
                command.value.text.length = strlen(text);
            } else
                command.value.link = text;
            break;
        case TIKU_RICHCMD_IMAGE_PARAMETER:
            valid = dr_image(payload, &image, &pixels, alt);
            command.value.image = &image;
            break;
        case TIKU_RICHCMD_FRAGMENT_PARAMETER:
            valid = dr_fragment(d, payload, &fragment);
            command.value.fragment = fragment;
            break;
        case TIKU_RICHCMD_DEFINITION_PARAMETER:
            valid = dr_definition(payload, &command);
            break;
        case TIKU_RICHCMD_CHARACTER_PARAMETER:
            valid = dr_chars(payload, &command.value.character.values,
                             &command.value.character.clear);
            break;
        case TIKU_RICHCMD_PARAGRAPH_PARAMETER:
            valid = dr_para(payload, &command.value.paragraph.values);
            break;
        case TIKU_RICHCMD_STYLE_PARAMETER:
            valid = tiku_app_resource_counter(input, "payload", &id);
            command.value.style = id;
            break;
        case TIKU_RICHCMD_COLOR_PARAMETER:
            valid =
                tiku_app_resource_integer(input, "payload", 0, UINT32_MAX, &n);
            command.value.color = n;
            break;
        case TIKU_RICHCMD_TABLE_PARAMETER:
            valid = dr_table(payload, &command.value.table);
            break;
        case TIKU_RICHCMD_LIST_PARAMETER: {
            const char *keys[] = {"kind", "level", "restart"};
            valid = tiku_app_resource_keys(payload, keys, 3) &&
                    tiku_app_resource_integer(payload, "kind", 0, 2, &n);
            command.value.list.kind = n;
            valid &= tiku_app_resource_integer(payload, "level", 0, 8, &n);
            command.value.list.level = n;
            valid &= tiku_app_resource_integer(payload, "restart", 0,
                                               UINT32_MAX, &n);
            command.value.list.restart = n;
            break;
        }
#ifdef TIKU_APP_DOCUMENT_ADVANCED
        case TIKU_RICHCMD_CASE_PARAMETER:
            valid =
                tiku_app_resource_integer(input, "payload", 0, INT32_MAX, &n);
            command.value.case_mode = n;
            break;
        case TIKU_RICHCMD_FORMAT_SAMPLE_PARAMETER: {
            const char *keys[] = {"chars", "paragraph"};
            tiku_msg_t *chars = tiku_app_resource_find(payload, "chars"),
                       *para = tiku_app_resource_find(payload, "paragraph");
            unsigned clear = 0;
            valid = dr_keys(payload, keys, 2) && chars &&
                    dr_chars(chars, &command.value.format.chars, &clear) &&
                    !clear;
            if (para) {
                command.value.format.paragraphs = 1;
                valid =
                    valid && dr_para(para, &command.value.format.paragraph);
            }
            tiku_msg_free(chars);
            tiku_msg_free(para);
            break;
        }
#endif
        default:
            tiku_msg_free(payload);
            return TIKU_RESOURCE_UNSUPPORTED;
        }
        if (valid)
            error = dr_status(
                d->execute
                    ? d->execute(d->context, tiku_richdoc_generation(d->doc),
                                 &command)
                    : tiku_richview_execute(
                          d->view, tiku_richdoc_generation(d->doc), &command));
        else
            error = TIKU_RESOURCE_VALUE;
        tiku_msg_free(payload);
        free(pixels);
        tiku_richfragment_release(fragment);
    }
    if (error) {
        tiku_msg_free(result);
        return error;
    }
    if (!read)
        *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
    if (!result)
        result = tiku_app_resource_object(0);
    int bad = DR_TEXT(result, "command", name) |
              DR_BOOL(result, "dirty", tiku_richdoc_dirty(d->doc));
    if (dr_token(d, actual))
        bad = 1;
    else
        bad |= DR_TEXT(result, "context", actual);
    if (bad) {
        tiku_msg_free(result);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, result,
                                      !strcmp(name, "save")
                                          ? "native_save_queued"
                                          : "native_complete")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
#endif
