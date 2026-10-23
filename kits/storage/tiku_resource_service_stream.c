/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_service_stream.c - connection-owned frozen read leases.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_service_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Release a lease's retained content and identity. */
static void close_lease(resource_stream_lease_t *lease)
{
    tiku_resource_stream_close(lease->snapshot);memset(lease,0,sizeof *lease);
}

void tiku_resource_service_streams_close(tiku_resource_service_t *service,int expired_only)
{
    unsigned i;uint64_t now=tiku_resource_registry_now(service->registry);
    for(i=0;i<8;i++)if(service->streams[i].snapshot && (!expired_only || !now || now>=service->streams[i].expires))close_lease(&service->streams[i]);
}

tiku_resource_error_t tiku_resource_service_stream(tiku_resource_service_t *service,const tiku_msg_t *request,tiku_msg_t **out)
{
    const char *op=tiku_msg_find_string(request,"op",0),*id=tiku_msg_find_string(request,"lease",0);
    resource_stream_lease_t *lease=NULL;
    uint64_t now=tiku_resource_registry_now(service->registry),offset;
    unsigned i;size_t retained=0;
    tiku_resource_error_t error;
    tiku_resource_service_streams_close(service,1);
    if(!now || now>UINT64_MAX-30000)return TIKU_RESOURCE_UNAVAILABLE;
    if(!strcmp(op,"stream-open")){
        tiku_resource_stamp_t expected;size_t bytes;
        if(!tiku_resource_service_expected(request,&expected) ||
            tiku_resource_counter_parse(tiku_msg_find_string(request,"maximum_bytes",0),&offset))return TIKU_RESOURCE_INVALID;
        if(offset>service->limits.stream_bytes)return TIKU_RESOURCE_LIMIT;
        for(i=0;i<service->limits.stream_leases;i++){
            if(service->streams[i].snapshot)retained+=service->streams[i].bytes;
            else if(!lease)lease=&service->streams[i];
        }
        if(!lease)return TIKU_RESOURCE_LIMIT;
        if(tiku_resource_new_id(lease->id))return TIKU_RESOURCE_UNAVAILABLE;
        error=tiku_resource_stream_open(service->registry,&service->caller,tiku_msg_find_string(request,"path",0),&expected,(size_t)offset,&lease->snapshot,&bytes);
        if(error){memset(lease,0,sizeof *lease);return error;}
        if(bytes>service->limits.stream_bytes-retained){close_lease(lease);return TIKU_RESOURCE_LIMIT;}
        lease->stamp=expected;lease->bytes=bytes;lease->expires=now+30000;
        *out=tiku_msg_new(0);
        {
            char size[21];snprintf(size,sizeof size,"%llu",(unsigned long long)bytes);
            if(!*out || !tiku_msg_add_string(*out,"lease",lease->id) || !tiku_msg_add_string(*out,"bytes",size) ||
                !tiku_resource_service_stamp(*out,&expected)){close_lease(lease);return TIKU_RESOURCE_NO_MEMORY;}
        }
        return TIKU_RESOURCE_OK;
    }
    for(i=0;i<8;i++)if(service->streams[i].snapshot && !strcmp(service->streams[i].id,id)){lease=&service->streams[i];break;}
    if(!strcmp(op,"stream-close")){if(lease)close_lease(lease);return TIKU_RESOURCE_OK;}
    if(!lease)return TIKU_RESOURCE_OWNER_LOST;
    {
        int32_t count;size_t bytes;void *data;
        if(tiku_resource_counter_parse(tiku_msg_find_string(request,"offset",0),&offset) || !tiku_msg_find_int32(request,"byte_count",0,&count))return TIKU_RESOURCE_INVALID;
        if(count<1 || (unsigned)count>service->limits.stream_chunk_bytes)return TIKU_RESOURCE_LIMIT;
        data=malloc((size_t)count);if(!data)return TIKU_RESOURCE_NO_MEMORY;
        error=tiku_resource_stream_read(lease->snapshot,&service->caller,offset,data,(size_t)count,&bytes);
        if(!error){
            *out=tiku_msg_new(0);
            if(!*out || !tiku_msg_add_blob(*out,"data",data,bytes) || !tiku_resource_service_stamp(*out,&lease->stamp))error=TIKU_RESOURCE_NO_MEMORY;
            else lease->expires=now+30000;
        }
        free(data);if(error==TIKU_RESOURCE_OWNER_LOST || error==TIKU_RESOURCE_DENIED)close_lease(lease);
        return error;
    }
}
