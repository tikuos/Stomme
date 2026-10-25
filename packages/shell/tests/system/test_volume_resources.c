/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_volume_resources.c - observed mount replacement and native policy checks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_volume_resources.h"
#include "tiku_trk_resource_arguments.h"
#include "tiku_resource_jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char fixture_path[]="/tmp/stomme-volume-XXXXXX";
static dev_t fixture_device;
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int requests;
/** @brief Accept a queued native fixture request without unmounting a filesystem. */
static int request(void *context,const tiku_volume_t *volume,char *why,size_t capacity)
{
    (void)context;CHECK(volume->dev==fixture_device&&!strcmp(volume->mount,fixture_path));if(capacity)why[0]=0;requests++;return 1;
}
/** @brief Resolve a generated identity through its public native mount record. */
static void find(tiku_resource_registry_t *registry,const tiku_resource_caller_t *caller,const char *mount,char path[600])
{
    tiku_resource_entry_t rows[64];size_t count;uint64_t epoch=0;CHECK(!tiku_resource_list(registry,caller,"/volumes",0,&epoch,rows,64,&count));
    for(size_t i=0;i<count;i++)if(strcmp(rows[i].path,"/volumes/status")) {
        char value[600],text[512];tiku_resource_snapshot_t snapshot={0};snprintf(value,sizeof value,"%s/mounts",rows[i].path);
        CHECK(!tiku_resource_read(registry,caller,value,&snapshot));CHECK(trk_arguments_text(snapshot.applied,"path",text,sizeof text));tiku_resource_snapshot_clear(&snapshot);
        if(!strcmp(text,mount)){snprintf(path,600,"%s",rows[i].path);return;}
    }
    CHECK(0);
}
int main(void)
{
    struct stat native;CHECK(mkdtemp(fixture_path)&&!stat(fixture_path,&native));fixture_device=native.st_dev;
    tiku_volumes_t volumes={.n=2};strcpy(volumes.v[0].mount,"/");volumes.v[0].dev=1;volumes.v[0].is_boot=1;
    strcpy(volumes.v[1].mount,fixture_path);strcpy(volumes.v[1].source,"fixture-device");strcpy(volumes.v[1].fstype,"fixturefs");volumes.v[1].dev=fixture_device;volumes.v[1].removable=1;
    tiku_volumes_cache_set(&volumes,2);tiku_resource_registry_t *registry=tiku_resource_registry_new(NULL);tiku_resource_caller_t caller={.authenticated=1,.scopes=63};strcpy(caller.principal,"00000000000000000000000000000001");
    tiku_trk_volume_resources_t *owner=tiku_trk_volume_resources_open(registry,request,NULL);CHECK(owner);
    char root[600],base[600],path[700],old[600],id[33];find(registry,&caller,"/",root);find(registry,&caller,fixture_path,base);
    tiku_resource_snapshot_t snapshot={0};tiku_resource_job_info_t job={0},status={0};tiku_msg_t *empty=trk_value_container("object");
    CHECK(!rmdir(fixture_path));tiku_trk_volume_resources_poll(owner,500);
    snprintf(path,sizeof path,"%s/state",base);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot));
    tiku_msg_t *allowed=trk_value_find(snapshot.applied,"unmount_supported");int policy;
    CHECK(tiku_msg_find_bool(allowed,"value",0,&policy)&&policy==1);tiku_msg_free(allowed);tiku_resource_snapshot_clear(&snapshot);
    CHECK(!mkdir(fixture_path,0700));
    snprintf(path,sizeof path,"%s/actions/unmount",root);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot)&&!tiku_resource_new_id(id));
    CHECK(tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,id,empty,1,3000,&job)==TIKU_RESOURCE_UNSUPPORTED);tiku_resource_snapshot_clear(&snapshot);
    snprintf(path,sizeof path,"%s/actions/eject",base);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot)&&!tiku_resource_new_id(id));
    CHECK(tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,id,empty,1,3000,&job)==TIKU_RESOURCE_UNSUPPORTED);tiku_resource_snapshot_clear(&snapshot);
    snprintf(path,sizeof path,"%s/actions/unmount",base);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot)&&!tiku_resource_new_id(id));
    CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,id,empty,1,3000,&job));
    volumes.v[1].dev=fixture_device+1;tiku_volumes_cache_set(&volumes,2);CHECK(tiku_resource_jobs_pump(registry)>=0&&!tiku_resource_job_status(registry,&caller,job.id,&status)&&status.error==TIKU_RESOURCE_OWNER_LOST&&!requests);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&snapshot);strcpy(old,base);
    tiku_trk_volume_resources_poll(owner,1000);CHECK(tiku_resource_read(registry,&caller,path,&snapshot)==TIKU_RESOURCE_NOT_FOUND);
    find(registry,&caller,fixture_path,base);CHECK(strcmp(old,base));volumes.v[1].dev=fixture_device;tiku_volumes_cache_set(&volumes,2);tiku_trk_volume_resources_poll(owner,2000);find(registry,&caller,fixture_path,base);
    snprintf(path,sizeof path,"%s/actions/unmount",base);CHECK(!tiku_resource_read(registry,&caller,path,&snapshot)&&!tiku_resource_new_id(id));
    CHECK(!tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,id,empty,1,3000,&job));CHECK(tiku_resource_jobs_pump(registry)==1&&requests==1);
    CHECK(!tiku_resource_job_status(registry,&caller,job.id,&status)&&status.state==TIKU_RESOURCE_JOB_SUCCEEDED);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_resource_snapshot_clear(&snapshot);tiku_msg_free(empty);tiku_trk_volume_resources_close(owner);tiku_resource_registry_free(registry);
    tiku_resource_limits_t limits={.providers=2};registry=tiku_resource_registry_new(&limits);owner=tiku_trk_volume_resources_open(registry,request,NULL);CHECK(owner);
    tiku_trk_volume_resources_poll(owner,1000);CHECK(!tiku_resource_read(registry,&caller,"/volumes/status",&snapshot));int64_t registered;CHECK(trk_arguments_integer(snapshot.applied,"registered",0,32,&registered)&&registered==1);
    tiku_resource_snapshot_clear(&snapshot);tiku_trk_volume_resources_close(owner);tiku_resource_registry_free(registry);
    CHECK(!rmdir(fixture_path));puts("PASS volume resources: native policy, observed replacement, owner dispatch and capacity feedback");return 0;
}
