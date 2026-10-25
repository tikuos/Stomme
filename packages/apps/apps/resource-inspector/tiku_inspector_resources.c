/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_inspector_resources.c - read-only observations of the native inspector.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_inspector_private.h"
#include <stdlib.h>
#include <string.h>
/** @brief Copy the inspector's current observed target and native operation state. */
static tiku_msg_t *value(ri_app_t *p)
{
    tiku_msg_t *v = tiku_app_resource_object(0);
    int bad = 0;
#define TEXT(k, s)                                                            \
    tiku_app_resource_member(                                                 \
        v, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, s))
#define INT(k, n)                                                             \
    tiku_app_resource_member(                                                 \
        v, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, n))
    tiku_edit_snapshot_t path = {0};
    if (tiku_editdoc_snapshot(p->doc[0], &path)) {
        tiku_msg_free(v);
        return NULL;
    }
    bad |= TEXT("path", path.text);
    const char *shown = tiku_msg_find_string(p->description, "path", 0);
    bad |= TEXT("observed_path", shown ? shown : "");
    bad |= TEXT("endpoint", p->endpoint.text);
    bad |= TEXT("connected_endpoint", p->connected);
    bad |= TEXT("status", p->status);
    bad |= TEXT("job", p->job);
    bad |= TEXT("request_id", p->mutation);
    bad |= INT("events", p->events);
    bad |= INT("busy", ri_transport_busy(p->transport));
    bad |= INT("refresh_required", p->refresh_required);
    bad |= INT("catalog_entries", p->count);
    tiku_edit_snapshot_free(&path);
#undef TEXT
#undef INT
    if (bad) {
        tiku_msg_free(v);
        return NULL;
    }
    return v;
}
/** @brief The inspector exposes one read-only model observation. */
static int catalog(void *state, tiku_app_resources_t *owner, size_t index,
                   tiku_resource_initial_t *out)
{
    (void)owner;
    if (index)
        return 0;
    return tiku_app_resource_initial(out, "inspection", TIKU_RESOURCE_STATE,
                                     TIKU_RESOURCE_OBJECT, value(state));
}
/** @brief Capture owned UI state without dispatching a second resource request. */
static tiku_resource_error_t capture(void *state, tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    (void)owner;
    if (strcmp(path, "inspection"))
        return TIKU_RESOURCE_UNSUPPORTED;
    return tiku_app_resource_snapshot(out, value(state),
                                      "native_inspector_owner")
               ? TIKU_RESOURCE_NO_MEMORY
               : TIKU_RESOURCE_OK;
}
/** @brief Proposal dispatch remains in native UI controls; exported menus are observational. */
static tiku_resource_error_t change(void *state, tiku_app_resources_t *owner,
                                    const char *path, const tiku_msg_t *input,
                                    int invoke, tiku_resource_snapshot_t *out,
                                    tiku_resource_effect_t *effect)
{
    (void)state;
    (void)owner;
    (void)path;
    (void)input;
    (void)invoke;
    (void)out;
    *effect = TIKU_RESOURCE_EFFECT_NONE;
    return TIKU_RESOURCE_UNSUPPORTED;
}
/** @brief Publish the versioned read-only inspector extension. */
__attribute__((visibility("default")))
const tiku_app_resource_export_t tiku_app_resources_v1 = {
    .version = 1,
    .size = sizeof(tiku_app_resource_export_t),
    .catalog = catalog,
    .capture = capture,
    .change = change};
