/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_render.c - bounded scalar and JSON resource representations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource.h"
#include "tiku_resource_literal.h"
#include "tiku_resource_literal_private.h"
#include <errno.h>
#include <math.h>
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A representation staged before copying to the caller. */
typedef struct {
    char *bytes;
    size_t used, capacity;
    int failed;
} output_t;

/** @brief Append bytes within the output bound. */
static void append(output_t *out, const char *text, size_t count)
{
    if (count > out->capacity - out->used) { out->failed = 1; return; }
    memcpy(out->bytes + out->used, text, count);
    out->used += count;
}

/** @brief Append a string without its terminator. */
static void string(output_t *out, const char *text)
{
    append(out, text, strlen(text));
}

/** @brief Quote UTF-8 text with JSON control-character escaping. */
static void quote(output_t *out, const char *text)
{
    string(out, "\"");
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c == '"' || c == '\\') {
            char pair[2] = {'\\', (char)c};
            append(out, pair, sizeof pair);
        } else if (c < 32) {
            char escaped[7];
            snprintf(escaped, sizeof escaped, "\\u%04x", c);
            append(out, escaped, 6);
        } else append(out, (const char *)&c, 1);
    }
    string(out, "\"");
}

/** @brief Render a validated value recursively as diagnostic JSON. */
static void render(output_t *out, const tiku_msg_t *value, int scalar,
                   int lossless)
{
    const char *type = tiku_msg_find_string(value, "type", 0);
    int64_t integer;
    int boolean, i, count;
    double number;
    char text[64];
    if (lossless && (!strcmp(type, "counter") || !strcmp(type, "enum") ||
                     !strcmp(type, "color"))) {
        string(out, "{\"$type\":");
        quote(out, type);
        string(out, ",\"$value\":");
        render(out, value, 0, 0);
        string(out, "}");
        return;
    }
    if (!strcmp(type, "object") || !strcmp(type, "array")) {
        int object = !strcmp(type, "object");
        const char *field = object ? "member" : "item";
        count = tiku_msg_count(value, field);
        string(out, object ? "{" : "[");
        for (i = 0; i < count && !out->failed; i++) {
            tiku_msg_t *member = tiku_msg_find_message(value, field, i), *child;
            if (!member) { out->failed = 1; break; }
            if (i) string(out, ",");
            if (object) {
                quote(out, tiku_msg_find_string(member, "key", 0));
                string(out, ":");
                child = tiku_msg_find_message(member, "value", 0);
            } else child = member;
            if (child)
                render(out, child, 0, lossless);
            else out->failed = 1;
            if (child != member) tiku_msg_free(child);
            tiku_msg_free(member);
        }
        string(out, object ? "}" : "]");
    } else if (!strcmp(type, "null")) string(out, "null");
    else if (!strcmp(type, "boolean")) {
        tiku_msg_find_bool(value, "value", 0, &boolean);
        string(out, boolean ? "true" : "false");
    } else if (!strcmp(type, "integer") || !strcmp(type, "color")) {
        tiku_msg_find_int64(value, "value", 0, &integer);
        snprintf(text, sizeof text, "%lld", (long long)integer);
        string(out, text);
    } else if (!strcmp(type, "number")) {
        tiku_msg_find_double(value, "value", 0, &number);
        snprintf(text, sizeof text, "%.17g", number);
        string(out, text);
    } else {
        const char *s = tiku_msg_find_string(value, "value", 0);
        if (scalar) string(out, s);
        else quote(out, s);
    }
}

/** @brief Stage one complete bounded representation under a C numeric locale. */
static int format(const tiku_msg_t *value, void *out, size_t max, int scalar,
                  int lossless)
{
    output_t staged = {0};
    locale_t numeric, previous;
    if (!out || tiku_resource_value_check(value)) return -1;
    staged.capacity = max < TIKU_RESOURCE_FRAME_MAX ? max : TIKU_RESOURCE_FRAME_MAX;
    staged.bytes = malloc(staged.capacity ? staged.capacity : 1);
    if (!staged.bytes) return -1;
    numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric) { free(staged.bytes); return -1; }
    previous = uselocale(numeric);
    render(&staged, value, scalar, lossless);
    uselocale(previous);
    freelocale(numeric);
    string(&staged, "\n");
    if (!staged.failed) memcpy(out, staged.bytes, staged.used);
    free(staged.bytes);
    return staged.failed ? -1 : (int)staged.used;
}
int tiku_resource_value_render(const tiku_msg_t *value, void *out, size_t max)
{
    return format(value, out, max, 1, 0);
}
int tiku_resource_literal_format(const tiku_msg_t *value, void *out,
                                 size_t max)
{
    return format(value, out, max, 0, 1);
}
int tiku_resource_value_json(const tiku_msg_t *value, void *out, size_t max)
{
    return format(value, out, max, 0, 0);
}
int tiku_resource_number_decode(const char *text, double *out)
{
    locale_t numeric = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!numeric)
        return 0;
    locale_t previous = uselocale(numeric);
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    int valid = end != text && !*end && !errno && isfinite(value);
    uselocale(previous);
    freelocale(numeric);
    if (valid)
        *out = value;
    return valid;
}
