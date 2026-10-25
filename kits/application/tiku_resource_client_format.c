/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_resource_client_format.c - bounded JSON for public resource metadata.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
/** @brief Staged JSON with an explicit output bound. */
typedef struct {
    char *bytes;
    size_t used, capacity;
    int failed;
} output_t;
/** @brief Append a complete byte sequence while retaining the first error. */
static void append(output_t *o, const char *text, size_t n)
{
    if (n > o->capacity - o->used) {
        o->failed = 1;
        return;
    }
    memcpy(o->bytes + o->used, text, n);
    o->used += n;
}
/** @brief Append a C string without its terminator. */
static void text(output_t *o, const char *s)
{
    append(o, s, strlen(s));
}
/** @brief Encode scalar values through the resource codec's C numeric locale. */
static void scalar(output_t *o, tiku_msg_t *value)
{
    char *bytes = malloc(TIKU_RESOURCE_FRAME_MAX);
    int n =
        bytes ? tiku_resource_value_json(value, bytes, TIKU_RESOURCE_FRAME_MAX)
              : -1;
    tiku_msg_free(value);
    if (n < 1)
        o->failed = 1;
    else
        append(o, bytes, (size_t)n - 1);
    free(bytes);
}
/** @brief Quote a complete metadata string as JSON. */
static void quoted(output_t *o, const char *s)
{
    scalar(o, tiku_resource_value_string(TIKU_RESOURCE_TEXT, s));
}
/** @brief Copy numeric metadata into a typed number. */
static tiku_msg_t *number(double n)
{
    if (!isfinite(n))
        return NULL;
    tiku_msg_t *v = tiku_msg_new(0);
    if (!v || !tiku_msg_add_string(v, "type", "number") ||
        !tiku_msg_add_double(v, "value", n)) {
        tiku_msg_free(v);
        return NULL;
    }
    return v;
}
static void message(output_t *o, const tiku_msg_t *m, unsigned depth);
/** @brief Render one supported public message value; binary content reports size. */
static void item(output_t *o, const tiku_msg_t *m, const char *name,
                 tiku_msg_type_t type, int index, unsigned depth)
{
    int boolean;
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    float f;
    double d;
    switch (type) {
    case TIKU_MSG_T_BOOL:
        if (tiku_msg_find_bool(m, name, index, &boolean))
            scalar(o, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,
                                                  boolean));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_INT8:
        if (tiku_msg_find_int8(m, name, index, &i8))
            scalar(o, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, i8));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_INT16:
        if (tiku_msg_find_int16(m, name, index, &i16))
            scalar(o, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, i16));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_INT32:
        if (tiku_msg_find_int32(m, name, index, &i32))
            scalar(o, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, i32));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_INT64:
        if (tiku_msg_find_int64(m, name, index, &i64))
            scalar(o, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, i64));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_FLOAT:
        if (tiku_msg_find_float(m, name, index, &f))
            scalar(o, number(f));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_DOUBLE:
        if (tiku_msg_find_double(m, name, index, &d))
            scalar(o, number(d));
        else
            o->failed = 1;
        break;
    case TIKU_MSG_T_STRING:
        quoted(o, tiku_msg_find_string(m, name, index));
        break;
    case TIKU_MSG_T_REF:
        quoted(o, tiku_msg_find_ref(m, name, index));
        break;
    case TIKU_MSG_T_MESSAGE: {
        tiku_msg_t *sub = tiku_msg_find_message(m, name, index);
        if (sub)
            message(o, sub, depth + 1);
        else
            o->failed = 1;
        tiku_msg_free(sub);
        break;
    }
    case TIKU_MSG_T_BLOB: {
        size_t n = 0;
        (void)tiku_msg_find_blob(m, name, index, &n);
        char bytes[64];
        snprintf(bytes, sizeof bytes, "{\"bytes\":%zu}", n);
        text(o, bytes);
        break;
    }
    default:
        o->failed = 1;
        break;
    }
}
/** @brief Render nested metadata fields without interpreting resource identities. */
static void message(output_t *o, const tiku_msg_t *m, unsigned depth)
{
    if (depth > TIKU_RESOURCE_DEPTH_MAX) {
        o->failed = 1;
        return;
    }
    text(o, "{\n");
    int fields = tiku_msg_field_count(m);
    for (int i = 0; i < fields && !o->failed; i++) {
        tiku_msg_type_t type;
        int count;
        const char *name = tiku_msg_field_at(m, i, &type, &count);
        if (i)
            text(o, ",\n");
        for (unsigned j = 0; j <= depth; j++)
            text(o, "  ");
        quoted(o, name);
        text(o, ": ");
        int array = count != 1 || !strcmp(name, "entry") ||
                    !strcmp(name, "member") || !strcmp(name, "item") ||
                    !strcmp(name, "choice");
        if (array)
            text(o, "[");
        for (int j = 0; j < count && !o->failed; j++) {
            if (j)
                text(o, ",");
            item(o, m, name, type, j, depth);
        }
        if (array)
            text(o, "]");
    }
    text(o, "\n");
    for (unsigned j = 0; j < depth; j++)
        text(o, "  ");
    text(o, "}");
}
int tiku_resource_client_format(const tiku_msg_t *m, void *out,
                                size_t capacity)
{
    if (!m || !out || !capacity || capacity > 1024 * 1024)
        return -1;
    output_t o = {.capacity = capacity, .bytes = malloc(capacity)};
    if (!o.bytes)
        return -1;
    message(&o, m, 0);
    text(&o, "\n");
    if (!o.failed)
        memcpy(out, o.bytes, o.used);
    free(o.bytes);
    return o.failed ? -1 : (int)o.used;
}
