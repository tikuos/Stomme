/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_catalog_settings.c - association save ownership and loaded type discovery.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_settings.h"
#include "tiku_resource_jobs.h"
#include "tiku_state_update.h"
#include "tiku_work.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(test) do { if(!(test)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test);exit(1);} } while(0)
static unsigned serial;
static tiku_resource_caller_t caller={.principal="00000000000000000000000000000001",.authenticated=1,.scopes=63};
/** @brief Collect isolated worker completions and waiting catalog submissions. */
static void drain(tiku_trk_catalog_settings_t *owner)
{
    unsigned i;for(i=0;i<200;i++){tiku_trk_catalog_settings_poll(owner);tiku_work_collect();usleep(1000);}CHECK(!tiku_work_busy());
}
/** @brief Read one typed snapshot with test authority. */
static tiku_resource_snapshot_t read_value(tiku_resource_registry_t *registry,const char *path)
{
    tiku_resource_snapshot_t out={0};CHECK(!tiku_resource_read(registry,&caller,path,&out));return out;
}
/** @brief Submit an async handler replacement and collect its retained outcome. */
static tiku_resource_error_t change(tiku_resource_registry_t *registry,tiku_trk_catalog_settings_t *owner,const char *path,const char *name)
{
    tiku_resource_snapshot_t before=read_value(registry,path);tiku_resource_job_info_t job={0},out={0};
    tiku_msg_t *input=tiku_resource_value_string(TIKU_RESOURCE_TEXT,name);char id[33];tiku_resource_error_t error;
    snprintf(id,sizeof id,"%032x",++serial);
    error=tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&before.stamp,id,input,0,10000,&job);
    tiku_resource_snapshot_clear(&before);tiku_msg_free(input);if(error)return error;
    CHECK(tiku_resource_jobs_pump(registry)==1);drain(owner);CHECK(!tiku_resource_job_status(registry,&caller,job.id,&out));
    error=out.error;tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&out);return error;
}
/** @brief Preserve the GUI completion result separately from its draft. */
static void saved(void *context,tiku_resource_error_t error){*(int *)context=error;}

int main(void)
{
    char home[]="/tmp/stomme-catalog-XXXXXX",path[512],preferred[TIKU_RESOURCE_PATH_MAX],meta[TIKU_RESOURCE_PATH_MAX],typed[TIKU_RESOURCE_PATH_MAX],saved_path[520],buf[80];
    tiku_trk_apps_t *apps=calloc(1,sizeof *apps);tiku_trk_types_t types={0};tiku_trk_prefs_t prefs;
    tiku_trk_associations_t *draft=calloc(1,sizeof *draft);tiku_resource_limits_t limits={.resources=1024};
    tiku_resource_registry_t *registry=tiku_resource_registry_new(&limits);tiku_trk_catalog_settings_t *owner;tiku_trk_settings_t *settings;
    tiku_store_t *store,*disk;tiku_resource_snapshot_t out;int result=99;
    CHECK(apps && draft && registry && mkdtemp(home) && !setenv("XDG_DATA_HOME",home,1));
    CHECK(tiku_trk_launch_add_document(apps,"Editor","/runner","/editor.so","text/plain,text/")>=0);
    CHECK(tiku_trk_launch_add_document(apps,"Viewer","/runner","/viewer.so","text/plain")>=0);
    CHECK(tiku_trk_launch_add_document(apps,"Image","/runner","/image.so","image/png")>=0);
    CHECK(tiku_trk_types_add(&types,"text/plain","Plain text","text","name,size,attr:rating,document")>=0);
    store=tiku_store_sidecar_open("test");CHECK(store);
    owner=tiku_trk_catalog_settings_open(store,apps,&types,NULL,NULL);CHECK(owner);
    snprintf(path,sizeof path,"%s/settings",home);tiku_trk_prefs_init(&prefs);
    settings=tiku_trk_settings_open_with_catalog(&prefs,path,registry,NULL,owner,NULL,NULL);CHECK(settings);drain(owner);
    CHECK(!tiku_trk_catalog_settings_path("/settings/associations","text/plain","preferred",preferred));
    CHECK(!tiku_trk_catalog_settings_path("/settings/associations","text/plain","details",meta));
    CHECK(!tiku_trk_catalog_settings_path("/settings/file-types","text/plain",NULL,typed));
    out=read_value(registry,typed);CHECK(!strcmp(out.apply_state,"applied") && tiku_msg_count(out.applied,"member")==6);tiku_resource_snapshot_clear(&out);
    out=read_value(registry,meta);CHECK(tiku_msg_count(out.applied,"member")==3);tiku_resource_snapshot_clear(&out);
    CHECK(change(registry,owner,preferred,"Image")==TIKU_RESOURCE_VALUE);
    CHECK(change(registry,owner,preferred,"Missing app")==TIKU_RESOURCE_VALUE);
    CHECK(!change(registry,owner,preferred,"Viewer"));CHECK(!strcmp(tiku_trk_launch_preferred(store,"text/plain",buf,sizeof buf),"Viewer"));
    disk=tiku_store_sidecar_open("test");CHECK(!strcmp(tiku_trk_launch_preferred(disk,"text/plain",buf,sizeof buf),"Viewer"));tiku_store_free(disk);
    tiku_trk_associations_load(draft,apps,&types,store);
    for(int i=0;i<draft->count;i++)if(!strcmp(draft->row[i].type,"text/plain"))strcpy(draft->row[i].chosen,"Editor");
    CHECK(!tiku_trk_catalog_settings_submit(owner,draft,saved,&result));CHECK(result==99);
    CHECK(!strcmp(tiku_trk_launch_preferred(store,"text/plain",buf,sizeof buf),"Viewer"));drain(owner);CHECK(!result);
    CHECK(!strcmp(tiku_trk_launch_preferred(store,"text/plain",buf,sizeof buf),"Editor"));
    CHECK(tiku_trk_catalog_settings_submit(owner,draft,saved,&result)==0);result=99;drain(owner);CHECK(result==TIKU_RESOURCE_STALE);
    {
        tiku_store_update_t *other=tiku_store_update_begin(store);CHECK(other);
        tiku_trk_associations_load(draft,apps,&types,store);
        for(int i=0;i<draft->count;i++)if(!strcmp(draft->row[i].type,"text/plain"))strcpy(draft->row[i].chosen,"Viewer");
        CHECK(!tiku_trk_catalog_settings_submit(owner,draft,saved,&result));result=99;tiku_trk_catalog_settings_poll(owner);CHECK(result==99 && !tiku_work_busy());
        CHECK(!tiku_state_write(tiku_store_update_model(other),"/unrelated","window","frame",5));tiku_store_update_run(other);CHECK(tiku_store_update_finish(other)==TIKU_STORE_UPDATE_OK);tiku_store_update_free(other);
        drain(owner);CHECK(!result && !strcmp(tiku_trk_launch_preferred(store,"text/plain",buf,sizeof buf),"Viewer"));
        CHECK(tiku_state_read(store,"/unrelated","window",buf,sizeof buf)==5 && !memcmp(buf,"frame",5));
    }
    snprintf(path,sizeof path,"%s/tracker/devices/test.state",home);snprintf(saved_path,sizeof saved_path,"%s.saved",path);CHECK(!rename(path,saved_path) && !mkdir(path,0700));
    CHECK(change(registry,owner,preferred,"Editor")==TIKU_RESOURCE_PERSISTENCE_FAILED);CHECK(!strcmp(tiku_trk_launch_preferred(store,"text/plain",buf,sizeof buf),"Viewer"));
    CHECK(!rmdir(path) && !rename(saved_path,path));
    CHECK(!tiku_trk_launch_set_preferred(store,"text/plain","Missing app"));tiku_trk_catalog_settings_poll(owner);
    out=read_value(registry,preferred);CHECK(!strcmp(tiku_msg_find_string(out.persisted,"value",0),"Viewer") && !strcmp(out.apply_state,"pending_persistence"));tiku_resource_snapshot_clear(&out);
    CHECK(!tiku_store_flush(store));tiku_trk_catalog_settings_poll(owner);
    out=read_value(registry,preferred);CHECK(!strcmp(out.apply_state,"fallback") && !strcmp(tiku_msg_find_string(out.desired,"value",0),"Missing app") && strcmp(tiku_msg_find_string(out.applied,"value",0),"Missing app"));tiku_resource_snapshot_clear(&out);
    CHECK(!change(registry,owner,preferred,""));out=read_value(registry,preferred);CHECK(!strcmp(tiku_msg_find_string(out.desired,"value",0),"") && !strcmp(out.apply_state,"applied"));tiku_resource_snapshot_clear(&out);
    {
        tiku_resource_snapshot_t old=read_value(registry,typed),ignored={0};tiku_msg_t *input=tiku_resource_value_string(TIKU_RESOURCE_TEXT,"bad");
        CHECK(tiku_resource_change(registry,&caller,typed,&old.stamp,"00000000000000000000000000000999",input,0,&ignored)==TIKU_RESOURCE_READ_ONLY);
        tiku_msg_free(input);tiku_resource_snapshot_clear(&old);
    }
    tiku_trk_catalog_settings_close(owner);tiku_trk_settings_close(settings);tiku_resource_registry_free(registry);tiku_trk_prefs_free(&prefs);tiku_store_free(store);free(apps);free(draft);
    CHECK(!unlink(path));snprintf(path,sizeof path,"%s/tracker/devices",home);CHECK(!rmdir(path));snprintf(path,sizeof path,"%s/tracker",home);CHECK(!rmdir(path) && !rmdir(home));
    puts("PASS catalog settings: typed handlers, GUI draft completion, stale input, serialized store, durable failure, fallback and loaded type metadata");return 0;
}
