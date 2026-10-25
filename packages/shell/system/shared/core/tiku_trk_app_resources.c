/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_resources.c - installed catalog actions and complete observation streams.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_app_resources.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_stream.h"
#include <stdlib.h>
#include <string.h>
#define CATALOG_BYTES (4u*1024u*1024u)
static const char *const tables[]={"manifests","capabilities","types","associations"};
struct tiku_trk_app_resources {
    tiku_resource_registry_t *registry;
    tiku_resource_stamp_t stamp;
    const tiku_trk_apps_t *apps;
    int (*count)(void *,const char *);
    tiku_msg_t *(*capture)(void *,const char *,int);
    tiku_resource_error_t (*run)(void *,const tiku_trk_app_t *,const char *,tiku_msg_t **);
    void *context;
    char *streams[4];size_t lengths[4];int ready[4];uint64_t next_poll;
};
/** @brief Find an exact installed manifest action; display names are not selectors. */
static const tiku_trk_app_t *target(tiku_trk_app_resources_t *owner,const char *path,int *open)
{
    char expected[TIKU_RESOURCE_PATH_MAX];
    for(int i=0;i<owner->apps->count;i++) {
        const tiku_trk_app_t *app=&owner->apps->app[i];if(!app->id[0]||app->is_tracker)continue;
        for(int mode=0;mode<2;mode++) {
            snprintf(expected,sizeof expected,"/apps/catalog/%s/actions/%s",app->id,mode?"open":"launch");
            if(!strcmp(path,expected)){*open=mode;return app;}
        }
    }
    return NULL;
}
/** @brief Copy a single bounded absolute document path from an exact argument object. */
static int document(const tiku_msg_t *value,char *out,size_t capacity)
{
    tiku_msg_t *member=trk_value_find(value,"path");const char *type=tiku_msg_find_string(member,"type",0),*path=tiku_msg_find_string(member,"value",0);
    int valid=type&&!strcmp(type,"text")&&path&&path[0]=='/'&&strlen(path)<capacity&&tiku_msg_count(value,"member")==1;
    if(valid)snprintf(out,capacity,"%s",path);
    tiku_msg_free(member);return valid;
}
/** @brief Validate catalog membership and supported launch arguments. */
static tiku_resource_error_t validate(void *opaque,const char *path,const tiku_msg_t *value,int invoke)
{
    tiku_trk_app_resources_t *owner=opaque;int open=0;char file[TIKU_PATH_MAX];const char *type=tiku_msg_find_string(value,"type",0);
    const tiku_trk_app_t *app=target(owner,path,&open);
    if(!invoke||!app)return TIKU_RESOURCE_UNSUPPORTED;
    if(!type||strcmp(type,"object"))return TIKU_RESOURCE_VALUE;
    if(open && !app->ntype && !app->takes_anything)return TIKU_RESOURCE_UNSUPPORTED;
    if(open)return document(value,file,sizeof file)?0:TIKU_RESOURCE_VALUE;
    return tiku_msg_count(value,"member")?TIKU_RESOURCE_VALUE:0;
}
/** @brief Dispatch through the existing launch owner; exit reports remain asynchronous. */
static tiku_resource_error_t start(void *opaque,const char *job,const char *path,const tiku_msg_t *value,int invoke,tiku_resource_snapshot_t *out)
{
    tiku_trk_app_resources_t *owner=opaque;tiku_resource_error_t error=validate(opaque,path,value,invoke);
    int open=0;char file[TIKU_PATH_MAX];tiku_msg_t *result=NULL;const tiku_trk_app_t *app;(void)job;
    if(error)return error;
    app=target(owner,path,&open);if(open)document(value,file,sizeof file);
    error=owner->run(owner->context,app,open?file:NULL,&result);
    if(error){tiku_msg_free(result);return error;}
    return trk_value_snapshot(out,result,"launch_requested")?TIKU_RESOURCE_UNKNOWN_OUTCOME:0;
}
/** @brief Render one bounded complete NDJSON stream from native catalog rows. */
static int render(tiku_trk_app_resources_t *owner,const char *table,char **out,size_t *length)
{
    int count=owner->count(owner->context,table);size_t capacity=4096,used=0;char *bytes=malloc(capacity),*row=malloc(TIKU_RESOURCE_FRAME_MAX);
    if(count<0||!bytes||!row){free(bytes);free(row);return -1;}
    for(int i=0;i<count;i++) {
        tiku_msg_t *value=owner->capture(owner->context,table,i);const char *type=tiku_msg_find_string(value,"type",0);
        int size=type&&!strcmp(type,"object")?tiku_resource_value_render(value,row,TIKU_RESOURCE_FRAME_MAX):-1;tiku_msg_free(value);
        if(size<0||(size_t)size>CATALOG_BYTES-used){free(bytes);free(row);return -1;}
        if(used+(size_t)size>capacity){size_t next=capacity*2;if(next<used+(size_t)size)next=used+(size_t)size;char *grown=realloc(bytes,next);if(!grown){free(bytes);free(row);return -1;}bytes=grown;capacity=next;}
        memcpy(bytes+used,row,(size_t)size);used+=(size_t)size;
    }
    free(row);*out=bytes;*length=used;return 0;
}
/** @brief Mark a retained stream unavailable without publishing partial new rows. */
static void unavailable(tiku_trk_app_resources_t *owner,const char *path)
{
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63};tiku_resource_snapshot_t old={0};strcpy(caller.principal,"00000000000000000000000000000001");
    if(!tiku_resource_read(owner->registry,&caller,path,&old)){strcpy(old.apply_state,"last_good_unavailable");trk_value_publish(owner->registry,path,&old);}
    tiku_resource_snapshot_clear(&old);
}
void tiku_trk_app_resources_poll(tiku_trk_app_resources_t *owner,uint64_t now)
{
    if(!owner||now<owner->next_poll)return;
    owner->next_poll=now+250;
    for(int i=0;i<4;i++) {
        char path[96],*bytes=NULL;size_t length=0;tiku_resource_caller_t caller={.authenticated=1,.scopes=63};tiku_resource_snapshot_t current={0};
        snprintf(path,sizeof path,"/apps/catalog/%s",tables[i]);strcpy(caller.principal,"00000000000000000000000000000001");
        if(render(owner,tables[i],&bytes,&length)){unavailable(owner,path);continue;}
        if(!tiku_resource_read(owner->registry,&caller,path,&current)) {
            if(!owner->ready[i]||owner->lengths[i]!=length||(length&&memcmp(owner->streams[i],bytes,length))) {
                if(!tiku_resource_stream_publish(owner->registry,&current.stamp,path,bytes,length,&owner->stamp)) {
                    free(owner->streams[i]);owner->streams[i]=bytes;bytes=NULL;owner->lengths[i]=length;owner->ready[i]=1;
                } else unavailable(owner,path);
            } else {strcpy(current.apply_state,"applied");trk_value_publish(owner->registry,path,&current);}
        }
        tiku_resource_snapshot_clear(&current);free(bytes);
    }
}
tiku_trk_app_resources_t *tiku_trk_app_resources_open(tiku_resource_registry_t *registry,const tiku_trk_apps_t *apps,
    int (*count)(void *,const char *),tiku_msg_t *(*capture)(void *,const char *,int),
    tiku_resource_error_t (*run)(void *,const tiku_trk_app_t *,const char *,tiku_msg_t **),void *context)
{
    tiku_trk_app_resources_t *owner;tiku_resource_initial_t *initial;char (*paths)[TIKU_RESOURCE_PATH_MAX];int n=4,error=0;
    tiku_resource_async_controller_t controller={.validate=validate,.start=start};
    if(!registry||!apps||apps->count<0||apps->count>TIKU_TRK_APP_MAX||!count||!capture||!run)return NULL;
    owner=calloc(1,sizeof *owner);initial=calloc(4+2*TIKU_TRK_APP_MAX,sizeof *initial);paths=calloc(4+2*TIKU_TRK_APP_MAX,sizeof *paths);
    if(!owner||!initial||!paths){free(owner);free(initial);free(paths);return NULL;}
    owner->registry=registry;owner->apps=apps;owner->count=count;owner->capture=capture;owner->run=run;owner->context=context;
    for(int i=0;i<4;i++) {
        snprintf(paths[i],sizeof paths[i],"/apps/catalog/%s",tables[i]);
        initial[i].spec=(tiku_resource_spec_t){.path=paths[i],.kind=TIKU_RESOURCE_STREAM,.schema_version=1,.schema={.type=TIKU_RESOURCE_OBJECT},.read_scopes=TIKU_RESOURCE_READ,.sensitive=1,.persistence="none",.controller="installed-applications"};
        if(trk_value_snapshot(&initial[i].initial,tiku_resource_stream_metadata(0,""),"loading"))error=-1;
    }
    for(int i=0;i<apps->count;i++)if(apps->app[i].id[0]&&!apps->app[i].is_tracker)for(int mode=0;mode<2;mode++) {
        snprintf(paths[n],sizeof paths[n],"/apps/catalog/%s/actions/%s",apps->app[i].id,mode?"open":"launch");
        initial[n].spec=(tiku_resource_spec_t){.path=paths[n],.kind=TIKU_RESOURCE_ACTION,.schema_version=1,.schema={.type=TIKU_RESOURCE_OBJECT},.read_scopes=TIKU_RESOURCE_READ,.change_scopes=TIKU_RESOURCE_INVOKE|TIKU_RESOURCE_CROSS_APP,.sensitive=1,.persistence="none",.controller="installed-applications"};
        if(trk_value_snapshot(&initial[n].initial,trk_value_container("object"),"observed"))error=-1;
        n++;
    }
    if(!error)error=tiku_resource_register_async(registry,"installed-applications","/apps/catalog",NULL,initial,(size_t)n,&controller,owner,&owner->stamp);
    for(int i=0;i<n;i++)tiku_resource_snapshot_clear(&initial[i].initial);
    free(initial);free(paths);
    if(error){free(owner);return NULL;}tiku_trk_app_resources_poll(owner,1);return owner;
}
void tiku_trk_app_resources_close(tiku_trk_app_resources_t *owner)
{
    if(!owner)return;
    tiku_resource_unregister(owner->registry,&owner->stamp);
    for(int i=0;i<4;i++)free(owner->streams[i]);
    free(owner);
}
