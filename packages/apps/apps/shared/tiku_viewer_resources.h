/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_viewer_resources.h - app-owned document observations for native viewers.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VIEWER_RESOURCES_H_
#define TIKU_VIEWER_RESOURCES_H_
#include "tiku_app_resources.h"
#include <string.h>
#define VR_MEMBER tiku_app_resource_member
#define VR_TEXT(o, k, v)                                                      \
    VR_MEMBER(o, k, tiku_resource_value_string(TIKU_RESOURCE_TEXT, v))
#define VR_INT(o, k, v)                                                       \
    VR_MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_INTEGER, v))
#define VR_BOOL(o, k, v)                                                      \
    VR_MEMBER(o, k, tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, !!(v)))
#define VR_NUMBER(o, k, v) VR_MEMBER(o, k, tiku_app_resource_number_value(v))
#define VR_COUNTER(o, k, v) VR_MEMBER(o, k, tiku_app_resource_counter_value(v))
/** @brief Borrowed native viewer facts and optional application-owned view controllers. */
typedef struct {
    uint64_t identity, generation;
    uint32_t window;
    const char *path, *type;
    int available, busy, dirty, read_only;
    void *controller;
    tiku_resource_error_t (*observe)(void *, const char *, tiku_msg_t **);
    int (*option)(void *, size_t, tiku_resource_initial_t *);
    tiku_resource_error_t (*change)(void *, const char *, const tiku_msg_t *,
                                    int, tiku_resource_snapshot_t *,
                                    tiku_resource_effect_t *);
} vr_document_t;
/** @brief Match a live owner-local document identifier before reading native state. */
static inline const char *vr_suffix(tiku_app_resources_t *owner,
                                    const vr_document_t *document,
                                    const char *path)
{
    const char *id = tiku_app_resources_document(owner, document->identity);
    return id && !strncmp(path, "documents/", 10) && strlen(path) > 42 &&
                   !strncmp(path + 10, id, 32) && path[42] == '/'
               ? path + 43
               : NULL;
}
/** @brief Capture native state; redraw requests do not count as document dirtiness. */
static inline tiku_resource_error_t vr_capture(tiku_app_resources_t *owner,
                                               const vr_document_t *d,
                                               const char *path,
                                               tiku_resource_snapshot_t *out)
{
    const char *name = vr_suffix(owner, d, path);
    if (!name)
        return TIKU_RESOURCE_UNSUPPORTED;
    tiku_msg_t *value = NULL;
    int bad = 0;
    if (!strcmp(name, "identity")) {
        value = tiku_app_resource_object(0);
        bad |= VR_TEXT(value, "id",
                       tiku_app_resources_document(owner, d->identity));
        bad |= VR_COUNTER(value, "generation", d->generation);
        bad |= VR_INT(value, "window", d->window);
        bad |= VR_BOOL(value, "available", d->available);
        bad |= VR_BOOL(value, "busy", d->busy);
        bad |= VR_BOOL(value, "read_only", d->read_only);
    } else if (!strcmp(name, "path"))
        value = tiku_resource_value_string(TIKU_RESOURCE_TEXT,
                                           d->path ? d->path : "");
    else if (!strcmp(name, "type"))
        value = tiku_resource_value_string(TIKU_RESOURCE_TEXT, d->type);
    else if (!strcmp(name, "dirty"))
        value = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, d->dirty);
    else if (!strcmp(name, "commands")) {
        value = tiku_app_resource_object(0);
        bad |= VR_TEXT(value, "dispatch", "application_native_menus");
        bad |= VR_BOOL(value, "read_only", d->read_only);
    } else {
        tiku_resource_error_t error =
            d->observe ? d->observe(d->controller, name, &value)
                       : TIKU_RESOURCE_UNSUPPORTED;
        if (error) {
            tiku_msg_free(value);
            return error;
        }
    }
    if (bad) {
        tiku_msg_free(value);
        return TIKU_RESOURCE_NO_MEMORY;
    }
    return tiku_app_resource_snapshot(out, value, "observed")
               ? TIKU_RESOURCE_NO_MEMORY
               : 0;
}
/** @brief Enumerate native session observations and optional view settings. */
static inline int vr_catalog(tiku_app_resources_t *owner,
                             const vr_document_t *d, size_t index,
                             tiku_resource_initial_t *out)
{
    static const char *const keys[] = {"identity", "path",      "type",
                                       "dirty",    "selection", "view",
                                       "commands", "objects"};
    const char *id = tiku_app_resources_document(owner, d->identity);
    if (!id)
        return -1;
    if (index >= 8) {
        int found = d->option ? d->option(d->controller, index - 8, out) : 0;
        if (found > 0)
            out->spec.path = tiku_app_resources_path(owner, "documents/%s/%s",
                                                     id, out->spec.path);
        return found;
    }
    const char *path =
        tiku_app_resources_path(owner, "documents/%s/%s", id, keys[index]);
    out->spec = (tiku_resource_spec_t){
        .path = path,
        .kind = index >= 5 ? TIKU_RESOURCE_COLLECTION : TIKU_RESOURCE_STATE,
        .schema_version = 1,
        .schema = {.type = index == 1 || index == 2 ? TIKU_RESOURCE_TEXT
                           : index == 3             ? TIKU_RESOURCE_BOOLEAN
                                                    : TIKU_RESOURCE_OBJECT,
                   .text_max = 4096},
        .persistence = "none"};
    return vr_capture(owner, d, path, &out->initial) ? -1 : 1;
}
/** @brief Forward a current document request to its native application controller. */
static inline tiku_resource_error_t
vr_change(tiku_app_resources_t *owner, const vr_document_t *d,
          const char *path, const tiku_msg_t *value, int invoke,
          tiku_resource_snapshot_t *out, tiku_resource_effect_t *effect)
{
    const char *name = vr_suffix(owner, d, path);
    return name && d->change
               ? d->change(d->controller, name, value, invoke, out, effect)
               : TIKU_RESOURCE_UNSUPPORTED;
}
#endif
