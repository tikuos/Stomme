/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_lifecycle_vfs.c - native application, service and job observations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_desktop_lifecycle_vfs_private.h"
#include "tiku_sys.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

static const tiku_trk_observation_t service_fields[]={
    {"/services/runner/identity",0,1},{"/services/runner/state",0,1},{"/services/runner/applications",1,1},{"/services/runner/failures",1,1},
    {"/services/supervisor/identity",0,1},{"/services/supervisor/state",0,1},{"/services/supervisor/child",0,1},
    {"/services/supervisor/last-exit",0,1},{"/services/supervisor/restart-count",0,1},{"/services/lifecycle",0,1}
};
static const tiku_trk_observation_t notification_fields[]={
    {"/notifications/owner",0,1},{"/notifications/pending",0,1},{"/notifications/status",0,1}
};

/** @brief Consume a same-user parent-published snapshot without blocking. */
static void supervisor_poll(struct desktop_lifecycle_vfs *owner)
{
    tiku_trk_supervisor_state_t snapshot;ssize_t size;
    if(owner->supervisor_available && owner->supervisor.supervisor!=getppid())owner->supervisor_available=0;
    if(owner->supervisor_fd<0)return;
    size=recv(owner->supervisor_fd,&snapshot,sizeof snapshot,MSG_DONTWAIT);
    if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return;
    if(size==sizeof snapshot&&snapshot.version==1&&snapshot.child==getpid()&&snapshot.supervisor==getppid()) {
        owner->supervisor=snapshot;owner->supervisor_available=1;
    }
    close(owner->supervisor_fd);owner->supervisor_fd=-1;
}

void desktop_lifecycle_vfs_prepare(desktop_t *app)
{
    struct desktop_lifecycle_vfs *owner;const char *number;char *end;long fd,pid=0;int handle;
    if(app->lifecycle_vfs)return;
    owner=calloc(1,sizeof *owner);if(!owner)return;owner->app=app;owner->supervisor_fd=-1;app->lifecycle_vfs=owner;
    number=getenv("STOMME_SUPERVISOR_FD");if(!number)return;
    errno=0;fd=strtol(number,&end,10);int invalid=errno||end==number||*end||fd<3||fd>INT_MAX;
    unsetenv("STOMME_SUPERVISOR_FD");if(invalid)return;
    handle=tiku_sys_process_pin((int)fd,&pid);
    if(handle<0||pid!=getppid()){if(handle>=0)close(handle);close((int)fd);return;}
    close(handle);
    if(fcntl((int)fd,F_SETFD,FD_CLOEXEC)<0){close((int)fd);return;}
    owner->supervisor_fd=(int)fd;
    struct pollfd ready={.fd=(int)fd,.events=POLLIN};(void)poll(&ready,1,100);
    supervisor_poll(owner);
}

int desktop_lifecycle_vfs_restarting(desktop_t *app)
{
    struct desktop_lifecycle_vfs *owner=app->lifecycle_vfs;if(!owner)return 0;
    supervisor_poll(owner);return owner->supervisor_available&&owner->supervisor.has_exit;
}

/** @brief Find loaded manifest metadata by its stable identity. */
static const tiku_trk_app_t *manifest(struct desktop_lifecycle_vfs *owner,const char *id)
{
    for(int i=0;i<owner->app->apps.count;i++)if(id[0]&&!strcmp(owner->app->apps.app[i].id,id))return &owner->app->apps.app[i];
    return NULL;
}

/** @brief Bind a peer PID to a currently owned child, excluding retained PID history. */
static void identify_child(lifecycle_app_t *record)
{
    tiku_trk_launch_observation_t child;
    for(int i=0;!tiku_trk_launch_observe(i,&child);i++)if(child.running&&child.pid==record->pid) {
        snprintf(record->manifest,sizeof record->manifest,"%s",child.app_id);return;
    }
}

/** @brief Copy connected, embedded and pending instances with bounded discovery status. */
static void instances(struct desktop_lifecycle_vfs *owner)
{
    desktop_t *app=owner->app;tiku_trk_launch_observation_t child;owner->app_count=0;owner->apps_complete=1;
    for(int i=0;i<TIKU_REMOTE_SESSIONS;i++)if(app->remote.session[i].used&&app->remote.session[i].fd>=0) {
        tiku_remote_session_t *session=&app->remote.session[i];lifecycle_app_t *record=&owner->apps[owner->app_count++];
        if(owner->peers[i].generation!=session->generation) {
            long pid=0;int handle=tiku_remote_process_handle(session,&pid);if(handle>=0)close(handle);
            owner->peers[i].generation=session->generation;owner->peers[i].pid=handle>=0?pid:0;
        }
        memset(record,0,sizeof *record);record->kind=0;record->index=i;record->generation=session->generation;record->pid=owner->peers[i].pid;
        snprintf(record->token,sizeof record->token,"remote-%llu",(unsigned long long)session->generation);
        snprintf(record->name,sizeof record->name,"%s",session->name);identify_child(record);
    }
    for(int i=0;i<TIKU_TRK_EMBED_MAX;i++)if(app->embeds[i].running) {
        lifecycle_app_t *record=&owner->apps[owner->app_count++];memset(record,0,sizeof *record);
        record->kind=1;record->index=i;record->generation=0;record->pid=getpid();
        for(int j=0;j<TIKU_TRK_EMBED_WINDOWS;j++)if(app->embeds[i].win[j].window&&app->embeds[i].win[j].window->open) {
            uint64_t generation=app->embeds[i].win[j].window->generation;
            if(!record->generation||generation<record->generation)record->generation=generation;
        }
        snprintf(record->token,sizeof record->token,"embedded-%d-%llu",i,(unsigned long long)record->generation);
        snprintf(record->name,sizeof record->name,"%s",app->embeds[i].descriptor->name);
        snprintf(record->manifest,sizeof record->manifest,"%s",app->embeds[i].descriptor->id);
    }
    for(int i=0;!tiku_trk_launch_observe(i,&child);i++)if(child.running) {
        int seen=0;for(int j=0;j<owner->app_count;j++)if(owner->apps[j].pid==child.pid)seen=1;
        if(seen)continue;
        if(owner->app_count==64){owner->apps_complete=0;continue;}
        lifecycle_app_t *record=&owner->apps[owner->app_count++];memset(record,0,sizeof *record);
        record->kind=2;record->pid=child.pid;record->generation=child.generation;
        snprintf(record->token,sizeof record->token,"child-%llu",child.generation);snprintf(record->name,sizeof record->name,"%s",child.name);
        snprintf(record->manifest,sizeof record->manifest,"%s",child.app_id);
    }
}

/** @brief Resolve a copied instance token and recheck the live native incarnation. */
static lifecycle_app_t *instance(struct desktop_lifecycle_vfs *owner,const char *token)
{
    for(int i=0;i<owner->app_count;i++)if(!strcmp(owner->apps[i].token,token)) {
        lifecycle_app_t *record=&owner->apps[i];desktop_t *app=owner->app;
        if(record->kind==0) {
            tiku_remote_session_t *session=&app->remote.session[record->index];
            if(session->used&&session->fd>=0&&session->generation==record->generation)return record;
        } else if(record->kind==1) {
            if(app->embeds[record->index].running) {
                for(int j=0;j<TIKU_TRK_EMBED_WINDOWS;j++)if(app->embeds[record->index].win[j].window&&app->embeds[record->index].win[j].window->open &&
                    app->embeds[record->index].win[j].window->generation==record->generation)return record;
            }
        } else {
            tiku_trk_launch_observation_t child;
            for(int j=0;!tiku_trk_launch_observe(j,&child);j++)if(child.running&&child.generation==record->generation&&child.pid==record->pid)return record;
        }
    }
    return NULL;
}

/** @brief Whether one native window belongs to this exact instance. */
static int owns(struct desktop_lifecycle_vfs *owner,const lifecycle_app_t *record,tiku_window_t *window)
{
    if(record->kind==0)return tiku_remote_owner(&owner->app->remote,window)==&owner->app->remote.session[record->index];
    if(record->kind==1) {
        struct embed_win *embedded=embed_win_of_window(owner->app,window);
        return embedded&&embedded->slot==&owner->app->embeds[record->index];
    }
    return 0;
}

/** @brief Copy app identity, loaded manifest, native windows and current capabilities. */
static tiku_msg_t *app_value(struct desktop_lifecycle_vfs *owner,const char *token,const char *field)
{
    lifecycle_app_t *record=instance(owner,token);tiku_msg_t *out=trk_value_container("object");int bad=0,count=0;
    if(!record){tiku_msg_free(out);return NULL;}
    for(int i=0;i<tiku_workspace_count(owner->app->desk);i++) {
        tiku_window_t *window=tiku_workspace_at(owner->app->desk,i);
        if(owns(owner,record,window)) {
            if(!strcmp(field,"windows")) {
                char id[33],key[24];snprintf(key,sizeof key,"%d",count);
                if(tiku_trk_window_resources_id(owner->app->window_resources,window,id,sizeof id)){tiku_msg_free(out);return NULL;}
                bad|=LIFE_TEXT(out,key,id);
            }
            count++;
        }
    }
    if(!strcmp(field,"identity")) {
        bad|=LIFE_TEXT(out,"token",token);bad|=LIFE_TEXT(out,"name",record->name);bad|=LIFE_INT(out,"pid",record->pid);
        bad|=lifecycle_counter(out,"generation",record->generation);bad|=LIFE_TEXT(out,"session",tiku_resource_registry_session(owner->registry));
    } else if(!strcmp(field,"manifest")) {
        const tiku_trk_app_t *item=manifest(owner,record->manifest);
        bad|=LIFE_TEXT(out,"id",record->manifest);bad|=LIFE_BOOL(out,"verified_child",record->kind==2||(record->kind==0&&record->manifest[0]));
        bad|=LIFE_BOOL(out,"loaded",item!=NULL);bad|=LIFE_TEXT(out,"name",item?item->name:record->name);bad|=LIFE_TEXT(out,"module",item?item->module:"");
    } else if(!strcmp(field,"capabilities")) {
        bad|=LIFE_BOOL(out,"activate",record->kind!=2&&count>0);bad|=LIFE_BOOL(out,"request_quit",record->kind!=2&&count>0);
        bad|=LIFE_BOOL(out,"force_quit",0);bad|=LIFE_TEXT(out,"quit_completion","application_owned");
    } else if(!strcmp(field,"state")) {
        bad|=LIFE_TEXT(out,"phase",record->kind==2?"starting":"connected");bad|=LIFE_BOOL(out,"running",1);bad|=LIFE_INT(out,"window_count",count);
    }
    if(bad){tiku_msg_free(out);return NULL;}return out;
}

/** @brief Locate a copied worker observation by its immutable pool generation. */
static tiku_work_observation_t *work_item(struct desktop_lifecycle_vfs *owner,const char *token)
{
    char expected[96];
    for(size_t i=0;i<owner->work_count;i++){snprintf(expected,sizeof expected,"work-%llu",owner->work[i].id);if(!strcmp(expected,token))return &owner->work[i];}
    return NULL;
}

/** @brief Copy worker lifecycle fields without claiming application-level success. */
static tiku_msg_t *work_value(struct desktop_lifecycle_vfs *owner,const char *token,const char *field)
{
    tiku_work_observation_t *item=work_item(owner,token);tiku_msg_t *out=trk_value_container("object");int bad=0;
    static const char *const states[]={"queued","running","finished","cancelled"};
    if(!item){tiku_msg_free(out);return NULL;}
    if(!strcmp(field,"identity")){bad|=LIFE_TEXT(out,"kind","worker");bad|=lifecycle_counter(out,"generation",item->id);bad|=LIFE_TEXT(out,"key",item->key);}
    else if(!strcmp(field,"owner")){bad|=LIFE_TEXT(out,"owner","desktop-work-pool");bad|=LIFE_INT(out,"pid",getpid());bad|=LIFE_BOOL(out,"slow",item->slow);}
    else if(!strcmp(field,"state")){bad|=LIFE_TEXT(out,"phase",states[item->state]);bad|=LIFE_INT(out,"posted_ms",item->posted_ms);bad|=LIFE_INT(out,"began_ms",item->began_ms);bad|=LIFE_INT(out,"ended_ms",item->ended_ms);}
    else if(!strcmp(field,"cancellation")){bad|=LIFE_BOOL(out,"supported",0);bad|=LIFE_BOOL(out,"requested",item->cancellation_requested);bad|=LIFE_TEXT(out,"reason","owner_cleanup_required");}
    else {bad|=LIFE_BOOL(out,"supported",0);if(!strcmp(field,"result"))bad|=LIFE_BOOL(out,"lifecycle_complete",item->state>=2);}
    if(bad){tiku_msg_free(out);return NULL;}return out;
}

/** @brief Copy the file engine's actual progress, cancellation and result counts. */
static tiku_msg_t *file_value(struct desktop_lifecycle_vfs *owner,const char *field)
{
    const tiku_progress_t *progress=&owner->file_progress;tiku_msg_t *out=trk_value_container("object");int bad=0;
    if(!strcmp(field,"identity")){bad|=LIFE_TEXT(out,"kind","filesystem");bad|=lifecycle_counter(out,"generation",owner->file_generation);}
    else if(!strcmp(field,"owner")){bad|=LIFE_TEXT(out,"owner","desktop-file-engine");bad|=LIFE_INT(out,"pid",getpid());}
    else if(!strcmp(field,"state"))bad|=LIFE_TEXT(out,"phase",owner->file_busy?(progress->conflict||progress->error_ask?"awaiting_confirmation":"running"):progress->cancelled?"cancelled":progress->failed?"completed_with_errors":"finished");
    else if(!strcmp(field,"progress")){bad|=LIFE_BOOL(out,"supported",1);bad|=LIFE_INT(out,"done",progress->done);bad|=LIFE_INT(out,"total",progress->total);bad|=LIFE_INT(out,"bytes",progress->bytes);bad|=LIFE_TEXT(out,"current",progress->current);bad|=LIFE_TEXT(out,"destination",progress->to);}
    else if(!strcmp(field,"cancellation")){bad|=LIFE_BOOL(out,"supported",owner->file_busy);bad|=LIFE_BOOL(out,"requested",progress->cancelled);}
    else if(!strcmp(field,"error")){bad|=LIFE_BOOL(out,"supported",1);bad|=LIFE_TEXT(out,"text",progress->error);bad|=LIFE_BOOL(out,"awaiting_answer",progress->error_ask);}
    else {bad|=LIFE_BOOL(out,"supported",1);bad|=LIFE_INT(out,"done",progress->done);bad|=LIFE_INT(out,"skipped",progress->skipped);bad|=LIFE_INT(out,"failed",progress->failed);bad|=LIFE_BOOL(out,"cancelled",progress->cancelled);}
    if(bad){tiku_msg_free(out);return NULL;}return out;
}

/** @brief Enumerate copied native tokens with bounded family capacities. */
static int enumerate(void *opaque,tiku_trk_lifecycle_kind_t kind,int index,char *out,size_t capacity)
{
    struct desktop_lifecycle_vfs *owner=opaque;
    if(kind==TIKU_TRK_LIFECYCLE_APP){if(index<0||index>=owner->app_count)return -1;snprintf(out,capacity,"%s",owner->apps[index].token);}
    else if(kind==TIKU_TRK_LIFECYCLE_WORK){if(index<0||(size_t)index>=owner->work_count)return -1;snprintf(out,capacity,"work-%llu",owner->work[index].id);}
    else {if(index||!owner->file_available)return -1;snprintf(out,capacity,"file-%llu",(unsigned long long)owner->file_generation);}
    return 0;
}

/** @brief Capture one current native object without registry mutation. */
static tiku_msg_t *capture(void *opaque,tiku_trk_lifecycle_kind_t kind,const char *token,const char *field)
{
    struct desktop_lifecycle_vfs *owner=opaque;
    if(kind==TIKU_TRK_LIFECYCLE_APP)return app_value(owner,token,field);
    if(kind==TIKU_TRK_LIFECYCLE_WORK)return work_value(owner,token,field);
    return file_value(owner,field);
}

/** @brief Recheck current owners before permitting an app or native file command. */
static tiku_resource_error_t validate(void *opaque,tiku_trk_lifecycle_kind_t kind,const char *token,const char *action,const tiku_msg_t *value)
{
    struct desktop_lifecycle_vfs *owner=opaque;(void)value;
    if(kind==TIKU_TRK_LIFECYCLE_WORK)return work_item(owner,token)?TIKU_RESOURCE_UNSUPPORTED:TIKU_RESOURCE_OWNER_LOST;
    if(kind==TIKU_TRK_LIFECYCLE_FILE) {
        uint64_t generation;int busy;tiku_progress_t progress;char expected[96];
        if(!tiku_fs_observe(owner->app->fs,&generation,&progress,&busy))return TIKU_RESOURCE_OWNER_LOST;
        snprintf(expected,sizeof expected,"file-%llu",(unsigned long long)generation);
        if(strcmp(token,expected))return TIKU_RESOURCE_OWNER_LOST;
        return busy?TIKU_RESOURCE_OK:TIKU_RESOURCE_UNSUPPORTED;
    }
    lifecycle_app_t *record=instance(owner,token);if(!record)return TIKU_RESOURCE_OWNER_LOST;
    if(record->kind==2)return TIKU_RESOURCE_UNAVAILABLE;
    if(strcmp(action,"activate")&&strcmp(action,"request-quit"))return TIKU_RESOURCE_UNSUPPORTED;
    for(int i=0;i<tiku_workspace_count(owner->app->desk);i++)if(owns(owner,record,tiku_workspace_at(owner->app->desk,i)))return 0;
    return TIKU_RESOURCE_UNAVAILABLE;
}

/** @brief Route an app lifecycle request or a generation-bound file cancellation. */
static tiku_resource_error_t perform(void *opaque,tiku_trk_lifecycle_kind_t kind,const char *token,const char *action,const tiku_msg_t *value,tiku_msg_t **out)
{
    struct desktop_lifecycle_vfs *owner=opaque;desktop_t *app=owner->app;tiku_resource_error_t error=validate(opaque,kind,token,action,value);
    tiku_msg_t *result=trk_value_container("object");if(error){tiku_msg_free(result);return error;}
    if(LIFE_TEXT(result,"token",token)||LIFE_TEXT(result,"action",action)){tiku_msg_free(result);return TIKU_RESOURCE_NO_MEMORY;}
    if(kind==TIKU_TRK_LIFECYCLE_FILE) {
        uint64_t generation;int busy;tiku_progress_t progress;
        if(!tiku_fs_observe(app->fs,&generation,&progress,&busy)||!tiku_fs_cancel_generation(app->fs,owner->file_generation)){tiku_msg_free(result);return TIKU_RESOURCE_OWNER_LOST;}
        tiku_alert_t *alert=alert_model(app);
        if((progress.conflict&&alert->tag==TIKU_TRK_ALERT_TAG_CONFLICT)||(progress.error_ask&&alert->tag==TIKU_TRK_ALERT_TAG_ERROR))alert->open=0;
    } else {
        lifecycle_app_t *record=instance(owner,token);tiku_window_t *window=NULL;
        for(int i=tiku_workspace_count(app->desk)-1;i>=0;i--)if(owns(owner,record,tiku_workspace_at(app->desk,i))){window=tiku_workspace_at(app->desk,i);break;}
        if(!window){tiku_msg_free(result);return TIKU_RESOURCE_OWNER_LOST;}
        if(!strcmp(action,"activate")){tiku_workspace_restore(app->desk,window);tiku_workspace_activate(app->desk,window);}
        else if(record->kind==0) {
            if(!tiku_remote_request_quit(&app->remote,window)){tiku_msg_free(result);return TIKU_RESOURCE_BUSY;}
        } else {
            struct embed_win *embedded=embed_win_of_window(app,window);tiku_event_t event={.type=TIKU_EVENT_KEY_DOWN,.key='q',.modifiers=TIKU_MOD_CMD};
            if(!embedded){tiku_msg_free(result);return TIKU_RESOURCE_OWNER_LOST;}(void)embed_deliver(embedded,&event);
        }
    }
    desktop_damage_all(app);*out=result;return 0;
}

/** @brief Count all current children and retained failure rows from the launch owner. */
static int observed_count(void *opaque,const char *path)
{
    tiku_trk_launch_observation_t child;int count=0;(void)opaque;
    for(int i=0;!tiku_trk_launch_observe(i,&child);i++)if(!strcmp(path,"/services/runner/applications")||(!child.running&&!child.expected&&(child.signal||child.status)))count++;
    return count;
}

/** @brief Copy one tracked launch or retained native exit report. */
static tiku_msg_t *child_value(const char *path,int index)
{
    tiku_trk_launch_observation_t child;tiku_msg_t *out=trk_value_container("object");int bad=0;
    for(int i=0;!tiku_trk_launch_observe(i,&child);i++) {
        if(strcmp(path,"/services/runner/applications")&&(child.running||child.expected||(!child.signal&&!child.status)))continue;
        if(index--)continue;
        bad|=lifecycle_counter(out,"generation",child.generation);bad|=LIFE_INT(out,"pid",child.pid);bad|=LIFE_TEXT(out,"name",child.name);
        bad|=LIFE_TEXT(out,"app_id",child.app_id);bad|=LIFE_BOOL(out,"running",child.running);bad|=LIFE_INT(out,"signal",child.signal);
        bad|=LIFE_INT(out,"status",child.status);bad|=LIFE_BOOL(out,"expected",child.expected);bad|=LIFE_TEXT(out,"report",child.report);
        if(bad){tiku_msg_free(out);return NULL;}return out;
    }
    tiku_msg_free(out);return NULL;
}

/** @brief Copy the supervisor's cached record and existing notification owners. */
static tiku_msg_t *observed_value(void *opaque,const char *path,int index)
{
    struct desktop_lifecycle_vfs *owner=opaque;desktop_t *app=owner->app;tiku_msg_t *out;int bad=0;
    if(!strcmp(path,"/services/runner/applications")||!strcmp(path,"/services/runner/failures"))return child_value(path,index);
    out=trk_value_container("object");
    if(!strncmp(path,"/services/supervisor/",21)) {
        tiku_trk_supervisor_state_t *state=&owner->supervisor;int available=owner->supervisor_available;
        bad|=LIFE_BOOL(out,"available",available);
        if(!strcmp(path,"/services/supervisor/identity")){bad|=LIFE_INT(out,"pid",available?state->supervisor:0);bad|=LIFE_TEXT(out,"source","authenticated_parent_socket");}
        else if(!strcmp(path,"/services/supervisor/child")){bad|=LIFE_INT(out,"pid",available?state->child:0);bad|=LIFE_BOOL(out,"running",available&&app->running);}
        else if(!strcmp(path,"/services/supervisor/last-exit")){bad|=LIFE_BOOL(out,"known",available&&state->has_exit);bad|=LIFE_INT(out,"signal",available?state->signal:0);bad|=LIFE_INT(out,"status",available?state->status:0);}
        else if(!strcmp(path,"/services/supervisor/restart-count"))bad|=LIFE_INT(out,"count",available?state->restart_count:0);
        else bad|=LIFE_TEXT(out,"phase",available?"waiting_for_child":"not_supervised");
    } else if(!strcmp(path,"/services/runner/identity")){bad|=LIFE_TEXT(out,"mode","per_application_process");bad|=LIFE_TEXT(out,"executable",app->runner);}
    else if(!strcmp(path,"/services/runner/state")){bad|=LIFE_BOOL(out,"available",app->runner[0]);bad|=LIFE_INT(out,"tracked_children",tiku_trk_launch_running());bad|=LIFE_TEXT(out,"phase",tiku_trk_launch_running()?"running":"on_demand");}
    else if(!strcmp(path,"/services/lifecycle")){int expected,registered;tiku_trk_lifecycle_resources_counts(owner->entities,&expected,&registered);bad|=LIFE_INT(out,"expected_entities",expected);bad|=LIFE_INT(out,"registered_entities",registered);bad|=LIFE_BOOL(out,"registration_complete",expected==registered);bad|=LIFE_BOOL(out,"instances_complete",owner->apps_complete);bad|=LIFE_INT(out,"instances",owner->app_count);bad|=LIFE_INT(out,"instance_capacity",64);bad|=LIFE_INT(out,"observed_workers",owner->work_count);bad|=LIFE_INT(out,"worker_history_capacity",64);}
    else if(!strcmp(path,"/notifications/owner")){bad|=LIFE_TEXT(out,"owner","desktop-alert-controller");bad|=LIFE_INT(out,"pid",getpid());}
    else if(!strcmp(path,"/notifications/pending")) {
        tiku_alert_t *alert=alert_model(app);bad|=LIFE_BOOL(out,"open",alert->open);
        bad|=LIFE_INT(out,"kind",alert->kind);bad|=LIFE_INT(out,"tag",alert->tag);bad|=LIFE_TEXT(out,"text",alert->open?alert->text:"");
        bad|=LIFE_INT(out,"button_count",alert->open?alert->nbutton:0);bad|=LIFE_INT(out,"chosen",alert->chosen);
    } else {bad|=LIFE_TEXT(out,"status",app->status);bad|=LIFE_BOOL(out,"progress_open",progress_model(app)->open);bad|=LIFE_BOOL(out,"running",app->running);}
    if(bad){tiku_msg_free(out);return NULL;}return out;
}

void desktop_lifecycle_vfs_poll(desktop_t *app,uint64_t now)
{
    struct desktop_lifecycle_vfs *owner=app->lifecycle_vfs;if(!owner)return;supervisor_poll(owner);
    if(!owner->registry||now<owner->next_poll)return;
    owner->next_poll=now+250;
    instances(owner);owner->work_count=tiku_work_observe(owner->work,128);if(owner->work_count>128)owner->work_count=128;
    owner->file_available=tiku_fs_observe(app->fs,&owner->file_generation,&owner->file_progress,&owner->file_busy);
    desktop_lifecycle_actions_poll(owner);
    tiku_trk_lifecycle_resources_poll(owner->entities);tiku_trk_app_resources_poll(owner->catalog,now);
    tiku_trk_resource_observer_poll(owner->services,now);tiku_trk_resource_observer_poll(owner->notifications,now);
}

int desktop_lifecycle_vfs_open(desktop_t *app)
{
    const tiku_trk_lifecycle_host_t host={.enumerate=enumerate,.capture=capture,.validate=validate,.perform=perform};
    struct desktop_lifecycle_vfs *owner;desktop_lifecycle_vfs_prepare(app);owner=app->lifecycle_vfs;if(!owner)return -1;
    owner->registry=tiku_trk_resource_session_registry(app->resource_session);if(!owner->registry)return 0;
    instances(owner);owner->work_count=tiku_work_observe(owner->work,128);if(owner->work_count>128)owner->work_count=128;
    owner->file_available=tiku_fs_observe(app->fs,&owner->file_generation,&owner->file_progress,&owner->file_busy);
    owner->entities=tiku_trk_lifecycle_resources_open(owner->registry,&host,owner);
    owner->catalog=tiku_trk_app_resources_open(owner->registry,&app->apps,desktop_lifecycle_catalog_count,desktop_lifecycle_catalog_capture,desktop_lifecycle_catalog_run,owner);
    owner->services=tiku_trk_resource_observer_open(owner->registry,"native-services","/services",service_fields,sizeof service_fields/sizeof service_fields[0],observed_count,observed_value,owner);
    owner->notifications=tiku_trk_resource_observer_open(owner->registry,"native-notifications","/notifications",notification_fields,3,observed_count,observed_value,owner);
    if(!owner->entities||!owner->catalog||!owner->services||!owner->notifications||desktop_lifecycle_actions_open(owner))return -1;
    return 0;
}

void desktop_lifecycle_vfs_close(desktop_t *app)
{
    struct desktop_lifecycle_vfs *owner=app->lifecycle_vfs;if(!owner)return;
    desktop_lifecycle_actions_close(owner);tiku_trk_resource_observer_close(owner->notifications);tiku_trk_resource_observer_close(owner->services);
    tiku_trk_app_resources_close(owner->catalog);tiku_trk_lifecycle_resources_close(owner->entities);
    if(owner->supervisor_fd>=0)close(owner->supervisor_fd);
    free(owner);app->lifecycle_vfs=NULL;
}
