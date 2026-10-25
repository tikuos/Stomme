/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_native_settings.c - typed record catalogs, guarded writes and saved/live distinctions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_native_settings.h"
#include "tiku_trk_native_record.h"
#include "tiku_trk_resource_values.h"
#include "tiku_trk_settings.h"
#include "tiku_resource_jobs.h"
#include "tiku_work.h"
#include "tiku_state_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)
static int invoking;
#if defined(__linux__)
static unsigned store_delay_us;
/** @brief Delay store work so a concurrent catalog read completes first. */
void __wrap_tiku_store_update_run(tiku_store_update_t *update)
{
    extern void __real_tiku_store_update_run(tiku_store_update_t *);
    if (store_delay_us) usleep(store_delay_us);
    __real_tiku_store_update_run(update);
}
#endif
/** @brief Collect the worker queue with a bounded deadline. */
static void drain(void){unsigned i;for(i=0;i<10000 && tiku_work_busy();i++){tiku_work_collect();usleep(1000);}CHECK(!tiku_work_busy());}
/** @brief Read a copied snapshot using trusted test authority. */
static tiku_resource_snapshot_t read_value(tiku_resource_registry_t *registry,const char *path)
{
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63};tiku_resource_snapshot_t out={0};strcpy(caller.principal,"00000000000000000000000000000001");
    CHECK(!tiku_resource_read(registry,&caller,path,&out));return out;
}
/** @brief Submit one typed owner job and check its terminal result. */
static tiku_resource_error_t change(tiku_resource_registry_t *registry,const char *path,tiku_msg_t *input,unsigned serial)
{
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63};tiku_resource_job_info_t job={0},out={0};tiku_resource_snapshot_t old=read_value(registry,path);
    char id[33];tiku_resource_error_t error;strcpy(caller.principal,"00000000000000000000000000000001");snprintf(id,sizeof id,"%032x",serial);
    error=tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&old.stamp,id,input,invoking,10000,&job);tiku_resource_snapshot_clear(&old);
    if(error)return error;
    CHECK(tiku_resource_jobs_pump(registry)==1);drain();CHECK(!tiku_resource_job_status(registry,&caller,job.id,&out));error=out.error;
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&out);return error;
}

/** @brief Capture the fixture's current restorable folder path. */
static tiku_msg_t *captured(void *context)
{
    tiku_msg_t *out=trk_value_container("array"), *item=trk_value_container("object"); (void)context;
    CHECK(!trk_value_member(item,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,"/captured")) &&
        !trk_value_member(item,"minimized",tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,0)) && !trk_value_item(out,item)); return out;
}

/** @brief Resolve a complete catalog entry through its public node metadata. */
static void find_value(tiku_resource_registry_t *registry, const char *family, const char *node, char *out)
{
    tiku_resource_caller_t caller={.authenticated=1,.scopes=63};
    tiku_resource_entry_t entries[64]; uint64_t epoch=0; size_t count,i;
    strcpy(caller.principal,"00000000000000000000000000000001");
    CHECK(!tiku_resource_list(registry,&caller,family,0,&epoch,entries,64,&count));
    for(i=0;i<count;i++) {
        char path[TIKU_RESOURCE_PATH_MAX]; tiku_resource_snapshot_t snapshot={0}; tiku_msg_t *value;
        snprintf(path,sizeof path,"%s/node",entries[i].path);
        CHECK(!tiku_resource_read(registry,&caller,path,&snapshot)); value=trk_value_find(snapshot.desired,"node");
        if(value && !strcmp(tiku_msg_find_string(value,"value",0),node)) {
            snprintf(out,TIKU_RESOURCE_PATH_MAX,"%s/value",entries[i].path); tiku_msg_free(value); tiku_resource_snapshot_clear(&snapshot); return;
        }
        tiku_msg_free(value); tiku_resource_snapshot_clear(&snapshot);
    }
    CHECK(0);
}

int main(void)
{
    char directory[]="/tmp/stomme-native-settings-XXXXXX",node[512],path[512],target[TIKU_RESOURCE_PATH_MAX],backup[520];
    tiku_store_t *store,*attributes; tiku_trk_prefs_t table; tiku_trk_deskbar_prefs_t bar; tiku_trk_fontprefs_t fonts;
    tiku_trk_record_settings_t *records; tiku_trk_settings_t *settings; tiku_trk_native_settings_t *native;
    tiku_resource_registry_t *registry=tiku_resource_registry_new(NULL); tiku_resource_snapshot_t snapshot; tiku_msg_t *input;
    int family; int64_t number;
    CHECK(registry && mkdtemp(directory) && !setenv("XDG_DATA_HOME",directory,1));
    snprintf(node,sizeof node,"%s/folder",directory); CHECK(!mkdir(node,0700)); snprintf(path,sizeof path,"%s/preferences",directory);
    store=tiku_store_memory_open(); attributes=tiku_store_xattr_open(); CHECK(store && attributes);
    tiku_trk_prefs_init(&table); tiku_trk_deskbar_prefs_init(&bar); tiku_trk_fontprefs_init(&fonts);
    records=tiku_trk_record_settings_open(store,&bar,&fonts,NULL,NULL); CHECK(records);
    settings=tiku_trk_settings_open_with_records(&table,path,registry,records,NULL,NULL); CHECK(settings); drain();
    native=tiku_trk_native_settings_open(registry,records); CHECK(native);
    for(family=0;family<TIKU_TRK_NATIVE_COUNT;family++) {
        if(family==TIKU_TRK_NATIVE_DEFAULTS || family==TIKU_TRK_NATIVE_DESKBAR_PREFS || family==TIKU_TRK_NATIVE_FONT) continue;
        CHECK(!tiku_trk_native_settings_add(native,family,family==TIKU_TRK_NATIVE_FRAME ? store : attributes,node));
    }
    CHECK(!tiku_trk_native_settings_add(native,TIKU_TRK_NATIVE_SESSION,store,TIKU_KEY_SESSION));
    tiku_trk_native_settings_bind_capture(native,captured,NULL);
    tiku_trk_native_settings_poll(native,1);
#if defined(__linux__)
    store_delay_us=30000;
#endif
    snapshot=read_value(registry,"/session/records/deskbar-prefs");
    CHECK(!change(registry,"/session/records/deskbar-prefs",snapshot.desired,100)); tiku_resource_snapshot_clear(&snapshot);
#if defined(__linux__)
    store_delay_us=0;
#endif
    tiku_trk_native_settings_poll(native,2); drain();
    find_value(registry,"/session/records/view-state",node,target);
    input=tiku_trk_native_record_decode(TIKU_TRK_RECORD_VIEW,"icon 64 0 0 0 0 0 0 -1 0 0",strlen("icon 64 0 0 0 0 0 0 -1 0 0")); CHECK(input);
    CHECK(!change(registry,target,input,1)); tiku_msg_free(input);
    snapshot=read_value(registry,target); CHECK(!strcmp(snapshot.apply_state,"saved_for_next_open") && tiku_msg_count(snapshot.applied,"member")==0);
    input=trk_value_find(snapshot.persisted,"icon_size"); CHECK(tiku_msg_find_int64(input,"value",0,&number) && number==64); tiku_msg_free(input); tiku_resource_snapshot_clear(&snapshot);
    CHECK(!tiku_state_write(attributes,node,TIKU_ATTR_VIEW_STATE,"bad record",10));
    tiku_trk_native_settings_poll(native,1001); drain(); snapshot=read_value(registry,target); CHECK(!strcmp(snapshot.apply_state,"last_good_unavailable")); tiku_resource_snapshot_clear(&snapshot);
    input=tiku_trk_native_record_decode(TIKU_TRK_RECORD_VIEW,"mini 48",7); CHECK(input); CHECK(!change(registry,target,input,2)); tiku_msg_free(input);
    snprintf(backup,sizeof backup,"%s.saved",node); CHECK(!rename(node,backup) && !mkdir(node,0700));
    input=tiku_trk_native_record_decode(TIKU_TRK_RECORD_VIEW,"mini 96",7); CHECK(input); CHECK(change(registry,target,input,3)==TIKU_RESOURCE_STALE); tiku_msg_free(input);
    CHECK(!rmdir(node) && !rename(backup,node));
    find_value(registry,"/session/records/frame",node,target);
    input=trk_value_container("object"); CHECK(!trk_value_member(input,"x",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,20)) &&
        !trk_value_member(input,"y",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,30)) && !trk_value_member(input,"width",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,640)) &&
        !trk_value_member(input,"height",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,400)));
    CHECK(!change(registry,target,input,4)); tiku_msg_free(input);
    snapshot=read_value(registry,"/session/records/defaults"); CHECK(tiku_msg_count(snapshot.desired,"member")==0); tiku_resource_snapshot_clear(&snapshot);
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,1); CHECK(change(registry,"/session/records/defaults",input,5)==TIKU_RESOURCE_VALUE); tiku_msg_free(input);
    snapshot=read_value(registry,"/session/records/deskbar-prefs"); CHECK(tiku_msg_count(snapshot.desired,"member")==18);
    CHECK(!change(registry,"/session/records/deskbar-prefs",snapshot.desired,6)); tiku_resource_snapshot_clear(&snapshot);
    tiku_trk_native_settings_poll(native,2001); drain();
    find_value(registry,"/session/records/original",node,target);
    input=tiku_resource_value_string(TIKU_RESOURCE_TEXT,"/untrusted"); CHECK(change(registry,target,input,7)==TIKU_RESOURCE_READ_ONLY); tiku_msg_free(input);
    find_value(registry,"/session/records/session",TIKU_KEY_SESSION,target);
    input=trk_value_container("array");
    for(family=0;family<16;family++) {
        char long_path[320]; tiku_msg_t *item=trk_value_container("object");
        memset(long_path,'p',sizeof long_path); long_path[0]='/'; snprintf(long_path+300,20,"%d",family);
        CHECK(!trk_value_member(item,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,long_path)) &&
            !trk_value_member(item,"minimized",tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,0)) && !trk_value_item(input,item));
    }
    CHECK(change(registry,target,input,10)==TIKU_RESOURCE_PERSISTENCE_FAILED); tiku_msg_free(input);
    invoking=1; input=trk_value_container("object"); CHECK(!change(registry,"/session/records/capture",input,8)); tiku_msg_free(input);
    find_value(registry,"/session/records/session",TIKU_KEY_SESSION,target);
    snapshot=read_value(registry,target); CHECK(tiku_msg_count(snapshot.persisted,"item")==1); tiku_resource_snapshot_clear(&snapshot);
    input=trk_value_container("object"); CHECK(!change(registry,"/session/records/clear",input,9)); tiku_msg_free(input);
    snapshot=read_value(registry,target); CHECK(tiku_msg_count(snapshot.persisted,"item")==0); tiku_resource_snapshot_clear(&snapshot);
    tiku_trk_native_settings_close(native); tiku_trk_record_settings_close(records); tiku_trk_settings_close(settings); tiku_resource_registry_free(registry);
    tiku_trk_prefs_free(&table); tiku_store_free(store); tiku_store_free(attributes); CHECK(!rmdir(node) && !rmdir(directory));
    puts("PASS native settings: complete catalogs, aliases, saved/live distinction, malformed repair and target conflicts"); return 0;
}
