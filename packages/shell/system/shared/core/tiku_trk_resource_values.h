/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_values.h - private typed snapshot construction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RESOURCE_VALUES_H_
#define TIKU_TRK_RESOURCE_VALUES_H_
#include "tiku_resource_registry.h"
#include <stdio.h>
#include <string.h>

/** @brief Allocate an empty typed container. */
static inline tiku_msg_t *trk_value_container(const char *type)
{
    tiku_msg_t *out = tiku_msg_new(0);
    if (out && !tiku_msg_add_string(out, "type", type)) {
        tiku_msg_free(out);
        out = NULL;
    }
    return out;
}

/** @brief Append an object member and consume its value, including on
 * failure.
 */
static inline int trk_value_member(tiku_msg_t *object, const char *key,
                                   tiku_msg_t *value)
{
    tiku_msg_t *member = tiku_msg_new(0);
    int ok = object && member && value &&
             tiku_msg_add_string(member, "key", key) &&
             tiku_msg_add_message(member, "value", value) &&
             tiku_msg_add_message(object, "member", member);
    tiku_msg_free(member); tiku_msg_free(value); return ok ? 0 : -1;
}

/** @brief Append an array item and consume its value. */
static inline int trk_value_item(tiku_msg_t *array, tiku_msg_t *value)
{
    int ok = array && value && tiku_msg_add_message(array, "item", value);
    tiku_msg_free(value); return ok ? 0 : -1;
}

/** @brief Return an owned copy of an exact object member, or NULL. */
static inline tiku_msg_t *trk_value_find(const tiku_msg_t *object,
                                         const char *key)
{
    int i;
    for (i = 0; i < tiku_msg_count(object, "member"); i++) {
        tiku_msg_t *member = tiku_msg_find_message(object, "member", i),
                   *value = NULL;
        const char *name = tiku_msg_find_string(member, "key", 0);
        if (name && !strcmp(name, key))
            value = tiku_msg_find_message(member, "value", 0);
        tiku_msg_free(member); if (value) return value;
    }
    return NULL;
}

/** @brief Copy one observed value into independent snapshot fields and
 * consume it.
 */
static inline int trk_value_snapshot(tiku_resource_snapshot_t *out,
                                     tiku_msg_t *value, const char *state)
{
    memset(out, 0, sizeof *out);
    out->desired = tiku_msg_copy(value);
    out->persisted = tiku_msg_copy(value);
    out->applied = value;
    if (!out->desired || !out->persisted || !value) {
        tiku_resource_snapshot_clear(out);
        return -1;
    }
    (void)snprintf(out->apply_state, sizeof out->apply_state, "%s", state);
    return 0;
}

/** @brief Publish a changed snapshot after recovering the combined provider's
 * current stamp.
 */
static inline int trk_value_publish(tiku_resource_registry_t *registry,
                                    const char *path,
                                    const tiku_resource_snapshot_t *next)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    tiku_resource_snapshot_t before = {0};
    tiku_resource_stamp_t stamp;
    tiku_msg_t *description = NULL;
    tiku_resource_error_t error;
    strcpy(caller.principal, "00000000000000000000000000000001");
    if (!registry) return 0;
    error = tiku_resource_read(registry, &caller, path, &before);
    if (!error && tiku_resource_value_equal(before.desired, next->desired) &&
        tiku_resource_value_equal(before.persisted, next->persisted) &&
        tiku_resource_value_equal(before.applied, next->applied) &&
        !strcmp(before.apply_state, next->apply_state)) {
        tiku_resource_snapshot_clear(&before); return 0;
    }
    tiku_resource_snapshot_clear(&before);
    if (tiku_resource_describe(registry, &caller, path, &description))
        return -1;
    strcpy(stamp.provider, tiku_msg_find_string(description, "provider", 0));
    strcpy(stamp.incarnation,
           tiku_msg_find_string(description, "incarnation", 0));
    tiku_resource_counter_parse(
        tiku_msg_find_string(description, "revision", 0), &stamp.revision);
    tiku_msg_free(description);
    return tiku_resource_publish(registry, &stamp, path, next, &stamp) ? -1 : 0;
}
#endif
