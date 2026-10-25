/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_lifecycle_resources.c - copied lifetimes and native lifecycle dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_lifecycle_resources.h"
#include "tiku_trk_resource_values.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int generation=1,calls;
/** @brief Offer one current native instance for each family. */
static int enumerate(void *context,tiku_trk_lifecycle_kind_t kind,int index,char *out,size_t capacity)
{
    (void)context;if(index)return -1;snprintf(out,capacity,"native-%d-%d",kind,generation);return 0;
}
/** @brief Copy an observed object from the fixture owner. */
static tiku_msg_t *capture(void *context,tiku_trk_lifecycle_kind_t kind,const char *token,const char *field)
{
    tiku_msg_t *out=trk_value_container("object");(void)context;(void)kind;
    if(trk_value_member(out,"token",tiku_resource_value_string(TIKU_RESOURCE_TEXT,token))||trk_value_member(out,"field",tiku_resource_value_string(TIKU_RESOURCE_TEXT,field))){tiku_msg_free(out);return NULL;}return out;
}
/** @brief Reject old generations and unsupported work-pool cancellation. */
static tiku_resource_error_t validate(void *context,tiku_trk_lifecycle_kind_t kind,const char *token,const char *action,const tiku_msg_t *value)
{
    char expected[96];(void)context;(void)action;(void)value;snprintf(expected,sizeof expected,"native-%d-%d",kind,generation);
    if(strcmp(token,expected))return TIKU_RESOURCE_OWNER_LOST;
    return kind==TIKU_TRK_LIFECYCLE_WORK?TIKU_RESOURCE_UNSUPPORTED:0;
}
/** @brief Count owner dispatches and return its copied native result. */
static tiku_resource_error_t perform(void *context,tiku_trk_lifecycle_kind_t kind,const char *token,const char *action,const tiku_msg_t *value,tiku_msg_t **out)
{
    tiku_resource_error_t error=validate(context,kind,token,action,value);if(error)return error;calls++;*out=capture(context,kind,token,action);return *out?0:TIKU_RESOURCE_NO_MEMORY;
}
int main(void)
{
    tiku_resource_limits_t limits={.providers=16,.resources=256};tiku_resource_registry_t *registry=tiku_resource_registry_new(&limits);
    tiku_trk_lifecycle_host_t host={.enumerate=enumerate,.capture=capture,.validate=validate,.perform=perform};
    tiku_trk_lifecycle_resources_t *owner=tiku_trk_lifecycle_resources_open(registry,&host,NULL);
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63},limited;tiku_resource_entry_t entries[8];uint64_t epoch=0;size_t count=0;
    tiku_resource_snapshot_t snapshot={0};tiku_resource_job_info_t job={0},status={0};tiku_msg_t *empty=trk_value_container("object");char path[600],old[600],request[33];
    strcpy(caller.principal,"00000000000000000000000000000001");CHECK(owner);
    CHECK(!tiku_resource_list(registry,&caller,"/apps",0,&epoch,entries,8,&count)&&count==1);snprintf(old,sizeof old,"%s",entries[0].path);
    snprintf(path,sizeof path,"%s/actions/activate",entries[0].path);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot));CHECK(!tiku_resource_new_id(request));
    limited=caller;limited.scopes&=~TIKU_RESOURCE_HOST_CONTROL;
    CHECK(tiku_resource_job_submit(registry,&limited,"00000000000000000000000000000002",path,&snapshot.stamp,request,empty,1,3000,&job)==TIKU_RESOURCE_DENIED);
    CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,request,empty,1,3000,&job));
    generation++;CHECK(tiku_resource_jobs_pump(registry)>=0);CHECK(!tiku_resource_job_status(registry,&caller,job.id,&status));
    CHECK(status.error==TIKU_RESOURCE_OWNER_LOST&&calls==0);tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&snapshot);
    tiku_trk_lifecycle_resources_poll(owner);CHECK(tiku_resource_read(registry,&caller,path,&snapshot)==TIKU_RESOURCE_NOT_FOUND);
    epoch=0;CHECK(!tiku_resource_list(registry,&caller,"/apps",0,&epoch,entries,8,&count)&&count==1&&strcmp(old,entries[0].path));
    snprintf(path,sizeof path,"%s/actions/request-quit",entries[0].path);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot));CHECK(!tiku_resource_new_id(request));
    CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,request,empty,1,3000,&job));
    CHECK(tiku_resource_jobs_pump(registry)==1);CHECK(!tiku_resource_job_status(registry,&caller,job.id,&status));CHECK(status.state==TIKU_RESOURCE_JOB_SUCCEEDED&&calls==1);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&snapshot);
    int expected,registered;tiku_trk_lifecycle_resources_counts(owner,&expected,&registered);CHECK(expected==3&&registered==3);
    tiku_trk_lifecycle_resources_close(owner);tiku_resource_registry_free(registry);tiku_msg_free(empty);
    limits.providers=1;registry=tiku_resource_registry_new(&limits);owner=tiku_trk_lifecycle_resources_open(registry,&host,NULL);CHECK(owner);
    tiku_trk_lifecycle_resources_counts(owner,&expected,&registered);CHECK(expected==3&&registered==1);
    tiku_trk_lifecycle_resources_close(owner);tiku_resource_registry_free(registry);
    puts("PASS lifecycle resources: owner dispatch, generation replacement, authority and discovery capacity");return 0;
}
