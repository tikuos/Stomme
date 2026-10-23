/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource.c - canonical resource paths, identities and typed values.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *tiku_resource_error_name(tiku_resource_error_t error)
{
    static const char *const names[] = {
        "ok", "invalid_request", "invalid_value", "limit_exceeded",
        "permission_denied", "not_found", "read_only", "stale_revision",
        "owner_lost", "unavailable", "unsupported", "busy", "no_memory",
        "request_id_reused", "unknown_outcome", "persistence_failed", "apply_failed",
        "restart_required", "confirmation_required", "pending", "deadline_exceeded",
        "cancelled", "outcome_unavailable", "stale_cursor", "atomic_unsupported"
    };
    return (unsigned)error < sizeof names / sizeof names[0]
        ? names[error] : "invalid_request";
}

const char *tiku_resource_type_name(tiku_resource_type_t type)
{
    static const char *const names[] = {
        "null", "boolean", "integer", "counter", "number", "text", "enum",
        "color", "object", "array"
    };
    return (unsigned)type < sizeof names / sizeof names[0] ? names[type] : NULL;
}

/** @brief Whether a byte is permitted within a canonical path component. */
static int path_byte(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

int tiku_resource_path_valid(const char *path)
{
    size_t length, start;
    if (!path || path[0] != '/') return 0;
    length = strnlen(path, TIKU_RESOURCE_PATH_MAX);
    if (!length || length >= TIKU_RESOURCE_PATH_MAX) return 0;
    if (length == 1) return 1;
    for (start = 1; start < length;) {
        size_t end = start;
        while (end < length && path[end] != '/') {
            if (!path_byte((unsigned char)path[end])) return 0;
            end++;
        }
        if (end == start || end - start > 127 ||
            (end - start == 1 && path[start] == '.') ||
            (end - start == 2 && path[start] == '.' && path[start + 1] == '.'))
            return 0;
        if (end == length) return 1;
        start = end + 1;
    }
    return 0;
}

int tiku_resource_id_valid(const char *id)
{
    size_t i;
    if (!id || strnlen(id, 33) != 32) return 0;
    for (i = 0; i < 32; i++)
        if (!((id[i] >= '0' && id[i] <= '9') ||
              (id[i] >= 'a' && id[i] <= 'f'))) return 0;
    return 1;
}

int tiku_resource_new_id(char out[TIKU_RESOURCE_ID_MAX])
{
    unsigned char bytes[16];
    size_t used = 0, i;
    int fd;
    static const char hex[] = "0123456789abcdef";
    if (!out) return -1;
    out[0] = 0;
    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    while (used < sizeof bytes) {
        ssize_t n = read(fd, bytes + used, sizeof bytes - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        used += (size_t)n;
    }
    close(fd);
    for (i = 0; i < sizeof bytes; i++) {
        out[i * 2] = hex[bytes[i] >> 4];
        out[i * 2 + 1] = hex[bytes[i] & 15];
    }
    out[32] = 0;
    return 0;
}

int tiku_resource_counter_parse(const char *text, uint64_t *out)
{
    uint64_t result = 0;
    size_t i, size;
    if (!text || !out) return -1;
    size = strnlen(text, 21);
    if (!size || size > 20 || (size > 1 && text[0] == '0')) return -1;
    for (i = 0; i < size; i++) {
        unsigned digit = (unsigned char)text[i] - '0';
        if (digit > 9 || result > (UINT64_MAX - digit) / 10) return -1;
        result = result * 10 + digit;
    }
    *out = result;
    return 0;
}

int tiku_resource_utf8_valid(const char *text, size_t max)
{
    size_t length, i = 0;
    if (!text || max > TIKU_RESOURCE_FRAME_MAX) return 0;
    length = strnlen(text, max + 1);
    if (length > max) return 0;
    while (i < length) {
        uint32_t code;
        unsigned char c = (unsigned char)text[i++];
        unsigned more, j;
        uint32_t minimum;
        if (c < 128) continue;
        if (c >= 0xc2 && c <= 0xdf) { code = c & 31; more = 1; minimum = 128; }
        else if (c >= 0xe0 && c <= 0xef) { code = c & 15; more = 2; minimum = 2048; }
        else if (c >= 0xf0 && c <= 0xf4) { code = c & 7; more = 3; minimum = 65536; }
        else return 0;
        if (length - i < more) return 0;
        for (j = 0; j < more; j++) {
            c = (unsigned char)text[i++];
            if ((c & 0xc0) != 0x80) return 0;
            code = (code << 6) | (c & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            return 0;
    }
    return 1;
}

/** @brief Resolve a stable type key. */
static int value_type(const tiku_msg_t *value)
{
    const char *name = tiku_msg_find_string(value, "type", 0);
    int i;
    if (!name || tiku_msg_count(value, "type") != 1) return -1;
    for (i = TIKU_RESOURCE_NULL; i <= TIKU_RESOURCE_ARRAY; i++)
        if (!strcmp(name, tiku_resource_type_name((tiku_resource_type_t)i))) return i;
    return -1;
}

/** @brief Validate a typed message and its bounded descendants. */
static tiku_resource_error_t check_value(const tiku_msg_t *value, unsigned depth)
{
    int type = value_type(value), fields = tiku_msg_field_count(value);
    int count, i;
    int64_t integer;
    double number;
    uint64_t counter;
    const char *text;
    if (depth > TIKU_RESOURCE_DEPTH_MAX) return TIKU_RESOURCE_LIMIT;
    if (type < 0) return TIKU_RESOURCE_VALUE;
    if (type == TIKU_RESOURCE_NULL)
        return fields == 1 ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    if (type == TIKU_RESOURCE_OBJECT || type == TIKU_RESOURCE_ARRAY) {
        const char *field = type == TIKU_RESOURCE_OBJECT ? "member" : "item";
        char keys[TIKU_RESOURCE_MEMBERS_MAX][128];
        count = tiku_msg_count(value, field);
        if (count > TIKU_RESOURCE_MEMBERS_MAX) return TIKU_RESOURCE_LIMIT;
        if (fields != 1 + (count != 0) ||
            (count && !tiku_msg_has(value, field, TIKU_MSG_T_MESSAGE)))
            return TIKU_RESOURCE_VALUE;
        for (i = 0; i < count; i++) {
            tiku_msg_t *member = tiku_msg_find_message(value, field, i), *child;
            tiku_resource_error_t error;
            if (!member) return TIKU_RESOURCE_VALUE;
            if (type == TIKU_RESOURCE_OBJECT) {
                int j;
                const char *key = tiku_msg_find_string(member, "key", 0);
                if (!key || !key[0] || !tiku_resource_utf8_valid(key, 127) ||
                    tiku_msg_count(member, "key") != 1 ||
                    tiku_msg_count(member, "value") != 1 ||
                    tiku_msg_field_count(member) != 2) {
                    tiku_msg_free(member); return TIKU_RESOURCE_VALUE;
                }
                for (j = 0; j < i; j++) if (!strcmp(key, keys[j])) {
                    tiku_msg_free(member); return TIKU_RESOURCE_VALUE;
                }
                strcpy(keys[i], key);
                child = tiku_msg_find_message(member, "value", 0);
            } else child = member;
            error = check_value(child, depth + 1);
            if (child != member) tiku_msg_free(child);
            tiku_msg_free(member);
            if (error) return error;
        }
        return TIKU_RESOURCE_OK;
    }
    if (fields != 2 || tiku_msg_count(value, "value") != 1) return TIKU_RESOURCE_VALUE;
    switch (type) {
    case TIKU_RESOURCE_BOOLEAN:
        return tiku_msg_has(value, "value", TIKU_MSG_T_BOOL) ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    case TIKU_RESOURCE_INTEGER:
    case TIKU_RESOURCE_COLOR:
        if (!tiku_msg_find_int64(value, "value", 0, &integer)) return TIKU_RESOURCE_VALUE;
        return type != TIKU_RESOURCE_COLOR || (integer >= 0 && (uint64_t)integer <= UINT32_MAX)
            ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    case TIKU_RESOURCE_NUMBER:
        return tiku_msg_find_double(value, "value", 0, &number) && isfinite(number)
            ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    case TIKU_RESOURCE_COUNTER:
        return !tiku_resource_counter_parse(tiku_msg_find_string(value, "value", 0), &counter)
            ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    default:
        text = tiku_msg_find_string(value, "value", 0);
        return tiku_resource_utf8_valid(text, TIKU_RESOURCE_TEXT_MAX)
            ? TIKU_RESOURCE_OK : TIKU_RESOURCE_VALUE;
    }
}

tiku_resource_error_t tiku_resource_value_check(const tiku_msg_t *value)
{
    size_t size = tiku_msg_flat_size(value);
    if (!size) return TIKU_RESOURCE_VALUE;
    if (size > TIKU_RESOURCE_FRAME_MAX) return TIKU_RESOURCE_LIMIT;
    {
        unsigned char *bytes = malloc(size);
        tiku_resource_error_t error;
        if (!bytes) return TIKU_RESOURCE_NO_MEMORY;
        if (!tiku_msg_flatten(value, bytes, size, NULL)) { free(bytes); return TIKU_RESOURCE_VALUE; }
        error = tiku_resource_message_check(bytes, size);
        free(bytes);
        if (error) return error == TIKU_RESOURCE_INVALID ? TIKU_RESOURCE_VALUE : error;
    }
    return check_value(value, 1);
}

tiku_msg_t *tiku_resource_value_string(tiku_resource_type_t type, const char *value)
{
    tiku_msg_t *out = tiku_msg_new(0);
    const char *name = tiku_resource_type_name(type);
    if (!name || (type != TIKU_RESOURCE_TEXT && type != TIKU_RESOURCE_ENUM &&
                  type != TIKU_RESOURCE_COUNTER) || !out ||
        !tiku_msg_add_string(out, "type", name) || !tiku_msg_add_string(out, "value", value) ||
        tiku_resource_value_check(out)) {
        tiku_msg_free(out); return NULL;
    }
    return out;
}

tiku_msg_t *tiku_resource_value_integer(tiku_resource_type_t type, int64_t value)
{
    tiku_msg_t *out = tiku_msg_new(0);
    const char *name = tiku_resource_type_name(type);
    int added = 0;
    if (out && name && tiku_msg_add_string(out, "type", name)) {
        if (type == TIKU_RESOURCE_BOOLEAN && (value == 0 || value == 1))
            added = tiku_msg_add_bool(out, "value", (int)value);
        else if (type == TIKU_RESOURCE_INTEGER || type == TIKU_RESOURCE_COLOR)
            added = tiku_msg_add_int64(out, "value", value);
    }
    if (!added || tiku_resource_value_check(out)) { tiku_msg_free(out); return NULL; }
    return out;
}

/** @brief Compare validated values independently of object and field order. */
static int equal_value(const tiku_msg_t *a, const tiku_msg_t *b)
{
    const char *type = tiku_msg_find_string(a, "type", 0);
    int64_t ai, bi;
    double ad, bd;
    int ab, bb;
    int count, i;
    if (strcmp(type, tiku_msg_find_string(b, "type", 0))) return 0;
    if (!strcmp(type, "null")) return 1;
    if (!strcmp(type, "boolean")) {
        return tiku_msg_find_bool(a, "value", 0, &ab) && tiku_msg_find_bool(b, "value", 0, &bb) && ab == bb;
    }
    if (!strcmp(type, "integer") || !strcmp(type, "color")) {
        return tiku_msg_find_int64(a, "value", 0, &ai) && tiku_msg_find_int64(b, "value", 0, &bi) && ai == bi;
    }
    if (!strcmp(type, "number")) {
        return tiku_msg_find_double(a, "value", 0, &ad) && tiku_msg_find_double(b, "value", 0, &bd) && ad == bd;
    }
    if (!strcmp(type, "array")) {
        count = tiku_msg_count(a, "item");
        if (count != tiku_msg_count(b, "item")) return 0;
        for (i = 0; i < count; i++) {
            tiku_msg_t *left = tiku_msg_find_message(a, "item", i), *right = tiku_msg_find_message(b, "item", i);
            int same = left && right && equal_value(left, right);
            tiku_msg_free(left); tiku_msg_free(right);
            if (!same) return 0;
        }
        return 1;
    }
    if (!strcmp(type, "object")) {
        tiku_msg_t *left[128] = {0}, *right[128] = {0};
        int same = 0;
        count = tiku_msg_count(a, "member");
        if (count != tiku_msg_count(b, "member")) return 0;
        for (i = 0; i < count; i++) {
            left[i] = tiku_msg_find_message(a, "member", i); right[i] = tiku_msg_find_message(b, "member", i);
            if (!left[i] || !right[i]) goto done;
        }
        for (i = 0; i < count; i++) {
            int j;
            tiku_msg_t *lv, *rv;
            const char *key = tiku_msg_find_string(left[i], "key", 0);
            for (j = 0; j < count; j++) if (!strcmp(key, tiku_msg_find_string(right[j], "key", 0))) break;
            if (j == count) goto done;
            lv = tiku_msg_find_message(left[i], "value", 0); rv = tiku_msg_find_message(right[j], "value", 0);
            same = lv && rv && equal_value(lv, rv);
            tiku_msg_free(lv); tiku_msg_free(rv);
            if (!same) goto done;
        }
        same = 1;
    done:
        for (i = 0; i < count; i++) { tiku_msg_free(left[i]); tiku_msg_free(right[i]); }
        return same;
    }
    return !strcmp(tiku_msg_find_string(a, "value", 0), tiku_msg_find_string(b, "value", 0));
}

int tiku_resource_value_equal(const tiku_msg_t *a, const tiku_msg_t *b)
{
    if (tiku_resource_value_check(a) || tiku_resource_value_check(b)) return 0;
    return a == b || equal_value(a, b);
}

void tiku_resource_snapshot_clear(tiku_resource_snapshot_t *snapshot)
{
    if (!snapshot) return;
    tiku_msg_free(snapshot->desired);
    tiku_msg_free(snapshot->persisted);
    tiku_msg_free(snapshot->applied);
    memset(snapshot, 0, sizeof *snapshot);
}
