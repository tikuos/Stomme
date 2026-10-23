/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_replacements.c - bounded complete setting replacement lists.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource.h"
#include <stdlib.h>
#include <string.h>

/** @brief Check complete unique canonical paths, values and persistence scopes. */
tiku_resource_error_t tiku_resource_replacements_check(const tiku_resource_replacement_t *items, size_t count)
{
    size_t i, j, paths = 0, bytes = 0;
    if (!items || !count) return TIKU_RESOURCE_INVALID;
    if (count > TIKU_RESOURCE_TRANSACTION_MAX) return TIKU_RESOURCE_LIMIT;
    for (i = 0; i < count; i++) {
        const char *durability = items[i].durability;
        tiku_resource_error_t error;
        if (!tiku_resource_path_valid(items[i].path) || !durability ||
            (strcmp(durability, "user") && strcmp(durability, "session") && strcmp(durability, "document"))) return TIKU_RESOURCE_INVALID;
        paths += strlen(items[i].path) + 1;
        if (paths > TIKU_RESOURCE_GROUP_PATH_BYTES) return TIKU_RESOURCE_LIMIT;
        for (j = 0; j < i; j++) if (!strcmp(items[i].path, items[j].path)) return TIKU_RESOURCE_INVALID;
        error = tiku_resource_value_check(items[i].value);
        if (error) return error;
        bytes += tiku_msg_flat_size(items[i].value);
        if (bytes > TIKU_RESOURCE_FRAME_MAX) return TIKU_RESOURCE_LIMIT;
    }
    return TIKU_RESOURCE_OK;
}

tiku_msg_t *tiku_resource_replacements_message(const tiku_resource_replacement_t *items, size_t count)
{
    tiku_msg_t *message;
    size_t i;
    if (tiku_resource_replacements_check(items, count)) return NULL;
    message = tiku_msg_new(0);
    if (!message || !tiku_msg_add_int32(message, "count", (int32_t)count)) goto fail;
    for (i = 0; i < count; i++) {
        tiku_msg_t *entry = tiku_msg_new(0);
        int ok = entry && tiku_msg_add_string(entry, "path", items[i].path) &&
            tiku_msg_add_string(entry, "durability", items[i].durability) &&
            tiku_msg_add_message(entry, "value", items[i].value) && tiku_msg_add_message(message, "entry", entry);
        tiku_msg_free(entry); if (!ok) goto fail;
    }
    if (tiku_msg_flat_size(message) > TIKU_RESOURCE_FRAME_MAX - 2048) goto fail;
    return message;
fail:
    tiku_msg_free(message); return NULL;
}

void tiku_resource_replacements_clear(tiku_resource_replacement_t *items, size_t count)
{
    size_t i;
    if (!items) return;
    for (i = 0; i < count; i++) {
        free((void *)items[i].path); free((void *)items[i].durability);
        tiku_msg_free((tiku_msg_t *)items[i].value); memset(&items[i], 0, sizeof items[i]);
    }
}

tiku_resource_error_t tiku_resource_replacements_decode(const tiku_msg_t *message,
    tiku_resource_replacement_t *out, size_t *count)
{
    int32_t length;
    size_t i;
    tiku_resource_error_t error = TIKU_RESOURCE_INVALID;
    if (count) *count = 0;
    if (!out || !count || !message || tiku_msg_what(message) || tiku_msg_field_count(message) != 2 ||
        tiku_msg_count(message, "count") != 1 || !tiku_msg_find_int32(message, "count", 0, &length) || length < 1) return error;
    if (length > TIKU_RESOURCE_TRANSACTION_MAX || tiku_msg_flat_size(message) > TIKU_RESOURCE_FRAME_MAX - 2048) return TIKU_RESOURCE_LIMIT;
    if (!tiku_msg_has(message, "entry", TIKU_MSG_T_MESSAGE) || tiku_msg_count(message, "entry") != length) return error;
    memset(out, 0, (size_t)length * sizeof *out);
    for (i = 0; i < (size_t)length; i++) {
        tiku_msg_t *entry = tiku_msg_find_message(message, "entry", (int)i);
        const char *path = tiku_msg_find_string(entry, "path", 0), *durability = tiku_msg_find_string(entry, "durability", 0);
        int valid = entry && !tiku_msg_what(entry) && tiku_msg_field_count(entry) == 3 &&
            tiku_msg_count(entry, "path") == 1 && tiku_msg_count(entry, "durability") == 1 &&
            tiku_msg_count(entry, "value") == 1 && tiku_msg_has(entry, "value", TIKU_MSG_T_MESSAGE) &&
            tiku_resource_path_valid(path) && durability;
        if (valid) {
            out[i].path = strdup(path); out[i].durability = strdup(durability);
            out[i].value = tiku_msg_find_message(entry, "value", 0);
        }
        tiku_msg_free(entry);
        if (!valid) goto fail;
        if (!out[i].path || !out[i].durability || !out[i].value) { error = TIKU_RESOURCE_NO_MEMORY; goto fail; }
    }
    error = tiku_resource_replacements_check(out, (size_t)length);
    if (error) goto fail;
    *count = (size_t)length; return TIKU_RESOURCE_OK;
fail:
    tiku_resource_replacements_clear(out, (size_t)length); return error;
}
