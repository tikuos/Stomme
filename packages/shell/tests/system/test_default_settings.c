/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_default_settings.c - staged folder defaults and persistence policy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_settings.h"
#include "tiku_trk_frame.h"
#include "tiku_state_update.h"
#include "tiku_resource_jobs.h"
#include "tiku_resource_transaction.h"
#include "tiku_resource_service.h"
#include "tiku_work.h"
#include <limits.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(test) do { if(!(test)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test);exit(1);} } while(0)
#define DEFAULT_PATH "/settings/tracker/window-defaults"
typedef struct {
    tiku_store_t *store;
    tiku_trk_prefs_t table;
    tiku_trk_deskbar_prefs_t bar;
    tiku_trk_fontprefs_t font;
    tiku_trk_record_settings_t *records;
    tiku_trk_settings_t *settings;
    tiku_resource_registry_t *registry;
    int changes;
} fixture_t;
static unsigned serial;
static tiku_resource_caller_t caller={.principal="00000000000000000000000000000001",.authenticated=1,.scopes=63};
/** @brief Count committed-model and failed-save redraw notifications. */
static void changed(void *context){((fixture_t *)context)->changes++;}
/** @brief Encode one member and release its temporary typed value. */
static void member(tiku_msg_t *out,const char *key,tiku_msg_t *value)
{
    tiku_msg_t *entry=tiku_msg_new(0);
    CHECK(entry && value && tiku_msg_add_string(entry,"key",key) && tiku_msg_add_message(entry,"value",value) && tiku_msg_add_message(out,"member",entry));
    tiku_msg_free(entry);tiku_msg_free(value);
}
/** @brief Build a complete native frame proposal. */
static tiku_msg_t *frame(int64_t x,int64_t y,int64_t w,int64_t h)
{
    tiku_msg_t *out=tiku_msg_new(0);CHECK(out && tiku_msg_add_string(out,"type","object"));
    member(out,"x",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,x));member(out,"y",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,y));
    member(out,"width",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,w));member(out,"height",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,h));return out;
}
/** @brief Read a copied native default snapshot. */
static tiku_resource_snapshot_t read_value(fixture_t *f)
{
    tiku_resource_snapshot_t out={0};CHECK(!tiku_resource_read(f->registry,&caller,DEFAULT_PATH,&out));return out;
}
/** @brief Collect workers and queued record proposals. */
static void drain(void)
{
    unsigned i;for(i=0;i<10000 && tiku_work_busy();i++){tiku_work_collect();usleep(1000);}CHECK(!tiku_work_busy());
}
/** @brief Start a resource proposal at its current provider revision. */
static tiku_resource_error_t submit(fixture_t *f,tiku_msg_t *input,tiku_resource_job_info_t *job)
{
    tiku_resource_snapshot_t before=read_value(f);char id[33];tiku_resource_error_t error;
    snprintf(id,sizeof id,"%032x",++serial);
    error=tiku_resource_job_submit(f->registry,&caller,"00000000000000000000000000000002",DEFAULT_PATH,&before.stamp,id,input,0,10000,job);
    tiku_resource_snapshot_clear(&before);if(!error)CHECK(tiku_resource_jobs_pump(f->registry)==1);return error;
}
/** @brief Read and release a terminal resource outcome. */
static tiku_resource_error_t outcome(fixture_t *f,tiku_resource_job_info_t *job)
{
    tiku_resource_job_info_t out={0};tiku_resource_error_t error;
    CHECK(!tiku_resource_job_status(f->registry,&caller,job->id,&out) && out.state>=TIKU_RESOURCE_JOB_SUCCEEDED);
    error=out.error;tiku_resource_job_info_clear(&out);tiku_resource_job_info_clear(job);return error;
}
/** @brief Complete one proposal and return its observed outcome. */
static tiku_resource_error_t change(fixture_t *f,tiku_msg_t *input)
{
    tiku_resource_job_info_t job={0};tiku_resource_error_t error=submit(f,input,&job);if(error)return error;
    if(tiku_work_busy())CHECK(strstr(tiku_trk_record_settings_status(f->records),"Saving") || strstr(tiku_trk_record_settings_status(f->records),"pending"));
    drain();return outcome(f,&job);
}
/** @brief Bind the shared settings provider to an isolated record store. */
static fixture_t *open_fixture(const char *path,int persistent)
{
    fixture_t *f=calloc(1,sizeof *f);CHECK(f);
    f->store=persistent?tiku_store_sidecar_open("defaults"):tiku_store_memory_open();f->registry=tiku_resource_registry_new(NULL);CHECK(f->store && f->registry);
    tiku_trk_prefs_init(&f->table);tiku_trk_deskbar_prefs_init(&f->bar);tiku_trk_fontprefs_init(&f->font);
    f->records=tiku_trk_record_settings_open(f->store,&f->bar,&f->font,changed,f);CHECK(f->records);
    f->settings=tiku_trk_settings_open_with_records(&f->table,path,f->registry,f->records,NULL,NULL);CHECK(f->settings);drain();return f;
}
/** @brief Close record workers before retiring the combined registry. */
static void close_fixture(fixture_t *f)
{
    tiku_trk_record_settings_close(f->records);tiku_trk_settings_close(f->settings);drain();
    tiku_resource_registry_free(f->registry);tiku_trk_prefs_free(&f->table);tiku_store_free(f->store);free(f);
}
/** @brief Hold the record worker key until the fixture permits persistence. */
static void barrier(void *context)
{
    atomic_int *released=context;while(!atomic_load(released))usleep(1000);
}
/** @brief Reserve a worker key before submitting a copied record update. */
static void block(atomic_int *released)
{
    tiku_work_job_t work={.run=barrier,.ctx=released};strcpy(work.key,"desktop-record-settings");CHECK(!tiku_work_post(&work));
}
/** @brief Check scope, durability and unavailable atomic-group policy through the broker. */
static void policy(fixture_t *f,int persistent,tiku_msg_t *input)
{
    tiku_msg_t *description=NULL,*request,*tuple,*reply=NULL,*result,*changes=NULL;
    tiku_resource_snapshot_t before=read_value(f);tiku_resource_job_info_t job={0};char id[33];
    tiku_resource_caller_t limited=caller;limited.scopes=TIKU_RESOURCE_READ;
    CHECK(!tiku_resource_describe(f->registry,&caller,DEFAULT_PATH,&description));
    CHECK(!strcmp(tiku_msg_find_string(description,"persistence",0),persistent?"user":"session"));tiku_msg_free(description);
    snprintf(id,sizeof id,"%032x",++serial);
    CHECK(tiku_resource_job_submit(f->registry,&limited,"00000000000000000000000000000002",DEFAULT_PATH,&before.stamp,id,input,0,10000,&job)==TIKU_RESOURCE_DENIED);
    tiku_resource_replacement_t replacement={.path=DEFAULT_PATH,.value=input,.durability=persistent?"user":"session"};
    CHECK(tiku_resource_transaction(f->registry,&caller,&before.stamp,id,&replacement,1,10000,&changes)==TIKU_RESOURCE_ATOMIC_UNSUPPORTED && !changes);
    tiku_resource_service_t *service=tiku_resource_service_new(f->registry,&caller,TIKU_RESOURCE_FEATURES);CHECK(service);
    request=tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);tuple=tiku_msg_new(0);
    CHECK(request && tuple && tiku_msg_add_string(request,"op","set") && tiku_msg_add_string(request,"request_id",id) &&
        tiku_msg_add_string(request,"session",tiku_resource_registry_session(f->registry)) && tiku_msg_add_string(request,"path",DEFAULT_PATH) &&
        tiku_msg_add_string(request,"durability",persistent?"session":"user") && tiku_msg_add_int32(request,"deadline_ms",3000) &&
        tiku_msg_add_string(tuple,"provider",before.stamp.provider) && tiku_msg_add_string(tuple,"incarnation",before.stamp.incarnation));
    char revision[32];snprintf(revision,sizeof revision,"%llu",(unsigned long long)before.stamp.revision);
    CHECK(tiku_msg_add_string(tuple,"revision",revision) && tiku_msg_add_message(request,"expected",tuple) && tiku_msg_add_message(request,"value",input));
    reply=tiku_resource_service_dispatch(service,&caller,request);CHECK(reply);result=tiku_msg_find_message(reply,"error",0);
    CHECK(result && !strcmp(tiku_msg_find_string(result,"code",0),"unsupported"));
    tiku_msg_free(result);tiku_msg_free(reply);
    tiku_msg_remove(request,"request_id");tiku_msg_remove(request,"durability");snprintf(id,sizeof id,"%032x",++serial);
    CHECK(tiku_msg_add_string(request,"request_id",id) && tiku_msg_add_string(request,"durability",persistent?"user":"session"));
    reply=tiku_resource_service_dispatch(service,&caller,request);CHECK(reply);result=tiku_msg_find_message(reply,"result",0);CHECK(result);
    const char *job_id=tiku_msg_find_string(result,"job",0);CHECK(job_id);strcpy(job.id,job_id);
    CHECK(tiku_resource_service_pump(service)==1);drain();CHECK(!outcome(f,&job));
    tiku_msg_free(result);tiku_msg_free(reply);tiku_msg_free(request);tiku_msg_free(tuple);tiku_resource_service_free(service);tiku_resource_snapshot_clear(&before);
    tiku_msg_t *empty=tiku_msg_new(0);CHECK(empty && tiku_msg_add_string(empty,"type","object") && !change(f,empty));tiku_msg_free(empty);
}
int main(void)
{
    tiku_trk_frame_t extreme={INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX};
    tiku_trk_frame_clamp(&extreme,1000,700);CHECK(extreme.x==940 && extreme.y==676 && extreme.w==1000 && extreme.h==700);
    extreme=(tiku_trk_frame_t){INT32_MAX,0,INT32_MAX,120};
    tiku_trk_frame_clamp(&extreme,INT32_MAX,700);CHECK(extreme.x==INT32_MAX-60 && extreme.w==INT32_MAX);
    char home[]="/tmp/stomme-defaults-XXXXXX",path[512],sidecar[512],backup[520];CHECK(mkdtemp(home) && !setenv("XDG_DATA_HOME",home,1));
    snprintf(path,sizeof path,"%s/settings",home);snprintf(sidecar,sizeof sidecar,"%s/tracker/devices/defaults.state",home);snprintf(backup,sizeof backup,"%s.saved",sidecar);
    for(int persistent=0;persistent<2;persistent++){
        fixture_t *f=open_fixture(path,persistent);tiku_trk_frame_t got={0};tiku_resource_snapshot_t out; tiku_msg_t *input=frame(31,42,600,400);
        policy(f,persistent,input);
        CHECK(!tiku_trk_frame_default(f->store,&got));CHECK(!change(f,input));tiku_msg_free(input);
        CHECK(tiku_trk_frame_default(f->store,&got) && got.x==31 && got.w==600 && f->bar.value.icon_size==16);
        CHECK(tiku_trk_frame_inherit(f->store,"/unvisited/folder",&got) && got.w==600 && !tiku_trk_frame_inherit(f->store,"/",&got));
        out=read_value(f);CHECK(tiku_resource_value_equal(out.desired,out.persisted) && tiku_resource_value_equal(out.desired,out.applied) && !strcmp(out.apply_state,"applied"));tiku_resource_snapshot_clear(&out);
        input=frame(0,0,179,400);CHECK(change(f,input)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
        input=frame(0,0,600,119);CHECK(change(f,input)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
        input=frame((int64_t)INT32_MAX+1,0,600,400);CHECK(change(f,input)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
        input=frame(0,0,600,400);member(input,"unexpected",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0));CHECK(change(f,input)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
        input=tiku_msg_new(0);CHECK(input && tiku_msg_add_string(input,"type","object"));
        member(input,"width",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,600));member(input,"height",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,400));member(input,"x",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,0));
        CHECK(change(f,input)==TIKU_RESOURCE_VALUE);tiku_msg_free(input);
        tiku_trk_frame_t parent={80,70,500,300};CHECK(tiku_trk_frame_save(f->store,"/parent",&parent,NULL));CHECK(tiku_trk_frame_inherit(f->store,"/parent/child",&got) && got.x==102 && got.w==500);
        input=tiku_msg_new(0);CHECK(input && tiku_msg_add_string(input,"type","object"));CHECK(!change(f,input));tiku_msg_free(input);CHECK(!tiku_trk_frame_default(f->store,&got));
        out=read_value(f);CHECK(!tiku_msg_count(out.persisted,"member"));tiku_resource_snapshot_clear(&out);
        input=frame(31,42,600,400);CHECK(!change(f,input));tiku_msg_free(input);
        tiku_trk_frame_t cached={90,80,620,420};tiku_trk_frame_set_default(f->store,&cached);
        tiku_trk_record_settings_poll(f->records,1000);out=read_value(f);
        CHECK(persistent ? (!tiku_resource_value_equal(out.desired,out.persisted) && !strcmp(out.apply_state,"pending_persistence")) : tiku_resource_value_equal(out.desired,out.persisted));
        tiku_resource_snapshot_clear(&out);
        drain();CHECK(tiku_trk_frame_default(f->store,&got) && got.w==620);
        tiku_store_update_t *held=tiku_store_update_begin(f->store);CHECK(held);
        input=frame(0,0,630,430);CHECK(change(f,input)==TIKU_RESOURCE_BUSY);tiku_msg_free(input);tiku_store_update_free(held);
        tiku_trk_record_settings_poll(f->records,2000);tiku_resource_job_info_t queued={0};input=frame(0,0,640,440);CHECK(!submit(f,input,&queued));tiku_msg_free(input);
        cached.w=650;tiku_trk_frame_set_default(f->store,&cached);drain();CHECK(outcome(f,&queued)==TIKU_RESOURCE_STALE && tiku_trk_frame_default(f->store,&got) && got.w==650);
        input=frame(0,0,660,460);CHECK(!change(f,input));tiku_msg_free(input);
        if(persistent){
            out=read_value(f);input=frame(0,0,661,460);CHECK(!change(f,input));
            tiku_resource_job_info_t stale={0};char stale_id[33];snprintf(stale_id,sizeof stale_id,"%032x",++serial);
            CHECK(tiku_resource_job_submit(f->registry,&caller,"00000000000000000000000000000002",DEFAULT_PATH,&out.stamp,stale_id,input,0,10000,&stale)==TIKU_RESOURCE_STALE);
            tiku_msg_free(input);tiku_resource_snapshot_clear(&out);input=frame(0,0,660,460);CHECK(!change(f,input));tiku_msg_free(input);
            tiku_store_t *disk=tiku_store_sidecar_open("defaults");CHECK(disk && tiku_trk_frame_default(disk,&got) && got.w==660);tiku_store_free(disk);
            CHECK(!rename(sidecar,backup) && !mkdir(sidecar,0700));int before=f->changes;
            input=frame(0,0,670,470);CHECK(change(f,input)==TIKU_RESOURCE_PERSISTENCE_FAILED);tiku_msg_free(input);
            CHECK(tiku_trk_frame_default(f->store,&got) && got.w==660 && strstr(tiku_trk_record_settings_status(f->records),"Could not save") && f->changes>before);
            CHECK(!rmdir(sidecar) && !rename(backup,sidecar));tiku_trk_record_settings_poll(f->records,3000);drain();CHECK(strstr(tiku_trk_record_settings_status(f->records),"Could not save"));
            atomic_int released=0;block(&released);input=frame(0,0,680,480);tiku_resource_job_info_t job={0};CHECK(!submit(f,input,&job));tiku_msg_free(input);
            disk=tiku_store_sidecar_open("defaults");cached.w=690;tiku_trk_frame_set_default(disk,&cached);CHECK(!tiku_state_write(disk,"/external","unknown","retained",9) && !tiku_store_flush(disk));tiku_store_free(disk);
            atomic_store(&released,1);drain();CHECK(outcome(f,&job)==TIKU_RESOURCE_STALE && tiku_trk_frame_default(f->store,&got) && got.w==660);
            tiku_trk_record_settings_poll(f->records,4000);drain();CHECK(tiku_trk_frame_default(f->store,&got) && got.w==690);
            char keep[16];CHECK(tiku_state_read(f->store,"/external","unknown",keep,sizeof keep)==9 && !strcmp(keep,"retained"));
            atomic_store(&released,0);block(&released);input=frame(0,0,700,500);CHECK(!submit(f,input,&job));tiku_msg_free(input);
            cached.w=710;tiku_trk_frame_set_default(f->store,&cached);atomic_store(&released,1);drain();CHECK(outcome(f,&job)==TIKU_RESOURCE_UNKNOWN_OUTCOME);
            out=read_value(f);CHECK(!tiku_resource_value_equal(out.desired,out.persisted) && !strcmp(out.apply_state,"pending_persistence"));tiku_resource_snapshot_clear(&out);
            input=frame(0,0,720,520);CHECK(!change(f,input));tiku_msg_free(input);CHECK(!*tiku_trk_record_settings_status(f->records));
            struct stat stat_before,stat_after;struct timespec times[2];CHECK(!stat(sidecar,&stat_before));
#if defined(__APPLE__)
            times[0]=stat_before.st_atimespec;times[1]=stat_before.st_mtimespec;
#else
            times[0]=stat_before.st_atim;times[1]=stat_before.st_mtim;
#endif
            disk=tiku_store_sidecar_open("defaults");cached.w=730;tiku_trk_frame_set_default(disk,&cached);CHECK(!tiku_store_flush(disk));tiku_store_free(disk);
            CHECK(!utimensat(AT_FDCWD,sidecar,times,0) && !stat(sidecar,&stat_after) && stat_after.st_size==stat_before.st_size);
            tiku_trk_record_settings_poll(f->records,5000);drain();CHECK(tiku_trk_frame_default(f->store,&got) && got.w==730);
            input=frame(0,0,720,520);CHECK(!change(f,input));tiku_msg_free(input);
        }
        close_fixture(f);
        if(persistent){f=open_fixture(path,1);CHECK(tiku_trk_frame_default(f->store,&got) && got.w==720);close_fixture(f);}
    }
    CHECK(!unlink(sidecar));snprintf(path,sizeof path,"%s/tracker/devices",home);CHECK(!rmdir(path));snprintf(path,sizeof path,"%s/tracker",home);CHECK(!rmdir(path));CHECK(!rmdir(home));
    puts("defaults: PASS native schema policy persistence conflicts cache restart and feedback");return 0;
}
