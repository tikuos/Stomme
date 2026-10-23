/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_wire.c - allocation-bounded resource message validation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Decode a little-endian unsigned word. */
static uint32_t word(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/** @brief Validate a flattened message before allocating codec objects. */
static tiku_resource_error_t inspect(const unsigned char *p, size_t size, unsigned depth)
{
    const unsigned char *names[128];
    size_t lengths[128];
    size_t at = 12;
    uint32_t fields, i;
    if (depth > TIKU_RESOURCE_DEPTH_MAX) return TIKU_RESOURCE_LIMIT;
    if (size < 12 || word(p) != TIKU_MSG_MAGIC) return TIKU_RESOURCE_INVALID;
    fields = word(p + 8);
    if (fields > 128) return TIKU_RESOURCE_LIMIT;
    for (i = 0; i < fields; i++) {
        size_t length, j;
        uint32_t type, values, k;
        if (size - at < 2) return TIKU_RESOURCE_INVALID;
        length = p[at] | (size_t)p[at + 1] << 8;
        at += 2;
        if (!length || length > 127 || length > size - at) return TIKU_RESOURCE_INVALID;
        names[i] = p + at; lengths[i] = length;
        for (j = 0; j < length; j++)
            if (p[at + j] < 33 || p[at + j] > 126) return TIKU_RESOURCE_INVALID;
        for (j = 0; j < i; j++)
            if (lengths[j] == length && !memcmp(names[j], names[i], length)) return TIKU_RESOURCE_INVALID;
        at += length;
        if (size - at < 8) return TIKU_RESOURCE_INVALID;
        type = word(p + at); values = word(p + at + 4); at += 8;
        if (!values || values > 128) return TIKU_RESOURCE_LIMIT;
        if (type < TIKU_MSG_T_BOOL || type > TIKU_MSG_T_REF) return TIKU_RESOURCE_INVALID;
        for (k = 0; k < values; k++) {
            size_t bytes, fixed = 0;
            if (size - at < 4) return TIKU_RESOURCE_INVALID;
            bytes = word(p + at); at += 4;
            if (bytes > size - at) return TIKU_RESOURCE_INVALID;
            if (type == TIKU_MSG_T_BOOL || type == TIKU_MSG_T_INT8) fixed = 1;
            if (type == TIKU_MSG_T_INT16) fixed = 2;
            if (type == TIKU_MSG_T_INT32 || type == TIKU_MSG_T_FLOAT) fixed = 4;
            if (type == TIKU_MSG_T_INT64 || type == TIKU_MSG_T_DOUBLE || type == TIKU_MSG_T_POINT) fixed = 8;
            if (type == TIKU_MSG_T_RECT) fixed = 16;
            if (fixed && bytes != fixed) return TIKU_RESOURCE_INVALID;
            if (type == TIKU_MSG_T_BOOL && p[at] > 1) return TIKU_RESOURCE_INVALID;
            if (type == TIKU_MSG_T_STRING || type == TIKU_MSG_T_REF) {
                if (!bytes || bytes > TIKU_RESOURCE_TEXT_MAX + 1 || p[at + bytes - 1] ||
                    memchr(p + at, 0, bytes - 1) ||
                    !tiku_resource_utf8_valid((const char *)p + at, bytes - 1)) return TIKU_RESOURCE_INVALID;
            }
            if (type == TIKU_MSG_T_DOUBLE) {
                uint64_t bits = word(p + at) | (uint64_t)word(p + at + 4) << 32;
                double number;
                memcpy(&number, &bits, sizeof number);
                if (!isfinite(number)) return TIKU_RESOURCE_INVALID;
            }
            if (type == TIKU_MSG_T_FLOAT || type == TIKU_MSG_T_POINT || type == TIKU_MSG_T_RECT) {
                size_t offset;
                for (offset = 0; offset < bytes; offset += 4) {
                    uint32_t bits = word(p + at + offset);
                    float number;
                    memcpy(&number, &bits, sizeof number);
                    if (!isfinite(number)) return TIKU_RESOURCE_INVALID;
                }
            }
            if (type == TIKU_MSG_T_MESSAGE) {
                tiku_resource_error_t error = inspect(p + at, bytes, depth + 1);
                if (error) return error;
            }
            at += bytes;
        }
    }
    return at == size ? TIKU_RESOURCE_OK : TIKU_RESOURCE_INVALID;
}

tiku_resource_error_t tiku_resource_message_check_nested(const void *bytes, size_t size, unsigned ancestors)
{
    if (!bytes) return TIKU_RESOURCE_INVALID;
    if (size > TIKU_RESOURCE_FRAME_MAX || ancestors >= TIKU_RESOURCE_DEPTH_MAX) return TIKU_RESOURCE_LIMIT;
    return inspect(bytes, size, ancestors + 1);
}

tiku_resource_error_t tiku_resource_message_check(const void *bytes, size_t size)
{
    return tiku_resource_message_check_nested(bytes, size, 0);
}

tiku_resource_error_t tiku_resource_message_decode(const void *bytes, size_t size, tiku_msg_t **out)
{
    tiku_resource_error_t error;
    if (!out) return TIKU_RESOURCE_INVALID;
    *out = NULL;
    error = tiku_resource_message_check(bytes, size);
    if (error) return error;
    *out = tiku_msg_unflatten(bytes, size, NULL);
    return *out ? TIKU_RESOURCE_OK : TIKU_RESOURCE_NO_MEMORY;
}
