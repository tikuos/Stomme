/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_lifecycle_resources.c - lifetime-checked app and background-job providers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_lifecycle_resources.h"
#include "tiku_trk_resource_values.h"
#include <stdlib.h>
#include <string.h>
#define ENTITY_MAX 193
static const char *const app_fields[]={"identity","manifest","capabilities","windows","state"};
static const char *const job_fields[]={"identity","owner","state","progress","cancellation","error","result"};
/** @brief One copied native token and its resource-provider incarnation. */
typedef struct {
    struct tiku_trk_lifecycle_resources *owner;
    tiku_trk_lifecycle_kind_t kind;
    char token[96],id[33],base[96];
    tiku_resource_stamp_t stamp;
} entity_t;
struct tiku_trk_lifecycle_resources {
    tiku_resource_registry_t *registry;
    tiku_trk_lifecycle_host_t host;
    void *context;
    int expected,registered;
    entity_t entries[ENTITY_MAX];
};
/** @brief Find a declared action suffix beneath this incarnation. */
static const char *action(entity_t *entry,const char *path)
{
    char expected[160];const char *names[]={"activate","request-quit","cancel"};int begin=entry->kind==TIKU_TRK_LIFECYCLE_APP?0:2,end=begin?3:2;
    for(int i=begin;i<end;i++){snprintf(expected,sizeof expected,"%s/actions/%s",entry->base,names[i]);if(!strcmp(path,expected))return names[i];}
    return NULL;
}
/** @brief Validate the current native target and exact action arguments. */
static tiku_resource_error_t validate(void *opaque,const char *path,const tiku_msg_t *value,int invoke)
{
    entity_t *entry=opaque;const char *name=action(entry,path),*type=tiku_msg_find_string(value,"type",0);
    if(!invoke||!name)return TIKU_RESOURCE_UNSUPPORTED;
    if(!type||strcmp(type,"object")||tiku_msg_count(value,"member"))return TIKU_RESOURCE_VALUE;
    return entry->owner->host.validate(entry->owner->context,entry->kind,entry->token,name,value);
}
/** @brief Dispatch through the native owner and transfer a copied result. */
static tiku_resource_error_t start(void *opaque,const char *job,const char *path,const tiku_msg_t *value,int invoke,tiku_resource_snapshot_t *out)
{
    entity_t *entry=opaque;tiku_msg_t *result=NULL;tiku_resource_error_t error=validate(opaque,path,value,invoke);(void)job;
    if(error)return error;
    error=entry->owner->host.perform(entry->owner->context,entry->kind,entry->token,action(entry,path),value,&result);
    if(error){tiku_msg_free(result);return error;}
    if(trk_value_snapshot(out,result,"request_dispatched"))return TIKU_RESOURCE_UNKNOWN_OUTCOME;
    return 0;
}
/** @brief Retire one provider before its native token can be replaced. */
static void retire(entity_t *entry)
{
    tiku_resource_unregister(entry->owner->registry,&entry->stamp);memset(entry,0,sizeof *entry);
}
/** @brief Register one app instance or one native job with its complete schema. */
static int install(tiku_trk_lifecycle_resources_t *owner,entity_t *entry,tiku_trk_lifecycle_kind_t kind,const char *token)
{
    const char *const *fields=kind==TIKU_TRK_LIFECYCLE_APP?app_fields:job_fields;
    int nfields=kind==TIKU_TRK_LIFECYCLE_APP?5:7,nactions=kind==TIKU_TRK_LIFECYCLE_APP?2:1,error=0;
    tiku_resource_initial_t initial[9]={0};char paths[9][160],key[64];
    tiku_resource_async_controller_t controller={.validate=validate,.start=start};
    entry->owner=owner;entry->kind=kind;memcpy(entry->token,token,strlen(token)+1);
    if(tiku_resource_new_id(entry->id))goto fail;
    char id[33];memcpy(id,entry->id,sizeof id);
    snprintf(entry->base,sizeof entry->base,"/%s/%s",kind==TIKU_TRK_LIFECYCLE_APP?"apps":"jobs",id);
    snprintf(key,sizeof key,"lifecycle-%s",entry->id);
    for(int i=0;i<nfields+nactions;i++) {
        int is_action=i>=nfields;const char *name=is_action?(kind==TIKU_TRK_LIFECYCLE_APP?(i==nfields?"activate":"request-quit"):"cancel"):fields[i];
        snprintf(paths[i],sizeof paths[i],is_action?"%s/actions/%s":"%s/%s",entry->base,name);
        initial[i].spec=(tiku_resource_spec_t){.path=paths[i],.kind=is_action?TIKU_RESOURCE_ACTION:TIKU_RESOURCE_STATE,.schema_version=1,
            .schema={.type=TIKU_RESOURCE_OBJECT},.read_scopes=TIKU_RESOURCE_READ,.change_scopes=is_action?TIKU_RESOURCE_INVOKE|TIKU_RESOURCE_CROSS_APP:0,
            .sensitive=1,.persistence="none",.controller="native-lifecycle"};
        if(trk_value_snapshot(&initial[i].initial,is_action?trk_value_container("object"):owner->host.capture(owner->context,kind,token,name),"observed")){error=-1;break;}
    }
    if(!error)error=tiku_resource_register_async(owner->registry,key,entry->base,NULL,initial,(size_t)(nfields+nactions),&controller,entry,&entry->stamp);
    for(int i=0;i<9;i++)tiku_resource_snapshot_clear(&initial[i].initial);
    if(!error)return 0;
fail:memset(entry,0,sizeof *entry);return -1;
}
/** @brief Publish a complete value or retain prior content as unavailable. */
static void publish(entity_t *entry,const char *field)
{
    char path[160];tiku_resource_snapshot_t next={0};snprintf(path,sizeof path,"%s/%s",entry->base,field);
    if(trk_value_snapshot(&next,entry->owner->host.capture(entry->owner->context,entry->kind,entry->token,field),"observed") || trk_value_publish(entry->owner->registry,path,&next)) {
        tiku_resource_caller_t caller={.authenticated=1,.scopes=63};tiku_resource_snapshot_t old={0};strcpy(caller.principal,"00000000000000000000000000000001");
        if(!tiku_resource_read(entry->owner->registry,&caller,path,&old)){strcpy(old.apply_state,"last_good_unavailable");trk_value_publish(entry->owner->registry,path,&old);}
        tiku_resource_snapshot_clear(&old);
    }
    tiku_resource_snapshot_clear(&next);
}
void tiku_trk_lifecycle_resources_poll(tiku_trk_lifecycle_resources_t *owner)
{
    char tokens[ENTITY_MAX][96];tiku_trk_lifecycle_kind_t kinds[ENTITY_MAX];int count=0;
    if(!owner)return;
    for(int kind=0;kind<3;kind++)for(int i=0;count<ENTITY_MAX;i++) {
        if(owner->host.enumerate(owner->context,(tiku_trk_lifecycle_kind_t)kind,i,tokens[count],sizeof tokens[count]))break;
        if(!memchr(tokens[count],0,sizeof tokens[count])||!tokens[count][0])break;
        kinds[count++]=(tiku_trk_lifecycle_kind_t)kind;
    }
    for(int i=0;i<ENTITY_MAX;i++)if(owner->entries[i].token[0]) {
        entity_t *entry=&owner->entries[i];int j;
        for(j=0;j<count;j++)if(entry->kind==kinds[j]&&!strcmp(entry->token,tokens[j]))break;
        if(j==count)retire(entry);
    }
    for(int i=0;i<count;i++) {
        int j;for(j=0;j<ENTITY_MAX;j++)if(owner->entries[j].token[0]&&owner->entries[j].kind==kinds[i]&&!strcmp(owner->entries[j].token,tokens[i]))break;
        if(j<ENTITY_MAX)continue;
        for(j=0;j<ENTITY_MAX&&owner->entries[j].token[0];j++){}
        if(j<ENTITY_MAX)install(owner,&owner->entries[j],kinds[i],tokens[i]);
    }
    owner->expected=count;owner->registered=0;
    for(int i=0;i<ENTITY_MAX;i++)if(owner->entries[i].token[0]) {
        owner->registered++;
        entity_t *entry=&owner->entries[i];const char *const *fields=entry->kind==TIKU_TRK_LIFECYCLE_APP?app_fields:job_fields;
        for(int j=0;j<(entry->kind==TIKU_TRK_LIFECYCLE_APP?5:7);j++)publish(entry,fields[j]);
    }
}
tiku_trk_lifecycle_resources_t *tiku_trk_lifecycle_resources_open(tiku_resource_registry_t *registry,const tiku_trk_lifecycle_host_t *host,void *context)
{
    tiku_trk_lifecycle_resources_t *owner;
    if(!registry||!host||!host->enumerate||!host->capture||!host->validate||!host->perform)return NULL;
    owner=calloc(1,sizeof *owner);if(!owner)return NULL;owner->registry=registry;owner->host=*host;owner->context=context;
    tiku_trk_lifecycle_resources_poll(owner);return owner;
}
void tiku_trk_lifecycle_resources_close(tiku_trk_lifecycle_resources_t *owner)
{
    if(!owner)return;
    for(int i=0;i<ENTITY_MAX;i++)if(owner->entries[i].token[0])retire(&owner->entries[i]);
    free(owner);
}

void tiku_trk_lifecycle_resources_counts(const tiku_trk_lifecycle_resources_t *owner,int *expected,int *registered)
{
    if(expected)*expected=owner?owner->expected:0;
    if(registered)*registered=owner?owner->registered:0;
}
