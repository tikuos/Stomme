/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_resource_values.c - owned application observations and exact arguments.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app_resources.h"
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "tiku_crypto.h"
#include <string.h>
tiku_msg_t *tiku_app_resource_object(int array)
{
    tiku_msg_t *out = tiku_msg_new(0);
    if (out && !tiku_msg_add_string(out, "type", array ? "array" : "object")) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
int tiku_app_resource_member(tiku_msg_t *object, const char *key,
                             tiku_msg_t *value)
{
    tiku_msg_t *member = tiku_msg_new(0);
    int ok = object && key && value && member &&
             tiku_msg_add_string(member, "key", key) &&
             tiku_msg_add_message(member, "value", value) &&
             tiku_msg_add_message(object, "member", member);
    tiku_msg_free(member);
    tiku_msg_free(value);
    return ok ? 0 : -1;
}
int tiku_app_resource_item(tiku_msg_t *array, tiku_msg_t *value)
{
    int ok = array && value && tiku_msg_add_message(array, "item", value);
    tiku_msg_free(value);
    return ok ? 0 : -1;
}
tiku_msg_t *tiku_app_resource_find(const tiku_msg_t *value, const char *key)
{
    for (int i = 0; i < tiku_msg_count(value, "member"); i++) {
        tiku_msg_t *member = tiku_msg_find_message(value, "member", i);
        const char *name = tiku_msg_find_string(member, "key", 0);
        tiku_msg_t *out = name && !strcmp(name, key)
                              ? tiku_msg_find_message(member, "value", 0)
                              : NULL;
        tiku_msg_free(member);
        if (out)
            return out;
    }
    return NULL;
}
int tiku_app_resource_snapshot(tiku_resource_snapshot_t *out,
                               tiku_msg_t *owned, const char *phase)
{
    if (!out || !owned || tiku_resource_value_check(owned)) {
        tiku_msg_free(owned);
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->desired = owned;
    out->persisted = tiku_msg_copy(owned);
    out->applied = tiku_msg_copy(owned);
    snprintf(out->apply_state, sizeof out->apply_state, "%s",
             phase ? phase : "observed");
    if (!out->persisted || !out->applied) {
        tiku_resource_snapshot_clear(out);
        return -1;
    }
    return 0;
}
tiku_msg_t *tiku_app_resource_stream_metadata(void)
{
    tiku_msg_t *out = tiku_app_resource_object(0);
    if (tiku_app_resource_member(
            out, "bytes",
            tiku_resource_value_string(TIKU_RESOURCE_COUNTER, "0")) ||
        tiku_app_resource_member(
            out, "content_id",
            tiku_resource_value_string(TIKU_RESOURCE_TEXT, ""))) {
        tiku_msg_free(out);
        return NULL;
    }
    return out;
}
int tiku_app_resource_keys(const tiku_msg_t *value, const char *const *keys,
                           size_t count)
{
    const char *type = tiku_msg_find_string(value, "type", 0);
    uint64_t seen = 0;
    if (!type || strcmp(type, "object") || count > 64)
        return 0;
    for (int i = 0; i < tiku_msg_count(value, "member"); i++) {
        tiku_msg_t *member = tiku_msg_find_message(value, "member", i);
        const char *key = tiku_msg_find_string(member, "key", 0);
        size_t index;
        for (index = 0; index < count; index++)
            if (key && !strcmp(keys[index], key))
                break;
        tiku_msg_free(member);
        if (index == count || (seen & (UINT64_C(1) << index)))
            return 0;
        seen |= UINT64_C(1) << index;
    }
    return seen == (count == 64 ? UINT64_MAX : ((UINT64_C(1) << count) - 1));
}
int tiku_app_resource_text(const tiku_msg_t *value, const char *key, char *out,
                           size_t capacity)
{
    tiku_msg_t *member = tiku_app_resource_find(value, key);
    const char *type = tiku_msg_find_string(member, "type", 0),
               *text = tiku_msg_find_string(member, "value", 0);
    int valid = out && type && !strcmp(type, "text") && text &&
                strlen(text) < capacity;
    if (valid)
        memcpy(out, text, strlen(text) + 1);
    tiku_msg_free(member);
    return valid;
}
int tiku_app_resource_integer(const tiku_msg_t *value, const char *key,
                              int64_t low, int64_t high, int64_t *out)
{
    tiku_msg_t *member = tiku_app_resource_find(value, key);
    const char *type = tiku_msg_find_string(member, "type", 0);
    int64_t number;
    int valid = out && type && !strcmp(type, "integer") &&
                tiku_msg_find_int64(member, "value", 0, &number) &&
                number >= low && number <= high;
    if (valid)
        *out = number;
    tiku_msg_free(member);
    return valid;
}
tiku_msg_t *tiku_app_resource_counter_value(uint64_t value)
{
    char text[32];
    snprintf(text, sizeof text, "%llu", (unsigned long long)value);
    return tiku_resource_value_string(TIKU_RESOURCE_COUNTER, text);
}
int tiku_app_resource_counter(const tiku_msg_t *value, const char *key,
                              uint64_t *out)
{
    tiku_msg_t *member = tiku_app_resource_find(value, key);
    const char *type = tiku_msg_find_string(member, "type", 0),
               *text = tiku_msg_find_string(member, "value", 0);
    int valid = type && !strcmp(type, "counter") && text &&
                !tiku_resource_counter_parse(text, out);
    tiku_msg_free(member);
    return valid;
}
int tiku_app_resource_token(const tiku_msg_t *value, char out[65])
{
    char text[16384];
    unsigned char hash[32];
    int n = tiku_resource_value_render(value, text, sizeof text);
    if (n < 0 || tiku_hash(TIKU_HASH_SHA256, text, (size_t)n, hash) != 32)
        return -1;
    for (size_t i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", hash[i]);
    return 0;
}
int tiku_app_resource_initial(tiku_resource_initial_t *out, const char *path,
                              tiku_resource_kind_t kind,
                              tiku_resource_type_t type, tiku_msg_t *value)
{
    out->spec = (tiku_resource_spec_t){
        .path = path,
        .kind = kind,
        .schema_version = 1,
        .schema = {.type = type,
                   .integer_min = INT64_MIN,
                   .integer_max = INT64_MAX,
                   .number_min = -DBL_MAX,
                   .number_max = DBL_MAX,
                   .text_max = TIKU_RESOURCE_TEXT_MAX},
        .persistence = kind == TIKU_RESOURCE_SETTING ? "session" : "none"};
    return tiku_app_resource_snapshot(&out->initial, value, "observed") ? -1
                                                                        : 1;
}

tiku_msg_t *tiku_app_resource_number_value(double number)
{
    if (!isfinite(number))
        return NULL;
    tiku_msg_t *value = tiku_msg_new(0);
    if (!value || !tiku_msg_add_string(value, "type", "number") ||
        !tiku_msg_add_double(value, "value", number)) {
        tiku_msg_free(value);
        return NULL;
    }
    return value;
}
int tiku_app_resource_scalar(const tiku_msg_t *value,
                             tiku_resource_type_t wanted, int64_t *out)
{
    const char *type = tiku_msg_find_string(value, "type", 0);
    if (!out || !type || strcmp(type, tiku_resource_type_name(wanted)))
        return 0;
    if (wanted == TIKU_RESOURCE_BOOLEAN) {
        int n;
        if (!tiku_msg_find_bool(value, "value", 0, &n))
            return 0;
        *out = n;
        return 1;
    }
    return wanted == TIKU_RESOURCE_INTEGER &&
           tiku_msg_find_int64(value, "value", 0, out);
}
