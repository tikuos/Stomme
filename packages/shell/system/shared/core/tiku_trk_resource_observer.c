/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_resource_observer.c - bounded immutable shell observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_observer.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_stream.h"
#include <stdlib.h>
#include <string.h>
#define OBSERVATION_MAX (4u * 1024u * 1024u)
/** @brief One retained complete stream and its length. */
typedef struct { char *bytes; size_t length; int ready; } observed_stream_t;
struct tiku_trk_resource_observer {
    tiku_resource_registry_t *registry;
    tiku_resource_stamp_t stamp;
    const tiku_trk_observation_t *entries;
    observed_stream_t *streams;
    size_t count;
    uint64_t next_poll;
    int (*count_items)(void *, const char *);
    tiku_msg_t *(*capture)(void *, const char *, int);
    void *context;
};

/** @brief Serialize complete typed object rows within the stream limit. */
static int observer_capture(tiku_trk_resource_observer_t *owner, size_t index, observed_stream_t *out)
{
    int i, count = owner->count_items(owner->context, owner->entries[index].path);
    size_t capacity = 0;
    char *row;
    if (count < 0) return -1;
    row = malloc(TIKU_RESOURCE_FRAME_MAX); if (!row) return -1;
    for (i = 0; i < count; i++) {
        tiku_msg_t *value = owner->capture(owner->context, owner->entries[index].path, i);
        const char *type = tiku_msg_find_string(value, "type", 0);
        int length = type && !strcmp(type, "object") ? tiku_resource_value_render(value, row, TIKU_RESOURCE_FRAME_MAX) : -1;
        tiku_msg_free(value);
        if (length < 0 || (size_t)length > OBSERVATION_MAX - out->length) { free(row); return -1; }
        if (out->length + (size_t)length > capacity) {
            size_t next = capacity ? capacity * 2u : 4096u;
            char *bytes;
            if (next < out->length + (size_t)length) next = out->length + (size_t)length;
            if (next > OBSERVATION_MAX) next = OBSERVATION_MAX;
            bytes = realloc(out->bytes, next); if (!bytes) { free(row); return -1; }
            out->bytes = bytes; capacity = next;
        }
        memcpy(out->bytes + out->length, row, (size_t)length); out->length += (size_t)length;
    }
    free(row); out->ready = 1; return 0;
}

/** @brief Publish last successful bytes with explicit unavailable feedback. */
static void observer_unavailable(tiku_trk_resource_observer_t *owner, const char *path)
{
    tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
    tiku_resource_snapshot_t previous = {0};
    strcpy(caller.principal, "00000000000000000000000000000001");
    if (!tiku_resource_read(owner->registry, &caller, path, &previous)) {
        strcpy(previous.apply_state, "last_good_unavailable"); trk_value_publish(owner->registry, path, &previous);
    }
    tiku_resource_snapshot_clear(&previous);
}

/** @brief Capture and publish one complete observed value without partial successful output. */
static void observer_publish(tiku_trk_resource_observer_t *owner, size_t index)
{
    const char *path = owner->entries[index].path;
    if (owner->entries[index].stream) {
        observed_stream_t next = {0}, *previous = &owner->streams[index];
        if (observer_capture(owner, index, &next)) { free(next.bytes); observer_unavailable(owner, path); return; }
        if (!previous->ready || previous->length != next.length || (next.length && memcmp(previous->bytes, next.bytes, next.length))) {
            tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63};
            tiku_resource_snapshot_t current = {0};
            strcpy(caller.principal, "00000000000000000000000000000001");
            if (!tiku_resource_read(owner->registry, &caller, path, &current) &&
                !tiku_resource_stream_publish(owner->registry, &current.stamp, path, next.bytes ? next.bytes : "", next.length, &owner->stamp)) {
                free(previous->bytes); *previous = next; next.bytes = NULL;
            } else observer_unavailable(owner, path);
            tiku_resource_snapshot_clear(&current);
        } else {
            tiku_resource_caller_t caller = {.authenticated = 1, .scopes = 63}; tiku_resource_snapshot_t current = {0};
            strcpy(caller.principal, "00000000000000000000000000000001");
            if (!tiku_resource_read(owner->registry, &caller, path, &current)) {
                strcpy(current.apply_state, "applied"); trk_value_publish(owner->registry, path, &current);
            }
            tiku_resource_snapshot_clear(&current);
        }
        free(next.bytes);
    } else {
        tiku_resource_snapshot_t next = {0}; tiku_msg_t *value = owner->capture(owner->context, path, -1);
        if (trk_value_snapshot(&next, value, "observed") || trk_value_publish(owner->registry, path, &next)) observer_unavailable(owner, path);
        tiku_resource_snapshot_clear(&next);
    }
}

tiku_trk_resource_observer_t *tiku_trk_resource_observer_open(tiku_resource_registry_t *registry,
    const char *key, const char *prefix, const tiku_trk_observation_t *entries, size_t count,
    int (*count_items)(void *, const char *), tiku_msg_t *(*capture)(void *, const char *, int), void *context)
{
    tiku_trk_resource_observer_t *owner;
    tiku_resource_initial_t *initial; size_t i; int error;
    if (!registry || !entries || !count || !count_items || !capture) return NULL;
    owner = calloc(1, sizeof *owner); if (!owner) return NULL;
    owner->registry = registry; owner->entries = entries; owner->count = count;
    owner->context = context; owner->capture = capture; owner->count_items = count_items;
    owner->streams = calloc(count, sizeof *owner->streams); initial = calloc(count, sizeof *initial);
    if (!owner->streams || !initial) { free(initial); free(owner->streams); free(owner); return NULL; }
    error = 0;
    for (i = 0; i < count; i++) {
        tiku_msg_t *value = entries[i].stream ? tiku_resource_stream_metadata(0, "") : capture(context, entries[i].path, -1);
        initial[i].spec = (tiku_resource_spec_t){.path = entries[i].path, .kind = entries[i].stream ? TIKU_RESOURCE_STREAM : TIKU_RESOURCE_STATE,
            .schema_version = 1, .schema = {.type = TIKU_RESOURCE_OBJECT}, .read_scopes = TIKU_RESOURCE_READ,
            .sensitive = entries[i].sensitive, .public_read = !entries[i].sensitive, .persistence = "none", .controller = key};
        if (trk_value_snapshot(&initial[i].initial, value, entries[i].stream ? "loading" : "observed")) { error = -1; break; }
    }
    if (!error) error = tiku_resource_register(registry, key, prefix, NULL, initial, count, NULL, NULL, &owner->stamp);
    for (i = 0; i < count; i++) tiku_resource_snapshot_clear(&initial[i].initial);
    free(initial);
    if (error) { free(owner->streams); free(owner); return NULL; }
    tiku_trk_resource_observer_poll(owner, 1); return owner;
}

void tiku_trk_resource_observer_poll(tiku_trk_resource_observer_t *owner, uint64_t now)
{
    size_t i;
    if (!owner || now < owner->next_poll) return;
    owner->next_poll = now + 250;
    for (i = 0; i < owner->count; i++) observer_publish(owner, i);
}

void tiku_trk_resource_observer_close(tiku_trk_resource_observer_t *owner)
{
    size_t i;
    if (!owner) return;
    tiku_resource_unregister(owner->registry, &owner->stamp);
    for (i = 0; i < owner->count; i++) free(owner->streams[i].bytes);
    free(owner->streams); free(owner);
}
