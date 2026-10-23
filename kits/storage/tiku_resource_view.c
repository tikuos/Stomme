/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_view.c - caller-filtered discovery and bounded subscriptions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_registry_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(RESOURCE_EVENT_CHARGE * 32u <= RESOURCE_EVENT_BYTES_MAX, "observer overflow markers exceed byte budget");

/** @brief Whether a directory contains a resource visible to this caller. */
static int visible(tiku_resource_registry_t *registry, const resource_node_t *node,
                   const tiku_resource_caller_t *caller)
{
    unsigned i;
    if (node->provider) return tiku_resource_registry_visible(node, caller);
    if (!strcmp(node->entry.path, "/")) return 1;
    for (i = 0; i < registry->count; i++) {
        const resource_node_t *child = registry->nodes[i];
        if (child->provider && tiku_resource_registry_below(child->entry.path, node->entry.path) &&
            tiku_resource_registry_visible(child, caller)) return 1;
    }
    return 0;
}

tiku_resource_error_t tiku_resource_entry(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *path, tiku_resource_entry_t *out)
{
    resource_node_t *node;
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_path_valid(path))
        return TIKU_RESOURCE_INVALID;
    node = tiku_resource_registry_node(registry, path);
    if (!node || !visible(registry, node, caller)) return TIKU_RESOURCE_NOT_FOUND;
    *out = node->entry;
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_list(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *path, uint64_t after,
    uint64_t *epoch, tiku_resource_entry_t *out, size_t capacity, size_t *count)
{
    resource_node_t *parent;
    unsigned i;
    size_t length;
    if (count) *count = 0;
    if (!tiku_resource_registry_thread(registry) || !epoch || !out || !count ||
        !tiku_resource_path_valid(path)) return TIKU_RESOURCE_INVALID;
    if (!capacity || capacity > 128) return TIKU_RESOURCE_LIMIT;
    parent = tiku_resource_registry_node(registry, path);
    if (!parent || !visible(registry, parent, caller) || (parent->entry.kind != TIKU_RESOURCE_DIRECTORY && parent->entry.kind != TIKU_RESOURCE_COLLECTION))
        return TIKU_RESOURCE_NOT_FOUND;
    if (*epoch && *epoch != registry->epoch) return TIKU_RESOURCE_STALE;
    *epoch = registry->epoch;
    length = strlen(path);
    for (i = 0; i < registry->count && *count < capacity; i++) {
        resource_node_t *node = registry->nodes[i];
        const char *suffix;
        if (node == parent || node->entry.node_id <= after ||
            !tiku_resource_registry_below(node->entry.path, path)) continue;
        suffix = node->entry.path + length + (length != 1);
        if (strchr(suffix, '/') || !visible(registry, node, caller)) continue;
        out[(*count)++] = node->entry;
    }
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_describe(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *path, tiku_msg_t **out)
{
    resource_node_t *node;
    tiku_msg_t *message, *schema;
    size_t i;
    int ok;
    static const char *const kinds[] = {"directory", "setting", "state", "action", "collection", "stream"};
    if (out) *out = NULL;
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_path_valid(path))
        return TIKU_RESOURCE_INVALID;
    node = tiku_resource_registry_node(registry, path);
    if (!node || !visible(registry, node, caller)) return TIKU_RESOURCE_NOT_FOUND;
    message = tiku_msg_new(0); schema = tiku_msg_new(0);
    if (!message || !schema) goto fail;
    ok = tiku_msg_add_string(message, "path", node->entry.path) &&
        tiku_msg_add_string(message, "id", node->entry.id) &&
        tiku_msg_add_string(message, "kind", kinds[node->entry.kind]);
    if (node->provider) {
        char revision[21];
        const tiku_resource_spec_t *spec = &node->spec;
        snprintf(revision, sizeof revision, "%llu", (unsigned long long)node->provider->stamp.revision);
        ok = ok && tiku_msg_add_string(message, "provider", node->provider->key) &&
            tiku_msg_add_string(message, "incarnation", node->provider->stamp.incarnation) &&
            tiku_msg_add_string(message, "revision", revision) &&
            tiku_msg_add_int32(message, "schema_version", (int32_t)spec->schema_version) &&
            tiku_msg_add_bool(message, "sensitive", spec->sensitive) &&
            tiku_msg_add_bool(message, "public_read", spec->public_read) &&
            tiku_msg_add_int64(message, "read_scopes", (int64_t)spec->read_scopes) &&
            tiku_msg_add_int64(message, "change_scopes", (int64_t)spec->change_scopes) &&
            tiku_msg_add_string(message, "availability", node->unavailable ? "unavailable" : "available") &&
            tiku_msg_add_string(message, "persistence", spec->persistence) &&
            tiku_msg_add_string(message, "controller", spec->controller) &&
            tiku_msg_add_bool(message, "atomic", spec->kind == TIKU_RESOURCE_SETTING && node->provider->atomic.commit != NULL) &&
            tiku_msg_add_string(message, "transaction_group", spec->kind == TIKU_RESOURCE_SETTING && node->provider->atomic.commit ? node->provider->key : "") &&
            tiku_msg_add_string(message, "dispatch", node->provider->asynchronous ? "asynchronous" : "synchronous") &&
            tiku_msg_add_int64(message, "change_features", (spec->kind == TIKU_RESOURCE_SETTING || spec->kind == TIKU_RESOURCE_ACTION) ?
                (int64_t)(TIKU_RESOURCE_FEATURE_CHANGE | (node->provider->asynchronous ? TIKU_RESOURCE_FEATURE_JOBS : 0) |
                    (spec->kind == TIKU_RESOURCE_SETTING && node->provider->atomic.commit ? TIKU_RESOURCE_FEATURE_TRANSACTION : 0)) : 0) &&
            tiku_msg_add_string(schema, "type", tiku_resource_type_name(spec->schema.type)) &&
            tiku_msg_add_int64(schema, "integer_min", spec->schema.integer_min) &&
            tiku_msg_add_int64(schema, "integer_max", spec->schema.integer_max) &&
            tiku_msg_add_double(schema, "number_min", spec->schema.number_min) &&
            tiku_msg_add_double(schema, "number_max", spec->schema.number_max) &&
            tiku_msg_add_int32(schema, "text_max", (int32_t)spec->schema.text_max);
        for (i = 0; ok && i < spec->schema.choice_count; i++)
            ok = tiku_msg_add_string(schema, "choice", spec->schema.choices[i]);
        ok = ok && tiku_msg_add_message(message, "value_schema", schema);
    }
    tiku_msg_free(schema);
    if (!ok) { tiku_msg_free(message); return TIKU_RESOURCE_NO_MEMORY; }
    *out = message;
    return TIKU_RESOURCE_OK;
fail:
    tiku_msg_free(message); tiku_msg_free(schema);
    return TIKU_RESOURCE_NO_MEMORY;
}

/** @brief Whether two watches belong to the same principal and read context. */
static int same_caller(const resource_watch_t *a, const resource_watch_t *b)
{
    return a->caller.authenticated == b->caller.authenticated &&
        !strcmp(a->caller.principal, b->caller.principal);
}

/** @brief Pause a watch with one overflow marker. */
static void overflow(resource_watch_t *watch, const resource_node_t *node)
{
    tiku_resource_event_t *event = &watch->events[0];
    memset(event, 0, sizeof *event);
    strcpy(event->stamp.provider, watch->provider);
    strcpy(event->stamp.incarnation, watch->incarnation);
    event->stamp.revision = watch->revision;
    (void)node;
    event->sequence = ++watch->sequence;
    event->overflow = 1;
    strcpy(event->path, watch->prefix);
    watch->head = 0; watch->count = 1; watch->paused = 1;
}

/** @brief Queue one ordered record containing the paths visible to each watch. */
static void emit(tiku_resource_registry_t *registry, resource_node_t *const *nodes, size_t count, int removed, int grouped)
{
    unsigned i;
    const resource_node_t *node = nodes[0];
    for (i = 0; i < registry->limits.watches; i++) {
        resource_watch_t *watch = &registry->watches[i];
        tiku_resource_event_t filtered = {0}, *event;
        unsigned j, queued = 0;
        size_t n, bytes = 0;
        if (!watch->id || watch->paused || strcmp(watch->provider, node->provider->key) ||
            strcmp(watch->incarnation, node->provider->stamp.incarnation)) continue;
        for (n = 0; n < count; n++) {
            const resource_node_t *changed = nodes[n];
            size_t length;
            if (!tiku_resource_registry_below(changed->entry.path, watch->prefix) ||
                !tiku_resource_registry_visible(changed, &watch->caller)) continue;
            if (!filtered.path[0]) strcpy(filtered.path, changed->entry.path);
            if (grouped) {
                length = strlen(changed->entry.path) + 1;
                if (bytes + length > sizeof filtered.affected) { overflow(watch, node); break; }
                memcpy(filtered.affected + bytes, changed->entry.path, length);
                bytes += length; filtered.affected_count++;
            }
        }
        if (watch->paused || !filtered.path[0]) continue;
        watch->revision = node->provider->stamp.revision;
        for (j = 0; j < registry->limits.watches; j++)
            if (registry->watches[j].id && same_caller(watch, &registry->watches[j]))
                queued += registry->watches[j].count;
        if (queued >= 256 || queued >= RESOURCE_EVENT_BYTES_MAX / RESOURCE_EVENT_CHARGE) {
            for (j = 0; j < registry->limits.watches; j++)
                if (registry->watches[j].id && same_caller(watch, &registry->watches[j]))
                    overflow(&registry->watches[j], node);
            continue;
        }
        if (watch->count == registry->limits.events_per_watch) { overflow(watch, node); continue; }
        event = &watch->events[(watch->head + watch->count++) % registry->limits.events_per_watch];
        *event = filtered;
        event->stamp = node->provider->stamp;
        event->sequence = ++watch->sequence;
        event->removed = removed;
    }
}

void tiku_resource_registry_emit(tiku_resource_registry_t *registry, const resource_node_t *node, int removed)
{
    resource_node_t *one = (resource_node_t *)node;
    emit(registry, &one, 1, removed, 0);
}

void tiku_resource_registry_emit_group(tiku_resource_registry_t *registry, resource_node_t *const *nodes, size_t count)
{
    if (count) emit(registry, nodes, count, 0, 1);
}

tiku_resource_error_t tiku_resource_watch_open(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *key, const char *prefix,
    tiku_resource_watch_t *out, tiku_msg_t **snapshot)
{
    resource_provider_t *provider;
    resource_watch_t *watch = NULL;
    tiku_msg_t *message;
    unsigned i, owned = 0, entries = 0;
    char revision[21];
    if (out) *out = 0;
    if (snapshot) *snapshot = NULL;
    if (!tiku_resource_registry_thread(registry) || !key || !out || !snapshot ||
        !tiku_resource_path_valid(prefix)) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    provider = tiku_resource_registry_provider(registry, key);
    if (!provider) return TIKU_RESOURCE_NOT_FOUND;
    if (!tiku_resource_registry_below(prefix, provider->prefix)) return TIKU_RESOURCE_INVALID;
    for (i = 0; i < registry->limits.watches; i++) {
        resource_watch_t *item = &registry->watches[i];
        if (!item->id) { if (!watch) watch = item; }
        else if ((!caller && !item->caller.authenticated) ||
                 (caller && item->caller.authenticated == caller->authenticated &&
                  !strcmp(item->caller.principal, caller->principal))) owned++;
    }
    if (!watch || owned >= 32 || registry->next_watch == UINT64_MAX) return TIKU_RESOURCE_LIMIT;
    message = tiku_msg_new(0);
    if (!message) return TIKU_RESOURCE_NO_MEMORY;
    snprintf(revision, sizeof revision, "%llu", (unsigned long long)provider->stamp.revision);
    if (!tiku_msg_add_string(message, "provider", key) ||
        !tiku_msg_add_string(message, "incarnation", provider->stamp.incarnation) ||
        !tiku_msg_add_string(message, "revision", revision)) goto fail;
    for (i = 0; i < registry->count; i++) {
        resource_node_t *node = registry->nodes[i];
        tiku_msg_t *entry;
        int ok;
        if (node->provider != provider || !tiku_resource_registry_below(node->entry.path, prefix) ||
            !tiku_resource_registry_visible(node, caller)) continue;
        if (++entries > 128) { tiku_msg_free(message); return TIKU_RESOURCE_LIMIT; }
        entry = tiku_msg_new(0);
        if (!entry) goto fail;
        ok = tiku_msg_add_string(entry, "path", node->entry.path) &&
            tiku_msg_add_bool(entry, "available", !node->unavailable);
        if (node->value.desired) ok = ok && tiku_msg_add_message(entry, "desired", node->value.desired);
        if (node->value.persisted) ok = ok && tiku_msg_add_message(entry, "persisted", node->value.persisted);
        if (node->value.applied) ok = ok && tiku_msg_add_message(entry, "applied", node->value.applied);
        ok = ok && tiku_msg_add_string(entry, "apply_state", node->value.apply_state);
        ok = ok && tiku_msg_add_message(message, "entry", entry);
        tiku_msg_free(entry);
        if (!ok) goto fail;
        if (tiku_msg_flat_size(message) > TIKU_RESOURCE_FRAME_MAX) {
            tiku_msg_free(message); return TIKU_RESOURCE_LIMIT;
        }
    }
    if (!entries) { tiku_msg_free(message); return TIKU_RESOURCE_NOT_FOUND; }
    watch->events = calloc(registry->limits.events_per_watch, sizeof *watch->events);
    if (!watch->events) goto fail;
    if (caller) watch->caller = *caller;
    strcpy(watch->provider, key); strcpy(watch->incarnation, provider->stamp.incarnation);
    strcpy(watch->prefix, prefix);
    watch->revision = provider->stamp.revision;
    watch->id = ++registry->next_watch;
    *out = watch->id; *snapshot = message;
    return TIKU_RESOURCE_OK;
fail:
    tiku_msg_free(message);
    return TIKU_RESOURCE_NO_MEMORY;
}

int tiku_resource_watch_next(tiku_resource_registry_t *registry, tiku_resource_watch_t id,
                             tiku_resource_event_t *out)
{
    unsigned i;
    if (!tiku_resource_registry_thread(registry) || !id || !out) return -1;
    for (i = 0; i < registry->limits.watches; i++) {
        resource_watch_t *watch = &registry->watches[i];
        if (watch->id != id) continue;
        if (!watch->count) {
            if (watch->retired) { tiku_resource_watch_close(registry, id); return -1; }
            return 0;
        }
        *out = watch->events[watch->head];
        watch->head = (watch->head + 1) % registry->limits.events_per_watch;
        watch->count--;
        return 1;
    }
    return -1;
}

void tiku_resource_watch_close(tiku_resource_registry_t *registry, tiku_resource_watch_t id)
{
    unsigned i;
    if (!tiku_resource_registry_thread(registry)) return;
    for (i = 0; i < registry->limits.watches; i++) if (registry->watches[i].id == id && id) {
        free(registry->watches[i].events);
        memset(&registry->watches[i], 0, sizeof registry->watches[i]);
        return;
    }
}
