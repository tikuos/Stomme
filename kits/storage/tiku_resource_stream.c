/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_stream.c - immutable content and caller-bound snapshot lifetimes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_stream.h"
#include "tiku_resource_registry_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Refcounted content charged once to the registry's retained-byte budget. */
typedef struct resource_blob {
    tiku_resource_registry_t *registry;
    unsigned references;
    size_t size;
    unsigned char data[];
} resource_blob_t;

struct tiku_resource_stream {
    tiku_resource_registry_t *registry;
    resource_blob_t *blob;
    char principal[TIKU_RESOURCE_ID_MAX], path[TIKU_RESOURCE_PATH_MAX], node[TIKU_RESOURCE_ID_MAX];
    tiku_resource_stamp_t stamp;
};

void tiku_resource_stream_drop(resource_blob_t *blob)
{
    if (blob && !--blob->references) {
        blob->registry->stream_bytes -= blob->size; free(blob);
    }
}

tiku_msg_t *tiku_resource_stream_metadata(size_t bytes, const char *content_id)
{
    char size[21];
    tiku_msg_t *value=tiku_msg_new(0), *member=tiku_msg_new(0), *counter, *identity;
    int ok;
    snprintf(size,sizeof size,"%llu",(unsigned long long)bytes);
    counter=tiku_resource_value_string(TIKU_RESOURCE_COUNTER,size);
    identity=tiku_resource_value_string(TIKU_RESOURCE_TEXT,content_id ? content_id : "");
    ok=value && member && counter && identity && tiku_msg_add_string(value,"type","object") &&
        tiku_msg_add_string(member,"key","bytes") && tiku_msg_add_message(member,"value",counter) && tiku_msg_add_message(value,"member",member);
    tiku_msg_free(member);member=tiku_msg_new(0);
    ok=ok && member && tiku_msg_add_string(member,"key","content_id") && tiku_msg_add_message(member,"value",identity) && tiku_msg_add_message(value,"member",member);
    tiku_msg_free(member);tiku_msg_free(counter);tiku_msg_free(identity);
    if(ok)return value;
    tiku_msg_free(value);return NULL;
}

tiku_resource_error_t tiku_resource_stream_publish(tiku_resource_registry_t *registry,
    const tiku_resource_stamp_t *expected,const char *path,const void *data,size_t bytes,tiku_resource_stamp_t *out)
{
    resource_node_t *node;
    resource_blob_t *blob;
    tiku_resource_snapshot_t snapshot={0};
    tiku_resource_error_t error;
    char id[TIKU_RESOURCE_ID_MAX];
    size_t released;
    if(!tiku_resource_registry_thread(registry) || !tiku_resource_registry_stamp_valid(expected) ||
        !tiku_resource_path_valid(path) || (!data && bytes) || !out)return TIKU_RESOURCE_INVALID;
    if(registry->dispatching)return TIKU_RESOURCE_BUSY;
    node=tiku_resource_registry_node(registry,path);
    if(!node || !node->provider)return TIKU_RESOURCE_NOT_FOUND;
    if(node->spec.kind!=TIKU_RESOURCE_STREAM)return TIKU_RESOURCE_UNSUPPORTED;
    if(!tiku_resource_registry_matches(node->provider,expected))return TIKU_RESOURCE_STALE;
    released=node->blob && node->blob->references==1 ? node->blob->size : 0;
    if(bytes>TIKU_RESOURCE_STREAM_BYTES || registry->stream_bytes-released+bytes>16u*1024u*1024u)return TIKU_RESOURCE_LIMIT;
    if(tiku_resource_new_id(id))return TIKU_RESOURCE_UNAVAILABLE;
    blob=malloc(sizeof *blob+bytes);if(!blob)return TIKU_RESOURCE_NO_MEMORY;
    blob->registry=registry;blob->size=bytes;blob->references=1;if(bytes)memcpy(blob->data,data,bytes);
    snapshot.desired=tiku_resource_stream_metadata(bytes,id);
    snapshot.persisted=tiku_msg_copy(snapshot.desired);snapshot.applied=tiku_msg_copy(snapshot.desired);strcpy(snapshot.apply_state,"applied");
    if(!snapshot.desired || !snapshot.persisted || !snapshot.applied)error=TIKU_RESOURCE_NO_MEMORY;
    else error=tiku_resource_publish(registry,expected,path,&snapshot,out);
    tiku_resource_snapshot_clear(&snapshot);
    if(error){free(blob);return error;}
    tiku_resource_stream_drop(node->blob);node->blob=blob;registry->stream_bytes+=bytes;
    return TIKU_RESOURCE_OK;
}

/** @brief Require current read/content authority on a live stream. */
static int visible(const resource_node_t *node,const tiku_resource_caller_t *caller)
{
    return node && node->provider && node->spec.kind==TIKU_RESOURCE_STREAM && caller && caller->authenticated &&
        (caller->scopes&TIKU_RESOURCE_CONTENT) && tiku_resource_registry_visible(node,caller) &&
        (!node->provider->principal[0] || !strcmp(node->provider->principal,caller->principal) || (caller->scopes&TIKU_RESOURCE_CROSS_APP));
}

tiku_resource_error_t tiku_resource_stream_open(tiku_resource_registry_t *registry,
    const tiku_resource_caller_t *caller,const char *path,const tiku_resource_stamp_t *expected,
    size_t maximum,tiku_resource_stream_t **out,size_t *bytes)
{
    resource_node_t *node;tiku_resource_stream_t *stream;
    if(out)*out=NULL;
    if(bytes)*bytes=0;
    if(!tiku_resource_registry_thread(registry) || !out || !bytes || !tiku_resource_path_valid(path) ||
        !tiku_resource_registry_stamp_valid(expected))return TIKU_RESOURCE_INVALID;
    if(registry->dispatching)return TIKU_RESOURCE_BUSY;
    node=tiku_resource_registry_node(registry,path);if(!visible(node,caller))return TIKU_RESOURCE_NOT_FOUND;
    if(!tiku_resource_registry_matches(node->provider,expected))return TIKU_RESOURCE_STALE;
    if(node->unavailable || !node->blob)return TIKU_RESOURCE_UNAVAILABLE;
    if(!maximum || maximum>TIKU_RESOURCE_STREAM_BYTES || node->blob->size>maximum || registry->stream_leases>=64)return TIKU_RESOURCE_LIMIT;
    stream=calloc(1,sizeof *stream);if(!stream)return TIKU_RESOURCE_NO_MEMORY;
    stream->registry=registry;stream->blob=node->blob;stream->stamp=node->provider->stamp;
    strcpy(stream->principal,caller->principal);strcpy(stream->path,path);strcpy(stream->node,node->entry.id);
    stream->blob->references++;registry->stream_leases++;*bytes=stream->blob->size;*out=stream;return TIKU_RESOURCE_OK;
}

tiku_resource_error_t tiku_resource_stream_read(tiku_resource_stream_t *stream,
    const tiku_resource_caller_t *caller,uint64_t offset,void *out,size_t capacity,size_t *read)
{
    resource_node_t *node;size_t count;
    if(read)*read=0;
    if(!stream || !tiku_resource_registry_thread(stream->registry) || !read || !out || !capacity || capacity>TIKU_RESOURCE_STREAM_CHUNK)return TIKU_RESOURCE_INVALID;
    node=tiku_resource_registry_node(stream->registry,stream->path);
    if(!node || !node->provider || strcmp(node->entry.id,stream->node) ||
        strcmp(node->provider->stamp.incarnation,stream->stamp.incarnation))return TIKU_RESOURCE_OWNER_LOST;
    if(!caller || strcmp(caller->principal,stream->principal) || !visible(node,caller))return TIKU_RESOURCE_DENIED;
    if(offset>stream->blob->size)return TIKU_RESOURCE_INVALID;
    count=stream->blob->size-(size_t)offset;if(count>capacity)count=capacity;
    if(count)memcpy(out,stream->blob->data+(size_t)offset,count);
    *read=count;return TIKU_RESOURCE_OK;
}

void tiku_resource_stream_close(tiku_resource_stream_t *stream)
{
    if(!stream || !tiku_resource_registry_thread(stream->registry))return;
    stream->registry->stream_leases--;tiku_resource_stream_drop(stream->blob);free(stream);
}
