/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_record_settings.c - shared Deskbar and Fonts resource and persistence owner.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_settings.h"
#include "tiku_resource_jobs.h"
#include "tiku_work.h"
#include "tiku_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(test) do { if(!(test)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test);exit(1);} } while(0)
static tiku_trk_fontprefs_t fonts;
/** @brief Apply through the normal font owner on the collecting thread. */
static void applied(void *context){(void)context;tiku_trk_fontprefs_apply(&fonts);}
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

int main(void)
{
    char directory[]="/tmp/stomme-records-XXXXXX",path[512];tiku_store_t *store;tiku_trk_prefs_t table;tiku_trk_deskbar_prefs_t bar;
    tiku_trk_record_settings_t *records;tiku_trk_settings_t *settings;tiku_resource_registry_t *registry=tiku_resource_registry_new(NULL);tiku_msg_t *input;tiku_resource_snapshot_t snapshot;int64_t n;
    CHECK(registry && mkdtemp(directory));snprintf(path,sizeof path,"%s/settings",directory);CHECK(!setenv("XDG_DATA_HOME",directory,1));store=tiku_store_sidecar_open("test");CHECK(store);
    tiku_trk_prefs_init(&table);tiku_trk_deskbar_prefs_init(&bar);tiku_trk_fontprefs_init(&fonts);
    records=tiku_trk_record_settings_open(store,&bar,&fonts,applied,NULL);CHECK(records);
    settings=tiku_trk_settings_open_with_records(&table,path,registry,records,NULL,NULL);CHECK(settings);drain();
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,14);CHECK(!change(registry,"/settings/ui/font/size",input,1));tiku_msg_free(input);
    CHECK(fonts.value.size==14 && bar.value.font_size==14 && tiku_font_size()==14);
    snapshot=read_value(registry,"/settings/deskbar/font_size");CHECK(tiku_msg_find_int64(snapshot.applied,"value",0,&n) && n==14);tiku_resource_snapshot_clear(&snapshot);
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,13);CHECK(change(registry,"/settings/ui/font/size",input,2)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
    input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,97);CHECK(change(registry,"/settings/deskbar/icon_size",input,3)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
    input=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,0);CHECK(!change(registry,"/settings/deskbar/show_clock",input,4));tiku_msg_free(input);CHECK(!bar.value.show_clock);
    bar.value.icon_size=48;CHECK(!tiku_trk_record_settings_submit(records,0));CHECK(bar.value.icon_size==16);drain();CHECK(bar.value.icon_size==48);
    fonts.value.size=16;CHECK(!tiku_trk_record_settings_submit(records,1));drain();CHECK(fonts.value.size==16 && bar.value.font_size==16 && tiku_font_size()==16);
    fonts.value.size = 10;
    CHECK(!tiku_trk_record_settings_submit(records, 1));
    fonts.value.size = 12;
    CHECK(!tiku_trk_record_settings_submit(records, 1));
    fonts.value.size = 14;
    CHECK(!tiku_trk_record_settings_submit(records, 1));
    drain();
    CHECK(fonts.value.size == 14 && bar.value.font_size == 14 &&
          tiku_font_size() == 14);
    fonts.value.size = 16;
    CHECK(!tiku_trk_record_settings_submit(records, 1));
    drain();
    input=tiku_resource_value_string(TIKU_RESOURCE_TEXT,"Missing test family");CHECK(!change(registry,"/settings/ui/font/dropped",input,5));tiku_msg_free(input);
    snapshot=read_value(registry,"/settings/ui/font/dropped");CHECK(!strcmp(snapshot.apply_state,"fallback") && !strcmp(tiku_msg_find_string(snapshot.desired,"value",0),"Missing test family") && !strcmp(tiku_msg_find_string(snapshot.applied,"value",0),""));tiku_resource_snapshot_clear(&snapshot);
    {
        tiku_store_t *other=tiku_store_sidecar_open("test");tiku_trk_deskbar_prefs_t saved;tiku_trk_deskbar_prefs_init(&saved);CHECK(!tiku_trk_deskbar_prefs_load(&saved,other));CHECK(saved.value.icon_size==48 && saved.value.font_size==16 && !saved.value.show_clock);
        saved.value.icon_size=64;CHECK(!tiku_trk_deskbar_prefs_save(&saved,other) && !tiku_store_flush(other));tiku_store_free(other);
    }
    tiku_trk_record_settings_poll(records,1000);drain();CHECK(bar.value.icon_size==64);snapshot=read_value(registry,"/settings/deskbar/icon_size");CHECK(tiku_msg_find_int64(snapshot.persisted,"value",0,&n) && n==64);tiku_resource_snapshot_clear(&snapshot);
    {
        unsigned i;input=tiku_msg_new(0);CHECK(input && tiku_msg_add_string(input,"type","array"));
        for(i=0;i<3;i++){tiku_msg_t *item=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,(int)i+20);CHECK(item && tiku_msg_add_message(input,"item",item));tiku_msg_free(item);}
        CHECK(!change(registry,"/settings/deskbar/recent_count",input,7));tiku_msg_free(input);CHECK(bar.value.recent_count[0]==20 && bar.value.recent_count[2]==22);
        input=tiku_msg_new(0);CHECK(input && tiku_msg_add_string(input,"type","array"));CHECK(change(registry,"/settings/deskbar/recent_count",input,8)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
    }
    {
        char sidecar[512],backup[520];snprintf(sidecar,sizeof sidecar,"%s/tracker/devices/test.state",directory);snprintf(backup,sizeof backup,"%s.bak",sidecar);
        CHECK(!rename(sidecar,backup) && !mkdir(sidecar,0700));
        input=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1);CHECK(change(registry,"/settings/deskbar/show_clock",input,9)==TIKU_RESOURCE_PERSISTENCE_FAILED);tiku_msg_free(input);CHECK(!bar.value.show_clock);
        CHECK(!rmdir(sidecar) && !rename(backup,sidecar));
    }
    tiku_trk_record_settings_poll(records,2000);input=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1);CHECK(!change(registry,"/settings/deskbar/show_clock",input,6));tiku_msg_free(input);CHECK(bar.value.show_clock);
    {
        const char *paths[]={"always_on_top","auto_raise","sort_apps","tracker_first","show_expander","expand_new_apps","hide_app_names","auto_hide","show_clock","show_seconds","show_day_of_week","show_timezone","show_replicants"};
        const int values[]={0,1,1,0,0,0,1,1,0,1,1,1,0};unsigned i;char target[96];
        for(i=0;i<sizeof values/sizeof values[0];i++){
            snprintf(target,sizeof target,"/settings/deskbar/%s",paths[i]);input=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,values[i]);
            CHECK(!change(registry,target,input,i+100));tiku_msg_free(input);
        }
        CHECK(!bar.value.always_on_top && bar.value.auto_raise && bar.value.sort_apps && !bar.value.tracker_first && !bar.value.show_expander && !bar.value.expand_new_apps);
        CHECK(bar.value.hide_app_names && bar.value.auto_hide && !bar.value.show_clock && bar.value.show_seconds && bar.value.show_day_of_week && bar.value.show_timezone && !bar.value.show_replicants);
        input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,80);CHECK(!change(registry,"/settings/deskbar/icon_size",input,140));tiku_msg_free(input);CHECK(bar.value.icon_size==80);
        input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,12);CHECK(!change(registry,"/settings/deskbar/font_size",input,141));tiku_msg_free(input);CHECK(fonts.value.size==12 && bar.value.font_size==12);
        input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,1);CHECK(!change(registry,"/settings/deskbar/font_family",input,142));tiku_msg_free(input);CHECK(fonts.value.family==1 && !fonts.value.dropped[0]);
        input=tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0);CHECK(!change(registry,"/settings/ui/font/family",input,143));tiku_msg_free(input);CHECK(!fonts.value.family && !bar.value.font_family);
        input=tiku_msg_new(0);CHECK(input && tiku_msg_add_string(input,"type","array"));
        for(i=0;i<3;i++){tiku_msg_t *item=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,i==1);CHECK(item && tiku_msg_add_message(input,"item",item));tiku_msg_free(item);}
        CHECK(!change(registry,"/settings/deskbar/recent_enabled",input,144));tiku_msg_free(input);CHECK(!bar.value.recent_enabled[0] && bar.value.recent_enabled[1] && !bar.value.recent_enabled[2]);
        snapshot=read_value(registry,"/settings/deskbar/profile");CHECK(tiku_msg_count(snapshot.desired,"member")==18 && !change(registry,"/settings/deskbar/profile",snapshot.desired,145));tiku_resource_snapshot_clear(&snapshot);
        snapshot=read_value(registry,"/settings/ui/font/profile");CHECK(tiku_msg_count(snapshot.desired,"member")==3 && !change(registry,"/settings/ui/font/profile",snapshot.desired,146));tiku_resource_snapshot_clear(&snapshot);
    }
    tiku_trk_record_settings_close(records);tiku_trk_settings_close(settings);tiku_resource_registry_free(registry);tiku_trk_prefs_free(&table);tiku_store_free(store);
    snprintf(path,sizeof path,"%s/tracker/devices/test.state",directory);CHECK(!unlink(path));snprintf(path,sizeof path,"%s/tracker/devices",directory);CHECK(!rmdir(path));snprintf(path,sizeof path,"%s/tracker",directory);CHECK(!rmdir(path) && !rmdir(directory));
    tiku_font_use_baked();tiku_font_set_size(12);
    puts("PASS record settings: typed bounds, GUI saves, font aliases, actual fallback, external reload and queued request");return 0;
}
