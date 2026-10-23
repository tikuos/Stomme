/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_collection.c - atomic replacement of bounded collection catalogs.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_stream.h"
#include "tiku_resource_registry_private.h"
#include <stdlib.h>
#include <string.h>

tiku_resource_error_t tiku_resource_collection_replace(tiku_resource_registry_t *registry,
    const tiku_resource_stamp_t *expected,const char *path,const tiku_resource_initial_t *values,
    size_t count,tiku_resource_stamp_t *out)
{
    tiku_resource_registry_t *prepared=NULL;
    resource_node_t *collection,**old=NULL,**next=NULL;
    resource_provider_t *owner;
    tiku_resource_stamp_t temporary;
    tiku_resource_error_t error=TIKU_RESOURCE_INVALID;
    unsigned i, removed=0, added=0;
    if(!tiku_resource_registry_thread(registry) || !tiku_resource_registry_stamp_valid(expected) ||
        !tiku_resource_path_valid(path) || !out || (count && !values))return error;
    if(registry->dispatching)return TIKU_RESOURCE_BUSY;
    collection=tiku_resource_registry_node(registry,path);
    if(!collection || !collection->provider)return TIKU_RESOURCE_NOT_FOUND;
    if(collection->spec.kind!=TIKU_RESOURCE_COLLECTION)return TIKU_RESOURCE_UNSUPPORTED;
    owner=collection->provider;if(!tiku_resource_registry_matches(owner,expected))return TIKU_RESOURCE_STALE;
    if(owner->stamp.revision==UINT64_MAX || registry->epoch==UINT64_MAX)return TIKU_RESOURCE_UNAVAILABLE;
    if(count>registry->limits.resources)return TIKU_RESOURCE_LIMIT;
    for(i=0;i<count;i++)if(!tiku_resource_path_valid(values[i].spec.path) ||
        !strcmp(values[i].spec.path,path) || !tiku_resource_registry_below(values[i].spec.path,path))return TIKU_RESOURCE_INVALID;
    if(count){
        prepared=tiku_resource_registry_new(&registry->limits);if(!prepared)return TIKU_RESOURCE_NO_MEMORY;
        if(owner->asynchronous)error=tiku_resource_register_async(prepared,owner->key,path,owner->principal[0]?owner->principal:NULL,
            values,count,&owner->async,owner->owner,&temporary);
        else error=tiku_resource_register(prepared,owner->key,path,owner->principal[0]?owner->principal:NULL,
            values,count,&owner->controller,owner->owner,&temporary);
        if(error)goto done;
        for(i=0;i<prepared->count;i++)if(strcmp(prepared->nodes[i]->entry.path,path) && tiku_resource_registry_below(prepared->nodes[i]->entry.path,path))added++;
    }
    for(i=0;i<registry->count;i++)if(registry->nodes[i]!=collection && tiku_resource_registry_below(registry->nodes[i]->entry.path,path))removed++;
    if(registry->count-removed+added>registry->limits.resources || registry->next_node>UINT64_MAX-added){error=TIKU_RESOURCE_LIMIT;goto done;}
    old=calloc(removed?removed:1,sizeof *old);next=calloc(registry->limits.resources,sizeof *next);
    if(!old || !next){error=TIKU_RESOURCE_NO_MEMORY;goto done;}
    removed=0;added=0;
    for(i=0;i<registry->count;i++){
        resource_node_t *node=registry->nodes[i];
        if(node!=collection && tiku_resource_registry_below(node->entry.path,path))old[removed++]=node;
        else next[added++]=node;
    }
    if(prepared)for(i=0;i<prepared->count;){
        resource_node_t *node=prepared->nodes[i];
        if(!strcmp(node->entry.path,path) || !tiku_resource_registry_below(node->entry.path,path)){i++;continue;}
        if(node->provider)node->provider=owner;
        node->entry.node_id=++registry->next_node;next[added++]=node;
        memmove(prepared->nodes+i,prepared->nodes+i+1,(--prepared->count-i)*sizeof *prepared->nodes);
    }
    free(registry->nodes);registry->nodes=next;next=NULL;registry->count=added;
    owner->stamp.revision++;registry->epoch++;collection->value.stamp=owner->stamp;
    for(i=0;i<removed;i++){if(old[i]->provider)tiku_resource_registry_emit(registry,old[i],1);tiku_resource_registry_node_free(old[i]);}
    tiku_resource_registry_emit(registry,collection,0);*out=owner->stamp;error=TIKU_RESOURCE_OK;
done:
    free(old);free(next);tiku_resource_registry_free(prepared);return error;
}
