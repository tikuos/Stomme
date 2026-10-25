/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_resources.c - application-owned schemas, snapshots and native dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app_resources_private.h"
#include <stdarg.h>
#include "tiku_crypto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/** @brief Release a copied catalog after its native provider has retired. */
static void catalog_free(app_resource_entry_t *entries, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        tiku_resource_initial_clear(&entries[i].initial);
        free(entries[i].stream);
    }
    free(entries);
}
/** @brief Resolve a canonical host path to its application-relative suffix. */
static const char *relative(tiku_app_resources_t *owner, const char *path)
{
    const char *prefix = tiku_resource_provider_client_prefix(owner->client);
    size_t n = prefix ? strlen(prefix) : 0;
    return n && !strncmp(path, prefix, n) && path[n] == '/' ? path + n + 1
                                                            : NULL;
}
/** @brief Copy a callback-owned schema and reject namespace or count violations. */
static int catalog_build(tiku_app_resources_t *owner,
                         app_resource_entry_t **out, size_t *length)
{
    app_resource_entry_t *entries =
        calloc(TIKU_RESOURCE_PROVIDER_CATALOG_MAX, sizeof *entries);
    size_t count = 0;
    if (!entries)
        return -1;
    for (int source = 0; source < 2; source++)
        for (size_t index = 0;; index++) {
            tiku_resource_initial_t input = {0};
            int found =
                source ? (owner->extension
                              ? owner->extension->catalog(owner->model, owner,
                                                          index, &input)
                              : 0)
                       : tiku_app_resource_base_catalog(owner, index, &input);
            if (found < 0 ||
                (found && count == TIKU_RESOURCE_PROVIDER_CATALOG_MAX)) {
                tiku_resource_snapshot_clear(&input.initial);
                goto fail;
            }
            if (!found) {
                tiku_resource_snapshot_clear(&input.initial);
                break;
            }
            char path[TIKU_RESOURCE_PATH_MAX];
            const char *prefix =
                tiku_resource_provider_client_prefix(owner->client);
            int n = input.spec.path ? snprintf(path, sizeof path, "%s/%s",
                                               prefix, input.spec.path)
                                    : -1;
            if (n < 0 || (size_t)n >= sizeof path ||
                !tiku_resource_path_valid(path)) {
                tiku_resource_snapshot_clear(&input.initial);
                goto fail;
            }
            input.spec.path = path;
            tiku_msg_t *wire = tiku_resource_initial_message(&input);
            int error = !wire || tiku_resource_initial_decode(
                                     wire, &entries[count].initial);
            tiku_msg_free(wire);
            tiku_resource_snapshot_clear(&input.initial);
            if (error)
                goto fail;
            count++;
        }
    *out = entries;
    *length = count;
    return 0;
fail:
    catalog_free(entries, TIKU_RESOURCE_PROVIDER_CATALOG_MAX);
    return -1;
}
/** @brief Compare namespace and schema identity independently of captured values. */
static int same_catalog(const app_resource_entry_t *a, size_t an,
                        const app_resource_entry_t *b, size_t bn)
{
    if (an != bn)
        return 0;
    for (size_t i = 0; i < an; i++) {
        tiku_msg_t *left = tiku_resource_initial_message(&a[i].initial),
                   *right = tiku_resource_initial_message(&b[i].initial);
        const char *fields[] = {"desired", "persisted", "applied",
                                "apply_state"};
        for (size_t j = 0; j < 4; j++) {
            tiku_msg_remove(left, fields[j]);
            tiku_msg_remove(right, fields[j]);
        }
        size_t n = tiku_msg_flat_size(left), m = tiku_msg_flat_size(right);
        void *one = malloc(n ? n : 1), *two = malloc(m ? m : 1);
        int same = left && right && one && two && n == m &&
                   tiku_msg_flatten(left, one, n, NULL) &&
                   tiku_msg_flatten(right, two, m, NULL) &&
                   !memcmp(one, two, n);
        free(one);
        free(two);
        tiku_msg_free(left);
        tiku_msg_free(right);
        if (!same)
            return 0;
    }
    return 1;
}
/** @brief Queue an already checked management frame through the native application channel. */
static int send_request(tiku_app_resources_t *owner, tiku_msg_t *message,
                        int kind)
{
    if (!message)
        return 0;
    int sent = owner->services->send_message(owner->services->ctx, message);
    tiku_msg_free(message);
    if (!sent) {
        owner->failed = 1;
        owner->phase = -1;
        return 0;
    }
    owner->pending_kind = kind;
    return 1;
}
/** @brief Capture through the module first, retaining native runner fallback paths. */
static tiku_resource_error_t capture(tiku_app_resources_t *owner,
                                     const char *path,
                                     tiku_resource_snapshot_t *out)
{
    tiku_resource_error_t error =
        owner->extension
            ? owner->extension->capture(owner->model, owner, path, out)
            : TIKU_RESOURCE_UNSUPPORTED;
    return error == TIKU_RESOURCE_UNSUPPORTED
               ? tiku_app_resource_base_capture(owner, path, out)
               : error;
}
/** @brief Capture a frozen module or runner stream without publishing partial bytes. */
static tiku_resource_error_t stream(tiku_app_resources_t *owner,
                                    const char *path, void **bytes,
                                    size_t *size)
{
    tiku_resource_error_t error =
        owner->extension && owner->extension->stream
            ? owner->extension->stream(owner->model, owner, path, bytes, size)
            : TIKU_RESOURCE_UNSUPPORTED;
    return error == TIKU_RESOURCE_UNSUPPORTED
               ? tiku_app_resource_base_stream(owner, path, bytes, size)
               : error;
}
/** @brief Execute one delivered owner command and queue its exact terminal result. */
static void perform(tiku_app_resources_t *owner, app_resource_call_t *slot)
{
    tiku_resource_provider_call_t *call = &slot->call;
    tiku_resource_snapshot_t out = {0};
    tiku_resource_effect_t effect = TIKU_RESOURCE_EFFECT_NONE;
    const char *path = relative(owner, call->path);
    tiku_resource_error_t error = TIKU_RESOURCE_OWNER_LOST;
    if (call->cancel)
        error = TIKU_RESOURCE_CANCELLED;
    else if (owner->now >= slot->deadline)
        error = TIKU_RESOURCE_DEADLINE;
    else if (path) {
        error = owner->extension
                    ? owner->extension->change(owner->model, owner, path,
                                               call->value, call->invoke, &out,
                                               &effect)
                    : TIKU_RESOURCE_UNSUPPORTED;
        if (error == TIKU_RESOURCE_UNSUPPORTED)
            error = tiku_app_resource_base_change(owner, path, call->value,
                                                  call->invoke, &out, &effect);
    }
    if (!error && !out.desired) {
        error = TIKU_RESOURCE_UNKNOWN_OUTCOME;
        effect = TIKU_RESOURCE_EFFECT_UNKNOWN;
    }
    tiku_msg_t *reply = tiku_resource_provider_client_complete(
        owner->client, call->dispatch, out.desired ? &out : NULL, error,
        effect);
    if (!send_request(owner, reply, 5)) {
        owner->failed = 1;
        owner->phase = -1;
    }
    tiku_resource_snapshot_clear(&out);
    tiku_resource_provider_call_clear(call);
    slot->deadline = 0;
}
/** @brief Upload one bounded chunk while retaining the frozen source bytes. */
static void upload_next(tiku_app_resources_t *owner)
{
    app_resource_entry_t *entry = &owner->entries[owner->pending_index];
    size_t count = owner->upload_bytes - owner->upload_at;
    if (count > TIKU_RESOURCE_STREAM_CHUNK)
        count = TIKU_RESOURCE_STREAM_CHUNK;
    owner->pending_count = count;
    if (!send_request(
            owner,
            tiku_resource_provider_client_stream(
                owner->client, entry->initial.spec.path, owner->upload_id,
                owner->upload_at, owner->upload_bytes,
                owner->upload
                    ? ((unsigned char *)owner->upload + owner->upload_at)
                    : NULL,
                count),
            4)) {
        owner->failed = 1;
        owner->phase = -1;
    }
}
/** @brief Publish capture failure once while preserving the last complete value. */
static int unavailable(tiku_app_resources_t *owner,
                       app_resource_entry_t *entry)
{
    if (entry->unavailable)
        return 0;
    if (entry->initial.spec.kind == TIKU_RESOURCE_SETTING) {
        entry->unavailable = 1;
        return 0;
    }
    if (send_request(owner,
                     tiku_resource_provider_client_availability(
                         owner->client, entry->initial.spec.path, 0),
                     7)) {
        entry->unavailable = 1;
        return 1;
    }
    return 0;
}
/** @brief Find a changed native capture and send one publication per acknowledged turn. */
static void publish_next(tiku_app_resources_t *owner)
{
    size_t checked = 0;
    while (checked++ < owner->count) {
        size_t index = owner->cursor++ % owner->count;
        app_resource_entry_t *entry = &owner->entries[index];
        const char *path = relative(owner, entry->initial.spec.path);
        if (!path)
            continue;
        if (entry->initial.spec.kind == TIKU_RESOURCE_ACTION)
            continue;
        owner->pending_index = (int)index;
        if (entry->initial.spec.kind == TIKU_RESOURCE_STREAM) {
            void *data = NULL;
            size_t bytes = 0;
            tiku_resource_error_t error = stream(owner, path, &data, &bytes);
            if (error || bytes > TIKU_RESOURCE_STREAM_BYTES ||
                (!data && bytes)) {
                free(data);
                if (unavailable(owner, entry))
                    return;
                continue;
            }
            if (entry->stream_ready && bytes == entry->bytes &&
                (!bytes || !memcmp(data, entry->stream, bytes))) {
                free(data);
                if (entry->unavailable) {
                    entry->unavailable = 0;
                    send_request(
                        owner,
                        tiku_resource_provider_client_availability(
                            owner->client, entry->initial.spec.path, 1),
                        7);
                    return;
                }
                continue;
            }
            if (tiku_resource_new_id(owner->upload_id)) {
                free(data);
                continue;
            }
            owner->upload = data;
            owner->upload_bytes = bytes;
            owner->upload_at = 0;
            upload_next(owner);
            return;
        }
        tiku_resource_snapshot_t value = {0};
        if (capture(owner, path, &value)) {
            tiku_resource_snapshot_clear(&value);
            if (unavailable(owner, entry))
                return;
            continue;
        }
        if (!entry->unavailable &&
            tiku_resource_value_equal(entry->initial.initial.desired,
                                      value.desired) &&
            tiku_resource_value_equal(entry->initial.initial.persisted,
                                      value.persisted) &&
            tiku_resource_value_equal(entry->initial.initial.applied,
                                      value.applied) &&
            !strcmp(entry->initial.initial.apply_state, value.apply_state)) {
            tiku_resource_snapshot_clear(&value);
            continue;
        }
        tiku_msg_t *message = tiku_resource_provider_client_publish(
            owner->client, entry->initial.spec.path, &value);
        if (!message) {
            tiku_resource_snapshot_clear(&value);
            continue;
        }
        owner->pending_value = value;
        entry->unavailable = 0;
        send_request(owner, message, 3);
        return;
    }
    owner->ready = 1;
}
tiku_app_resources_t *
tiku_app_resources_new(const tiku_app_services_t *services,
                       const tiku_app_descriptor_t *descriptor,
                       const tiku_app_resource_export_t *extension)
{
    if (!getenv("STOMME_RESOURCE_DIRECTORY") || !services ||
        !services->send_message || !descriptor)
        return NULL;
    if (extension &&
        (extension->version != 1 || extension->size != sizeof *extension ||
         !extension->catalog || !extension->capture || !extension->change))
        return NULL;
    tiku_app_resources_t *owner = calloc(1, sizeof *owner);
    if (!owner)
        return NULL;
    if(tiku_resource_new_id(owner->document_seed)){free(owner);return NULL;}
    owner->services = services;
    owner->descriptor = descriptor;
    owner->extension = extension;
    return owner;
}
void tiku_app_resources_model(tiku_app_resources_t *owner, void *state)
{
    if (owner) {
        owner->model = state;
        owner->started = 1;
    }
}
const char *tiku_app_resources_path(tiku_app_resources_t *owner,
                                    const char *format, ...)
{
    if (!owner || !format)
        return NULL;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(owner->scratch, sizeof owner->scratch, format, args);
    va_end(args);
    return n < 0 || (size_t)n >= sizeof owner->scratch ? NULL : owner->scratch;
}
const char *tiku_app_resources_document_named(tiku_app_resources_t *owner,
                                              const char *domain,
                                              uint64_t identity)
{
    if(!owner||!identity||!domain||!domain[0]||strlen(domain)>=32)return NULL;
    char input[128];
    unsigned char hash[32];
    int n = snprintf(input, sizeof input, "%s/%s/%llu", owner->document_seed,
                     domain, (unsigned long long)identity);
    if (n < 0 || (size_t)n >= sizeof input ||
        tiku_hash(TIKU_HASH_SHA256, input, (size_t)n, hash) != 32)
        return NULL;
    for(size_t i=0;i<16;i++)snprintf(owner->document_id+2*i,3,"%02x",hash[i]);
    return owner->document_id;
}
const char *tiku_app_resources_document(tiku_app_resources_t *owner,
                                        uint64_t identity)
{
    return tiku_app_resources_document_named(owner,"document",identity);
}
int tiku_app_resources_message(tiku_app_resources_t *owner,
                               const tiku_msg_t *message)
{
    if (!owner || !message)
        return 0;
    if (tiku_msg_what(message) == TIKU_APP_RESOURCE_HELLO) {
        if (owner->client)
            return 1;
        int32_t version;
        int64_t features;
        if (!tiku_msg_find_int32(message, "version", 0, &version) ||
            version != 1 ||
            !tiku_msg_find_int64(message, "features", 0, &features))
            return 1;
        owner->client = tiku_resource_provider_client_assigned(
            tiku_msg_find_string(message, "session", 0),
            tiku_msg_find_string(message, "principal", 0), (uint64_t)features);
        const char *connection =
            tiku_msg_find_string(message, "connection", 0);
        if (connection)
            tiku_resource_counter_parse(connection, &owner->connection);
        if (!owner->client || tiku_resource_new_id(owner->catalog) ||
            catalog_build(owner, &owner->entries, &owner->count)) {
            owner->phase = -1;
            owner->failed = 1;
        } else
            owner->phase = 1;
        return 1;
    }
    if (tiku_msg_what(message) != TIKU_RESOURCE_MESSAGE_CODE)
        return 0;
    if (!owner->client)
        return 1;
    const char *kind = tiku_msg_find_string(message, "kind", 0);
    if (kind) {
        tiku_resource_provider_call_t call = {0};
        if (tiku_resource_provider_client_event(owner->client, message, &call))
            return 1;
        for (int i = 0; i < 16; i++)
            if (owner->calls[i].call.dispatch[0] &&
                !strcmp(owner->calls[i].call.dispatch, call.dispatch)) {
                owner->calls[i].call.cancel |= call.cancel;
                tiku_resource_provider_call_clear(&call);
                return 1;
            }
        for (int i = 0; i < 16; i++)
            if (!owner->calls[i].call.dispatch[0]) {
                owner->calls[i].call = call;
                owner->calls[i].deadline =
                    owner->now + (int64_t)call.deadline_ms * 1000;
                return 1;
            }
        tiku_resource_provider_call_clear(&call);
        owner->failed = 1;
        owner->phase = -1;
        return 1;
    }
    int operation = owner->pending_kind;
    tiku_resource_error_t error =
        tiku_resource_provider_client_reply(owner->client, message);
    if (operation == 5 && owner->quit_requested &&
        tiku_resource_provider_client_ready(owner->client))
        owner->quit_acknowledged = 1;
    if (error) {
        owner->pending_kind = 0;
        if ((error == TIKU_RESOURCE_STALE ||
             error == TIKU_RESOURCE_OWNER_LOST ||
             error == TIKU_RESOURCE_DEADLINE) &&
            operation != 8 && operation != 9) {
            free(owner->upload);
            owner->upload = NULL;
            tiku_resource_snapshot_clear(&owner->pending_value);
            for (size_t i = 0; i < owner->count; i++)
                owner->entries[i].unavailable = 0;
            send_request(
                owner, tiku_resource_provider_client_status(owner->client), 8);
        } else {
            owner->failed = 1;
            owner->phase = -1;
        }
        return 1;
    }
    owner->pending_kind = 0;
    if (operation == 8) {
        tiku_msg_t *result = tiku_msg_find_message(message, "result", 0);
        int registered = 0;
        tiku_msg_find_bool(result, "registered", 0, &registered);
        tiku_msg_free(result);
        if (!registered) {
            send_request(owner,
                         tiku_resource_provider_client_abort(owner->client,
                                                             owner->catalog),
                         9);
            return 1;
        }
        owner->phase = 2;
    }
    if(operation==5&&owner->quit_requested)owner->quit_acknowledged=1;
    if (operation == 1)
        owner->staged += owner->pending_count;
    if (operation == 2)
        owner->phase = 2;
    if (operation == 3) {
        tiku_resource_snapshot_clear(
            &owner->entries[owner->pending_index].initial.initial);
        owner->entries[owner->pending_index].initial.initial =
            owner->pending_value;
        memset(&owner->pending_value, 0, sizeof owner->pending_value);
    }
    if (operation == 4) {
        owner->upload_at += owner->pending_count;
        if (owner->upload_at == owner->upload_bytes) {
            app_resource_entry_t *entry =
                &owner->entries[owner->pending_index];
            free(entry->stream);
            entry->stream = owner->upload;
            entry->bytes = owner->upload_bytes;
            entry->stream_ready = 1;
            entry->unavailable = 0;
            owner->upload = NULL;
        }
    }
    if (operation == 6 || operation == 9) {
        catalog_free(owner->entries, owner->count);
        owner->entries = NULL;
        owner->count = owner->staged = owner->cursor = 0;
        if (catalog_build(owner, &owner->entries, &owner->count) ||
            tiku_resource_new_id(owner->catalog)) {
            owner->failed = 1;
            owner->phase = -1;
        } else
            owner->phase = 1;
        for (int i = 0; i < 16; i++)
            tiku_resource_provider_call_clear(&owner->calls[i].call);
    }
    return 1;
}
void tiku_app_resources_poll(tiku_app_resources_t *owner, int64_t now)
{
    if (!owner || !owner->started || owner->phase < 0)
        return;
    owner->now = now;
    if (!owner->client) {
        if (now < owner->next_hello)
            return;
        owner->next_hello = now + 2000000;
        tiku_msg_t *hello = tiku_msg_new(TIKU_APP_RESOURCE_HELLO);
        if (hello && tiku_msg_add_int32(hello, "version", 1))
            owner->services->send_message(owner->services->ctx, hello);
        tiku_msg_free(hello);
        return;
    }
    if (!tiku_resource_provider_client_ready(owner->client) ||
        owner->quit_requested)
        return;
    if (owner->phase == 1) {
        if (owner->staged == owner->count) {
            send_request(owner,
                         tiku_resource_provider_client_commit(owner->client,
                                                              owner->catalog),
                         2);
            return;
        }
        tiku_resource_initial_t batch[32];
        size_t count = owner->count - owner->staged;
        if (count > 32)
            count = 32;
        while (count) {
            for (size_t i = 0; i < count; i++)
                batch[i] = owner->entries[owner->staged + i].initial;
            tiku_msg_t *request = tiku_resource_provider_client_stage(
                owner->client, owner->catalog, owner->staged, owner->count,
                batch, count);
            if (request) {
                owner->pending_count = count;
                send_request(owner, request, 1);
                return;
            }
            count--;
        }
        owner->phase = -1;
        owner->failed = 1;
        return;
    }
    for (int i = 0; i < 16; i++)
        if (owner->calls[i].call.dispatch[0]) {
            perform(owner, &owner->calls[i]);
            return;
        }
    if (owner->upload) {
        upload_next(owner);
        return;
    }
    if (owner->extension && now >= owner->next_capture) {
        owner->next_capture = now + 250000;
        uint64_t revision =
            owner->extension->catalog_revision
                ? owner->extension->catalog_revision(owner->model)
                : 0;
        if (revision && revision == owner->catalog_revision) {
            publish_next(owner);
            return;
        }
        owner->catalog_revision = revision;
        app_resource_entry_t *next = NULL;
        size_t count = 0;
        if (!catalog_build(owner, &next, &count)) {
            int same = same_catalog(owner->entries, owner->count, next, count);
            catalog_free(next, count);
            if (!same) {
                owner->ready = 0;
                send_request(
                    owner,
                    tiku_resource_provider_client_unregister(owner->client),
                    6);
                return;
            }
        }
    }
    publish_next(owner);
}
void tiku_app_resources_free(tiku_app_resources_t *owner)
{
    if (owner && owner->started && owner->extension &&
        owner->extension->retire)
        owner->extension->retire(owner->model);
    if (!owner)
        return;
    for (int i = 0; i < 16; i++)
        tiku_resource_provider_call_clear(&owner->calls[i].call);
    for (int i = 0; i < APP_RESOURCE_WINDOWS; i++) {
        free(owner->windows[i].menus);
        free(owner->windows[i].controls);
    }
    tiku_resource_provider_client_free(owner->client);
    catalog_free(owner->entries, owner->count);
    tiku_resource_snapshot_clear(&owner->pending_value);
    free(owner->upload);
    free(owner);
}

int tiku_app_resources_finished(const tiku_app_resources_t *owner)
{
    return owner && owner->quit_requested &&
           (owner->quit_acknowledged || owner->failed ||
            owner->now >= owner->quit_deadline);
}

void tiku_app_resources_request_exit(tiku_app_resources_t *owner)
{
    if (owner && !owner->quit_requested) {
        owner->quit_requested = 1;
        owner->quit_deadline = owner->now + 5000000;
    }
}
int tiku_app_resources_hold_exit(const tiku_app_resources_t *owner)
{
    return owner&&owner->quit_requested&&!tiku_app_resources_finished(owner);
}
