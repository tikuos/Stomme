/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_controlled_observer.c - native action results, scopes and deadlines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_observer.h"
#include "tiku_trk_resource_arguments.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int calls;static uint64_t now=100;
/** @brief Supply an isolated owner-thread monotonic clock. */
static uint64_t clock_now(void *context){(void)context;return now;}
/** @brief Offer no stream rows. */
static int count(void *context,const char *path){(void)context;(void)path;return 0;}
/** @brief Copy an observed owner counter. */
static tiku_msg_t *capture(void *context,const char *path,int index)
{
    (void)context;(void)path;(void)index;tiku_msg_t *out=trk_value_container("object");if(TRK_INT(out,"calls",calls)){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Require an exact empty object. */
static tiku_resource_error_t validate(void *context,const char *path,const tiku_msg_t *value,int invoke)
{
    (void)context;(void)path;return invoke&&trk_arguments_keys(value,NULL,0)?0:TIKU_RESOURCE_VALUE;
}
/** @brief Return the owner result while retaining its observed state. */
static tiku_resource_error_t start(void *context,const char *job,const char *path,const tiku_msg_t *value,int invoke,tiku_resource_snapshot_t *out)
{
    (void)job;(void)value;(void)invoke;calls++;return trk_value_snapshot(out,capture(context,path,-1),"completed")?TIKU_RESOURCE_NO_MEMORY:0;
}
int main(void)
{
    tiku_resource_limits_t limits={.providers=512};tiku_resource_registry_t *registry=tiku_resource_registry_new(&limits);CHECK(registry);
    limits.providers=513;CHECK(!tiku_resource_registry_new(&limits));CHECK(!tiku_resource_registry_clock(registry,clock_now,NULL));
    const tiku_trk_observation_t entries[]={{"/fixture/state",0,3},{"/fixture/actions/run",2,3}};
    CHECK(!tiku_trk_resource_observer_open(registry,"fixture","/fixture",entries,2,count,capture,NULL));
    const tiku_resource_async_controller_t controller={.validate=validate,.start=start};
    tiku_trk_resource_observer_t *owner=tiku_trk_resource_observer_open_controlled(registry,"fixture","/fixture",entries,2,count,capture,&controller,NULL);CHECK(owner);
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63},limited;strcpy(caller.principal,"00000000000000000000000000000001");
    tiku_resource_snapshot_t before={0},after={0};tiku_resource_job_info_t job={0},status={0};tiku_msg_t *empty=trk_value_container("object");char request[33];
    limited=caller;limited.scopes&=~TIKU_RESOURCE_CROSS_APP;CHECK(tiku_resource_read(registry,&limited,"/fixture/state",&before)==TIKU_RESOURCE_NOT_FOUND);
    CHECK(!tiku_resource_read(registry,&caller,"/fixture/actions/run",&before));CHECK(!tiku_resource_new_id(request));
    limited=caller;limited.scopes&=~TIKU_RESOURCE_HOST_CONTROL;CHECK(tiku_resource_job_submit(registry,&limited,"00000000000000000000000000000002","/fixture/actions/run",&before.stamp,request,empty,1,100,&job)==TIKU_RESOURCE_DENIED);
    CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002","/fixture/actions/run",&before.stamp,request,empty,1,100,&job));
    CHECK(tiku_resource_jobs_pump(registry)==1&&!tiku_resource_job_status(registry,&caller,job.id,&status)&&status.state==TIKU_RESOURCE_JOB_SUCCEEDED&&calls==1);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&before);
    CHECK(!tiku_resource_read(registry,&caller,"/fixture/actions/run",&before));now=1000;tiku_trk_resource_observer_poll(owner,now);CHECK(!tiku_resource_read(registry,&caller,"/fixture/actions/run",&after));
    CHECK(tiku_resource_value_equal(before.applied,after.applied)&&!strcmp(after.apply_state,"completed"));tiku_resource_snapshot_clear(&after);
    tiku_resource_snapshot_clear(&before);CHECK(!tiku_resource_read(registry,&caller,"/fixture/actions/run",&before));
    CHECK(!tiku_resource_new_id(request));CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002","/fixture/actions/run",&before.stamp,request,empty,1,10,&job));
    now+=11;CHECK(tiku_resource_jobs_pump(registry)>=0&&!tiku_resource_job_status(registry,&caller,job.id,&status)&&status.error==TIKU_RESOURCE_DEADLINE&&calls==1);
    tiku_resource_registry_observation_t stats;CHECK(tiku_resource_registry_observe(registry,&stats)&&stats.providers==1&&stats.provider_capacity==512&&!stats.active_jobs);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&before);tiku_msg_free(empty);tiku_trk_resource_observer_close(owner);
    CHECK(tiku_resource_registry_observe(registry,&stats)&&!stats.providers);tiku_resource_registry_free(registry);
    puts("PASS controlled observations: exact authority, retained action results, queue expiry and capacity");return 0;
}
