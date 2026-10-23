/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_service.c - broker handles, retained results and local clients.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "resource_fixture.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Read one connection-bound enumeration page. */
static tiku_msg_t *list(resource_fixture_t *fixture,tiku_resource_service_t *service,
    const char *path,const char *cursor,unsigned serial,const char *error)
{
    tiku_msg_t *request=resource_test_request(fixture,"list",path,serial),*result;
    RES_CHECK(tiku_msg_add_int32(request,"page_size",1));
    if(cursor)RES_CHECK(tiku_msg_add_string(request,"cursor",cursor));
    result=resource_test_call(service,&fixture->caller,request,error);tiku_msg_free(request);return result;
}

/** @brief Check cursor ownership, paths, epochs, expiry and resource discovery. */
static void cursors_test(void)
{
    resource_fixture_t fixture;
    tiku_msg_t *request,*result,*entry,*limits;
    char cursor[33],first[33];
    resource_test_open(&fixture,0);
    request=resource_test_request(&fixture,"describe","/",1);
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);
    limits=tiku_msg_find_message(result,"limits",0);RES_CHECK(limits);
    tiku_msg_free(limits);tiku_msg_free(request);tiku_msg_free(result);
    result=list(&fixture,fixture.a,"/settings",NULL,2,NULL);
    strcpy(cursor,tiku_msg_find_string(result,"next_cursor",0));
    entry=tiku_msg_find_message(result,"entry",0);RES_CHECK(entry);
    strcpy(first,tiku_msg_find_string(entry,"id",0));tiku_msg_free(entry);tiku_msg_free(result);
    result=list(&fixture,fixture.b,"/settings",cursor,3,"stale_cursor");tiku_msg_free(result);
    result=list(&fixture,fixture.a,"/work",cursor,4,"stale_cursor");tiku_msg_free(result);
    result=list(&fixture,fixture.a,"/settings",cursor,5,NULL);
    entry=tiku_msg_find_message(result,"entry",0);RES_CHECK(entry && strcmp(first,tiku_msg_find_string(entry,"id",0)));
    tiku_msg_free(entry);tiku_msg_free(result);
    result=list(&fixture,fixture.a,"/settings",NULL,6,NULL);strcpy(cursor,tiku_msg_find_string(result,"next_cursor",0));tiku_msg_free(result);
    request=resource_test_set(&fixture,"/settings/enabled",&fixture.settings,7,1);
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);fixture.settings=resource_test_stamp(result);
    tiku_msg_free(request);tiku_msg_free(result);
    result=list(&fixture,fixture.a,"/settings",cursor,8,"stale_cursor");tiku_msg_free(result);
    result=list(&fixture,fixture.a,"/settings",NULL,9,NULL);strcpy(cursor,tiku_msg_find_string(result,"next_cursor",0));tiku_msg_free(result);
    fixture.now+=30001;
    result=list(&fixture,fixture.a,"/settings",cursor,10,"stale_cursor");tiku_msg_free(result);
    resource_test_close(&fixture);
    resource_test_open(&fixture,0);
    { unsigned i;for(i=0;i<16;i++){result=list(&fixture,fixture.a,"/settings",NULL,i+1,NULL);tiku_msg_free(result);} }
    result=list(&fixture,fixture.a,"/settings",NULL,17,"busy");tiku_msg_free(result);
    resource_test_close(&fixture);
    resource_test_open(&fixture,0);
    {
        tiku_resource_service_t *extra[TIKU_RESOURCE_CONNECTIONS_MAX-2];unsigned i;
        for(i=0;i<TIKU_RESOURCE_CONNECTIONS_MAX-2;i++){extra[i]=tiku_resource_service_new(fixture.registry,&fixture.caller,15);RES_CHECK(extra[i]);}
        RES_CHECK(!tiku_resource_service_new(fixture.registry,&fixture.caller,15));
        for(i=0;i<TIKU_RESOURCE_CONNECTIONS_MAX-2;i++)tiku_resource_service_free(extra[i]);
    }
    resource_test_close(&fixture);
}

/** @brief Bound page bytes and reserve wire depth before exposing snapshots. */
static void response_limits_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_initial_t items[128]={0},deep={0};
    tiku_resource_stamp_t stamp;
    tiku_resource_snapshot_t value;
    char paths[128][481],prefix[420],part[128],cursor[33];
    tiku_msg_t *request,*result,*nested,*next;
    size_t i,total;int32_t count;
    resource_test_open(&fixture,0);memset(part,'x',127);part[127]=0;
    snprintf(prefix,sizeof prefix,"/large/%s/%s/%s",part,part,part);
    resource_test_snapshot(&value,0);
    for(i=0;i<128;i++){
        snprintf(paths[i],sizeof paths[i],"%s/item%03u",prefix,(unsigned)i);
        items[i].spec=(tiku_resource_spec_t){.path=paths[i],.kind=TIKU_RESOURCE_STATE,.schema_version=1,
            .schema={.type=TIKU_RESOURCE_BOOLEAN},.public_read=1,.persistence="none",.controller="fixture"};
        items[i].initial=value;
    }
    RES_CHECK(!tiku_resource_register(fixture.registry,"large","/large",NULL,items,128,NULL,NULL,&stamp));
    tiku_resource_snapshot_clear(&value);
    request=resource_test_request(&fixture,"list",prefix,1);RES_CHECK(tiku_msg_add_int32(request,"page_size",128));
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);
    RES_CHECK(tiku_msg_find_int32(result,"count",0,&count) && count>0 && count<128);total=(size_t)count;
    strcpy(cursor,tiku_msg_find_string(result,"next_cursor",0));tiku_msg_free(result);tiku_msg_free(request);
    request=resource_test_request(&fixture,"list",prefix,2);
    RES_CHECK(tiku_msg_add_int32(request,"page_size",128) && tiku_msg_add_string(request,"cursor",cursor));
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);
    RES_CHECK(tiku_msg_find_int32(result,"count",0,&count) && total+(size_t)count==128 && !tiku_msg_count(result,"next_cursor"));
    tiku_msg_free(result);tiku_msg_free(request);
    nested=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,1);
    for(i=0;i<5;i++){
        next=tiku_msg_new(0);RES_CHECK(next && tiku_msg_add_string(next,"type","array") && tiku_msg_add_message(next,"item",nested));
        tiku_msg_free(nested);nested=next;
    }
    RES_CHECK(!tiku_resource_value_check(nested));
    deep.spec=(tiku_resource_spec_t){.path="/depth/value",.kind=TIKU_RESOURCE_STATE,.schema_version=1,
        .schema={.type=TIKU_RESOURCE_ARRAY},.public_read=1,.persistence="none",.controller="fixture"};
    deep.initial.desired=deep.initial.persisted=deep.initial.applied=nested;strcpy(deep.initial.apply_state,"applied");
    RES_CHECK(tiku_resource_register(fixture.registry,"depth","/depth",NULL,&deep,1,NULL,NULL,&stamp)==TIKU_RESOURCE_LIMIT);
    tiku_msg_free(nested);resource_test_close(&fixture);
}

/** @brief Check session grants, durable-mode rejection and retained private data. */
static void permissions_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_caller_t wrong,restricted;
    tiku_resource_service_t *limited;
    tiku_msg_t *request,*result;
    resource_test_open(&fixture,0);wrong=fixture.caller;memset(wrong.principal,'2',32);
    request=resource_test_request(&fixture,"read","/settings/enabled",1);
    result=resource_test_call(fixture.a,&wrong,request,"permission_denied");tiku_msg_free(result);
    RES_CHECK(tiku_msg_add_int64(request,"caller_scopes",INT64_MAX));
    result=resource_test_call(fixture.a,&fixture.caller,request,"invalid_request");tiku_msg_free(result);tiku_msg_free(request);
    request=resource_test_set(&fixture,"/settings/enabled",&fixture.settings,2,1);
    tiku_msg_remove(request,"durability");RES_CHECK(tiku_msg_add_string(request,"durability","session"));
    result=resource_test_call(fixture.a,&fixture.caller,request,"unsupported");
    RES_CHECK(!fixture.changes);tiku_msg_free(result);tiku_msg_free(request);
    request=resource_test_set(&fixture,"/settings/private/enabled",&fixture.settings,3,1);
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);tiku_msg_free(result);tiku_msg_free(request);
    restricted=fixture.caller;restricted.scopes&=~TIKU_RESOURCE_CONTENT;
    limited=tiku_resource_service_new(fixture.registry,&restricted,15);RES_CHECK(limited);
    request=resource_test_request(&fixture,"request-status",NULL,4);
    RES_CHECK(tiku_msg_add_string(request,"prior_request_id","00000000000000000000000000000003"));
    result=resource_test_call(limited,&restricted,request,"outcome_unavailable");
    tiku_msg_free(result);tiku_msg_free(request);tiku_resource_service_free(limited);
    resource_test_close(&fixture);
}

/** @brief Check subscription ownership, overflow and broker cleanup. */
static void watches_test(void)
{
    resource_fixture_t fixture;
    tiku_msg_t *request,*result,*event=NULL;
    char subscription[33];unsigned i;
    resource_test_open(&fixture,0);
    request=resource_test_request(&fixture,"subscribe","/settings",1);
    RES_CHECK(tiku_msg_add_string(request,"provider","settings"));
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);
    strcpy(subscription,tiku_msg_find_string(result,"subscription",0));tiku_msg_free(result);tiku_msg_free(request);
    request=resource_test_request(&fixture,"unsubscribe",NULL,2);RES_CHECK(tiku_msg_add_string(request,"subscription",subscription));
    result=resource_test_call(fixture.b,&fixture.caller,request,"not_found");tiku_msg_free(result);tiku_msg_free(request);
    for(i=0;i<3;i++){
        request=resource_test_set(&fixture,"/settings/enabled",&fixture.settings,i+3,i%2==0);
        result=resource_test_call(fixture.b,&fixture.caller,request,NULL);fixture.settings=resource_test_stamp(result);
        tiku_msg_free(result);tiku_msg_free(request);
    }
    RES_CHECK(!tiku_resource_service_event(fixture.b,&fixture.caller,&event) && !event);
    RES_CHECK(tiku_resource_service_event(fixture.a,&fixture.caller,&event)==1);
    RES_CHECK(!strcmp(tiku_msg_find_string(event,"subscription",0),subscription) && !strcmp(tiku_msg_find_string(event,"kind",0),"overflow"));
    tiku_msg_free(event);event=NULL;
    RES_CHECK(!tiku_resource_service_event(fixture.a,&fixture.caller,&event));
    tiku_resource_service_free(fixture.a);fixture.a=NULL;
    request=resource_test_request(&fixture,"subscribe","/settings",8);RES_CHECK(tiku_msg_add_string(request,"provider","settings"));
    result=resource_test_call(fixture.b,&fixture.caller,request,NULL);tiku_msg_free(result);tiku_msg_free(request);
    resource_test_close(&fixture);
}

/** @brief Reconcile an expired synchronous reply through its retained outcome. */
static void reconciliation_test(void)
{
    resource_fixture_t fixture;
    tiku_msg_t *request,*result,*query;
    resource_test_open(&fixture,0);fixture.slow=1;
    request=resource_test_set(&fixture,"/settings/enabled",&fixture.settings,1,1);
    result=resource_test_call(fixture.a,&fixture.caller,request,"unknown_outcome");
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"effect",0),"unknown") && fixture.changes==1);tiku_msg_free(result);
    query=resource_test_request(&fixture,"request-status",NULL,2);RES_CHECK(tiku_msg_add_string(query,"prior_request_id","00000000000000000000000000000001"));
    result=resource_test_call(fixture.b,&fixture.caller,query,NULL);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"state",0),"completed") && !strcmp(tiku_msg_find_string(result,"code",0),"ok"));
    tiku_msg_free(result);tiku_msg_free(query);
    result=resource_test_call(fixture.a,&fixture.caller,request,NULL);RES_CHECK(fixture.changes==1);
    tiku_msg_free(result);
    {
        tiku_msg_t *reordered=tiku_msg_new(0);
        RES_CHECK(reordered && tiku_msg_add_bool(reordered,"value",1) && tiku_msg_add_string(reordered,"type","boolean"));
        tiku_msg_remove(request,"value");RES_CHECK(tiku_msg_add_message(request,"value",reordered));tiku_msg_free(reordered);
        result=resource_test_call(fixture.a,&fixture.caller,request,NULL);RES_CHECK(fixture.changes==1);tiku_msg_free(result);
        tiku_msg_remove(request,"deadline_ms");RES_CHECK(tiku_msg_add_int32(request,"deadline_ms",1));
        result=resource_test_call(fixture.a,&fixture.caller,request,"request_id_reused");tiku_msg_free(result);
    }
    tiku_msg_free(request);resource_test_close(&fixture);
}

/** @brief Grant the test's private local endpoint its preassigned principal. */
static int authorize(void *opaque,uid_t uid,pid_t pid,tiku_resource_caller_t *out)
{
    resource_fixture_t *fixture=opaque;(void)pid;
    if(uid!=geteuid())return 0;
    *out=fixture->caller;return 1;
}

/** @brief Dispatch local client messages through the production broker API. */
static tiku_msg_t *dispatch(void *opaque,const tiku_resource_caller_t *caller,const tiku_msg_t *request)
{
    resource_fixture_t *fixture=opaque;
    if(!fixture->a)fixture->a=tiku_resource_service_new(fixture->registry,caller,tiku_resource_channel_features(fixture->channel));
    return tiku_resource_service_dispatch(fixture->a,caller,request);
}

/** @brief Release the connection's broker when transport closes. */
static void closed(void *opaque,const tiku_resource_caller_t *caller)
{
    resource_fixture_t *fixture=opaque;(void)caller;
    tiku_resource_service_free(fixture->a);fixture->a=NULL;
}

/** @brief Exchange a request with an independent local server. */
static tiku_msg_t *exchange(tiku_resource_channel_t *channel,tiku_msg_t *request)
{
    tiku_msg_t *reply=NULL,*result;
    RES_CHECK(!tiku_resource_channel_send(channel,request,3000) && !tiku_resource_channel_receive(channel,&reply,3000));
    RES_CHECK(!strcmp(tiku_msg_find_string(reply,"request_id",0),tiku_msg_find_string(request,"request_id",0)));
    result=tiku_msg_find_message(reply,"result",0);RES_CHECK(result);
    tiku_msg_free(reply);tiku_msg_free(request);return result;
}

/** @brief Enumerate, watch, change and query async work from a separate process. */
static void client(resource_fixture_t *fixture,int fd)
{
    tiku_resource_channel_t *channel=tiku_resource_channel_client(fd);
    tiku_resource_stamp_t settings,work;
    tiku_msg_t *request,*result,*event;
    char subscription[33],job[33];
    close(fd);RES_CHECK(channel && !tiku_resource_channel_negotiate(channel,15,0,3000));
    RES_CHECK(!strcmp(tiku_resource_channel_session(channel),tiku_resource_registry_session(fixture->registry)));
    request=resource_test_request(fixture,"list","/settings",1);RES_CHECK(tiku_msg_add_int32(request,"page_size",128));
    result=exchange(channel,request);RES_CHECK(tiku_msg_count(result,"entry")==3);tiku_msg_free(result);
    request=resource_test_request(fixture,"read","/settings/enabled",2);result=exchange(channel,request);
    settings=resource_test_stamp(result);tiku_msg_free(result);
    request=resource_test_request(fixture,"describe","/work/enabled",3);result=exchange(channel,request);
    work=resource_test_stamp(result);tiku_msg_free(result);
    request=resource_test_request(fixture,"subscribe","/settings/enabled",4);RES_CHECK(tiku_msg_add_string(request,"provider","settings"));
    result=exchange(channel,request);strcpy(subscription,tiku_msg_find_string(result,"subscription",0));tiku_msg_free(result);
    request=resource_test_set(fixture,"/settings/enabled",&settings,5,1);result=exchange(channel,request);tiku_msg_free(result);
    RES_CHECK(!tiku_resource_channel_receive(channel,&event,3000) && !strcmp(tiku_msg_find_string(event,"kind",0),"changed"));
    RES_CHECK(!strcmp(tiku_msg_find_string(event,"subscription",0),subscription));tiku_msg_free(event);
    request=resource_test_request(fixture,"unsubscribe",NULL,6);RES_CHECK(tiku_msg_add_string(request,"subscription",subscription));result=exchange(channel,request);tiku_msg_free(result);
    request=resource_test_set(fixture,"/work/enabled",&work,7,1);result=exchange(channel,request);
    strcpy(job,tiku_msg_find_string(result,"job",0));RES_CHECK(!strcmp(tiku_msg_find_string(result,"state",0),"queued"));tiku_msg_free(result);
    request=resource_test_request(fixture,"job-status",NULL,8);RES_CHECK(tiku_msg_add_string(request,"job",job));result=exchange(channel,request);
    RES_CHECK(!strcmp(tiku_msg_find_string(result,"state",0),"succeeded"));tiku_msg_free(result);
    request=resource_test_request(fixture,"request-status",NULL,9);RES_CHECK(tiku_msg_add_string(request,"prior_request_id","00000000000000000000000000000007"));
    result=exchange(channel,request);RES_CHECK(!strcmp(tiku_msg_find_string(result,"job",0),job));tiku_msg_free(result);
    tiku_resource_channel_free(channel);
}

/** @brief Connect the general broker to the authenticated Application channel. */
static void local_client_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_channel_t *channel;
    tiku_resource_channel_ops_t ops={authorize,dispatch,closed};
    int pair[2],status,i;pid_t child;
    resource_test_open(&fixture,0);tiku_resource_service_free(fixture.a);fixture.a=NULL;
    RES_CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    fflush(NULL);child=fork();RES_CHECK(child>=0);
    if(!child){close(pair[0]);client(&fixture,pair[1]);resource_test_close(&fixture);exit(0);}
    close(pair[1]);channel=tiku_resource_channel_server(pair[0],tiku_resource_registry_session(fixture.registry),15,&ops,&fixture);close(pair[0]);RES_CHECK(channel);fixture.channel=channel;
    for(i=0;i<10;i++){
        RES_CHECK(!tiku_resource_channel_step(channel,3000));
        if(i==5){tiku_msg_t *event=NULL;RES_CHECK(tiku_resource_service_event(fixture.a,&fixture.caller,&event)==1);RES_CHECK(!tiku_resource_channel_send(channel,event,3000));tiku_msg_free(event);}
        if(i==7){tiku_resource_snapshot_t result;RES_CHECK(tiku_resource_service_pump(fixture.a)==1);resource_test_snapshot(&result,1);RES_CHECK(!tiku_resource_job_complete(fixture.registry,fixture.worker->last_job,&result,TIKU_RESOURCE_OK,TIKU_RESOURCE_EFFECT_PARTIAL));tiku_resource_snapshot_clear(&result);}
    }
    RES_CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    tiku_resource_channel_free(channel);RES_CHECK(!fixture.a);resource_test_close(&fixture);
}

/** @brief Reject async acceptance when the peer did not negotiate jobs. */
static void negotiated_jobs_test(void)
{
    resource_fixture_t fixture;
    tiku_resource_channel_ops_t ops={authorize,dispatch,closed};
    tiku_resource_channel_t *channel;
    int pair[2],status;pid_t child;
    resource_test_open(&fixture,0);tiku_resource_service_free(fixture.a);fixture.a=NULL;
    RES_CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));fflush(NULL);child=fork();RES_CHECK(child>=0);
    if(!child){
        tiku_msg_t *request,*reply=NULL,*error;
        close(pair[0]);channel=tiku_resource_channel_client(pair[1]);close(pair[1]);
        RES_CHECK(channel && !tiku_resource_channel_negotiate(channel,3,0,3000));
        request=resource_test_set(&fixture,"/work/enabled",&fixture.work,1,1);
        RES_CHECK(!tiku_resource_channel_send(channel,request,3000) && !tiku_resource_channel_receive(channel,&reply,3000));
        error=tiku_msg_find_message(reply,"error",0);RES_CHECK(error && !strcmp(tiku_msg_find_string(error,"code",0),"unsupported"));
        tiku_msg_free(error);tiku_msg_free(reply);tiku_msg_free(request);tiku_resource_channel_free(channel);
        resource_test_close(&fixture);exit(0);
    }
    close(pair[1]);channel=tiku_resource_channel_server(pair[0],tiku_resource_registry_session(fixture.registry),15,&ops,&fixture);
    close(pair[0]);RES_CHECK(channel);fixture.channel=channel;
    RES_CHECK(!tiku_resource_channel_step(channel,3000) && !tiku_resource_channel_step(channel,3000));
    RES_CHECK(tiku_resource_service_features(fixture.a)==3 && !tiku_resource_service_pump(fixture.a) && !fixture.worker->starts);
    RES_CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    tiku_resource_channel_free(channel);resource_test_close(&fixture);
}

int main(int argc,char **argv)
{
    struct{const char *name;void (*run)(void);}cases[]={
        {"cursors",cursors_test},{"limits",response_limits_test},{"permissions",permissions_test},{"watches",watches_test},
        {"reconciliation",reconciliation_test},{"local-client",local_client_test},{"negotiated-jobs",negotiated_jobs_test}};
    size_t i;for(i=0;i<sizeof cases/sizeof cases[0];i++)if(argc==1||(argc==2&&!strcmp(argv[1],cases[i].name))){cases[i].run();printf("PASS resource service %s\n",cases[i].name);}return 0;
}
