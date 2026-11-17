/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_paged_storage.c - bounded request buffers and deferred provider retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_paged_storage.h"
#include <string.h>

/** @brief Owned request buffers and provider retirement state. */
struct tiku_paged_storage {
    tiku_owned_buffer_t self, entries, bytes;
    tiku_storage_provider_t provider;
    tiku_storage_request_t request;
    tiku_resource_error_t status;
    size_t count;
    uint64_t epoch;
    int eof, pending, cancelled, closed, calling;
};

tiku_paged_storage_t *tiku_paged_storage_create(tiku_allocator_t allocator,
    tiku_storage_provider_t provider, const tiku_resource_caller_t *caller,
    size_t entries, size_t bytes)
{
    tiku_owned_buffer_t owner;
    tiku_paged_storage_t *store;
    const uint64_t allowed = TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM;
    if (!provider.ops || !provider.ops->submit || !provider.ops->cancel ||
        !(provider.capabilities & TIKU_RESOURCE_FEATURE_READ) || (provider.capabilities & ~allowed) ||
        !caller || !memchr(caller->principal, 0, sizeof caller->principal) ||
        (!entries && !bytes) || entries > TIKU_RESOURCE_MEMBERS_MAX || bytes > TIKU_RESOURCE_STREAM_CHUNK ||
        !tiku_buffer_init(&owner, allocator, _Alignof(tiku_paged_storage_t)) ||
        !tiku_buffer_resize(&owner, 1, sizeof *store, 1)) return NULL;
    store = owner.data;
    store->self = owner;
    store->provider = provider;
    store->request.caller = *caller;
    store->status = TIKU_RESOURCE_PENDING;
    if (!tiku_buffer_init(&store->entries, allocator, _Alignof(tiku_resource_entry_t)) ||
        !tiku_buffer_init(&store->bytes, allocator, 1) ||
        !tiku_buffer_resize(&store->entries, entries, sizeof(tiku_resource_entry_t), 1) ||
        !tiku_buffer_resize(&store->bytes, bytes, 1, 1)) {
        tiku_buffer_dispose(&store->entries);
        tiku_buffer_dispose(&store->bytes);
        tiku_buffer_dispose(&owner);
        return NULL;
    }
    return store;
}

/** @brief Submit a bounded immutable request and reject callback reentry. */
static tiku_resource_error_t begin(tiku_paged_storage_t *store, const char *path,
    tiku_storage_operation_t operation, uint64_t epoch, uint64_t id, uint64_t offset)
{
    size_t length = 0;
    tiku_resource_error_t result;
    uint64_t feature = operation == TIKU_STORAGE_LIST ? TIKU_RESOURCE_FEATURE_COLLECTION : TIKU_RESOURCE_FEATURE_STREAM;
    if (!store || !path) return TIKU_RESOURCE_INVALID;
    if (store->calling || store->pending) return TIKU_RESOURCE_BUSY;
    if (store->closed) return store->status;
    while (length < sizeof store->request.path && path[length]) ++length;
    if (!length || path[0] != '/' || length == sizeof store->request.path ||
        (operation == TIKU_STORAGE_LIST ? (!epoch && id) : (!epoch || !id))) return TIKU_RESOURCE_INVALID;
    if (!(store->provider.capabilities & feature) ||
        !(operation == TIKU_STORAGE_LIST ? store->entries.bytes : store->bytes.bytes)) return TIKU_RESOURCE_UNSUPPORTED;
    if (store->request.token == UINT64_MAX) return TIKU_RESOURCE_LIMIT;
    if (operation == TIKU_STORAGE_READ && offset > UINT64_MAX - store->bytes.bytes) return TIKU_RESOURCE_INVALID;
    store->request.operation = operation;
    memmove(store->request.path, path, length + 1);
    store->request.epoch = epoch;
    store->request.after = operation == TIKU_STORAGE_LIST ? id : 0;
    store->request.node_id = operation == TIKU_STORAGE_READ ? id : 0;
    store->request.offset = offset;
    store->request.entries = operation == TIKU_STORAGE_LIST ? store->entries.data : NULL;
    store->request.bytes = operation == TIKU_STORAGE_READ ? store->bytes.data : NULL;
    store->request.capacity = operation == TIKU_STORAGE_LIST ? store->entries.bytes / sizeof(tiku_resource_entry_t) : store->bytes.bytes;
    ++store->request.token;
    store->cancelled = 0;
    store->count = 0;
    store->status = TIKU_RESOURCE_PENDING;
    store->pending = store->calling = 1;
    result = store->provider.ops->submit(store->provider.context, store, &store->request);
    store->calling = 0;
    if (result != TIKU_RESOURCE_OK) {
        store->pending = 0;
        store->status = result == TIKU_RESOURCE_PENDING ? TIKU_RESOURCE_INVALID : result;
    }
    return store->status;
}

tiku_resource_error_t tiku_paged_storage_list(tiku_paged_storage_t *store,
    const char *path, uint64_t epoch, uint64_t after)
{ return begin(store, path, TIKU_STORAGE_LIST, epoch, after, 0); }

tiku_resource_error_t tiku_paged_storage_read(tiku_paged_storage_t *store,
    const char *path, uint64_t epoch, uint64_t node_id, uint64_t offset)
{ return begin(store, path, TIKU_STORAGE_READ, epoch, node_id, offset); }

int tiku_paged_storage_complete(tiku_paged_storage_t *store, uint64_t token,
    tiku_resource_error_t status, size_t count, uint64_t epoch, int eof)
{
    uint64_t previous;
    if (!store || store->calling || !store->pending || token != store->request.token) return 0;
    store->pending = 0;
    if (store->cancelled) return 1;
    if (status == TIKU_RESOURCE_PENDING || (status == TIKU_RESOURCE_OK &&
        (count > store->request.capacity || !epoch || (!count && !eof)))) status = TIKU_RESOURCE_INVALID;
    if (status == TIKU_RESOURCE_OK && store->request.epoch && epoch != store->request.epoch)
        status = TIKU_RESOURCE_STALE_CURSOR;
    previous = store->request.after;
    if (status == TIKU_RESOURCE_OK && store->request.operation == TIKU_STORAGE_LIST) {
        for (size_t i = 0; i < count; ++i) {
            const tiku_resource_entry_t *entry = &store->request.entries[i];
            if (entry->node_id <= previous ||
                !memchr(entry->path, 0, sizeof entry->path) || entry->path[0] != '/' ||
                !memchr(entry->id, 0, sizeof entry->id) || !entry->id[0] ||
                (entry->kind != TIKU_RESOURCE_DIRECTORY && entry->kind != TIKU_RESOURCE_STREAM)) {
                status = TIKU_RESOURCE_INVALID;
                break;
            }
            previous = entry->node_id;
        }
    }
    store->status = status;
    store->count = status == TIKU_RESOURCE_OK ? count : 0;
    store->epoch = epoch;
    store->eof = !!eof;
    return 1;
}

/** @brief Hide results and issue at most one provider cancellation per request. */
static void cancel(tiku_paged_storage_t *store, tiku_resource_error_t status)
{
    if (!store || store->calling) return;
    store->status = status;
    store->count = 0;
    if (store->cancelled) return;
    store->cancelled = 1;
    if (store->pending) {
        store->calling = 1;
        store->provider.ops->cancel(store->provider.context, store, store->request.token);
        store->calling = 0;
    }
}

void tiku_paged_storage_cancel(tiku_paged_storage_t *store)
{ if (store && !store->closed) cancel(store, TIKU_RESOURCE_CANCELLED); }

void tiku_paged_storage_unavailable(tiku_paged_storage_t *store)
{
    if (!store || store->calling) return;
    store->closed = 1;
    cancel(store, TIKU_RESOURCE_UNAVAILABLE);
}

void tiku_paged_storage_close(tiku_paged_storage_t *store)
{
    if (!store || store->calling || store->closed) return;
    store->closed = 1;
    cancel(store, TIKU_RESOURCE_CANCELLED);
}

int tiku_paged_storage_destroy(tiku_paged_storage_t **pointer)
{
    tiku_paged_storage_t *store;
    tiku_owned_buffer_t owner;
    if (!pointer || !(store = *pointer)) return 1;
    if (store->calling || !store->closed || store->pending) return 0;
    tiku_buffer_dispose(&store->entries);
    tiku_buffer_dispose(&store->bytes);
    owner = store->self;
    tiku_buffer_dispose(&owner);
    *pointer = NULL;
    return 1;
}

int tiku_paged_storage_pending(const tiku_paged_storage_t *store)
{ return store && store->pending; }

tiku_resource_error_t tiku_paged_storage_status(const tiku_paged_storage_t *store)
{ return store ? store->status : TIKU_RESOURCE_INVALID; }

const tiku_storage_request_t *tiku_paged_storage_result(const tiku_paged_storage_t *store,
    size_t *count, uint64_t *epoch, int *eof)
{
    if (!store || store->pending || store->status != TIKU_RESOURCE_OK || !count || !epoch || !eof) return NULL;
    *count = store->count;
    *epoch = store->epoch;
    *eof = store->eof;
    return &store->request;
}
