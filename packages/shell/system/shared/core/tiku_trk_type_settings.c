/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_type_settings.c - durable file definitions and restart
 * observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_type_settings.h"
#include "tiku_trk_catalog_settings.h"
#include "tiku_resource_jobs.h"
#include "tiku_resource_stream.h"
#include "tiku_atomic_file.h"
#include "tiku_work.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TYPE_FILE_MAX 65536u
#define TYPE_PATH_MAX 4096
#define TYPE_IDENTITIES_MAX 80
#define TYPE_ROOT "/settings/file-types"

typedef struct {
    char *bytes;
    size_t size;
    int exists;
    char target[TYPE_PATH_MAX];
} type_file_t;
typedef struct type_job {
    struct tiku_trk_type_settings *owner;
    tiku_trk_types_t next, observed;
    type_file_t before, after;
    char path[TYPE_PATH_MAX], id[TIKU_RESOURCE_ID_MAX],
        resource[TIKU_RESOURCE_PATH_MAX];
    char *output;
    size_t size;
    atomic_int cancelled;
    int write, running;
    tiku_resource_error_t error;
    tiku_resource_effect_t effect;
} type_job_t;
struct tiku_trk_type_settings {
    tiku_trk_types_t saved, applied;
    type_file_t file;
    type_job_t *reader, *writer;
    tiku_resource_registry_t *registry;
    char path[TYPE_PATH_MAX];
    char mime[TYPE_IDENTITIES_MAX][TIKU_TYPE_MAX];
    char paths[TYPE_IDENTITIES_MAX + 3][TIKU_RESOURCE_PATH_MAX];
    int identities, published, ready, resync;
    tiku_resource_error_t error;
    uint64_t next_poll;
};

/** @brief Read monotonic milliseconds for reload and teardown deadlines. */
static uint64_t milliseconds(void)
{
    struct timespec time;
    return clock_gettime(CLOCK_MONOTONIC, &time)
               ? 0
               : (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

/** @brief Release a complete captured file. */
static void file_clear(type_file_t *file)
{
    free(file->bytes); memset(file, 0, sizeof *file);
}

/** @brief Read bounded regular-file bytes and resolve symbolic-link targets.
 */
static int file_read(const char *path, type_file_t *out)
{
    char *target = realpath(path, NULL);
    struct stat st;
    int fd;
    ssize_t n;
    size_t used = 0;
    memset(out, 0, sizeof *out);
    if (snprintf(out->target, sizeof out->target, "%s",
                 target ? target : path) >= (int)sizeof out->target) {
        free(target);
        return -1;
    }
    free(target);
    fd = open(out->target, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size > TYPE_FILE_MAX) {
        close(fd);
        return -1;
    }
    out->bytes = malloc(TYPE_FILE_MAX + 1);
    if (!out->bytes) { close(fd); return -1; }
    while (used <= TYPE_FILE_MAX) {
        n = read(fd, out->bytes + used, TYPE_FILE_MAX + 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); file_clear(out); return -1; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd);
    if (used > TYPE_FILE_MAX || memchr(out->bytes, 0, used)) {
        file_clear(out);
        return -1;
    }
    out->bytes[used] = 0; out->size = used; out->exists = 1; return 0;
}

/** @brief Compare target identity and complete bytes, independent of
 * timestamps.
 */
static int file_equal(const type_file_t *a, const type_file_t *b)
{
    return a->exists == b->exists && !strcmp(a->target, b->target) &&
           a->size == b->size &&
           (!a->size || !memcmp(a->bytes, b->bytes, a->size));
}

/** @brief Copy a precondition without sharing its byte allocation. */
static int file_copy(type_file_t *out, const type_file_t *in)
{
    *out = *in; out->bytes = NULL;
    if (in->size) {
        out->bytes = malloc(in->size + 1);
        if (!out->bytes) return -1;
        memcpy(out->bytes, in->bytes, in->size); out->bytes[in->size] = 0;
    }
    return 0;
}

/** @brief Accept UTF-8 native fields without row separators or control bytes.
 */
static int text_valid(const char *text, size_t maximum)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!text || !tiku_resource_utf8_valid(text, maximum)) return 0;
    for (; *p; p++) if (*p < 32 || *p == 127) return 0;
    return 1;
}

/** @brief Accept native MIME names and hyphen-terminated supertype prefixes.
 */
static int mime_valid(const char *mime)
{
    const unsigned char *p = (const unsigned char *)mime;
    if (!text_valid(mime, TIKU_TYPE_MAX - 1) || !*mime || mime[0] == '/' ||
        (!strchr(mime, '/') &&
         (strlen(mime) < 2 || mime[strlen(mime) - 1] != '-')))
        return 0;
    for (; *p; p++) if (*p <= 32 || *p >= 127 || *p == ',') return 0;
    return 1;
}

/** @brief Accept attribute names representable by the native 24-byte token
 * parser.
 */
static int attr_valid(const char *text)
{
    size_t n;
    return text_valid(text, 18) && (n = strlen(text)) && !strchr(text, ',') &&
           text[0] != ' ' && text[n - 1] != ' ';
}

/** @brief Split one native row; opaque lines return zero and stay byte-
 * preserved.
 */
static int split(char line[512], char **mime, char **description, char **icon,
                 char **columns)
{
    char *tab;
    *mime = line; *description = *icon = *columns = NULL;
    if (!*line || *line == '#' || !(tab = strchr(line, '\t'))) return 0;
    *tab = 0; *description = tab + 1;
    if ((tab = strchr(*description, '\t'))) { *tab = 0; *icon = tab + 1; }
    if (*icon && (tab = strchr(*icon, '\t'))) { *tab = 0; *columns = tab + 1; }
    if (*columns && strchr(*columns, '\t')) return -1;
    if (!mime_valid(*mime) ||
        !text_valid(*description, TIKU_TRK_TYPE_DESC_MAX - 1) ||
        (*icon && !text_valid(*icon, TIKU_NAME_MAX - 1)))
        return -1;
    return 1;
}

/** @brief Return a trimmed native token, refusing truncation. */
static int token(const char *start, size_t length, char out[512])
{
    while (length && (*start == ' ' || *start == '\t')) { start++; length--; }
    while (length && (start[length - 1] == ' ' || start[length - 1] == '\t'))
        length--;
    if (length >= 512) return -1;
    memcpy(out, start, length); out[length] = 0; return 0;
}

/** @brief Validate known column tokens without discarding unknown extensions.
 */
static int columns_valid(const char *columns)
{
    int cols = 0, attrs = 0;
    const char *p = columns ? columns : "";
    while (*p) {
        const char *comma = strchr(p, ',');
        char word[512];
        if (token(p, comma ? (size_t)(comma - p) : strlen(p), word) ||
            !text_valid(word, 511))
            return 0;
        if (!strncmp(word, "attr:", 5)) {
            if (!attr_valid(word + 5) || ++attrs > TIKU_TRK_TYPE_COLS_MAX)
                return 0;
        } else if (tiku_trk_types_column(word) >= 0 &&
                   ++cols > TIKU_TRK_TYPE_COLS_MAX)
            return 0;
        if (!comma) break;
        p = comma + 1;
    }
    return 1;
}

/** @brief Parse complete native input; absent files select product defaults.
 */
static tiku_resource_error_t parse(const type_file_t *file,
                                   tiku_trk_types_t *db)
{
    size_t offset = 0;
    tiku_trk_types_init(db);
    if (!file->exists) { tiku_trk_types_defaults(db); return TIKU_RESOURCE_OK; }
    while (offset < file->size) {
        const char *newline =
            memchr(file->bytes + offset, '\n', file->size - offset);
        size_t length = newline ? (size_t)(newline - file->bytes - offset)
                                : file->size - offset;
        char line[512], *mime, *description, *icon, *columns;
        int kind;
        if (length >= sizeof line - 1) return TIKU_RESOURCE_LIMIT;
        memcpy(line, file->bytes + offset, length); line[length] = 0;
        offset += length + !!newline;
        if (!tiku_resource_utf8_valid(line, 511)) return TIKU_RESOURCE_VALUE;
        kind = split(line, &mime, &description, &icon, &columns);
        if (kind < 0) return TIKU_RESOURCE_VALUE;
        if (!kind) continue;
        if (tiku_trk_types_exact(db, mime) || !columns_valid(columns))
            return TIKU_RESOURCE_VALUE;
        if (tiku_trk_types_add(db, mime, description, icon, columns) < 0)
            return TIKU_RESOURCE_LIMIT;
    }
    return TIKU_RESOURCE_OK;
}

/** @brief Compare records without depending on structure padding. */
static int record_equal(const tiku_trk_typerec_t *a,
                        const tiku_trk_typerec_t *b)
{
    int i;
    if (!a || !b) return a == b;
    if (strcmp(a->type, b->type) || strcmp(a->desc, b->desc) ||
        strcmp(a->icon, b->icon) || a->document != b->document ||
        a->ncol != b->ncol || a->nattr != b->nattr)
        return 0;
    for (i = 0; i < a->ncol; i++) if (a->col[i] != b->col[i]) return 0;
    for (i = 0; i < a->nattr; i++) if (strcmp(a->attr[i], b->attr[i])) return 0;
    return 1;
}

/** @brief Compare complete definitions regardless of native row ordering. */
static int database_equal(const tiku_trk_types_t *a, const tiku_trk_types_t *b)
{
    int i;
    if (a->n != b->n) return 0;
    for (i = 0; i < a->n; i++)
        if (!record_equal(&a->rec[i], tiku_trk_types_exact(b, a->rec[i].type)))
            return 0;
    return 1;
}

/** @brief Append an owned child to a typed object and release the temporary
 * value.
 */
static int member(tiku_msg_t *object, const char *name, tiku_msg_t *value)
{
    tiku_msg_t *item = tiku_msg_new(0);
    int ok = item && value && tiku_msg_add_string(item, "key", name) &&
             tiku_msg_add_message(item, "value", value) &&
             tiku_msg_add_message(object, "member", item);
    tiku_msg_free(item); tiku_msg_free(value); return ok;
}

/** @brief Allocate a typed object or array. */
static tiku_msg_t *container(const char *type)
{
    tiku_msg_t *out = tiku_msg_new(0);
    if (out && !tiku_msg_add_string(out, "type", type)) {
        tiku_msg_free(out);
        out = NULL;
    }
    return out;
}

/** @brief Encode one complete record; an empty object denotes absence. */
static tiku_msg_t *record_value(const tiku_trk_typerec_t *record)
{
    tiku_msg_t *out = container("object"), *cols = NULL, *attrs = NULL;
    int i;
    if (!out || !record) return out;
    cols = container("array"); attrs = container("array");
    if (!cols || !attrs) goto fail;
    for (i = 0; i < record->ncol; i++) {
        tiku_msg_t *v =
            tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, record->col[i]);
        int ok = v && tiku_msg_add_message(cols, "item", v);
        tiku_msg_free(v);
        if (!ok)
            goto fail;
    }
    for (i = 0; i < record->nattr; i++) {
        tiku_msg_t *v =
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->attr[i]);
        int ok = v && tiku_msg_add_message(attrs, "item", v);
        tiku_msg_free(v);
        if (!ok)
            goto fail;
    }
    if (!member(
            out, "mime",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->type)) ||
        !member(
            out, "description",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->desc)) ||
        !member(
            out, "icon",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, record->icon)) ||
        !member(out, "document",
                tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                            record->document)))
        goto fail;
    { int ok = member(out, "columns", cols); cols = NULL; if (!ok) goto fail; }
    {
        int ok = member(out, "attributes", attrs);
        attrs = NULL;
        if (!ok)
            goto fail;
    }
    return out;
fail:
    tiku_msg_free(cols); tiku_msg_free(attrs); tiku_msg_free(out); return NULL;
}

/** @brief Copy a typed native field with exact bounds and type checks. */
static int input_text(const tiku_msg_t *value, char *out, size_t capacity)
{
    const char *type = value ? tiku_msg_find_string(value, "type", 0) : NULL;
    const char *text = value ? tiku_msg_find_string(value, "value", 0) : NULL;
    if (!type || strcmp(type, "text") || !text_valid(text, capacity - 1))
        return -1;
    strcpy(out, text); return 0;
}

/** @brief Decode exactly the six native members, rejecting unknown fields. */
static int input_record(const tiku_msg_t *value, tiku_trk_typerec_t *record)
{
    static const char *const names[] = {
        "mime", "description", "icon", "document", "columns", "attributes"};
    int i, seen = 0;
    const char *type = tiku_msg_find_string(value, "type", 0);
    memset(record, 0, sizeof *record);
    if (!type || strcmp(type, "object") ||
        tiku_msg_count(value, "member") != 6)
        return -1;
    for (i = 0; i < 6; i++) {
        tiku_msg_t *entry = tiku_msg_find_message(value, "member", i), *v;
        const char *name = entry ? tiku_msg_find_string(entry, "key", 0) : NULL;
        const char *kind;
        int k;
        v = entry ? tiku_msg_find_message(entry, "value", 0) : NULL;
        if (!name || !v) { tiku_msg_free(entry); tiku_msg_free(v); return -1; }
        for (k = 0; k < 6 && strcmp(name, names[k]); k++) {}
        kind = tiku_msg_find_string(v, "type", 0);
        if (k == 6 || (seen & (1 << k)) || !kind) goto bad;
        seen |= 1 << k;
        if (k < 3) {
            char *dest = k == 0   ? record->type
                         : k == 1 ? record->desc
                                  : record->icon;
            size_t cap = k == 0   ? sizeof record->type
                         : k == 1 ? sizeof record->desc
                                  : sizeof record->icon;
            if (input_text(v, dest, cap)) goto bad;
        } else if (k == 3) {
            int flag;
            if (strcmp(kind, "boolean") ||
                !tiku_msg_find_bool(v, "value", 0, &flag) ||
                (flag != 0 && flag != 1))
                goto bad;
            record->document = (int)flag;
        } else {
            int count = tiku_msg_count(v, "item"), j;
            if (strcmp(kind, "array") || count > TIKU_TRK_TYPE_COLS_MAX)
                goto bad;
            for (j = 0; j < count; j++) {
                tiku_msg_t *item = tiku_msg_find_message(v, "item", j);
                int valid;
                if (k == 4) {
                    int64_t column;
                    const char *item_type =
                        item ? tiku_msg_find_string(item, "type", 0) : NULL;
                    valid = item_type && !strcmp(item_type, "integer") &&
                            tiku_msg_find_int64(item, "value", 0, &column) &&
                            column >= 0 && column <= 8 &&
                            tiku_trk_types_column_name((int)column);
                    if (valid) record->col[record->ncol++] = (int)column;
                } else {
                    valid = item &&
                            !input_text(item, record->attr[record->nattr],
                                        sizeof record->attr[0]) &&
                            attr_valid(record->attr[record->nattr]);
                    if (valid) record->nattr++;
                }
                tiku_msg_free(item); if (!valid) goto bad;
            }
        }
        tiku_msg_free(v); tiku_msg_free(entry); continue;
bad:
        tiku_msg_free(v); tiku_msg_free(entry); return -1;
    }
    return mime_valid(record->type) ? 0 : -1;
}

/** @brief Build a compact action result and persistence observation. */
static tiku_msg_t *status_value(tiku_trk_type_settings_t *owner)
{
    tiku_msg_t *out = container("object");
    if (!out ||
        !member(out, "ready",
                tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                            owner->ready)) ||
        !member(out, "error",
                tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                            owner->error)) ||
        !member(out, "saved_records",
                tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                            owner->saved.n)) ||
        !member(out, "applied_records",
                tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                            owner->applied.n)) ||
        !member(out, "restart_required",
                tiku_resource_value_integer(
                    TIKU_RESOURCE_BOOLEAN,
                    !database_equal(&owner->saved, &owner->applied))) ||
        !member(out, "file_present",
                tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                            owner->file.exists)) ||
        !member(out, "record_limit",
                tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                            TIKU_TRK_TYPES_MAX)) ||
        !member(out, "identity_limit",
                tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,
                                            TYPE_IDENTITIES_MAX))) {
        tiku_msg_free(out); return NULL;
    }
    return out;
}

/** @brief Copy saved and startup records into one restart-aware snapshot. */
static int snapshot(tiku_trk_type_settings_t *owner, int index,
                    tiku_resource_snapshot_t *out)
{
    memset(out, 0, sizeof *out);
    if (index < 3) {
        out->desired = status_value(owner);
        out->persisted = out->desired ? tiku_msg_copy(out->desired) : NULL;
        out->applied = out->desired ? tiku_msg_copy(out->desired) : NULL;
        strcpy(out->apply_state, database_equal(&owner->saved, &owner->applied)
                                     ? "applied"
                                     : "restart_required");
    } else {
        const char *mime = owner->mime[index - 3];
        const tiku_trk_typerec_t *saved =
            tiku_trk_types_exact(&owner->saved, mime);
        const tiku_trk_typerec_t *applied =
            tiku_trk_types_exact(&owner->applied, mime);
        out->desired = record_value(saved);
        out->persisted = record_value(saved);
        out->applied = record_value(applied);
        strcpy(out->apply_state,
               record_equal(saved, applied) ? "applied" : "restart_required");
    }
    if (owner->error) strcpy(out->apply_state, "error");
    if (out->desired && out->persisted && out->applied) return 0;
    tiku_resource_snapshot_clear(out); return -1;
}

/** @brief Locate one retained child identity, including action and status
 * paths.
 */
static int index_for(tiku_trk_type_settings_t *owner, const char *path)
{
    int i;
    for (i = 0; i < owner->identities + 3; i++)
        if (!strcmp(owner->paths[i], path))
            return i;
    return -1;
}

/** @brief Check that the retained identity union fits before accepting an
 * effect.
 */
static int identities_fit(tiku_trk_type_settings_t *owner,
                          const tiku_trk_types_t *db)
{
    int i, j, count = owner->identities;
    for (i = 0; i < db->n; i++) {
        for (j = 0;
             j < owner->identities && strcmp(owner->mime[j], db->rec[i].type);
             j++) {
        }
        if (j == owner->identities) count++;
    }
    return count <= TYPE_IDENTITIES_MAX;
}

/** @brief Retain known MIME identities so deletions have observable empty
 * values.
 */
static void remember(tiku_trk_type_settings_t *owner,
                     const tiku_trk_types_t *db)
{
    int i, j;
    for (i = 0; i < db->n; i++) {
        for (j = 0;
             j < owner->identities && strcmp(owner->mime[j], db->rec[i].type);
             j++) {
        }
        if (j < owner->identities) continue;
        strcpy(owner->mime[j], db->rec[i].type);
        tiku_trk_catalog_settings_path(TYPE_ROOT, owner->mime[j], NULL,
                                       owner->paths[j + 3]);
        owner->identities++;
    }
}

/** @brief Decode a single replacement, upsert or complete ordered database
 * batch.
 */
static tiku_resource_error_t proposal(tiku_trk_type_settings_t *owner,
                                      const char *path,
                                      const tiku_msg_t *input, int invoke,
                                      tiku_trk_types_t *next)
{
    int index = index_for(owner, path), i;
    const char *kind;
    tiku_trk_typerec_t record;
    if (index < 0) return TIKU_RESOURCE_NOT_FOUND;
    if (index == 2 || invoke != (index < 2)) return TIKU_RESOURCE_READ_ONLY;
    if (tiku_resource_value_check(input)) return TIKU_RESOURCE_VALUE;
    kind = tiku_msg_find_string(input, "type", 0);
    *next = owner->saved;
    if (index == 1) {
        int count = tiku_msg_count(input, "item");
        if (strcmp(kind, "array")) return TIKU_RESOURCE_VALUE;
        if (count > TIKU_TRK_TYPES_MAX) return TIKU_RESOURCE_LIMIT;
        tiku_trk_types_init(next);
        for (i = 0; i < count; i++) {
            tiku_msg_t *item = tiku_msg_find_message(input, "item", i);
            int valid = item && !input_record(item, &record);
            tiku_msg_free(item);
            if (!valid || tiku_trk_types_exact(next, record.type))
                return TIKU_RESOURCE_VALUE;
            next->rec[next->n++] = record;
        }
    } else {
        int remove = index >= 3 && !strcmp(kind, "object") &&
                     !tiku_msg_count(input, "member");
        const char *mime;
        if (!remove && input_record(input, &record)) return TIKU_RESOURCE_VALUE;
        mime = remove ? owner->mime[index - 3] : record.type;
        if (index >= 3 && strcmp(mime, owner->mime[index - 3]))
            return TIKU_RESOURCE_VALUE;
        for (i = 0; i < next->n && strcmp(next->rec[i].type, mime); i++) {}
        if (remove) {
            if (i < next->n) {
                memmove(&next->rec[i], &next->rec[i + 1],
                        (size_t)(next->n - i - 1) * sizeof record);
                next->n--;
            }
        } else {
            if (i == next->n) {
                if (next->n == TIKU_TRK_TYPES_MAX)
                    return TIKU_RESOURCE_LIMIT;
                next->n++;
            }
            next->rec[i] = record;
        }
    }
    return identities_fit(owner, next) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_LIMIT;
}

/** @brief Write a changed record and retain unknown comma-separated
 * extensions.
 */
static int write_record(FILE *file, const tiku_trk_typerec_t *record,
                        const char *columns)
{
    int count = record->ncol + record->nattr + record->document;
    long start = ftell(file), end;
    const char *p = columns ? columns : "";
    if (start < 0 || tiku_trk_types_write_record(file, record)) return -1;
    while (*p) {
        const char *comma = strchr(p, ',');
        size_t length = comma ? (size_t)(comma - p) : strlen(p);
        char word[512];
        if (token(p, length, word)) return -1;
        if (*word && strcmp(word, "document") && strncmp(word, "attr:", 5) &&
            tiku_trk_types_column(word) < 0) {
            if ((count++ && fputc(',', file) == EOF) ||
                fwrite(p, 1, length, file) != length)
                return -1;
        }
        if (!comma) break;
        p = comma + 1;
    }
    end = ftell(file);
    if (end < start || end - start >= 511 || fputc('\n', file) == EOF)
        return -1;
    return 0;
}

/** @brief Preserve opaque lines and unchanged rows while applying explicit
 * edits.
 */
static int output(type_job_t *job)
{
    FILE *file = open_memstream(&job->output, &job->size);
    size_t offset = 0;
    int i, error = !file, written[TIKU_TRK_TYPES_MAX] = {0};
    while (!error && offset < job->before.size) {
        const char *start = job->before.bytes + offset;
        const char *newline = memchr(start, '\n', job->before.size - offset);
        size_t length =
            newline ? (size_t)(newline - start) : job->before.size - offset;
        size_t raw = length + !!newline;
        char line[512], *mime, *description, *icon, *columns;
        int kind;
        if (length >= sizeof line - 1) { error = 1; break; }
        memcpy(line, start, length); line[length] = 0; offset += raw;
        kind = split(line, &mime, &description, &icon, &columns);
        if (kind < 0) { error = 1; break; }
        if (!kind) { error = fwrite(start, 1, raw, file) != raw; continue; }
        for (i = 0; i < job->next.n && strcmp(job->next.rec[i].type, mime);
             i++) {
        }
        if (i == job->next.n) continue;
        written[i] = 1;
        if (record_equal(&job->next.rec[i],
                         tiku_trk_types_exact(&job->observed, mime)))
            error = fwrite(start, 1, raw, file) != raw;
        else error = write_record(file, &job->next.rec[i], columns) != 0;
    }
    for (i = 0; !error && i < job->next.n; i++) if (!written[i]) {
        if (ftell(file) > 0 && fflush(file)) { error = 1; break; }
        if (job->size && job->output[job->size - 1] != '\n' &&
            fputc('\n', file) == EOF) {
            error = 1;
            break;
        }
        error = write_record(file, &job->next.rec[i], NULL) != 0;
    }
    if (file && fclose(file)) error = 1;
    if (!error && job->size <= TYPE_FILE_MAX) {
        type_file_t verify = {
            .bytes = job->output, .size = job->size, .exists = 1};
        tiku_trk_types_t db;
        error = parse(&verify, &db) || !database_equal(&db, &job->next);
    } else error = 1;
    return error ? -1 : 0;
}

/** @brief Emit prevalidated bytes into the atomic replacement temporary file.
 */
static int produce(FILE *file, void *context)
{
    type_job_t *job = context;
    return fwrite(job->output, 1, job->size, file) == job->size ? 0 : -1;
}

/** @brief Refuse cancellation, changed bytes or a retargeted symbolic link.
 */
static int check(void *context)
{
    type_job_t *job = context;
    type_file_t current;
    int equal;
    if (atomic_load(&job->cancelled)) return -1;
    if (file_read(job->path, &current)) return -1;
    equal = file_equal(&current, &job->before); file_clear(&current);
    return equal ? 0 : -1;
}

/** @brief Read or persist worker-owned bytes without touching desktop state.
 */
static void run(void *context)
{
    type_job_t *job = context;
    tiku_atomic_result_t result;
    if (!job->write) {
        job->error = file_read(job->path, &job->after)
                         ? TIKU_RESOURCE_PERSISTENCE_FAILED
                         : parse(&job->after, &job->observed);
        return;
    }
    if (atomic_load(&job->cancelled)) {
        job->error = TIKU_RESOURCE_CANCELLED;
        return;
    }
    if (output(job)) { job->error = TIKU_RESOURCE_VALUE; return; }
    result = tiku_atomic_file_guarded(job->before.target, produce, check, job);
    if (result == TIKU_ATOMIC_CONFLICT) {
        job->error = atomic_load(&job->cancelled) ? TIKU_RESOURCE_CANCELLED
                                                  : TIKU_RESOURCE_STALE;
        return;
    }
    if (result == TIKU_ATOMIC_FAILED) {
        job->error = TIKU_RESOURCE_PERSISTENCE_FAILED;
        return;
    }
    job->effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    if (result == TIKU_ATOMIC_UNCERTAIN || file_read(job->path, &job->after) ||
        parse(&job->after, &job->observed) ||
        !database_equal(&job->observed, &job->next)) {
        job->error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        return;
    }
    job->effect = TIKU_RESOURCE_EFFECT_PARTIAL;
}

/** @brief Publish values or atomically rebuild a changed child catalog. */
static void publish(tiku_trk_type_settings_t *owner)
{
    tiku_resource_caller_t caller = {.principal =
                                         "00000000000000000000000000000001",
                                     .authenticated = 1,
                                     .scopes = 63};
    int i;
    if (!owner->registry || owner->writer ||
        (!owner->resync && owner->published == owner->identities))
        return;
    if (owner->published != owner->identities) {
        size_t count = tiku_trk_type_settings_count(owner), j;
        tiku_resource_initial_t *initial = calloc(count, sizeof *initial);
        tiku_resource_snapshot_t root = {0};
        int error = !initial;
        if (!error) error = tiku_trk_type_settings_initial(owner, initial);
        if (!error)
            error =
                tiku_resource_read(owner->registry, &caller, TYPE_ROOT, &root);
        if (!error)
            error = tiku_resource_collection_replace(
                owner->registry, &root.stamp, TYPE_ROOT, initial, count,
                &root.stamp);
        tiku_resource_snapshot_clear(&root);
        if (initial) {
            for (j = 0; j < count; j++)
                tiku_resource_snapshot_clear(&initial[j].initial);
            free(initial);
        }
        if (error) { owner->resync = 1; return; }
        owner->published = owner->identities;
    } else {
        tiku_resource_snapshot_t root = {0};
        tiku_resource_stamp_t stamp;
        if (tiku_resource_read(owner->registry, &caller, TYPE_ROOT, &root)) {
            owner->resync = 1;
            return;
        }
        stamp = root.stamp; tiku_resource_snapshot_clear(&root);
        for (i = 0; i < owner->identities + 3; i++) {
            tiku_resource_snapshot_t old = {0}, next = {0};
            tiku_resource_error_t error = tiku_resource_read(
                owner->registry, &caller, owner->paths[i], &old);
            int unavailable = error == TIKU_RESOURCE_UNAVAILABLE;
            if ((error && !unavailable) || snapshot(owner, i, &next)) {
                tiku_resource_snapshot_clear(&old);
                tiku_resource_snapshot_clear(&next);
                owner->resync = 1;
                return;
            }
            error = TIKU_RESOURCE_OK;
            if (unavailable ||
                !tiku_resource_value_equal(old.desired, next.desired) ||
                !tiku_resource_value_equal(old.persisted, next.persisted) ||
                !tiku_resource_value_equal(old.applied, next.applied) ||
                strcmp(old.apply_state, next.apply_state))
                error = tiku_resource_publish(owner->registry, &stamp,
                                              owner->paths[i], &next, &stamp);
            tiku_resource_snapshot_clear(&old);
            tiku_resource_snapshot_clear(&next);
            if (error) { owner->resync = 1; return; }
        }
    }
    owner->resync = 0;
}

/** @brief Free worker-owned models after collection or before dispatch. */
static void release(void *context)
{
    type_job_t *job = context;
    file_clear(&job->before);
    file_clear(&job->after);
    free(job->output);
    free(job);
}

/** @brief Apply worker observations and finish jobs before replacing child
 * identities.
 */
static void done(void *context)
{
    type_job_t *job = context;
    tiku_trk_type_settings_t *owner = job->owner;
    if (!owner) return;
    if (job->write) owner->writer = NULL; else owner->reader = NULL;
    if (!job->error && !identities_fit(owner, &job->observed))
        job->error = TIKU_RESOURCE_LIMIT;
    if (!job->error) {
        file_clear(&owner->file);
        owner->file = job->after;
        memset(&job->after, 0, sizeof job->after);
        owner->saved = job->observed;
        remember(owner, &owner->saved);
        owner->ready = 1;
        owner->error = TIKU_RESOURCE_OK;
    } else {
        owner->ready = 0; owner->error = job->error;
        if (job->write) owner->next_poll = 0;
    }
    owner->resync = 1;
    if (job->write && job->id[0] && owner->registry) {
        tiku_resource_snapshot_t out = {0};
        tiku_resource_error_t error = job->error;
        int index = index_for(owner, job->resource);
        if (!error && (index < 0 || snapshot(owner, index, &out)))
            error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        tiku_resource_job_complete(owner->registry, job->id,
                                   error ? NULL : &out, error,
                                   error == TIKU_RESOURCE_UNKNOWN_OUTCOME
                                       ? TIKU_RESOURCE_EFFECT_UNKNOWN
                                       : job->effect);
        tiku_resource_snapshot_clear(&out);
    }
    publish(owner);
}

/** @brief Post isolated copied inputs with one file-owner worker key. */
static int post(type_job_t *job)
{
    tiku_work_job_t work = {
        .run = run, .done = done, .free = release, .ctx = job};
    strcpy(work.key, "desktop-type-settings");
    if (tiku_work_post(&work)) return -1;
    job->running = 1; return 0;
}

tiku_trk_type_settings_t *
tiku_trk_type_settings_open(const char *path, const tiku_trk_types_t *applied)
{
    tiku_trk_type_settings_t *owner;
    if (!path || !*path || strlen(path) >= TYPE_PATH_MAX || !applied ||
        applied->n < 0 || applied->n > TIKU_TRK_TYPES_MAX)
        return NULL;
    owner = calloc(1, sizeof *owner);
    if (!owner) return NULL;
    strcpy(owner->path, path); owner->saved = owner->applied = *applied;
    strcpy(owner->paths[0], TYPE_ROOT "/upsert");
    strcpy(owner->paths[1], TYPE_ROOT "/replace");
    strcpy(owner->paths[2], TYPE_ROOT "/status");
    remember(owner, applied); owner->published = owner->identities;
    tiku_trk_type_settings_poll(owner); return owner;
}

size_t tiku_trk_type_settings_count(const tiku_trk_type_settings_t *owner)
{
    return owner ? (size_t)owner->identities + 3 : 0;
}

int tiku_trk_type_settings_initial(tiku_trk_type_settings_t *owner,
                                   tiku_resource_initial_t *initial)
{
    int i;
    for (i = 0; i < owner->identities + 3; i++) {
        initial[i].spec = (tiku_resource_spec_t){
            .path = owner->paths[i],
            .schema_version = 1,
            .kind = i < 2    ? TIKU_RESOURCE_ACTION
                    : i == 2 ? TIKU_RESOURCE_STATE
                             : TIKU_RESOURCE_SETTING,
            .schema = {.type = i == 1 ? TIKU_RESOURCE_ARRAY
                                      : TIKU_RESOURCE_OBJECT},
            .read_scopes = TIKU_RESOURCE_READ,
            .change_scopes = i < 2    ? TIKU_RESOURCE_INVOKE
                             : i == 2 ? 0
                                      : TIKU_RESOURCE_WRITE,
            .public_read = 1,
            .persistence = "user",
            .controller = "desktop-type-file"};
        if (snapshot(owner, i, &initial[i].initial)) return -1;
    }
    return 0;
}

void tiku_trk_type_settings_bind(tiku_trk_type_settings_t *owner,
                                 tiku_resource_registry_t *registry)
{
    if (owner) owner->registry = registry;
}

tiku_resource_error_t
tiku_trk_type_settings_validate(tiku_trk_type_settings_t *owner,
                                const char *path, const tiku_msg_t *input,
                                int invoke)
{
    tiku_trk_types_t next;
    if (!owner) return TIKU_RESOURCE_NOT_FOUND;
    if (owner->writer || owner->resync) return TIKU_RESOURCE_BUSY;
    if (!owner->ready)
        return owner->error ? TIKU_RESOURCE_UNAVAILABLE : TIKU_RESOURCE_BUSY;
    return proposal(owner, path, input, invoke, &next);
}

tiku_resource_error_t
tiku_trk_type_settings_start(tiku_trk_type_settings_t *owner, const char *id,
                             const char *path, const tiku_msg_t *input,
                             int invoke)
{
    tiku_resource_error_t error =
        tiku_trk_type_settings_validate(owner, path, input, invoke);
    type_job_t *job;
    if (error) return error;
    job = calloc(1, sizeof *job);
    if (!job) return TIKU_RESOURCE_NO_MEMORY;
    error = proposal(owner, path, input, invoke, &job->next);
    if (error || file_copy(&job->before, &owner->file)) {
        release(job);
        return error ? error : TIKU_RESOURCE_NO_MEMORY;
    }
    job->observed = owner->saved; job->owner = owner; job->write = 1;
    strcpy(job->id, id);
    strcpy(job->resource, path);
    strcpy(job->path, owner->path);
    atomic_init(&job->cancelled, 0); owner->writer = job;
    return TIKU_RESOURCE_PENDING;
}

void tiku_trk_type_settings_poll(tiku_trk_type_settings_t *owner)
{
    type_job_t *job;
    uint64_t now;
    if (!owner) return;
    if (owner->writer) {
        job = owner->writer;
        if (!job->running && !owner->reader && post(job)) {
            job->error = TIKU_RESOURCE_BUSY;
            done(job);
            release(job);
        }
        return;
    }
    publish(owner);
    now = milliseconds();
    if (owner->reader || !now || now < owner->next_poll || owner->resync)
        return;
    job = calloc(1, sizeof *job);
    if (!job) return;
    job->owner = owner;
    strcpy(job->path, owner->path);
    atomic_init(&job->cancelled, 0);
    if (post(job)) { release(job); return; }
    owner->reader = job; owner->next_poll = now + 500;
}

void tiku_trk_type_settings_close(tiku_trk_type_settings_t *owner)
{
    uint64_t now = milliseconds(), deadline = now ? now + 5000 : 0;
    if (!owner) return;
    while ((owner->reader || owner->writer) && (now = milliseconds()) &&
           now < deadline) {
        if (owner->writer && !owner->writer->running && !owner->reader) {
            if (post(owner->writer)) break;
        }
        tiku_work_collect();
        if (owner->reader || owner->writer) usleep(1000);
    }
    if (owner->reader) {
        owner->reader->owner = NULL;
        atomic_store(&owner->reader->cancelled, 1);
    }
    if (owner->writer) {
        type_job_t *job = owner->writer;
        job->owner = NULL; atomic_store(&job->cancelled, 1);
        if (!job->running) release(job);
    }
    file_clear(&owner->file); free(owner);
}
