/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_session.c - mounted desktop service and independent SDK client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_session.h"
#include "tiku_resource_endpoint.h"
#include "tiku_mount.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(test) do {if(!(test)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test);exit(1);}}while(0)

/** @brief Send one authenticated request and consume its result. */
static tiku_msg_t *call(tiku_resource_channel_t *channel,const char *op,const char *path,unsigned serial,const tiku_msg_t *expected)
{
    char id[33];tiku_msg_t *request=tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE),*reply=NULL,*result,*value=NULL;
    snprintf(id,sizeof id,"%032x",serial);
    CHECK(request && tiku_msg_add_string(request,"op",op) && tiku_msg_add_string(request,"request_id",id) &&
        tiku_msg_add_string(request,"session",tiku_resource_channel_session(channel)));
    if(path)CHECK(tiku_msg_add_string(request,"path",path));
    if(expected){
        tiku_msg_t *tuple=tiku_msg_new(0);
        CHECK(tuple && tiku_msg_add_string(tuple,"provider",tiku_msg_find_string(expected,"provider",0)) &&
            tiku_msg_add_string(tuple,"incarnation",tiku_msg_find_string(expected,"incarnation",0)) &&
            tiku_msg_add_string(tuple,"revision",tiku_msg_find_string(expected,"revision",0)) && tiku_msg_add_message(request,"expected",tuple));tiku_msg_free(tuple);
        if(!strcmp(op,"set")){
            value=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1);
            CHECK(value && tiku_msg_add_message(request,"value",value) && tiku_msg_add_string(request,"durability","user"));
        }else{
            value=tiku_msg_new(0);CHECK(value && tiku_msg_add_string(value,"type","object") && tiku_msg_add_message(request,"arguments",value));
        }
        CHECK(tiku_msg_add_int32(request,"deadline_ms",1000));tiku_msg_free(value);
    }
    if(!strcmp(op,"subscribe"))CHECK(tiku_msg_add_string(request,"provider","test"));
    CHECK(!tiku_resource_channel_send(channel,request,5000) && !tiku_resource_channel_receive(channel,&reply,5000));
    result=tiku_msg_find_message(reply,"result",0);CHECK(result && !tiku_msg_count(reply,"error"));tiku_msg_free(request);tiku_msg_free(reply);return result;
}

/** @brief Exercise the public channel from a separate process. */
static void client(const char *directory)
{
    tiku_resource_channel_t *channel=tiku_resource_endpoint_connect(directory,5000);
    tiku_msg_t *stamp,*result,*event=NULL;
    CHECK(channel && !tiku_resource_channel_negotiate(channel,7,0,5000));
    stamp=call(channel,"describe","/test/enabled",1,NULL);
    result=call(channel,"subscribe","/test",2,NULL);tiku_msg_free(result);
    result=call(channel,"set","/test/enabled",3,stamp);tiku_msg_free(stamp);stamp=result;
    CHECK(!tiku_resource_channel_receive(channel,&event,5000) && !strcmp(tiku_msg_find_string(event,"kind",0),"changed"));tiku_msg_free(event);
    result=call(channel,"invoke","/test/stop",4,stamp);tiku_msg_free(result);tiku_msg_free(stamp);tiku_resource_channel_free(channel);
}

/** @brief Observe current external subscriptions without telemetry feedback. */
static void discovery_events(tiku_trk_resource_session_t *session,
                             const tiku_resource_caller_t *reader)
{
    tiku_resource_registry_t *registry =
        tiku_trk_resource_session_registry(session);
    tiku_resource_watch_t watched, telemetry;
    tiku_msg_t *initial = NULL;
    CHECK(!tiku_resource_watch_open(registry, reader, "test", "/test",
                                    &watched, &initial));
    tiku_msg_free(initial);
    initial = NULL;
    CHECK(!tiku_resource_watch_open(registry, reader, "events", "/events",
                                    &telemetry, &initial));
    tiku_msg_free(initial);
    tiku_resource_event_observation_t observations;
    CHECK(tiku_resource_events_observe(registry, "events", &observations));
    CHECK(observations.subscriptions == 1 && !observations.maximum_sequence);
    tiku_resource_snapshot_t before = {0}, next = {0};
    CHECK(!tiku_resource_read(registry, reader, "/test/enabled", &before));
    next.desired = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    next.persisted = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    next.applied = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 1);
    strcpy(next.apply_state, "observed");
    tiku_resource_stamp_t stamp;
    CHECK(!tiku_resource_publish(registry, &before.stamp, "/test/enabled",
                                 &next, &stamp));
    CHECK(tiku_resource_events_observe(registry, "events", &observations));
    CHECK(observations.maximum_sequence == 1 && observations.queued == 1);
    (void)tiku_trk_resource_session_pump(session);
    tiku_resource_snapshot_t sequence = {0}, stable = {0};
    CHECK(
        !tiku_resource_read(registry, reader, "/events/sequence", &sequence));
    usleep(150000);
    (void)tiku_trk_resource_session_pump(session);
    CHECK(!tiku_resource_read(registry, reader, "/events/sequence", &stable));
    CHECK(sequence.stamp.revision == stable.stamp.revision);
    tiku_resource_snapshot_clear(&before);
    tiku_resource_snapshot_clear(&next);
    tiku_resource_snapshot_clear(&sequence);
    tiku_resource_snapshot_clear(&stable);
    tiku_resource_watch_close(registry, watched);
    tiku_resource_watch_close(registry, telemetry);
    tiku_resource_snapshot_t reset = {0};
    reset.desired = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    reset.persisted = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    reset.applied = tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN, 0);
    strcpy(reset.apply_state, "applied");
    CHECK(!tiku_resource_publish(registry, &stamp, "/test/enabled", &reset,
                                 &stamp));
    tiku_resource_snapshot_clear(&reset);
}

int main(void)
{
    char directory[96],collision[96],mount[32],path[128],data[32],error[64];
    const char *temporary=getenv("STOMME_RESOURCE_TEST_TMP")?getenv("STOMME_RESOURCE_TEST_TMP"):"/tmp";
    tiku_backend_t *root=tiku_backend_posix_open(),*namespace_backend=tiku_mount_open();
    tiku_trk_resource_session_t *session;tiku_model_t model;int status,stop=0;pid_t child;unsigned i;
    snprintf(directory,sizeof directory,"%s/r-XXXXXX",temporary);snprintf(collision,sizeof collision,"%s/c-XXXXXX",temporary);
    CHECK(root && namespace_backend && !tiku_mount_add(namespace_backend,"/",root,1) && mkdtemp(directory) && mkdtemp(collision));
    CHECK(!tiku_trk_resource_session_open(namespace_backend,directory,collision,"test",1));CHECK(!rmdir(collision));
    snprintf(mount,sizeof mount,"/sv2-%ld",(long)getpid());session=tiku_trk_resource_session_open(namespace_backend,directory,mount,"test",1);CHECK(session);
    const char *discovery[] = {"/about/product",
                               "/about/abi",
                               "/about/foundation-revision",
                               "/about/host",
                               "/about/sdk-identity",
                               "/about/version",
                               "/capabilities/features",
                               "/capabilities/limits",
                               "/capabilities/protocol",
                               "/capabilities/providers",
                               "/events/incarnation",
                               "/events/overflow",
                               "/events/sequence",
                               "/events/snapshots",
                               "/schema/descriptors",
                               "/schema/permissions",
                               "/schema/resource-kinds",
                               "/schema/schema-versions"};
    tiku_resource_caller_t reader = {.authenticated = 1,
                                     .scopes = TIKU_RESOURCE_READ};
    strcpy(reader.principal, "00000000000000000000000000000001");
    for (size_t n = 0; n < sizeof discovery / sizeof discovery[0]; n++) {
        tiku_msg_t *description = NULL;
        tiku_resource_snapshot_t observed = {0};
        CHECK(!tiku_resource_describe(
            tiku_trk_resource_session_registry(session), &reader, discovery[n],
            &description));
        CHECK(!strcmp(tiku_msg_find_string(description, "kind", 0), "state"));
        CHECK(!tiku_resource_read(tiku_trk_resource_session_registry(session),
                                  &reader, discovery[n], &observed));
        CHECK(observed.applied &&
              !tiku_resource_value_check(observed.applied));
        if (!strcmp(discovery[n], "/about/sdk-identity"))
            CHECK(!strcmp(tiku_msg_find_string(observed.applied, "value", 0),
                          TIKU_SDK_IDENTITY));
        tiku_resource_snapshot_clear(&observed);
        tiku_msg_free(description);
    }
    discovery_events(session, &reader);
    CHECK(!tiku_trk_resource_session_open(namespace_backend,directory,"/sv2-other","test",1));
    snprintf(path,sizeof path,"%s/test/enabled",mount);CHECK(!namespace_backend->ops->stat(namespace_backend,path,&model));
    CHECK(namespace_backend->ops->read(namespace_backend,path,data,sizeof data)==6 && !memcmp(data,"false\n",6));
    CHECK(namespace_backend->ops->write(namespace_backend,path,"true\n",5,error,sizeof error)<0 && !strcmp(error,"requires_context"));
    fflush(NULL);child=fork();CHECK(child>=0);if(!child){client(directory);exit(0);}
    for(i=0;i<10000 && !stop;i++){stop=tiku_trk_resource_session_pump(session);usleep(1000);}
    CHECK(stop && waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    CHECK(namespace_backend->ops->read(namespace_backend,path,data,sizeof data)==5 && !memcmp(data,"true\n",5));
    tiku_trk_resource_session_close(session);errno=0;CHECK(namespace_backend->ops->stat(namespace_backend,mount,&model)<0);
    snprintf(path,sizeof path,"%s/control.sock",directory);CHECK(access(path,F_OK)<0 && !rmdir(directory));
    tiku_backend_close(namespace_backend);puts("PASS desktop resource session: mount, collision, SDK client, change, watch, shutdown");return 0;
}
