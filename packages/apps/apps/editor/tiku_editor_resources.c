/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_editor_resources.c - native Editor preferences and guarded document commands.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_editor_private.h"
#include "tiku_app_resources.h"
#include <stddef.h>
#include "tiku_crypto.h"
#define MEMBER tiku_app_resource_member
#define TEXT(o, k, s)                                                         \
    MEMBER(o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, s))
#define INT(o, k, n)                                                          \
    MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, n))
#define BOOL(o, k, n)                                                         \
    MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, !!(n)))
/** @brief Native preference fields and their accepted scalar ranges. */
static const struct {
    const char *name;
    size_t offset;
    int low, high, boolean;
} fields[] = {
#define FIELD(n, l, h, b) {#n, offsetof(editor_preferences_t, n), l, h, b}
    FIELD(tab, 1, 32, 0),
    FIELD(indent, 0, 32, 0),
    FIELD(column, 1, 240, 0),
    FIELD(zoom, -2, 4, 0),
    FIELD(colors, 0, 2, 0),
    FIELD(numbers, 0, 1, 1),
    FIELD(wrap, 0, 1, 1),
    FIELD(guides, 0, 1, 1),
    FIELD(ruler, 0, 1, 1),
    FIELD(invisibles, 0, 1, 1),
    FIELD(caret_line, 0, 1, 1),
    FIELD(block_caret, 0, 1, 1)
#undef FIELD
};
/** @brief Resource command names paired with the native view dispatch enum. */
static const char *const commands[] = {
    "undo",      "redo",         "copy",   "cut",    "paste",     "select-all",
    "newline",   "backspace",    "delete", "indent", "unindent",  "comment",
    "duplicate", "delete-lines", "fold",   "unfold", "line-ends", "keep-main",
    "move",      "replace-text", "select", "save",   "read-text"};
static const char *const observations[] = {"identity", "path",      "type",
                                           "dirty",    "selection", "view",
                                           "commands", "objects"};
/** @brief Find a preference without accepting an arbitrary structure offset. */
static int field(const char *path)
{
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
        if (!strncmp(path, "preferences/", 12) &&
            !strcmp(path + 12, fields[i].name))
            return (int)i;
    return -1;
}
/** @brief Borrow one validated native preference field. */
static int *preference(editor_preferences_t *prefs, int i)
{
    return (int *)((char *)prefs + fields[i].offset);
}
/** @brief Copy a bounded selection preview and hash every native range. */
static tiku_msg_t *selection(edit_state_t *st)
{
 size_t main=0,count=tiku_codeview_selections(st->view,NULL,0,&main);
 if(count>SIZE_MAX/sizeof(tiku_edit_selection_t))return NULL;
 tiku_edit_selection_t *ranges = count ? malloc(count * sizeof *ranges) : NULL;
 if (count && !ranges)
     return NULL;
 if (tiku_codeview_selections(st->view, ranges, count, &main) != count) {
     free(ranges);
     return NULL;
 }
 tiku_hash_t hash;
 unsigned char bytes[32];
 char digest[65];
 tiku_hash_init(&hash, TIKU_HASH_SHA256);
 for (size_t i = 0; i < count; i++) {
     char row[64];
     int n = snprintf(row, sizeof row, "%lld:%lld;",
                      (long long)ranges[i].anchor, (long long)ranges[i].caret);
     tiku_hash_update(&hash, row, n);
 }
 tiku_hash_final(&hash, bytes);
 for (size_t i = 0; i < 32; i++)
     snprintf(digest + 2 * i, 3, "%02x", bytes[i]);
 tiku_msg_t *value = tiku_app_resource_object(0),
            *list = tiku_app_resource_object(1);
 int bad = INT(value, "main", main) | INT(value, "count", count) |
           BOOL(value, "truncated", count > 16) |
           TEXT(value, "selection_digest", digest);
 bad |=
     MEMBER(value, "view",
            tiku_app_resource_counter_value(
                ((uint64_t)tiku_codeview_doc_view(st->view).generation << 32) |
                tiku_codeview_doc_view(st->view).slot));
 for (size_t i = 0; i < count && i < 16; i++) {
     char row[64];
     snprintf(row, sizeof row, "%lld:%lld", (long long)ranges[i].anchor,
              (long long)ranges[i].caret);
     bad |= tiku_app_resource_item(
         list, tiku_resource_value_string(TIKU_RESOURCE_TEXT, row));
 }
 if (main < count) {
     bad |= INT(value, "main_anchor", ranges[main].anchor);
     bad |= INT(value, "main_caret", ranges[main].caret);
 }
 free(ranges);
 bad |= MEMBER(value, "ranges", list);
 bad |=
     MEMBER(value, "generation",
            tiku_app_resource_counter_value(tiku_editdoc_generation(st->doc)));
 bad |=
     MEMBER(value, "document",
            tiku_app_resource_counter_value(tiku_editdoc_identity(st->doc)));
 if(bad){tiku_msg_free(value);return NULL;}return value;
}
/** @brief Produce the precondition from native document and selection state. */
static int context(edit_state_t *st, char token[65])
{
    tiku_msg_t *value = selection(st);
    int error = !value || tiku_app_resource_token(value, token);
    tiku_msg_free(value);
    return error;
}
/** @brief Resolve a live document UUID without accepting another application's path. */
static const char *suffix(edit_state_t *st, tiku_app_resources_t *owner,
                          const char *path)
{
    const char *id =
        tiku_app_resources_document(owner, tiku_editdoc_identity(st->doc));
    if (!id || strncmp(path, "documents/", 10) || strncmp(path + 10, id, 32) ||
        path[42] != '/')
        return NULL;
    return path + 43;
}
/** @brief Capture the current document's public observations on its owner thread. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    edit_state_t *st = state;
    int i = field(path);
    tiku_msg_t *value = NULL;
    int bad = 0;
    if (i >= 0)
        value = tiku_resource_value_integer(
            fields[i].boolean ? TIKU_RESOURCE_BOOLEAN : TIKU_RESOURCE_INTEGER,
            *preference(&st->preferences, i));
    else {
        const char *name = suffix(st, owner, path);
        if (!name)
            return TIKU_RESOURCE_UNSUPPORTED;
        if (!strcmp(name, "path"))
            value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, st->path);
        else if (!strcmp(name, "type"))
            value = tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                               "text/plain; charset=utf-8");
        else if (!strcmp(name, "dirty"))
            value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                                tiku_editor_modified(st));
        else if (!strcmp(name, "selection")) {
            value = selection(st);
            char token[65];
            if (!value || context(st, token))
                bad = 1;
            else
                bad |= TEXT(value, "context", token);
        } else if (!strcmp(name, "identity")) {
            value = tiku_app_resource_object(0);
            bad |= TEXT(value, "id",
                        tiku_app_resources_document(
                            owner, tiku_editdoc_identity(st->doc)));
            bad |= INT(value, "window", st->id);
            bad |= MEMBER(value, "generation",
                          tiku_app_resource_counter_value(
                              tiku_editdoc_generation(st->doc)));
            bad |= BOOL(value, "read_only", tiku_editdoc_read_only(st->doc));
            bad |= BOOL(value, "load_failed", st->load_failed);
        } else if (!strcmp(name, "view")) {
            value = tiku_app_resource_object(0);
            bad |= BOOL(value, "numbers", st->numbers);
            bad |= BOOL(value, "wrap", st->wrap);
            bad |= BOOL(value, "invisibles", st->show_invisibles);
            bad |= INT(value, "zoom", tiku_codeview_zoom(st->view));
        } else if (!strcmp(name, "commands") || !strcmp(name, "objects")) {
            value = tiku_app_resource_object(0);
            bad |= INT(value, "count",
                       !strcmp(name, "commands")
                           ? sizeof commands / sizeof commands[0]
                           : 0);
            bad |= TEXT(value, "scope",
                        !strcmp(name, "objects") ? "plain_text"
                                                 : "native_codeview");
        } else if (!strncmp(name, "commands/", 9)) {
            value = tiku_app_resource_object(0);
            bad |= TEXT(value, "command", name + 9);
            bad |= TEXT(value, "required", "context");
        } else
            return TIKU_RESOURCE_UNSUPPORTED;
    }
    if (bad) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, value, "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Enumerate native preferences and the live document command schema. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    edit_state_t *st = state;
    size_t nf = sizeof fields / sizeof fields[0],
           no = sizeof observations / sizeof observations[0],
           nc = sizeof commands / sizeof commands[0];
    const char *path;
    tiku_resource_kind_t kind;
    tiku_resource_type_t type = TIKU_RESOURCE_OBJECT;
    if (index < nf) {
        path = tiku_app_resources_path(owner, "preferences/%s",
                                       fields[index].name);
        kind = TIKU_RESOURCE_SETTING;
        type = fields[index].boolean ? TIKU_RESOURCE_BOOLEAN
                                     : TIKU_RESOURCE_INTEGER;
    } else {
        index -= nf;
        if (index >= no + nc)
            return 0;
        const char *id =
            tiku_app_resources_document(owner, tiku_editdoc_identity(st->doc));
        if (!id)
            return -1;
        path = tiku_app_resources_path(owner, "documents/%s/%s%s", id,
                                       index < no ? observations[index]
                                                  : "commands/",
                                       index < no ? "" : commands[index - no]);
        kind = index >= no  ? TIKU_RESOURCE_ACTION
               : index >= 6 ? TIKU_RESOURCE_COLLECTION
                            : TIKU_RESOURCE_STATE;
        if (index == 1 || index == 2)
            type = TIKU_RESOURCE_TEXT;
        else if (index == 3)
            type = TIKU_RESOURCE_BOOLEAN;
    }
    if (!path)
        return -1;
    out->spec = (tiku_resource_spec_t){.path = path,
                                       .kind = kind,
                                       .schema_version = 1,
                                       .schema = {.type = type},
                                       .persistence = "none"};
    int f = field(path);
    if (f >= 0) {
        out->spec.schema.integer_min = fields[f].low;
        out->spec.schema.integer_max = fields[f].high;
        out->spec.persistence = "user";
    }
    if (type == TIKU_RESOURCE_TEXT)
        out->spec.schema.text_max = 512;
    return capture(st, owner, path, &out->initial) ? -1 : 1;
}
/** @brief Map a native edit refusal without changing its document or history. */
static tiku_resource_error_t status(tiku_edit_status_t code)
{
    if (code == TIKU_EDIT_OK || code == TIKU_EDIT_NOTHING)
        return 0;
    if (code == TIKU_EDIT_READ_ONLY)
        return TIKU_RESOURCE_READ_ONLY;
    if (code == TIKU_EDIT_STALE)
        return TIKU_RESOURCE_STALE;
    if (code == TIKU_EDIT_BUSY)
        return TIKU_RESOURCE_BUSY;
    if (code == TIKU_EDIT_NO_MEMORY)
        return TIKU_RESOURCE_NO_MEMORY;
    return TIKU_RESOURCE_VALUE;
}
/** @brief Persist validated defaults or dispatch a selection-checked native command. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *input,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    edit_state_t *st = state;
    int f = field(path);
    if (f >= 0) {
        if (invoke)
            return TIKU_RESOURCE_UNSUPPORTED;
        int64_t n;
        const char *type = tiku_msg_find_string(input, "type", 0);
        if (!type || strcmp(type, fields[f].boolean ? "boolean" : "integer") ||
            !tiku_app_resource_scalar(input,
                                      fields[f].boolean
                                          ? TIKU_RESOURCE_BOOLEAN
                                          : TIKU_RESOURCE_INTEGER,
                                      &n) ||
            n < fields[f].low || n > fields[f].high)
            return TIKU_RESOURCE_VALUE;
        editor_preferences_t next = st->preferences;
        *preference(&next, f) = (int)n;
        int saved = tiku_editor_preferences_replace(st, &next);
        if (saved < 0)
            return TIKU_RESOURCE_PERSISTENCE_FAILED;
        *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
        tiku_resource_error_t error = capture(st, owner, path, out);
        if (!error && saved)
            snprintf(out->apply_state, sizeof out->apply_state,
                     "applied_directory_unsynced");
        return error;
    }
    const char *name = suffix(st, owner, path);
    if (!name || strncmp(name, "commands/", 9) || !invoke)
        return TIKU_RESOURCE_UNSUPPORTED;
    name += 9;
    if (st->pending || st->prefs_id || st->context_open)
        return TIKU_RESOURCE_BUSY;
    char expected[65], actual[65];
    if (!tiku_app_resource_text(input, "context", expected, sizeof expected))
        return TIKU_RESOURCE_VALUE;
    if (context(st, actual))
        return TIKU_RESOURCE_UNAVAILABLE;
    if (strcmp(expected, actual))
        return TIKU_RESOURCE_STALE;
    tiku_msg_t *result = NULL;
    int bad = 0;
    tiku_resource_error_t error = 0;
    if (!strcmp(name, "read-text")) {
        const char *keys[] = {"context", "offset", "limit"};
        int64_t offset, limit;
        size_t bytes = 0;
        char text[4097];
        if (!tiku_app_resource_keys(input, keys, 3) ||
            !tiku_app_resource_integer(
                input, "offset", 0, tiku_editdoc_length(st->doc), &offset) ||
            !tiku_app_resource_integer(input, "limit", 1, 4096, &limit))
            return TIKU_RESOURCE_VALUE;
        if (offset < tiku_editdoc_length(st->doc) &&
            (tiku_editdoc_byte_at(st->doc, offset) & 0xc0) == 0x80)
            return TIKU_RESOURCE_VALUE;
        int64_t end = offset + limit;
        if (end > tiku_editdoc_length(st->doc))
            end = tiku_editdoc_length(st->doc);
        while (end > offset &&
               (tiku_editdoc_byte_at(st->doc, end) & 0xc0) == 0x80)
            end--;
        if (end == offset && end < tiku_editdoc_length(st->doc))
            return TIKU_RESOURCE_LIMIT;
        error =
            status(tiku_editdoc_read(st->doc, (tiku_edit_range_t){offset, end},
                                     text, sizeof text, &bytes));
        if (error)
            return error;
        result = tiku_app_resource_object(0);
        bad |= TEXT(result, "text", text);
        bad |= INT(result, "next", end);
        bad |= BOOL(result, "eof", end == tiku_editdoc_length(st->doc));
    } else if (!strcmp(name, "replace-text")) {
        const char *keys[] = {"context", "text"};
        char text[4097];
        if (!tiku_app_resource_keys(input, keys, 2) ||
            !tiku_app_resource_text(input, "text", text, sizeof text))
            return TIKU_RESOURCE_VALUE;
        error = status(tiku_codeview_insert(st->view, text, strlen(text), 0));
    } else if (!strcmp(name, "select")) {
        const char *keys[] = {"context", "anchor", "caret"};
        int64_t a, b;
        if (!tiku_app_resource_keys(input, keys, 3) ||
            !tiku_app_resource_integer(input, "anchor", 0, INT64_MAX, &a) ||
            !tiku_app_resource_integer(input, "caret", 0, INT64_MAX, &b))
            return TIKU_RESOURCE_VALUE;
        error = status(tiku_codeview_select(st->view, a, b));
    } else if (!strcmp(name, "save")) {
        const char *keys[] = {"context"};
        if (!tiku_app_resource_keys(input, keys, 1))
            return TIKU_RESOURCE_VALUE;
        if (!tiku_editor_save(st))
            return TIKU_RESOURCE_APPLY_FAILED;
    } else {
        size_t i;
        for (i = 0; i < 19; i++)
            if (!strcmp(name, commands[i]))
                break;
        if (i == 19)
            return TIKU_RESOURCE_UNSUPPORTED;
        tiku_codeview_command_t command =
            (tiku_codeview_command_t)(TIKU_CODEVIEW_CMD_UNDO + i);
        int64_t move = 0, extend = 0;
        const char *plain[] = {"context"},
                   *moving[] = {"context", "move", "extend"};
        if (i == 18) {
            if (!tiku_app_resource_keys(input, moving, 3) ||
                !tiku_app_resource_integer(input, "move", 0,
                                           TIKU_CODEVIEW_DOC_END, &move) ||
                !tiku_app_resource_integer(input, "extend", 0, 1, &extend))
                return TIKU_RESOURCE_VALUE;
        } else if (!tiku_app_resource_keys(input, plain, 1))
            return TIKU_RESOURCE_VALUE;
        if (!tiku_codeview_command_enabled(st->view, command))
            return TIKU_RESOURCE_UNSUPPORTED;
        error = status(tiku_codeview_execute(
            st->view, command, (tiku_codeview_move_t)move, (int)extend));
    }
    if (error) {
        tiku_msg_free(result);
        return error;
    }
    if (!result)
        result = tiku_app_resource_object(0);
    if (context(st, actual))
        bad = 1;
    else
        bad |= TEXT(result, "context", actual);
    bad |= BOOL(result, "dirty", tiku_editor_modified(st));
    bad |= TEXT(result, "command", name);
    if (strcmp(name, "read-text")) {
        *effect = TIKU_RESOURCE_EFFECT_PARTIAL;
        tiku_editor_edited(st);
        tiku_editor_publish(st);
        tiku_editor_paint(st);
    }
    if (bad) {
        tiku_msg_free(result);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, result, "native_complete")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Catalog changes when a whole replacement changes the document identity. */
static uint64_t revision(void *state)
{
    return tiku_editdoc_identity(((edit_state_t *)state)->doc);
}
/** @brief Optional versioned native application resource callbacks. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog_revision = revision,
    .catalog = catalog,
    .capture = capture,
    .change = change};
