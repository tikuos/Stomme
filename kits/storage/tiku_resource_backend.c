/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_backend.c - resource discovery through the filesystem backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_backend.h"
#include "tiku_resource_registry_private.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A caller-bound registry view and its last observed catalog epoch. */
typedef struct {
    tiku_resource_registry_t *registry;
    tiku_resource_caller_t caller;
    uint64_t epoch;
} resource_backend_t;

/** @brief Copy one visible resource entry into a filesystem model. */
static void model(tiku_backend_t *backend, const tiku_resource_entry_t *entry, tiku_model_t *out)
{
    const char *name = strrchr(entry->path, '/');
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", name && name[1] ? name + 1 : "Desktop System");
    strcpy(out->path, entry->path);
    strcpy(out->type, "application/x-stomme-resource");
    out->kind = (entry->kind == TIKU_RESOURCE_DIRECTORY || entry->kind == TIKU_RESOURCE_COLLECTION) ? TIKU_KIND_VIRTUAL_DIR : TIKU_KIND_DEVICE_NODE;
    out->facts.perm = TIKU_P_READ;
    out->facts.cap_known = 1;
    out->node_id = entry->node_id;
    out->backend = backend;
}

/** @brief Stat a visible resource or implicit parent directory. */
static int resource_stat(tiku_backend_t *backend, const char *path, tiku_model_t *out)
{
    resource_backend_t *view = backend->impl;
    tiku_resource_entry_t entry;
    if (!out || tiku_resource_entry(view->registry, &view->caller, path, &entry)) return -1;
    model(backend, &entry, out);
    return 0;
}

/** @brief Enumerate a visible directory through bounded registry pages. */
static int resource_list(tiku_backend_t *backend, const char *path, tiku_entry_fn fn, void *ctx)
{
    resource_backend_t *view = backend->impl;
    tiku_resource_entry_t page[16];
    uint64_t epoch = 0, after = 0;
    size_t count, i;
    int total = 0;
    if (!fn) return -1;
    do {
        if (tiku_resource_list(view->registry, &view->caller, path, after, &epoch,
                               page, 16, &count)) return -1;
        for (i = 0; i < count; i++) {
            tiku_model_t item;
            model(backend, &page[i], &item);
            total++;
            if (fn(&item, ctx)) return total;
            after = page[i].node_id;
        }
    } while (count == 16);
    return total;
}

/** @brief Read a complete value or an action's identity descriptor. */
static int resource_read(tiku_backend_t *backend, const char *path, void *out, size_t max)
{
    resource_backend_t *view = backend->impl;
    tiku_resource_snapshot_t snapshot;
    tiku_resource_entry_t entry;
    int result;
    if (!out || tiku_resource_entry(view->registry, &view->caller, path, &entry) ||
        (entry.kind == TIKU_RESOURCE_DIRECTORY || entry.kind == TIKU_RESOURCE_COLLECTION)) return -1;
    if (entry.kind == TIKU_RESOURCE_ACTION) {
        char text[TIKU_RESOURCE_PATH_MAX + 128];
        result = snprintf(text, sizeof text, "{\"path\":\"%s\",\"kind\":\"action\",\"id\":\"%s\"}\n", path, entry.id);
        if (result < 0 || (size_t)result > max) return -1;
        memcpy(out, text, (size_t)result);
        return result;
    }
    if (tiku_resource_read(view->registry, &view->caller, path, &snapshot)) return -1;
    result = tiku_resource_value_render(snapshot.desired, out, max);
    tiku_resource_snapshot_clear(&snapshot);
    return result;
}

/** @brief Reject context-free writes before contacting a controller. */
static int resource_write(tiku_backend_t *backend, const char *path, const void *value,
                          size_t length, char *error, size_t max)
{
    (void)backend; (void)path; (void)value; (void)length;
    if (error && max) snprintf(error, max, "requires_context");
    errno = EPERM;
    return -1;
}

/** @brief Report catalog changes as requiring a complete caller-filtered relist. */
static int resource_changes(tiku_backend_t *backend, tiku_model_change_t *out, int max, int *complete)
{
    resource_backend_t *view = backend->impl;
    (void)out; (void)max;
    if (!tiku_resource_registry_thread(view->registry)) return -1;
    if (complete) *complete = view->epoch == view->registry->epoch;
    view->epoch = view->registry->epoch;
    return 0;
}

/** @brief Resolve a currently visible identity without reusing retired nodes. */
static int resource_path(tiku_backend_t *backend, uint64_t id, char *out, size_t max)
{
    resource_backend_t *view = backend->impl;
    unsigned i;
    if (!out || !tiku_resource_registry_thread(view->registry)) return -1;
    for (i = 0; i < view->registry->count; i++) {
        resource_node_t *node = view->registry->nodes[i];
        tiku_resource_entry_t visible;
        if (node->entry.node_id == id &&
            !tiku_resource_entry(view->registry, &view->caller, node->entry.path, &visible)) {
            size_t length = strlen(visible.path);
            if (length >= max) return -1;
            memcpy(out, visible.path, length + 1);
            return 0;
        }
    }
    return -1;
}

/** @brief Release a backend view; the registry retains its own lifetime. */
static void resource_close(tiku_backend_t *backend)
{
    if (backend) { free(backend->impl); free(backend); }
}

static const tiku_backend_ops_t resource_ops = {
    .name = "stomme-resources", .stat = resource_stat, .list = resource_list,
    .read = resource_read, .write = resource_write, .changes = resource_changes,
    .path_of_id = resource_path, .close = resource_close
};

tiku_backend_t *tiku_resource_backend_open(tiku_resource_registry_t *registry,
                                          const tiku_resource_caller_t *caller)
{
    tiku_backend_t *backend;
    resource_backend_t *view;
    if (!tiku_resource_registry_thread(registry)) return NULL;
    backend = calloc(1, sizeof *backend); view = calloc(1, sizeof *view);
    if (!backend || !view) { free(backend); free(view); return NULL; }
    view->registry = registry;
    view->epoch = registry->epoch;
    if (caller) view->caller = *caller;
    backend->impl = view; backend->ops = &resource_ops;
    return backend;
}
