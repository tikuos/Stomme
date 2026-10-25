/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_registry.c - owner-thread resource lifetimes and snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_registry_private.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int tiku_resource_registry_thread(const tiku_resource_registry_t *registry)
{
    return registry && pthread_equal(registry->thread, pthread_self());
}

int tiku_resource_registry_below(const char *path, const char *prefix)
{
    size_t length = strlen(prefix);
    return !strcmp(prefix, "/") || (!strncmp(path, prefix, length) &&
        (!path[length] || path[length] == '/'));
}

resource_node_t *tiku_resource_registry_node(tiku_resource_registry_t *registry, const char *path)
{
    unsigned i;
    for (i = 0; i < registry->count; i++)
        if (!strcmp(registry->nodes[i]->entry.path, path)) return registry->nodes[i];
    return NULL;
}

resource_provider_t *tiku_resource_registry_provider(tiku_resource_registry_t *registry, const char *key)
{
    unsigned i;
    for (i = 0; i < registry->limits.providers; i++)
        if (registry->providers[i].live && !strcmp(registry->providers[i].key, key))
            return &registry->providers[i];
    return NULL;
}

int tiku_resource_registry_copy(tiku_resource_snapshot_t *out, const tiku_resource_snapshot_t *source)
{
    memset(out, 0, sizeof *out);
    out->stamp = source->stamp;
    memcpy(out->apply_state, source->apply_state, sizeof out->apply_state);
    if (source->desired && !(out->desired = tiku_msg_copy(source->desired))) goto fail;
    if (source->persisted && !(out->persisted = tiku_msg_copy(source->persisted))) goto fail;
    if (source->applied && !(out->applied = tiku_msg_copy(source->applied))) goto fail;
    return 1;
fail:
    tiku_resource_snapshot_clear(out);
    return 0;
}

int tiku_resource_registry_visible(const resource_node_t *node, const tiku_resource_caller_t *caller)
{
    uint64_t granted = caller ? caller->scopes : 0;
    if (!node->provider) return 1;
    if (!caller || !caller->authenticated) return node->spec.public_read && !node->spec.sensitive;
    if (!tiku_resource_id_valid(caller->principal)) return 0;
    if ((granted & node->spec.read_scopes) != node->spec.read_scopes) return 0;
    if (node->spec.sensitive) {
        if (!(granted & TIKU_RESOURCE_CONTENT)) return 0;
        if (node->provider->principal[0] && strcmp(caller->principal, node->provider->principal) &&
            !(granted & TIKU_RESOURCE_CROSS_APP)) return 0;
    }
    return 1;
}

/** @brief Reserve depth for snapshot, job and structured-error envelopes. */
static tiku_resource_error_t snapshot_depth(const tiku_msg_t *value)
{
    size_t size = tiku_msg_flat_size(value);
    void *bytes;
    tiku_resource_error_t error;
    if (!size || size > TIKU_RESOURCE_FRAME_MAX) return TIKU_RESOURCE_LIMIT;
    bytes = malloc(size);
    if (!bytes) return TIKU_RESOURCE_NO_MEMORY;
    error = tiku_msg_flatten(value, bytes, size, NULL) ?
        tiku_resource_message_check_nested(bytes, size, 3) : TIKU_RESOURCE_INVALID;
    free(bytes);
    return error;
}

tiku_resource_error_t tiku_resource_registry_validate(const tiku_resource_spec_t *spec, const tiku_msg_t *value)
{
    tiku_resource_error_t error = tiku_resource_value_check(value);
    const char *type;
    int64_t integer;
    double number;
    size_t i;
    if (error) return error;
    if (spec->kind != TIKU_RESOURCE_ACTION && (error = snapshot_depth(value))) return error;
    type = tiku_msg_find_string(value, "type", 0);
    if (strcmp(type, tiku_resource_type_name(spec->schema.type))) return TIKU_RESOURCE_VALUE;
    switch (spec->schema.type) {
    case TIKU_RESOURCE_INTEGER:
    case TIKU_RESOURCE_COLOR:
        tiku_msg_find_int64(value, "value", 0, &integer);
        if (integer < spec->schema.integer_min || integer > spec->schema.integer_max)
            return TIKU_RESOURCE_VALUE;
        break;
    case TIKU_RESOURCE_NUMBER:
        tiku_msg_find_double(value, "value", 0, &number);
        if (number < spec->schema.number_min || number > spec->schema.number_max)
            return TIKU_RESOURCE_VALUE;
        break;
    case TIKU_RESOURCE_TEXT:
        if (strlen(tiku_msg_find_string(value, "value", 0)) > spec->schema.text_max)
            return TIKU_RESOURCE_LIMIT;
        break;
    case TIKU_RESOURCE_ENUM:
        for (i = 0; i < spec->schema.choice_count; i++)
            if (!strcmp(tiku_msg_find_string(value, "value", 0), spec->schema.choices[i])) break;
        if (i == spec->schema.choice_count) return TIKU_RESOURCE_VALUE;
        break;
    default: break;
    }
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_registry_snapshot_check(const resource_node_t *node, const tiku_resource_snapshot_t *value)
{
    const tiku_msg_t *parts[] = {value->desired, value->persisted, value->applied};
    size_t i, bytes = 0;
    if (!memchr(value->apply_state, 0, sizeof value->apply_state) || !value->apply_state[0])
        return TIKU_RESOURCE_VALUE;
    if ((node->spec.kind == TIKU_RESOURCE_DIRECTORY || node->spec.kind == TIKU_RESOURCE_COLLECTION)) return TIKU_RESOURCE_OK;
    for (i = 0; i < 3; i++) {
        tiku_resource_error_t error = node->spec.kind == TIKU_RESOURCE_ACTION
            ? tiku_resource_value_check(parts[i]) : tiku_resource_registry_validate(&node->spec, parts[i]);
        if (error) return error;
        if ((error = snapshot_depth(parts[i]))) return error;
        bytes += tiku_msg_flat_size(parts[i]);
    }
    return bytes <= TIKU_RESOURCE_FRAME_MAX - 2048 ? TIKU_RESOURCE_OK : TIKU_RESOURCE_LIMIT;
}

/** @brief Release a node's descriptor copies and current snapshot. */
void tiku_resource_registry_node_free(resource_node_t *node)
{
    size_t i;
    if (!node) return;
    tiku_resource_stream_drop(node->blob);
    for (i = 0; i < node->spec.schema.choice_count; i++) free(node->choices[i]);
    free(node->choices);
    free((char *)node->spec.persistence);
    free((char *)node->spec.controller);
    tiku_resource_snapshot_clear(&node->value);
    free(node);
}

/** @brief Allocate a node with a new nonreused backend identity. */
static resource_node_t *node_new(tiku_resource_registry_t *registry, const char *path)
{
    resource_node_t *node;
    if (registry->count == registry->limits.resources || registry->next_node == UINT64_MAX)
        return NULL;
    node = calloc(1, sizeof *node);
    if (!node) return NULL;
    if (tiku_resource_new_id(node->entry.id)) { free(node); return NULL; }
    strcpy(node->entry.path, path);
    node->entry.node_id = ++registry->next_node;
    node->entry.kind = TIKU_RESOURCE_DIRECTORY;
    registry->nodes[registry->count++] = node;
    return node;
}

tiku_resource_registry_t *tiku_resource_registry_new(const tiku_resource_limits_t *requested)
{
    tiku_resource_registry_t *registry = calloc(1, sizeof *registry);
    tiku_resource_limits_t limits = {16, 256, 32, 16, 16};
    if (!registry) return NULL;
    if (requested) {
        if (requested->providers) limits.providers = requested->providers;
        if (requested->resources) limits.resources = requested->resources;
        if (requested->watches) limits.watches = requested->watches;
        if (requested->events_per_watch) limits.events_per_watch = requested->events_per_watch;
        if (requested->receipts) limits.receipts = requested->receipts;
    }
    if (limits.providers > 512 || limits.resources > 16384 || limits.watches > 256 ||
        limits.events_per_watch > 256 || limits.receipts > 16) { free(registry); return NULL; }
    registry->thread = pthread_self();
    registry->limits = limits;
    registry->providers = calloc(limits.providers, sizeof *registry->providers);
    registry->nodes = calloc(limits.resources, sizeof *registry->nodes);
    registry->watches = calloc(limits.watches, sizeof *registry->watches);
    registry->receipts = calloc(limits.receipts, sizeof *registry->receipts);
    registry->epoch = 1;
    if (!registry->providers || !registry->nodes || !registry->watches || !registry->receipts ||
        tiku_resource_new_id(registry->session) || !node_new(registry, "/")) {
        tiku_resource_registry_free(registry); return NULL;
    }
    return registry;
}

void tiku_resource_registry_free(tiku_resource_registry_t *registry)
{
    unsigned i;
    if (!registry || !tiku_resource_registry_thread(registry) || registry->dispatching || registry->services || registry->stream_leases) return;
    for (i = 0; i < registry->count; i++) tiku_resource_registry_node_free(registry->nodes[i]);
    if (registry->watches) for (i = 0; i < registry->limits.watches; i++) free(registry->watches[i].events);
    if (registry->receipts) for (i = 0; i < registry->limits.receipts; i++) {
        tiku_resource_registry_receipt_clear(&registry->receipts[i]);
    }
    tiku_resource_jobs_drop(registry);
    free(registry->providers); free(registry->nodes);
    free(registry->watches); free(registry->receipts);
    free(registry);
}

const char *tiku_resource_registry_session(const tiku_resource_registry_t *registry)
{
    return tiku_resource_registry_thread(registry) ? registry->session : NULL;
}

/** @brief Copy a public descriptor and validate its schema bounds. */
static tiku_resource_error_t spec_copy(resource_node_t *node, const tiku_resource_spec_t *spec)
{
    size_t i, bytes = 0;
    const char *type = tiku_resource_type_name(spec->schema.type);
    if (!type || (unsigned)spec->kind > TIKU_RESOURCE_STREAM || !spec->schema_version || spec->schema_version > INT32_MAX ||
        spec->schema.text_max > TIKU_RESOURCE_TEXT_MAX ||
        spec->schema.integer_min > spec->schema.integer_max ||
        !isfinite(spec->schema.number_min) || !isfinite(spec->schema.number_max) ||
        spec->schema.number_min > spec->schema.number_max ||
        spec->schema.choice_count > TIKU_RESOURCE_MEMBERS_MAX ||
        (spec->schema.choice_count && !spec->schema.choices) ||
        (spec->sensitive && spec->public_read)) return TIKU_RESOURCE_INVALID;
    if (!spec->persistence || !spec->controller ||
        !tiku_resource_utf8_valid(spec->persistence, 127) ||
        !tiku_resource_utf8_valid(spec->controller, 127)) return TIKU_RESOURCE_INVALID;
    node->spec = *spec;
    node->spec.path = node->entry.path;
    node->spec.persistence = strdup(spec->persistence);
    node->spec.controller = strdup(spec->controller);
    node->spec.schema.choice_count = 0;
    node->choices = calloc(spec->schema.choice_count ? spec->schema.choice_count : 1, sizeof *node->choices);
    if (!node->spec.persistence || !node->spec.controller || !node->choices) return TIKU_RESOURCE_NO_MEMORY;
    node->spec.schema.choices = (const char *const *)node->choices;
    for (i = 0; i < spec->schema.choice_count; i++) {
        size_t j;
        if (!spec->schema.choices[i] || !spec->schema.choices[i][0] ||
            !tiku_resource_utf8_valid(spec->schema.choices[i], 127)) return TIKU_RESOURCE_INVALID;
        bytes += strlen(spec->schema.choices[i]);
        if (bytes > TIKU_RESOURCE_TEXT_MAX) return TIKU_RESOURCE_LIMIT;
        for (j = 0; j < i; j++) if (!strcmp(spec->schema.choices[j], spec->schema.choices[i]))
            return TIKU_RESOURCE_INVALID;
        if (!(node->choices[i] = strdup(spec->schema.choices[i]))) return TIKU_RESOURCE_NO_MEMORY;
        node->spec.schema.choice_count++;
    }
    if (spec->schema.type == TIKU_RESOURCE_ENUM && !spec->schema.choice_count)
        return TIKU_RESOURCE_INVALID;
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_register(tiku_resource_registry_t *registry,
    const char *key, const char *prefix, const char *principal,
    const tiku_resource_initial_t *values, size_t count,
    const tiku_resource_controller_t *controller, void *owner, tiku_resource_stamp_t *out)
{
    resource_provider_t *provider = NULL;
    unsigned i, before;
    tiku_resource_error_t error = TIKU_RESOURCE_INVALID;
    char key_path[130];
    if (!tiku_resource_registry_thread(registry) || !key || !out || !values || !count ||
        !tiku_resource_path_valid(prefix) ||
        !strcmp(prefix, "/") || (principal && !tiku_resource_id_valid(principal))) return error;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    if (registry->epoch == UINT64_MAX) return TIKU_RESOURCE_UNAVAILABLE;
    if (count > registry->limits.resources) return TIKU_RESOURCE_LIMIT;
    if (strnlen(key, TIKU_RESOURCE_KEY_MAX) >= TIKU_RESOURCE_KEY_MAX) return error;
    snprintf(key_path, sizeof key_path, "/%s", key);
    if (!key[0] || strchr(key, '/') || !tiku_resource_path_valid(key_path)) return error;
    for (i = 0; i < registry->limits.providers; i++) {
        resource_provider_t *p = &registry->providers[i];
        if (!p->live) { if (!provider) provider = p; continue; }
        if (!strcmp(p->key, key) || tiku_resource_registry_below(prefix, p->prefix) ||
            tiku_resource_registry_below(p->prefix, prefix)) return error;
    }
    if (!provider) return TIKU_RESOURCE_LIMIT;
    memset(provider, 0, sizeof *provider);
    strcpy(provider->key, key); strcpy(provider->prefix, prefix);
    strcpy(provider->stamp.provider, key);
    if (tiku_resource_new_id(provider->stamp.incarnation)) return TIKU_RESOURCE_UNAVAILABLE;
    provider->stamp.revision = 1;
    if (principal) strcpy(provider->principal, principal);
    if (controller) provider->controller = *controller;
    provider->owner = owner;
    before = registry->count;
    for (i = 0; i < count; i++) {
        const tiku_resource_spec_t *spec = &values[i].spec;
        error = TIKU_RESOURCE_INVALID;
        resource_node_t *node;
        if (!tiku_resource_path_valid(spec->path) || !tiku_resource_registry_below(spec->path, prefix) ||
            tiku_resource_registry_node(registry, spec->path)) goto fail;
        if (registry->count == registry->limits.resources) { error = TIKU_RESOURCE_LIMIT; goto fail; }
        node = node_new(registry, spec->path);
        if (!node) { error = TIKU_RESOURCE_NO_MEMORY; goto fail; }
        node->provider = provider;
        node->entry.kind = spec->kind;
        error = spec_copy(node, spec);
        if (error) goto fail;
        if ((spec->kind == TIKU_RESOURCE_SETTING || spec->kind == TIKU_RESOURCE_ACTION) &&
            (!controller || !controller->apply ||
             ((spec->schema.type == TIKU_RESOURCE_OBJECT || spec->schema.type == TIKU_RESOURCE_ARRAY) && !controller->validate))) {
            error = TIKU_RESOURCE_INVALID; goto fail;
        }
        error = tiku_resource_registry_snapshot_check(node, &values[i].initial);
        if (error) goto fail;
        if (!tiku_resource_registry_copy(&node->value, &values[i].initial)) {
            error = TIKU_RESOURCE_NO_MEMORY; goto fail;
        }
        node->value.stamp = provider->stamp;
    }
    for (i = before; i < registry->count; i++) {
        char parent[TIKU_RESOURCE_PATH_MAX], *slash;
        strcpy(parent, registry->nodes[i]->entry.path);
        while ((slash = strrchr(parent, '/')) && slash != parent) {
            resource_node_t *existing;
            *slash = 0;
            existing = tiku_resource_registry_node(registry, parent);
            if (existing && existing->entry.kind != TIKU_RESOURCE_DIRECTORY && existing->entry.kind != TIKU_RESOURCE_COLLECTION) { error = TIKU_RESOURCE_INVALID; goto fail; }
            if (!existing) {
                if (registry->count == registry->limits.resources) { error = TIKU_RESOURCE_LIMIT; goto fail; }
                if (!node_new(registry, parent)) { error = TIKU_RESOURCE_NO_MEMORY; goto fail; }
            }
        }
    }
    provider->live = 1;
    registry->epoch++;
    *out = provider->stamp;
    return TIKU_RESOURCE_OK;
fail:
    while (registry->count > before) tiku_resource_registry_node_free(registry->nodes[--registry->count]);
    memset(provider, 0, sizeof *provider);
    return error;
}

tiku_resource_error_t tiku_resource_unregister(tiku_resource_registry_t *registry,
                                               const tiku_resource_stamp_t *stamp)
{
    resource_provider_t *provider;
    unsigned i = 0;
    if (!tiku_resource_registry_thread(registry) || !stamp) return TIKU_RESOURCE_INVALID;
    if (registry->dispatching) return TIKU_RESOURCE_BUSY;
    if (!memchr(stamp->provider, 0, sizeof stamp->provider) || !tiku_resource_id_valid(stamp->incarnation))
        return TIKU_RESOURCE_INVALID;
    if (registry->epoch == UINT64_MAX) return TIKU_RESOURCE_UNAVAILABLE;
    provider = tiku_resource_registry_provider(registry, stamp->provider);
    if (!provider || strcmp(provider->stamp.incarnation, stamp->incarnation)) return TIKU_RESOURCE_OWNER_LOST;
    tiku_resource_jobs_owner_lost(registry, provider);
    while (i < registry->count) {
        resource_node_t *node = registry->nodes[i];
        if (node->provider == provider) {
            tiku_resource_registry_emit(registry, node, 1);
            tiku_resource_registry_node_free(node);
            memmove(registry->nodes + i, registry->nodes + i + 1,
                (--registry->count - i) * sizeof *registry->nodes);
        } else i++;
    }
    for (i = registry->count; i > 0;) {
        resource_node_t *node = registry->nodes[--i];
        unsigned j;
        if (node->provider || !strcmp(node->entry.path, "/")) continue;
        for (j = 0; j < registry->count; j++)
            if (j != i && registry->nodes[j]->provider &&
                tiku_resource_registry_below(registry->nodes[j]->entry.path, node->entry.path)) break;
        if (j == registry->count) {
            tiku_resource_registry_node_free(node);
            memmove(registry->nodes + i, registry->nodes + i + 1,
                (--registry->count - i) * sizeof *registry->nodes);
        }
    }
    for (i = 0; i < registry->limits.watches; i++)
        if (registry->watches[i].id && !strcmp(registry->watches[i].provider, provider->key) &&
            !strcmp(registry->watches[i].incarnation, provider->stamp.incarnation))
            registry->watches[i].retired = 1;
    provider->live = 0;
    registry->epoch++;
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_read(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller, const char *path, tiku_resource_snapshot_t *out)
{
    resource_node_t *node;
    if (out) memset(out, 0, sizeof *out);
    if (!tiku_resource_registry_thread(registry) || !out || !tiku_resource_path_valid(path)) return TIKU_RESOURCE_INVALID;
    node = tiku_resource_registry_node(registry, path);
    if (!node || !node->provider || !tiku_resource_registry_visible(node, caller)) return TIKU_RESOURCE_NOT_FOUND;
    if (node->unavailable) return TIKU_RESOURCE_UNAVAILABLE;
    if (!tiku_resource_registry_copy(out, &node->value)) return TIKU_RESOURCE_NO_MEMORY;
    out->stamp = node->provider->stamp;
    return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_registry_clock(tiku_resource_registry_t *registry,
    tiku_resource_clock_fn clock, void *context)
{
    if (!tiku_resource_registry_thread(registry) || !clock || registry->count != 1 || registry->epoch != 1 || registry->services ||
        registry->jobs || registry->next_watch || registry->dispatching) return TIKU_RESOURCE_INVALID;
    registry->clock = clock; registry->clock_context = context;
    return TIKU_RESOURCE_OK;
}

int tiku_resource_registry_observe(const tiku_resource_registry_t *registry,tiku_resource_registry_observation_t *out)
{
    if(!out)return 0;
    memset(out,0,sizeof *out);if(!tiku_resource_registry_thread(registry))return 0;
    out->provider_capacity=registry->limits.providers;out->node_capacity=registry->limits.resources;
    out->nodes=registry->count;out->services=registry->services;out->retained_stream_bytes=registry->stream_bytes;
    for(unsigned i=0;i<registry->limits.providers;i++)out->providers+=registry->providers[i].live!=0;
    for(unsigned i=0;i<registry->limits.watches;i++)if(registry->watches[i].id&&!registry->watches[i].retired) {
        out->watches++;out->paused_watches+=registry->watches[i].paused!=0;
    }
    if(registry->jobs)for(unsigned i=0;i<RESOURCE_JOBS_MAX;i++)if(registry->jobs[i].info.id[0]) {
        out->retained_jobs++;
        if(registry->jobs[i].info.state==TIKU_RESOURCE_JOB_QUEUED||registry->jobs[i].info.state==TIKU_RESOURCE_JOB_RUNNING)out->active_jobs++;
    }
    return 1;
}

int tiku_resource_events_observe(const tiku_resource_registry_t *registry,
                                 const char *exclude_provider,
                                 tiku_resource_event_observation_t *out)
{
    if (!out)
        return 0;
    memset(out, 0, sizeof *out);
    if (!tiku_resource_registry_thread(registry))
        return 0;
    for (unsigned i = 0; i < registry->limits.watches; i++) {
        const resource_watch_t *watch = &registry->watches[i];
        if (!watch->id || watch->retired ||
            (exclude_provider && !strcmp(exclude_provider, watch->provider)))
            continue;
        out->subscriptions++;
        out->paused += watch->paused != 0;
        out->queued += watch->count;
        if (watch->sequence > out->maximum_sequence)
            out->maximum_sequence = watch->sequence;
    }
    return 1;
}
