/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * tiku_paged_storage.c - bounded request buffers and deferred provider retirement.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_paged_storage.h"
#include <string.h>

/** @brief Owned request buffers and provider retirement state. */
struct tiku_paged_storage {
    tiku_owned_buffer_t self;
    tiku_resource_entry_t *entry_data;
    unsigned char *byte_data;
    size_t entry_bytes,byte_bytes;
    tiku_storage_provider_t provider;
    tiku_storage_request_t request;
    tiku_resource_error_t status;
    size_t count;
    uint64_t epoch, file_bytes, file_version, prepared, total;
    int described;
    int eof, pending, cancelled, closed, calling;
};

tiku_paged_storage_t *tiku_paged_storage_create(tiku_allocator_t allocator,
    tiku_storage_provider_t provider, const tiku_resource_caller_t *caller,
    size_t entries, size_t bytes)
{
    tiku_owned_buffer_t owner;
    tiku_paged_storage_t *store;
    const uint64_t allowed = TIKU_RESOURCE_FEATURE_READ | TIKU_RESOURCE_FEATURE_COLLECTION | TIKU_RESOURCE_FEATURE_STREAM | TIKU_RESOURCE_FEATURE_CHANGE;
    size_t alignment = _Alignof(tiku_paged_storage_t);
    if (alignment < _Alignof(tiku_resource_entry_t)) alignment = _Alignof(tiku_resource_entry_t);
    size_t offset = (sizeof *store + alignment - 1) & ~(alignment - 1);
    if (!provider.ops || !provider.ops->submit || !provider.ops->cancel ||
        !(provider.capabilities & TIKU_RESOURCE_FEATURE_READ) || (provider.capabilities & ~allowed) ||
        !caller || !memchr(caller->principal, 0, sizeof caller->principal) ||
        (!entries && !bytes && !(provider.capabilities & TIKU_RESOURCE_FEATURE_CHANGE)) ||
        entries > TIKU_RESOURCE_MEMBERS_MAX || bytes > TIKU_RESOURCE_STREAM_CHUNK ||
        entries > (SIZE_MAX - offset) / sizeof(tiku_resource_entry_t)) return NULL;
    size_t entry_bytes = entries * sizeof(tiku_resource_entry_t);
    if (bytes > SIZE_MAX - offset - entry_bytes ||
        !tiku_buffer_init(&owner, allocator, alignment) ||
        !tiku_buffer_resize(&owner, 1, offset + entry_bytes + bytes, 1)) return NULL;
    store = owner.data;
    store->self = owner;
    store->entry_data = entries ? (tiku_resource_entry_t *)((unsigned char *)store + offset) : NULL;
    store->byte_data = bytes ? (unsigned char *)store + offset + entry_bytes : NULL;
    store->entry_bytes = entry_bytes;store->byte_bytes = bytes;
    store->provider = provider;
    store->request.caller = *caller;
    store->status = TIKU_RESOURCE_PENDING;
    return store;
}

/** @brief Submit a bounded immutable request and reject callback reentry. */
static tiku_resource_error_t begin(tiku_paged_storage_t *store, const char *path,
    tiku_storage_operation_t operation, uint64_t epoch, uint64_t id, uint64_t offset, const char *destination,const void *input,size_t input_bytes)
{
    size_t length = 0, destination_length = 0;
    int mutation = operation >= TIKU_STORAGE_RENAME;
    tiku_resource_error_t result;
    uint64_t feature = mutation ? TIKU_RESOURCE_FEATURE_CHANGE :
        operation == TIKU_STORAGE_LIST ? TIKU_RESOURCE_FEATURE_COLLECTION : TIKU_RESOURCE_FEATURE_STREAM;
    if (!store || !path) return TIKU_RESOURCE_INVALID;
    if (store->calling || store->pending) return TIKU_RESOURCE_BUSY;
    if (store->closed) return store->status;
    while (length < sizeof store->request.path && path[length]) ++length;
    if (!length || path[0] != '/' || length == sizeof store->request.path ||
        (operation == TIKU_STORAGE_LIST ? (!epoch && id) : (!epoch || (!id && operation!=TIKU_STORAGE_REPLACE && operation!=TIKU_STORAGE_MKDIR && operation!=TIKU_STORAGE_DIRECTORY_UPGRADE)))) return TIKU_RESOURCE_INVALID;
    if(operation==TIKU_STORAGE_REPLACE && ((!input&&input_bytes)||input_bytes>UINT32_MAX))return TIKU_RESOURCE_INVALID;
    if(operation==TIKU_STORAGE_TREE_COPY&&(!input||!input_bytes||input_bytes>UINT32_MAX))return TIKU_RESOURCE_INVALID;
    if (operation == TIKU_STORAGE_RENAME || operation == TIKU_STORAGE_TREE_RENAME || operation == TIKU_STORAGE_TREE_COPY) {
        if (!destination || destination[0] != '/') return TIKU_RESOURCE_INVALID;
        while (destination_length < sizeof store->request.destination && destination[destination_length]) ++destination_length;
        if (!destination_length || destination_length == sizeof store->request.destination) return TIKU_RESOURCE_INVALID;
    }
    if (!(store->provider.capabilities & feature) ||
        (!mutation && !(operation == TIKU_STORAGE_LIST ? store->entry_bytes : store->byte_bytes))) return TIKU_RESOURCE_UNSUPPORTED;
    if (store->request.token == UINT64_MAX) return TIKU_RESOURCE_LIMIT;
    if (operation == TIKU_STORAGE_READ && offset > UINT64_MAX - store->byte_bytes) return TIKU_RESOURCE_INVALID;
    char copied_destination[TIKU_RESOURCE_PATH_MAX];
    if (destination) memmove(copied_destination, destination, destination_length + 1);
    store->request.operation = operation;
    memmove(store->request.path, path, length + 1);
    store->request.destination[0] = 0;
    if (destination) memcpy(store->request.destination, copied_destination, destination_length + 1);
    store->request.epoch = epoch;
    store->request.after = operation == TIKU_STORAGE_LIST ? id : 0;
    store->request.node_id = operation != TIKU_STORAGE_LIST ? id : 0;
    store->request.offset = offset;
    store->request.entries = operation == TIKU_STORAGE_LIST ? store->entry_data : NULL;
    store->request.bytes = operation == TIKU_STORAGE_READ ? store->byte_data : NULL;
    store->request.input=input;store->request.input_bytes=input_bytes;
    if(operation==TIKU_STORAGE_TREE_COPY){store->request.bytes=(unsigned char *)input;
        store->request.input=NULL;store->request.input_bytes=0;}
    store->request.capacity = (operation==TIKU_STORAGE_REPLACE||operation==TIKU_STORAGE_TREE_COPY)?input_bytes:mutation ? 0 : operation == TIKU_STORAGE_LIST ? store->entry_bytes / sizeof(tiku_resource_entry_t) : store->byte_bytes;
    ++store->request.token;
    store->prepared=store->total=0;
    store->described = 0;
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
{ return begin(store, path, TIKU_STORAGE_LIST, epoch, after, 0, NULL, NULL, 0); }

tiku_resource_error_t tiku_paged_storage_read(tiku_paged_storage_t *store,
    const char *path, uint64_t epoch, uint64_t node_id, uint64_t offset)
{ return begin(store, path, TIKU_STORAGE_READ, epoch, node_id, offset, NULL, NULL, 0); }

tiku_resource_error_t tiku_paged_storage_rename(tiku_paged_storage_t *store,
    const char *path,uint64_t epoch,uint64_t node_id,const char *destination)
{ return begin(store,path,TIKU_STORAGE_RENAME,epoch,node_id,0,destination,NULL,0); }

tiku_resource_error_t tiku_paged_storage_delete(tiku_paged_storage_t *store,
    const char *path,uint64_t epoch,uint64_t node_id)
{ return begin(store,path,TIKU_STORAGE_DELETE,epoch,node_id,0,NULL,NULL,0); }

tiku_resource_error_t tiku_paged_storage_replace(tiku_paged_storage_t *store,const char *path,
    uint64_t epoch,uint64_t node_id,const void *input,size_t bytes)
{ return begin(store,path,TIKU_STORAGE_REPLACE,epoch,node_id,0,NULL,input,bytes); }

tiku_resource_error_t tiku_paged_storage_tree_rename(tiku_paged_storage_t *store,const char *path,
    uint64_t epoch,uint64_t node_id,const char *destination)
{ return begin(store,path,TIKU_STORAGE_TREE_RENAME,epoch,node_id,0,destination,NULL,0); }
tiku_resource_error_t tiku_paged_storage_tree_delete(tiku_paged_storage_t *store,const char *path,uint64_t epoch,uint64_t node_id)
{ return begin(store,path,TIKU_STORAGE_TREE_DELETE,epoch,node_id,0,NULL,NULL,0); }
tiku_resource_error_t tiku_paged_storage_tree_copy(tiku_paged_storage_t *store,const char *path,
    uint64_t epoch,uint64_t node_id,const char *destination,void *workspace,size_t capacity)
{ return begin(store,path,TIKU_STORAGE_TREE_COPY,epoch,node_id,0,destination,workspace,capacity); }
int tiku_paged_storage_set_progress(tiku_paged_storage_t *s,uint64_t token,uint64_t prepared,uint64_t total)
{
    if(!s||s->calling||!s->pending||s->cancelled||s->request.token!=token||s->request.operation!=TIKU_STORAGE_TREE_COPY||
       prepared>total||prepared<s->prepared||(s->total&&s->total!=total))return 0;
    s->prepared=prepared;s->total=total;return 1;
}
int tiku_paged_storage_progress(const tiku_paged_storage_t *s,uint64_t *prepared,uint64_t *total)
{
    if(!s||!prepared||!total||s->cancelled||s->request.operation!=TIKU_STORAGE_TREE_COPY||
       (!s->pending&&s->status!=TIKU_RESOURCE_OK))return 0;
    *prepared=s->prepared;*total=s->total;return 1;
}
tiku_resource_error_t tiku_paged_storage_directory_upgrade(tiku_paged_storage_t *store,uint64_t epoch)
{ return begin(store,"/",TIKU_STORAGE_DIRECTORY_UPGRADE,epoch,0,0,NULL,NULL,0); }
tiku_resource_error_t tiku_paged_storage_mkdir(tiku_paged_storage_t *store,const char *path,uint64_t epoch)
{ return begin(store,path,TIKU_STORAGE_MKDIR,epoch,0,0,NULL,NULL,0); }

int tiku_paged_storage_complete(tiku_paged_storage_t *store, uint64_t token,
    tiku_resource_error_t status, size_t count, uint64_t epoch, int eof)
{
    uint64_t previous;
    if (!store || store->calling || !store->pending || token != store->request.token) return 0;
    store->pending = 0;
    if (store->cancelled) return 1;
    if (status == TIKU_RESOURCE_PENDING || (status == TIKU_RESOURCE_OK &&
        (count > store->request.capacity || !epoch || (!count && !eof)))) status = TIKU_RESOURCE_INVALID;
    if(status==TIKU_RESOURCE_OK && store->described &&
        ((count && (store->request.offset>store->file_bytes || count>store->file_bytes-store->request.offset)) ||
         (!!eof != (store->request.offset>=store->file_bytes || count==store->file_bytes-store->request.offset))))
        status=TIKU_RESOURCE_INVALID;
    if(status==TIKU_RESOURCE_OK && store->request.operation==TIKU_STORAGE_REPLACE &&
        (count!=store->request.input_bytes || !eof))status=TIKU_RESOURCE_INVALID;
    if (status == TIKU_RESOURCE_OK && store->request.operation < TIKU_STORAGE_RENAME &&
        store->request.epoch && epoch != store->request.epoch)
        status = TIKU_RESOURCE_STALE_CURSOR;
    if(status==TIKU_RESOURCE_OK&&store->request.operation==TIKU_STORAGE_TREE_COPY&&(count||!eof))status=TIKU_RESOURCE_INVALID;
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

int tiku_paged_storage_describe(tiku_paged_storage_t *s,uint64_t token,uint64_t bytes,uint64_t version)
{
    if(!s||s->calling||!s->pending||s->cancelled||s->request.token!=token||s->request.operation!=TIKU_STORAGE_READ)return 0;
    s->file_bytes=bytes;s->file_version=version;s->described=1;return 1;
}
int tiku_paged_storage_info(const tiku_paged_storage_t *s,uint64_t *bytes,uint64_t *version)
{
    if(!s||s->pending||s->cancelled||!s->described||s->status!=TIKU_RESOURCE_OK)return 0;
    if(bytes)*bytes=s->file_bytes;
    if(version)*version=s->file_version;
    return 1;
}
