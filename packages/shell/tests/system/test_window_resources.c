/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_window_resources.c - authority, native lifetimes and queued window requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_window_resources.h"
#include "tiku_trk_resource_values.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do {if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static uint64_t connection=1;
static int dispatched, consent;
static tiku_workspace_t *workspace;
static tiku_resource_caller_t caller={.authenticated=1,.scopes=63};
/** @brief Copy a fixture connection and its supported action mask. */
static int source(void *context,tiku_window_t *window,tiku_trk_window_source_t *out)
{
    (void)context;memset(out,0,sizeof *out);strcpy(out->kind,"application");strcpy(out->name,"fixture");
    out->connection=connection;out->generation=window->generation;out->local_id=1;
    out->actions=window->plain?1:(1u<<TIKU_TRK_WINDOW_ACTIONS)-1;return 0;
}
/** @brief Retain a window until its fixture owner consents. */
static int close_request(tiku_window_t *window,void *context){(void)window;(void)context;return consent;}
/** @brief Execute through the native workspace fixture owner. */
static tiku_resource_error_t perform(void *context,tiku_window_t *window,tiku_trk_window_action_t action,int a,int b)
{
    (void)context;dispatched++;
    if(action==TIKU_TRK_WINDOW_CLOSE)tiku_workspace_request_close(workspace,window);
    else if(action==TIKU_TRK_WINDOW_FOCUS)tiku_workspace_activate(workspace,window);
    else if(action==TIKU_TRK_WINDOW_MINIMIZE)tiku_workspace_minimize(workspace,window,(tiku_rect_t){0});
    else if(action==TIKU_TRK_WINDOW_RESTORE)tiku_workspace_restore(workspace,window);
    else if(action==TIKU_TRK_WINDOW_MOVE){window->frame.x=a;window->frame.y=b;}
    else if(action==TIKU_TRK_WINDOW_RESIZE){window->frame.w=a;window->frame.h=b;}
    return 0;
}
/** @brief Copy the first current window path using its public directory listing. */
static void first_window(tiku_resource_registry_t *registry,char *path,size_t capacity)
{
    tiku_resource_entry_t entries[16];size_t count=0;uint64_t epoch=0;
    CHECK(!tiku_resource_list(registry,&caller,"/windows",0,&epoch,entries,16,&count)&&count==1);
    snprintf(path,capacity,"%s",entries[0].path);
}
/** @brief Queue a request with a fresh caller revision and identity. */
static tiku_resource_error_t submit(tiku_resource_registry_t *registry,const tiku_resource_caller_t *who,const char *path,const tiku_msg_t *value,tiku_resource_job_info_t *job)
{
    tiku_resource_snapshot_t snapshot={0};char id[33];tiku_resource_error_t error;
    error=tiku_resource_read(registry,&caller,path,&snapshot);if(error)return error;
    CHECK(!tiku_resource_new_id(id));
    error=tiku_resource_job_submit(registry,who,"00000000000000000000000000000002",path,&snapshot.stamp,id,value,1,30000,job);
    tiku_resource_snapshot_clear(&snapshot);return error;
}
/** @brief Read the current job outcome after dispatch or retirement. */
static tiku_resource_job_info_t status(tiku_resource_registry_t *registry,tiku_resource_job_info_t *job)
{
    tiku_resource_job_info_t result={0};CHECK(!tiku_resource_job_status(registry,&caller,job->id,&result));
    tiku_resource_job_info_clear(job);return result;
}
int main(void)
{
    tiku_resource_limits_t limits={.providers=64,.resources=1024,.events_per_watch=64};
    tiku_resource_registry_t *registry=tiku_resource_registry_new(&limits);
    tiku_surface_t *surface=tiku_surface_new(800,600,0);tiku_window_t *window;
    tiku_trk_window_resources_t *owner;tiku_resource_snapshot_t snapshot={0};
    tiku_resource_job_info_t job={0},result={0};tiku_msg_t *empty=trk_value_container("object"),*pair;
    char base[256],path[300],replacement[256];tiku_resource_caller_t limited;
    tiku_resource_watch_t watch; tiku_msg_t *watched=NULL;tiku_resource_event_t event;int removals=0;
    strcpy(caller.principal,"00000000000000000000000000000001");CHECK(registry&&surface);
    workspace=tiku_workspace_new(surface);CHECK(workspace);
    window=tiku_workspace_open(workspace,"Document",(tiku_rect_t){10,20,300,200},NULL,NULL,NULL);CHECK(window);
    tiku_window_set_handlers(window,NULL,close_request,NULL);
    owner=tiku_trk_window_resources_open(registry,workspace,800,600,source,perform,NULL);CHECK(owner);
    first_window(registry,base,sizeof base);snprintf(path,sizeof path,"%s/actions/focus",base);
    limited=caller;limited.scopes&=~TIKU_RESOURCE_HOST_CONTROL;
    CHECK(submit(registry,&limited,path,empty,&job)==TIKU_RESOURCE_DENIED);
    limited=caller;limited.scopes&=~TIKU_RESOURCE_CROSS_APP;
    CHECK(submit(registry,&limited,path,empty,&job)==TIKU_RESOURCE_DENIED);
    limited=caller;limited.scopes&=~TIKU_RESOURCE_CONTENT;
    CHECK(tiku_resource_read(registry,&limited,path,&snapshot)==TIKU_RESOURCE_NOT_FOUND);
    snprintf(path,sizeof path,"%s/actions/move",base);pair=trk_value_container("object");
    CHECK(!trk_value_member(pair,"x",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,100)));
    CHECK(!trk_value_member(pair,"y",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,120)));
    CHECK(!submit(registry,&caller,path,pair,&job));CHECK(tiku_resource_jobs_pump(registry)==1);
    result=status(registry,&job);CHECK(result.state==TIKU_RESOURCE_JOB_SUCCEEDED&&window->frame.x==100);tiku_resource_job_info_clear(&result);
    CHECK(!trk_value_member(pair,"unknown",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,1)));
    CHECK(submit(registry,&caller,path,pair,&job)==TIKU_RESOURCE_VALUE);tiku_msg_free(pair);
    tiku_trk_window_resources_poll(owner);
    snprintf(path,sizeof path,"%s/actions/request-close",base);
    CHECK(!submit(registry,&caller,path,empty,&job));CHECK(tiku_resource_jobs_pump(registry)==1);
    result=status(registry,&job);CHECK(result.state==TIKU_RESOURCE_JOB_SUCCEEDED&&window->open);tiku_resource_job_info_clear(&result);
    CHECK(!tiku_resource_read(registry,&caller,path,&snapshot));
    CHECK(!tiku_resource_watch_open(registry,&caller,snapshot.stamp.provider,base,&watch,&watched));
    tiku_msg_free(watched);tiku_resource_snapshot_clear(&snapshot);
    CHECK(!submit(registry,&caller,path,empty,&job));
    snprintf(window->title,sizeof window->title,"Edited title");tiku_trk_window_resources_poll(owner);
    CHECK(tiku_resource_jobs_pump(registry)>=0);result=status(registry,&job);
    CHECK(result.error==TIKU_RESOURCE_STALE);tiku_resource_job_info_clear(&result);
    CHECK(!submit(registry,&caller,path,empty,&job));
    connection++;CHECK(tiku_resource_jobs_pump(registry)>=0);
    result=status(registry,&job);CHECK(result.error==TIKU_RESOURCE_OWNER_LOST);tiku_resource_job_info_clear(&result);
    tiku_trk_window_resources_poll(owner);first_window(registry,replacement,sizeof replacement);CHECK(strcmp(base,replacement));
    while(tiku_resource_watch_next(registry,watch,&event)==1)if(event.removed)removals++;
    CHECK(removals>=15);tiku_resource_watch_close(registry,watch);
    CHECK(tiku_resource_read(registry,&caller,path,&snapshot)==TIKU_RESOURCE_NOT_FOUND);
    snprintf(path,sizeof path,"%s/actions/focus",replacement);CHECK(!submit(registry,&caller,path,empty,&job));
    tiku_workspace_close(workspace,window);window=tiku_workspace_open(workspace,"Replacement",(tiku_rect_t){10,20,300,200},NULL,NULL,NULL);CHECK(window);
    CHECK(tiku_resource_jobs_pump(registry)>=0);result=status(registry,&job);CHECK(result.error==TIKU_RESOURCE_OWNER_LOST);tiku_resource_job_info_clear(&result);
    tiku_trk_window_resources_poll(owner);first_window(registry,base,sizeof base);CHECK(strcmp(base,replacement));
    tiku_window_set_plain(window,1);snprintf(path,sizeof path,"%s/actions/resize",base);
    CHECK(submit(registry,&caller,path,empty,&job)==TIKU_RESOURCE_UNSUPPORTED);
    tiku_window_set_plain(window,0);snprintf(path,sizeof path,"%s/actions/focus",base);
    {
        tiku_resource_entry_t entries[16];uint64_t epoch=0;size_t count=0;
        while(tiku_workspace_count(workspace)<TIKU_WINDOW_MAX)CHECK(tiku_workspace_open(workspace,"Additional",(tiku_rect_t){0,0,300,200},NULL,NULL,NULL));
        tiku_trk_window_resources_poll(owner);
        CHECK(!tiku_resource_list(registry,&caller,"/windows",0,&epoch,entries,16,&count)&&count==TIKU_WINDOW_MAX);
    }
    CHECK(!submit(registry,&caller,path,empty,&job));tiku_trk_window_resources_close(owner);
    result=status(registry,&job);CHECK(result.error==TIKU_RESOURCE_OWNER_LOST);tiku_resource_job_info_clear(&result);
    CHECK(dispatched==2);tiku_msg_free(empty);tiku_workspace_free(workspace);tiku_surface_free(surface);tiku_resource_registry_free(registry);
    {
        tiku_resource_limits_t small={.providers=1};tiku_resource_entry_t entries[1];size_t count=0;uint64_t epoch=0;
        registry=tiku_resource_registry_new(&small);surface=tiku_surface_new(800,600,0);workspace=tiku_workspace_new(surface);
        CHECK(tiku_workspace_open(workspace,"Unpublished",(tiku_rect_t){0,0,300,200},NULL,NULL,NULL));
        owner=tiku_trk_window_resources_open(registry,workspace,800,600,source,perform,NULL);CHECK(owner);
        CHECK(!tiku_resource_list(registry,&caller,"/workspaces",0,&epoch,entries,1,&count)&&count==1);
        snprintf(path,sizeof path,"%s/windows",entries[0].path);
        CHECK(!tiku_resource_read(registry,&caller,path,&snapshot));
        CHECK(!strcmp(snapshot.apply_state,"last_good_unavailable"));tiku_resource_snapshot_clear(&snapshot);
        tiku_trk_window_resources_close(owner);tiku_workspace_free(workspace);tiku_surface_free(surface);tiku_resource_registry_free(registry);
    }
    puts("PASS window resources: dispatch, close veto, authority, connection and native slot lifetimes");return 0;
}
