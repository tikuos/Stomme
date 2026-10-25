/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_app_resources.c - installed identity routing and complete catalog streams.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_app_resources.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int calls,broken;
/** @brief Offer a catalog larger than a preview page. */
static int count(void *context,const char *table){(void)context;(void)table;return 240;}
/** @brief Copy one row and permit a controlled capture failure. */
static tiku_msg_t *capture(void *context,const char *table,int row)
{
    tiku_msg_t *out=trk_value_container("object");(void)context;
    if(broken&&row==200){tiku_msg_free(out);return NULL;}
    if(trk_value_member(out,"table",tiku_resource_value_string(TIKU_RESOURCE_TEXT,table))||trk_value_member(out,"row",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,row))){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Require the exact installed app and literal absolute path. */
static tiku_resource_error_t run(void *context,const tiku_trk_app_t *app,const char *path,tiku_msg_t **out)
{
    (void)context;CHECK(!strcmp(app->id,"fixture"));if(path)CHECK(!strcmp(path,"/tmp/space document.txt"));calls++;*out=trk_value_container("object");return *out?0:TIKU_RESOURCE_NO_MEMORY;
}
int main(void)
{
    tiku_trk_apps_t *apps=calloc(1,sizeof *apps);tiku_resource_registry_t *registry=tiku_resource_registry_new(NULL);tiku_resource_caller_t caller={.authenticated=1,.scopes=63};
    tiku_resource_snapshot_t before={0},next={0};tiku_resource_stream_t *stream;size_t bytes;tiku_resource_job_info_t job={0},status={0};char request[33];
    tiku_msg_t *value=trk_value_container("object");CHECK(apps&&registry);apps->count=1;strcpy(apps->app[0].id,"fixture");apps->app[0].ntype=1;
    strcpy(caller.principal,"00000000000000000000000000000001");
    tiku_trk_app_resources_t *owner=tiku_trk_app_resources_open(registry,apps,count,capture,run,NULL);CHECK(owner);
    CHECK(!tiku_resource_read(registry,&caller,"/apps/catalog/manifests",&before));CHECK(!tiku_resource_stream_open(registry,&caller,"/apps/catalog/manifests",&before.stamp,65536,&stream,&bytes)&&bytes>5000);
    tiku_resource_stream_close(stream);broken=1;tiku_trk_app_resources_poll(owner,1000);CHECK(!tiku_resource_read(registry,&caller,"/apps/catalog/manifests",&next));
    CHECK(!strcmp(next.apply_state,"last_good_unavailable")&&tiku_resource_value_equal(before.applied,next.applied));tiku_resource_snapshot_clear(&next);
    broken=0;tiku_trk_app_resources_poll(owner,2000);CHECK(!tiku_resource_read(registry,&caller,"/apps/catalog/manifests",&next));CHECK(!strcmp(next.apply_state,"applied"));tiku_resource_snapshot_clear(&next);tiku_resource_snapshot_clear(&before);
    CHECK(!trk_value_member(value,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,"/tmp/space document.txt")));
    CHECK(!tiku_resource_read(registry,&caller,"/apps/catalog/fixture/actions/open",&before));CHECK(!tiku_resource_new_id(request));
    CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002","/apps/catalog/fixture/actions/open",&before.stamp,request,value,1,3000,&job));
    CHECK(tiku_resource_jobs_pump(registry)==1);CHECK(!tiku_resource_job_status(registry,&caller,job.id,&status));CHECK(status.state==TIKU_RESOURCE_JOB_SUCCEEDED&&calls==1);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&before);
    CHECK(!tiku_resource_read(registry,&caller,"/apps/catalog/fixture/actions/launch",&before));CHECK(!tiku_resource_new_id(request));
    CHECK(tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002","/apps/catalog/fixture/actions/launch",&before.stamp,request,value,1,3000,&job)==TIKU_RESOURCE_VALUE);
    tiku_resource_snapshot_clear(&before);tiku_msg_free(value);tiku_trk_app_resources_close(owner);tiku_resource_registry_free(registry);free(apps);
    puts("PASS application catalog: complete retained streams, literal documents and installed ID dispatch");return 0;
}
