/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_resource_literal.c - bounded JSON and explicit typed client inputs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_literal.h"
#include "tiku_resource_literal_private.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
/** @brief Parser cursor within an input already bounded by FRAME_MAX. */
typedef struct {
    const char *p;
    unsigned depth;
} parser_t;
/** @brief Skip JSON whitespace without accepting other control characters. */
static void space(parser_t *p)
{
    while (*p->p && strchr(" \t\r\n", *p->p))
        p->p++;
}
/** @brief Decode a hexadecimal digit or return -1. */
static int hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
/** @brief Consume four hexadecimal digits from a JSON Unicode escape. */
static int quad(parser_t *p, unsigned *out)
{
    unsigned n = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex(*p->p);
        if (d < 0)
            return 0;
        p->p++;
        n = n * 16 + (unsigned)d;
    }
    *out = n;
    return 1;
}
/** @brief Decode a whole UTF-8 string, refusing NUL and unpaired surrogates. */
static char *string(parser_t *p)
{
    if (*p->p++ != '"')
        return NULL;
    char *out = malloc(TIKU_RESOURCE_TEXT_MAX + 1);
    size_t n = 0;
    if (!out)
        return NULL;
    while (*p->p && *p->p != '"') {
        unsigned c = (unsigned char)*p->p++;
        if (c < 32)
            goto fail;
        if (c == '\\') {
            c = (unsigned char)*p->p;
            if (!c)
                goto fail;
            p->p++;
            const char *esc = "\"\\/bfnrt", *at = strchr(esc, (int)c);
            if (at) {
                static const unsigned char decoded[] = {
                    '"', '\\', '/', '\b', '\f', '\n', '\r', '\t'};
                c = decoded[at - esc];
            } else if (c == 'u') {
                if (!quad(p, &c))
                    goto fail;
                if (c >= 0xd800 && c <= 0xdbff) {
                    unsigned low;
                    if (p->p[0] != '\\' || p->p[1] != 'u')
                        goto fail;
                    p->p += 2;
                    if (!quad(p, &low) || low < 0xdc00 || low > 0xdfff)
                        goto fail;
                    c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00;
                } else if (c >= 0xdc00 && c <= 0xdfff)
                    goto fail;
                if (!c)
                    goto fail;
                unsigned char bytes[4];
                size_t len;
                if (c < 0x80) {
                    bytes[0] = c;
                    len = 1;
                } else if (c < 0x800) {
                    bytes[0] = 0xc0 | (c >> 6);
                    bytes[1] = 0x80 | (c & 63);
                    len = 2;
                } else if (c < 0x10000) {
                    bytes[0] = 0xe0 | (c >> 12);
                    bytes[1] = 0x80 | ((c >> 6) & 63);
                    bytes[2] = 0x80 | (c & 63);
                    len = 3;
                } else {
                    bytes[0] = 0xf0 | (c >> 18);
                    bytes[1] = 0x80 | ((c >> 12) & 63);
                    bytes[2] = 0x80 | ((c >> 6) & 63);
                    bytes[3] = 0x80 | (c & 63);
                    len = 4;
                }
                if (len > TIKU_RESOURCE_TEXT_MAX - n)
                    goto fail;
                memcpy(out + n, bytes, len);
                n += len;
                continue;
            } else
                goto fail;
        }
        if (n == TIKU_RESOURCE_TEXT_MAX)
            goto fail;
        out[n++] = (char)c;
    }
    if (*p->p != '"')
        goto fail;
    p->p++;
    out[n] = 0;
    if (!tiku_resource_utf8_valid(out, TIKU_RESOURCE_TEXT_MAX))
        goto fail;
    return out;
fail:
    free(out);
    return NULL;
}
/** @brief Allocate a typed container with no children. */
static tiku_msg_t *container(const char *type)
{
    tiku_msg_t *out = tiku_msg_new(0);
    if (out && !tiku_msg_add_string(out, "type", type)) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief Consume a value while adding an object member or array item. */
static int child(tiku_msg_t *out, const char *key, tiku_msg_t *value)
{
    tiku_msg_t *m = key ? tiku_msg_new(0) : NULL;
    int ok = value && (key ? (m && tiku_msg_add_string(m, "key", key) &&
                              tiku_msg_add_message(m, "value", value) &&
                              tiku_msg_add_message(out, "member", m))
                           : tiku_msg_add_message(out, "item", value));
    tiku_msg_free(m);
    tiku_msg_free(value);
    return ok;
}
/** @brief Parse one JSON number with canonical lexical boundaries. */
static tiku_msg_t *number(parser_t *p)
{
    const char *start = p->p;
    int real = 0;
    if (*p->p == '-')
        p->p++;
    if (*p->p == '0')
        p->p++;
    else {
        if (*p->p < '1' || *p->p > '9')
            return NULL;
        while (*p->p >= '0' && *p->p <= '9')
            p->p++;
    }
    if (*p->p == '.') {
        real = 1;
        p->p++;
        if (*p->p < '0' || *p->p > '9')
            return NULL;
        while (*p->p >= '0' && *p->p <= '9')
            p->p++;
    }
    if (*p->p == 'e' || *p->p == 'E') {
        real = 1;
        p->p++;
        if (*p->p == '+' || *p->p == '-')
            p->p++;
        if (*p->p < '0' || *p->p > '9')
            return NULL;
        while (*p->p >= '0' && *p->p <= '9')
            p->p++;
    }
    size_t n = (size_t)(p->p - start);
    char text[128];
    if (n >= sizeof text)
        return NULL;
    memcpy(text, start, n);
    text[n] = 0;
    if (!real) {
        char *end;
        errno = 0;
        long long value = strtoll(text, &end, 10);
        return errno || *end
                   ? NULL
                   : tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, value);
    }
    double value;
    if (!tiku_resource_number_decode(text, &value))
        return NULL;
    tiku_msg_t *out = container("number");
    if (out && !tiku_msg_add_double(out, "value", value)) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
/** @brief Convert an exact custom-type wrapper while retaining ordinary objects. */
static tiku_msg_t *wrapper(tiku_msg_t *out)
{
    tiku_msg_t *type = NULL, *value = NULL;
    int fields = tiku_msg_count(out, "member");
    for (int i = 0; i < fields; i++) {
        tiku_msg_t *m = tiku_msg_find_message(out, "member", i);
        const char *key = tiku_msg_find_string(m, "key", 0);
        if (key && !strcmp(key, "$type"))
            type = tiku_msg_find_message(m, "value", 0);
        if (key && !strcmp(key, "$value"))
            value = tiku_msg_find_message(m, "value", 0);
        tiku_msg_free(m);
    }
    if (!type && !value)
        return out;
    tiku_msg_t *converted = NULL;
    const char *kind = tiku_msg_find_string(type, "type", 0),
               *name = tiku_msg_find_string(type, "value", 0),
               *text = tiku_msg_find_string(value, "value", 0),
               *vtype = tiku_msg_find_string(value, "type", 0);
    if (fields == 2 && kind && !strcmp(kind, "text") && name && value) {
        if ((!strcmp(name, "counter") || !strcmp(name, "enum")) && vtype &&
            !strcmp(vtype, "text") && text)
            converted = tiku_resource_value_string(!strcmp(name, "counter")
                                                       ? TIKU_RESOURCE_COUNTER
                                                       : TIKU_RESOURCE_ENUM,
                                                   text);
        else if (!strcmp(name, "color")) {
            int64_t n;
            if (tiku_msg_find_int64(value, "value", 0, &n))
                converted =
                    tiku_resource_value_integer(TIKU_RESOURCE_COLOR, n);
        }
    }
    tiku_msg_free(type);
    tiku_msg_free(value);
    tiku_msg_free(out);
    return converted;
}
/** @brief Parse one recursively bounded JSON value. */
static tiku_msg_t *value(parser_t *p)
{
    space(p);
    if (++p->depth > TIKU_RESOURCE_DEPTH_MAX) {
        p->depth--;
        return NULL;
    }
    tiku_msg_t *out = NULL;
    char c = *p->p;
    if (c == '"') {
        char *s = string(p);
        if (s) {
            out = tiku_resource_value_string(TIKU_RESOURCE_TEXT, s);
            free(s);
        }
    } else if (c == '{' || c == '[') {
        int object = c == '{';
        char end = object ? '}' : ']';
        p->p++;
        out = container(object ? "object" : "array");
        space(p);
        unsigned count = 0;
        if (*p->p != end)
            for (;;) {
                char *key = NULL;
                if (++count > TIKU_RESOURCE_MEMBERS_MAX)
                    goto fail;
                if (object) {
                    space(p);
                    if (*p->p != '"' || !(key = string(p)))
                        goto fail;
                    space(p);
                    if (*p->p != ':') {
                        free(key);
                        goto fail;
                    }
                    p->p++;
                }
                tiku_msg_t *v = value(p);
                int ok = child(out, key, v);
                free(key);
                if (!ok)
                    goto fail;
                space(p);
                if (*p->p == end)
                    break;
                if (*p->p != ',')
                    goto fail;
                p->p++;
            }
        if (*p->p != end)
            goto fail;
        p->p++;
        if (tiku_resource_value_check(out))
            goto fail;
        if (object)
            out = wrapper(out);
    } else if (!strncmp(p->p, "true", 4)) {
        p->p += 4;
        out = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    } else if (!strncmp(p->p, "false", 5)) {
        p->p += 5;
        out = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    } else if (!strncmp(p->p, "null", 4)) {
        p->p += 4;
        out = container("null");
    } else if (c == '-' || (c >= '0' && c <= '9'))
        out = number(p);
    p->depth--;
    return out;
fail:
    tiku_msg_free(out);
    p->depth--;
    return NULL;
}
tiku_resource_error_t tiku_resource_literal_parse(const char *type,
                                                  const char *text,
                                                  tiku_msg_t **out)
{
    if (out)
        *out = NULL;
    if (!type || !text || !out)
        return TIKU_RESOURCE_INVALID;
    if (strnlen(text, TIKU_RESOURCE_FRAME_MAX + 1) > TIKU_RESOURCE_FRAME_MAX)
        return TIKU_RESOURCE_LIMIT;
    if (!strcmp(type, "text") || !strcmp(type, "enum") ||
        !strcmp(type, "counter"))
        *out = tiku_resource_value_string(
            !strcmp(type, "text")   ? TIKU_RESOURCE_TEXT
            : !strcmp(type, "enum") ? TIKU_RESOURCE_ENUM
                                    : TIKU_RESOURCE_COUNTER,
            text);
    else if (!strcmp(type, "color")) {
        char *end;
        errno = 0;
        unsigned long long n = strtoull(text, &end, 0);
        if (!errno && *text && !*end && text[0] >= '0' && text[0] <= '9' &&
            n <= UINT32_MAX)
            *out =
                tiku_resource_value_integer(TIKU_RESOURCE_COLOR, (int64_t)n);
    } else {
        parser_t p = {.p = text};
        *out = value(&p);
        space(&p);
        const char *actual = tiku_msg_find_string(*out, "type", 0);
        int valid = !*p.p && actual &&
                    (!strcmp(type, "json") || !strcmp(type, actual));
        if (!strcmp(type, "number") && actual && !strcmp(actual, "integer") &&
            !*p.p) {
            double n;
            if (tiku_resource_number_decode(text, &n)) {
                tiku_msg_free(*out);
                *out = container("number");
                valid = *out && tiku_msg_add_double(*out, "value", n);
            }
        }
        if (!valid) {
            tiku_msg_free(*out);
            *out = NULL;
        }
    }
    tiku_resource_error_t error =
        *out ? tiku_resource_value_check(*out) : TIKU_RESOURCE_VALUE;
    if (error) {
        tiku_msg_free(*out);
        *out = NULL;
    }
    return error;
}
