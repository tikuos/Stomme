/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_desktop_settings.c - native Desktop settings and persistence feedback.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_settings.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_jobs.h"
#include "tiku_work.h"
#include "tiku_state_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)
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
    error=tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&old.stamp,id,input,0,10000,&job);tiku_resource_snapshot_clear(&old);
    if(error)return error;
    CHECK(tiku_resource_jobs_pump(registry)==1);drain();CHECK(!tiku_resource_job_status(registry,&caller,job.id,&out));error=out.error;
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&out);return error;
}

/** @brief Count notifications from the native Desktop settings owner. */
static void changed(void *context)
{
    (*(unsigned *)context)++;
}

/** @brief Exercise one native or cached state store through the same owner API. */
static void exercise(tiku_store_t *store, const char *node, const char *path, int native)
{
    tiku_trk_deskprefs_t model;
    tiku_trk_prefs_t table;
    unsigned notifications = 0;
    tiku_resource_registry_t *registry = tiku_resource_registry_new(NULL);
    tiku_trk_desktop_settings_t *desktop; tiku_trk_settings_t *settings;
    tiku_resource_snapshot_t snapshot; tiku_msg_t *input; char bytes[2048], backup[1100]; int64_t number;
    const char *view = "icon 32 12 10 20 30 40 2 3 1 0";
    CHECK(registry && !tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,view,strlen(view)) && !tiku_store_flush(store));
    tiku_trk_deskprefs_init(&model); tiku_trk_prefs_init(&table);
    desktop = tiku_trk_desktop_settings_open(store, node, &model, changed,
                                             &notifications);
    CHECK(desktop);
    settings=tiku_trk_settings_open_with_desktop(&table,path,registry,NULL,NULL,desktop,NULL,NULL); CHECK(settings); drain();
    {
        unsigned before = notifications;
        tiku_trk_desktop_settings_poll(desktop, 501);
        drain();
        CHECK(notifications == before);
    }
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,48); CHECK(!change(registry,"/settings/desktop/icon_size",input,1)); tiku_msg_free(input);
    CHECK(model.value.icon_size==48);
    CHECK(tiku_state_read(store,node,TIKU_ATTR_DESK_VIEW_STATE,bytes,sizeof bytes-1)>0);
    CHECK(!memcmp(bytes,"icon 48 12 10 20 30 40 2 3 1 0",strlen(view)));
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,33); CHECK(change(registry,"/settings/desktop/icon_size",input,2)==TIKU_RESOURCE_VALUE); tiku_msg_free(input);
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,INT64_MAX); CHECK(change(registry,"/settings/desktop/icon_size",input,3)==TIKU_RESOURCE_VALUE); tiku_msg_free(input);
    input=trk_value_container("object");
    CHECK(!trk_value_member(input,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,"/tmp/picture with spaces.png")) &&
          !trk_value_member(input,"mode",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,1)) &&
          !trk_value_member(input,"x",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,-17)) &&
          !trk_value_member(input,"y",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,29)));
    CHECK(!change(registry,"/settings/desktop/bg",input,4)); tiku_msg_free(input);
    CHECK(model.value.have_bg && model.value.bg.mode==1 && model.value.bg.x==-17 && !strcmp(model.value.bg.path,"/tmp/picture with spaces.png"));
    snapshot=read_value(registry,"/settings/desktop/have_bg"); CHECK(tiku_msg_find_bool(snapshot.applied,"value",0,&(int){0})); tiku_resource_snapshot_clear(&snapshot);
    input=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1); CHECK(!change(registry,"/settings/desktop/reduce_motion",input,5)); tiku_msg_free(input);
    CHECK(tiku_trk_prefs_get(&table,"ReduceMotion"));
    snapshot=read_value(registry,"/settings/ui/reduce-motion"); CHECK(tiku_msg_find_bool(snapshot.persisted,"value",0,&(int){0})); tiku_resource_snapshot_clear(&snapshot);
    model.value.icon_size=64; CHECK(!tiku_trk_desktop_settings_submit(desktop)); CHECK(model.value.icon_size==48); drain(); CHECK(model.value.icon_size==64);
    CHECK(!strcmp(tiku_trk_desktop_settings_status(desktop),""));
    CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,"mini 96 9 8 7 6 5 4 3 0 1",25) && !tiku_store_flush(store));
    tiku_trk_desktop_settings_poll(desktop, 1001);
    drain();
    CHECK(model.value.icon_size == 96);
    CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,"bad record",10) && !tiku_store_flush(store));
    tiku_trk_desktop_settings_poll(desktop,2000); drain(); CHECK(model.value.icon_size==96 && strstr(tiku_trk_desktop_settings_status(desktop),"Could not save"));
    CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,view,strlen(view)) && !tiku_store_flush(store));
    tiku_trk_desktop_settings_poll(desktop,3000); drain(); CHECK(model.value.icon_size==32);
    {
        const char *bad="1 999999999999999999999999999 0 /tmp/invalid.png";
        const char *good="1 -17 29 /tmp/picture with spaces.png";
        CHECK(!tiku_state_write(store,node,TIKU_TRK_ATTR_BACKGROUND,bad,strlen(bad)) && !tiku_store_flush(store));
        tiku_trk_desktop_settings_poll(desktop,4000); drain();
        CHECK(!strcmp(model.value.bg.path,"/tmp/picture with spaces.png") && model.value.bg.x==-17);
        CHECK(!tiku_state_write(store,node,TIKU_TRK_ATTR_BACKGROUND,good,strlen(good)) && !tiku_store_flush(store));
        tiku_trk_desktop_settings_poll(desktop,5000); drain();
    }
    {
        const char *legacy="icon 80 0 0 0 0 0 0 -1 0 0";
        CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,legacy,strlen(legacy)) && !tiku_store_flush(store));
        tiku_trk_desktop_settings_poll(desktop,6000); drain(); CHECK(model.value.icon_size==80);
        model.value.bg.mode=TIKU_TRK_BG_SCALED; CHECK(!tiku_trk_desktop_settings_submit(desktop)); drain();
        CHECK(model.value.icon_size==80 && model.value.bg.mode==TIKU_TRK_BG_SCALED);
        CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,view,strlen(view)) && !tiku_store_flush(store));
        tiku_trk_desktop_settings_poll(desktop,7000); drain();
    }
    if(tiku_store_persistent(store)) {
        const char *cached="icon 64 12 10 20 30 40 2 3 1 0";
        tiku_trk_desktop_settings_poll(desktop,8000);
        CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,cached,strlen(cached))); drain();
        snapshot=read_value(registry,"/settings/desktop/icon_size");
        CHECK(!strcmp(snapshot.apply_state,"pending_persistence"));
        CHECK(tiku_msg_find_int64(snapshot.desired,"value",0,&number) && number==64);
        CHECK(tiku_msg_find_int64(snapshot.persisted,"value",0,&number) && number==32); tiku_resource_snapshot_clear(&snapshot);
        CHECK(!tiku_state_write(store,node,TIKU_ATTR_DESK_VIEW_STATE,view,strlen(view)) && !tiku_store_flush(store));
        tiku_trk_desktop_settings_poll(desktop,9000); drain();
    }
    if(native) {
        snprintf(backup,sizeof backup,"%s.saved",node); CHECK(!rename(node,backup) && !mkdir(node,0700));
        input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,128); CHECK(change(registry,"/settings/desktop/icon_size",input,6)==TIKU_RESOURCE_STALE); tiku_msg_free(input);
        CHECK(model.value.icon_size==32 && strstr(tiku_trk_desktop_settings_status(desktop),"changed"));
        CHECK(!rmdir(node) && !rename(backup,node));
        tiku_trk_desktop_settings_poll(desktop,8000); drain();
        CHECK(strstr(tiku_trk_desktop_settings_status(desktop),"changed"));
    }
    tiku_trk_desktop_settings_feedback(desktop,NULL,model.value.icon_size);
    tiku_trk_desktop_settings_poll(desktop,10000); drain();
    snapshot=read_value(registry,"/settings/desktop/bg");
    input=trk_value_find(snapshot.applied,"path"); CHECK(input && !strcmp(tiku_msg_find_string(input,"value",0),""));
    CHECK(!strcmp(snapshot.apply_state,"fallback")); tiku_msg_free(input); tiku_resource_snapshot_clear(&snapshot);
    tiku_trk_desktop_settings_close(desktop); tiku_trk_settings_close(settings); tiku_resource_registry_free(registry); tiku_trk_prefs_free(&table);
    tiku_trk_deskprefs_init(&model); desktop=tiku_trk_desktop_settings_open(store,node,&model,NULL,NULL); CHECK(desktop); drain();
    CHECK(model.value.icon_size==32 && model.value.have_bg && model.value.bg.x==-17); tiku_trk_desktop_settings_close(desktop);
    (void)number;
}

int main(void)
{
    char directory[]="/tmp/stomme-desktop-settings-XXXXXX",node[1024],path[1024]; tiku_store_t *store;
    CHECK(mkdtemp(directory)); snprintf(node,sizeof node,"%s/desktop",directory); CHECK(!mkdir(node,0700));
    snprintf(path,sizeof path,"%s/settings",directory); CHECK(!setenv("XDG_DATA_HOME",directory,1));
    store=tiku_store_xattr_open(); CHECK(store); exercise(store,node,path,1); tiku_store_free(store);
    store=tiku_store_memory_open(); CHECK(store); exercise(store,node,path,0); tiku_store_free(store);
    store=tiku_store_sidecar_open("desktop"); CHECK(store); exercise(store,node,path,0); tiku_store_free(store);
    CHECK(!unlink(path) && !rmdir(node));
    snprintf(path,sizeof path,"%s/tracker/devices/desktop.state",directory); CHECK(!unlink(path));
    snprintf(path,sizeof path,"%s/tracker/devices",directory); CHECK(!rmdir(path));
    snprintf(path,sizeof path,"%s/tracker",directory); CHECK(!rmdir(path) && !rmdir(directory));
    puts("PASS Desktop settings: guarded native writes, cached stores, aliases, reload, restart and failures"); return 0;
}
