/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_resource_limits.c - validated peer ceilings and bounded message checks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_limits.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/** @brief One named limit and its public structure offset and hard range. */
typedef struct {const char *name;size_t offset;unsigned minimum,maximum;} limit_t;
#define LIMIT(field,low,high) {#field,offsetof(tiku_resource_protocol_limits_t,field),low,high}
static const limit_t limits[]={
    LIMIT(frame_bytes,4096,TIKU_RESOURCE_FRAME_MAX),LIMIT(message_depth,4,TIKU_RESOURCE_DEPTH_MAX),
    LIMIT(text_bytes,1,TIKU_RESOURCE_TEXT_MAX),LIMIT(members,1,TIKU_RESOURCE_MEMBERS_MAX),LIMIT(page_entries,1,128),
    LIMIT(transaction_settings,1,TIKU_RESOURCE_TRANSACTION_MAX),LIMIT(subscriptions,1,32),LIMIT(active_jobs,1,16),
    LIMIT(provider_resources,1,32),LIMIT(stream_leases,1,8),LIMIT(stream_chunk_bytes,1,TIKU_RESOURCE_STREAM_CHUNK),
    LIMIT(stream_bytes,1,TIKU_RESOURCE_STREAM_BYTES)
};

void tiku_resource_protocol_limits_default(tiku_resource_protocol_limits_t *out)
{
    size_t i;if(!out)return;
    for(i=0;i<sizeof limits/sizeof limits[0];i++)*(unsigned *)((char *)out+limits[i].offset)=limits[i].maximum;
}

int tiku_resource_protocol_limits_valid(const tiku_resource_protocol_limits_t *input)
{
    size_t i;if(!input)return 0;
    for(i=0;i<sizeof limits/sizeof limits[0];i++){
        unsigned value=*(const unsigned *)((const char *)input+limits[i].offset);
        if(value<limits[i].minimum || value>limits[i].maximum)return 0;
    }
    return input->stream_chunk_bytes<=input->frame_bytes-1024;
}

int tiku_resource_protocol_limits_contains(const tiku_resource_protocol_limits_t *offer,const tiku_resource_protocol_limits_t *selection)
{
    size_t i;if(!tiku_resource_protocol_limits_valid(offer) || !tiku_resource_protocol_limits_valid(selection))return 0;
    for(i=0;i<sizeof limits/sizeof limits[0];i++)if(*(const unsigned *)((const char *)selection+limits[i].offset)>
        *(const unsigned *)((const char *)offer+limits[i].offset))return 0;
    return 1;
}

tiku_msg_t *tiku_resource_protocol_limits_message(const tiku_resource_protocol_limits_t *input)
{
    size_t i;tiku_msg_t *message;
    if(!tiku_resource_protocol_limits_valid(input))return NULL;
    message=tiku_msg_new(0);if(!message)return NULL;
    for(i=0;i<sizeof limits/sizeof limits[0];i++)if(!tiku_msg_add_int32(message,limits[i].name,
        (int32_t)*(const unsigned *)((const char *)input+limits[i].offset))){tiku_msg_free(message);return NULL;}
    return message;
}

int tiku_resource_protocol_limits_decode(const tiku_msg_t *message,tiku_resource_protocol_limits_t *out)
{
    size_t i;tiku_resource_protocol_limits_t decoded;
    if(!message || !out || tiku_msg_what(message) || tiku_msg_field_count(message)!=(int)(sizeof limits/sizeof limits[0]))return 0;
    for(i=0;i<sizeof limits/sizeof limits[0];i++){
        int32_t value;
        if(tiku_msg_count(message,limits[i].name)!=1 || !tiku_msg_find_int32(message,limits[i].name,0,&value) || value<0)return 0;
        *(unsigned *)((char *)&decoded+limits[i].offset)=(unsigned)value;
    }
    if(!tiku_resource_protocol_limits_valid(&decoded))return 0;
    *out=decoded;return 1;
}

void tiku_resource_protocol_limits_merge(tiku_resource_protocol_limits_t *out,const tiku_resource_protocol_limits_t *peer)
{
    size_t i;
    for(i=0;i<sizeof limits/sizeof limits[0];i++){
        unsigned *value=(unsigned *)((char *)out+limits[i].offset);
        unsigned offered=*(const unsigned *)((const char *)peer+limits[i].offset);
        if(*value>offered)*value=offered;
    }
    if(out->stream_chunk_bytes>out->frame_bytes-1024)out->stream_chunk_bytes=out->frame_bytes-1024;
}

/** @brief Walk preflighted nested messages and bound typed text and composites. */
static int nested(const tiku_msg_t *message,const tiku_resource_protocol_limits_t *selected,unsigned depth)
{
    const char *type=tiku_msg_find_string(message,"type",0);int i;
    if(depth>selected->message_depth)return 0;
    if(type && !strcmp(type,"text")){
        const char *value=tiku_msg_find_string(message,"value",0);
        if(value && strlen(value)>selected->text_bytes)return 0;
    }
    if(type && ((!strcmp(type,"object") && tiku_msg_count(message,"member")>(int)selected->members) ||
        (!strcmp(type,"array") && tiku_msg_count(message,"item")>(int)selected->members)))return 0;
    for(i=0;i<tiku_msg_field_count(message);i++){
        tiku_msg_type_t field;int count,j;const char *name=tiku_msg_field_at(message,i,&field,&count);
        if(field!=TIKU_MSG_T_MESSAGE)continue;
        for(j=0;j<count;j++){
            tiku_msg_t *child=tiku_msg_find_message(message,name,j);int ok=child && nested(child,selected,depth+1);
            tiku_msg_free(child);if(!ok)return 0;
        }
    }
    return 1;
}

tiku_resource_error_t tiku_resource_message_limits(const tiku_msg_t *message,const tiku_resource_protocol_limits_t *selected,unsigned ancestors)
{
    size_t size=tiku_msg_flat_size(message);void *bytes;tiku_resource_error_t error;
    if(!tiku_resource_protocol_limits_valid(selected))return TIKU_RESOURCE_INVALID;
    if(!size || size>selected->frame_bytes)return TIKU_RESOURCE_LIMIT;
    bytes=malloc(size);if(!bytes)return TIKU_RESOURCE_NO_MEMORY;
    error=tiku_msg_flatten(message,bytes,size,NULL)?tiku_resource_message_check_nested(bytes,size,ancestors):TIKU_RESOURCE_INVALID;
    free(bytes);if(error)return error;
    return nested(message,selected,ancestors+1)?TIKU_RESOURCE_OK:TIKU_RESOURCE_LIMIT;
}
