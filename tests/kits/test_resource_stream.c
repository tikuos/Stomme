/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_stream.c - catalogs, frozen leases and negotiated ceilings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "resource_fixture.h"
#include "tiku_resource_stream.h"
#include "tiku_resource_endpoint.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/** @brief Register a content stream and a collection under one native owner. */
static tiku_resource_stamp_t provider(resource_fixture_t *fixture)
{
    tiku_resource_initial_t initial[2]={0};tiku_resource_stamp_t stamp;
    tiku_msg_t *value=tiku_resource_stream_metadata(0,"");unsigned i;
    for(i=0;i<2;i++){
        initial[i].spec=(tiku_resource_spec_t){.path=i?"/data/items":"/data/content",.kind=i?TIKU_RESOURCE_COLLECTION:TIKU_RESOURCE_STREAM,
            .schema_version=1,.schema={.type=i?TIKU_RESOURCE_NULL:TIKU_RESOURCE_OBJECT},.read_scopes=TIKU_RESOURCE_READ,
            .sensitive=!i,.public_read=i,.persistence="none",.controller="content-owner"};
        initial[i].initial.desired=tiku_msg_copy(value);initial[i].initial.persisted=tiku_msg_copy(value);initial[i].initial.applied=tiku_msg_copy(value);
        strcpy(initial[i].initial.apply_state,"applied");
    }
    RES_CHECK(!tiku_resource_register(fixture->registry,"data","/data",NULL,initial,2,NULL,NULL,&stamp));
    for(i=0;i<2;i++)tiku_resource_snapshot_clear(&initial[i].initial);
    tiku_msg_free(value);return stamp;
}

/** @brief Freeze bytes across publication and invalidate on provider retirement. */
static void snapshots_test(void)
{
    resource_fixture_t fixture;tiku_resource_stamp_t stamp;tiku_resource_stream_t *old,*fresh;
    size_t bytes,read;char data[32];tiku_resource_caller_t limited;
    resource_test_open(&fixture,0);stamp=provider(&fixture);
    RES_CHECK(!tiku_resource_stream_publish(fixture.registry,&stamp,"/data/content","alpha",5,&stamp));
    RES_CHECK(!tiku_resource_stream_open(fixture.registry,&fixture.caller,"/data/content",&stamp,32,&old,&bytes) && bytes==5);
    limited=fixture.caller;limited.scopes&=~TIKU_RESOURCE_CONTENT;
    RES_CHECK(tiku_resource_stream_open(fixture.registry,&limited,"/data/content",&stamp,32,&fresh,&bytes)==TIKU_RESOURCE_NOT_FOUND);
    RES_CHECK(!tiku_resource_stream_publish(fixture.registry,&stamp,"/data/content","bravo",5,&stamp));
    RES_CHECK(!tiku_resource_stream_read(old,&fixture.caller,0,data,sizeof data,&read) && read==5 && !memcmp(data,"alpha",5));
    RES_CHECK(tiku_resource_stream_read(old,&limited,0,data,sizeof data,&read)==TIKU_RESOURCE_DENIED && !read);
    RES_CHECK(!tiku_resource_stream_open(fixture.registry,&fixture.caller,"/data/content",&stamp,32,&fresh,&bytes));
    RES_CHECK(!tiku_resource_stream_read(fresh,&fixture.caller,2,data,sizeof data,&read) && read==3 && !memcmp(data,"avo",3));
    RES_CHECK(tiku_resource_stream_read(fresh,&fixture.caller,UINT64_MAX,data,sizeof data,&read)==TIKU_RESOURCE_INVALID);
    RES_CHECK(!tiku_resource_unregister(fixture.registry,&stamp));
    RES_CHECK(tiku_resource_stream_read(old,&fixture.caller,0,data,sizeof data,&read)==TIKU_RESOURCE_OWNER_LOST);
    stamp=provider(&fixture);
    RES_CHECK(!tiku_resource_stream_publish(fixture.registry,&stamp,"/data/content","new",3,&stamp));
    RES_CHECK(tiku_resource_stream_read(old,&fixture.caller,0,data,sizeof data,&read)==TIKU_RESOURCE_OWNER_LOST);
    tiku_resource_stream_close(old);tiku_resource_stream_close(fresh);resource_test_close(&fixture);
}

/** @brief Create a stream-open request at one provider stamp. */
static tiku_msg_t *open_request(resource_fixture_t *fixture,const tiku_resource_stamp_t *stamp,unsigned serial)
{
    tiku_msg_t *request=resource_test_request(fixture,"stream-open","/data/content",serial);
    resource_test_expected(request,stamp);RES_CHECK(tiku_msg_add_string(request,"maximum_bytes","1024"));return request;
}

/** @brief Create a bounded chunk read on one connection-owned lease. */
static tiku_msg_t *read_request(resource_fixture_t *fixture,const char *lease,unsigned serial)
{
    tiku_msg_t *request=resource_test_request(fixture,"stream-read",NULL,serial);
    RES_CHECK(tiku_msg_add_string(request,"lease",lease) && tiku_msg_add_string(request,"offset","0") && tiku_msg_add_int32(request,"byte_count",32));return request;
}

/** @brief Enforce connection ownership, idle expiry, close replay and lease quotas. */
static void leases_test(void)
{
    resource_fixture_t fixture;tiku_resource_stamp_t stamp;tiku_resource_service_t *a,*b;
    tiku_resource_protocol_limits_t limits;char lease[33];tiku_msg_t *request,*result;size_t bytes;
    resource_test_open(&fixture,0);stamp=provider(&fixture);
    RES_CHECK(!tiku_resource_stream_publish(fixture.registry,&stamp,"/data/content","snapshot",8,&stamp));
    a=tiku_resource_service_new(fixture.registry,&fixture.caller,255);b=tiku_resource_service_new(fixture.registry,&fixture.caller,255);RES_CHECK(a && b);
    tiku_resource_protocol_limits_default(&limits);limits.stream_leases=1;RES_CHECK(!tiku_resource_service_limits(a,&limits));
    request=open_request(&fixture,&stamp,1);result=resource_test_call(a,&fixture.caller,request,NULL);strcpy(lease,tiku_msg_find_string(result,"lease",0));tiku_msg_free(result);tiku_msg_free(request);
    request=open_request(&fixture,&stamp,2);result=resource_test_call(a,&fixture.caller,request,"limit_exceeded");tiku_msg_free(result);tiku_msg_free(request);
    limits.stream_leases=8;limits.stream_bytes=8;RES_CHECK(!tiku_resource_service_limits(b,&limits));
    request=open_request(&fixture,&stamp,6);tiku_msg_remove(request,"maximum_bytes");RES_CHECK(tiku_msg_add_string(request,"maximum_bytes","8"));
    result=resource_test_call(b,&fixture.caller,request,NULL);tiku_msg_free(result);tiku_msg_free(request);
    request=open_request(&fixture,&stamp,7);tiku_msg_remove(request,"maximum_bytes");RES_CHECK(tiku_msg_add_string(request,"maximum_bytes","8"));
    result=resource_test_call(b,&fixture.caller,request,"limit_exceeded");tiku_msg_free(result);tiku_msg_free(request);
    request=read_request(&fixture,lease,3);result=resource_test_call(b,&fixture.caller,request,"owner_lost");tiku_msg_free(result);
    result=resource_test_call(a,&fixture.caller,request,NULL);RES_CHECK(tiku_msg_find_blob(result,"data",0,&bytes) && bytes==8);tiku_msg_free(result);tiku_msg_free(request);
    fixture.now+=30001;request=read_request(&fixture,lease,4);result=resource_test_call(a,&fixture.caller,request,"owner_lost");tiku_msg_free(result);tiku_msg_free(request);
    request=resource_test_request(&fixture,"stream-close",NULL,5);RES_CHECK(tiku_msg_add_string(request,"lease",lease));
    result=resource_test_call(a,&fixture.caller,request,NULL);tiku_msg_free(result);result=resource_test_call(a,&fixture.caller,request,NULL);tiku_msg_free(result);tiku_msg_free(request);
    tiku_resource_service_free(a);tiku_resource_service_free(b);resource_test_close(&fixture);
}

/** @brief Replace collection members atomically and invalidate child identities/cursors. */
static void collections_test(void)
{
    resource_fixture_t fixture;tiku_resource_stamp_t stamp,stale;tiku_resource_initial_t member={0};
    tiku_resource_entry_t entry,old,page[2];uint64_t epoch=0;size_t count;tiku_resource_watch_t watch;tiku_msg_t *snapshot;
    tiku_resource_event_t event;
    resource_test_open(&fixture,0);stamp=provider(&fixture);
    member.spec=(tiku_resource_spec_t){.path="/data/items/one",.kind=TIKU_RESOURCE_STATE,.schema_version=1,.schema={.type=TIKU_RESOURCE_BOOLEAN},.public_read=1,.persistence="none",.controller="catalog"};
    resource_test_snapshot(&member.initial,1);
    RES_CHECK(!tiku_resource_collection_replace(fixture.registry,&stamp,"/data/items",&member,1,&stamp));stale=stamp;
    RES_CHECK(!tiku_resource_entry(fixture.registry,&fixture.caller,member.spec.path,&old));
    RES_CHECK(!tiku_resource_list(fixture.registry,&fixture.caller,"/data/items",0,&epoch,page,2,&count) && count==1);
    RES_CHECK(!tiku_resource_watch_open(fixture.registry,&fixture.caller,"data","/data/items",&watch,&snapshot));tiku_msg_free(snapshot);
    member.spec.path="/settings/stolen";
    RES_CHECK(tiku_resource_collection_replace(fixture.registry,&stamp,"/data/items",&member,1,&stamp)==TIKU_RESOURCE_INVALID);
    RES_CHECK(!tiku_resource_entry(fixture.registry,&fixture.caller,"/data/items/one",&entry) && !strcmp(old.id,entry.id));
    member.spec.path="/data/items/one";
    RES_CHECK(!tiku_resource_collection_replace(fixture.registry,&stamp,"/data/items",&member,1,&stamp));
    RES_CHECK(!tiku_resource_entry(fixture.registry,&fixture.caller,member.spec.path,&entry) && strcmp(old.id,entry.id) && old.node_id!=entry.node_id);
    RES_CHECK(tiku_resource_list(fixture.registry,&fixture.caller,"/data/items",0,&epoch,page,2,&count)==TIKU_RESOURCE_STALE);
    RES_CHECK(tiku_resource_watch_next(fixture.registry,watch,&event)==1 && event.removed);
    RES_CHECK(tiku_resource_collection_replace(fixture.registry,&stale,"/data/items",NULL,0,&stale)==TIKU_RESOURCE_STALE);
    RES_CHECK(!tiku_resource_collection_replace(fixture.registry,&stamp,"/data/items",NULL,0,&stamp));
    RES_CHECK(tiku_resource_entry(fixture.registry,&fixture.caller,member.spec.path,&entry)==TIKU_RESOURCE_NOT_FOUND);
    tiku_resource_snapshot_clear(&member.initial);tiku_resource_watch_close(fixture.registry,watch);resource_test_close(&fixture);
}

/** @brief Check negotiated value ceilings and immutable broker selection. */
static void limits_test(void)
{
    resource_fixture_t fixture;tiku_resource_protocol_limits_t limits,copy;tiku_msg_t *message,*value,*request,*result;
    resource_test_open(&fixture,0);tiku_resource_protocol_limits_default(&limits);
    limits.text_bytes=3;limits.members=1;message=tiku_resource_protocol_limits_message(&limits);
    RES_CHECK(message && tiku_resource_protocol_limits_decode(message,&copy) && copy.text_bytes==3);tiku_msg_free(message);
    value=tiku_resource_value_string(TIKU_RESOURCE_TEXT,"four");RES_CHECK(tiku_resource_message_limits(value,&limits,0)==TIKU_RESOURCE_LIMIT);tiku_msg_free(value);
    limits.page_entries=1;RES_CHECK(!tiku_resource_service_limits(fixture.a,&limits));
    request=resource_test_request(&fixture,"list","/settings",1);RES_CHECK(tiku_msg_add_int32(request,"page_size",2));
    result=resource_test_call(fixture.a,&fixture.caller,request,"invalid_request");tiku_msg_free(result);tiku_msg_free(request);
    RES_CHECK(tiku_resource_service_limits(fixture.a,&limits)==TIKU_RESOURCE_BUSY);resource_test_close(&fixture);
}

/** @brief Verify private-directory and endpoint collision/replacement handling. */
static void endpoint_test(void)
{
    char directory[96],path[160],replacement[160];
    snprintf(directory,sizeof directory,"%s/e-XXXXXX",getenv("STOMME_RESOURCE_TEST_TMP")?getenv("STOMME_RESOURCE_TEST_TMP"):"/tmp");
    tiku_resource_endpoint_t *endpoint;struct stat state;FILE *file;
    RES_CHECK(mkdtemp(directory));RES_CHECK(!chmod(directory,0755));RES_CHECK(!tiku_resource_endpoint_open(directory));
    RES_CHECK(!chmod(directory,0700));endpoint=tiku_resource_endpoint_open(directory);RES_CHECK(endpoint && !tiku_resource_endpoint_open(directory));
    snprintf(path,sizeof path,"%s/control.sock",directory);snprintf(replacement,sizeof replacement,"%s/retired.sock",directory);
    RES_CHECK(!lstat(path,&state) && (state.st_mode&0777)==0600);
    RES_CHECK(!rename(path,replacement));file=fopen(path,"w");RES_CHECK(file && fputs("keep",file)>=0 && !fclose(file));
    tiku_resource_endpoint_close(endpoint);RES_CHECK(!lstat(path,&state) && S_ISREG(state.st_mode));
    RES_CHECK(!unlink(path) && !unlink(replacement) && !rmdir(directory));
    {
        int sockets[2];unsigned char oversized[4]={0,0,16,1};
        RES_CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));
        RES_CHECK(write(sockets[0],oversized,4)==4 && tiku_resource_endpoint_ready(sockets[1],4096)==-1);
        close(sockets[0]);close(sockets[1]);
    }
}

/** @brief Bound aggregate observer storage across subscriptions to one principal. */
static void event_budget_test(void)
{
    tiku_resource_limits_t limits={.events_per_watch=16};
    tiku_resource_registry_t *registry=tiku_resource_registry_new(&limits);
    tiku_resource_initial_t item={0};tiku_resource_stamp_t stamp;
    tiku_resource_caller_t caller={.scopes=TIKU_RESOURCE_READ,.authenticated=1};
    tiku_resource_watch_t watches[32];tiku_resource_event_t event;
    tiku_msg_t *snapshot;size_t count=0;unsigned i,overflows=0;
    memset(caller.principal,'1',32);caller.principal[32]=0;RES_CHECK(registry);
    item.spec=(tiku_resource_spec_t){.path="/queue/value",.kind=TIKU_RESOURCE_STATE,.schema_version=1,
        .schema={.type=TIKU_RESOURCE_BOOLEAN},.public_read=1,.persistence="none",.controller="queue"};
    resource_test_snapshot(&item.initial,0);
    RES_CHECK(!tiku_resource_register(registry,"queue","/queue",NULL,&item,1,NULL,NULL,&stamp));
    for(i=0;i<32;i++){RES_CHECK(!tiku_resource_watch_open(registry,&caller,"queue","/queue",&watches[i],&snapshot));tiku_msg_free(snapshot);}
    for(i=0;i<8;i++){
        tiku_resource_snapshot_clear(&item.initial);resource_test_snapshot(&item.initial,!(i%2));
        RES_CHECK(!tiku_resource_publish(registry,&stamp,item.spec.path,&item.initial,&stamp));
    }
    for(i=0;i<32;i++){
        while(tiku_resource_watch_next(registry,watches[i],&event)==1){count++;overflows+=(unsigned)event.overflow;}
        tiku_resource_watch_close(registry,watches[i]);
    }
    RES_CHECK(overflows && count*sizeof event<=1024u*1024u);
    tiku_resource_snapshot_clear(&item.initial);tiku_resource_registry_free(registry);
}

/** @brief Grant a fixture principal after OS credentials have been checked. */
static int authorize(void *opaque,uid_t uid,pid_t pid,tiku_resource_caller_t *out)
{
    resource_fixture_t *fixture=opaque;(void)pid;RES_CHECK(uid==geteuid());*out=fixture->caller;return 1;
}

/** @brief Create the broker with the actual negotiated limit selection. */
static tiku_msg_t *dispatch(void *opaque,const tiku_resource_caller_t *caller,const tiku_msg_t *request)
{
    resource_fixture_t *fixture=opaque;tiku_resource_protocol_limits_t limits;
    if(!fixture->b){fixture->b=tiku_resource_service_new(fixture->registry,caller,tiku_resource_channel_features(fixture->channel));
        RES_CHECK(fixture->b && !tiku_resource_channel_limits(fixture->channel,&limits) && !tiku_resource_service_limits(fixture->b,&limits));}
    return tiku_resource_service_dispatch(fixture->b,caller,request);
}

/** @brief Negotiate the minimum peer ceilings through independent processes. */
static void negotiation_test(void)
{
    resource_fixture_t fixture;tiku_resource_channel_ops_t ops={authorize,dispatch,NULL};tiku_resource_protocol_limits_t limits;
    int sockets[2],status;pid_t child;
    resource_test_open(&fixture,0);tiku_resource_service_free(fixture.b);fixture.b=NULL;RES_CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));fflush(NULL);child=fork();RES_CHECK(child>=0);
    if(!child){
        tiku_resource_channel_t *client; tiku_msg_t *request,*reply=NULL,*result;
        close(sockets[0]);client=tiku_resource_channel_client(sockets[1]);close(sockets[1]);RES_CHECK(client);
        tiku_resource_protocol_limits_default(&limits);limits.frame_bytes=8192;limits.stream_chunk_bytes=2048;limits.page_entries=2;
        RES_CHECK(!tiku_resource_channel_offer(client,&limits) && !tiku_resource_channel_negotiate(client,1,0,5000));
        RES_CHECK(!tiku_resource_channel_limits(client,&limits) && limits.frame_bytes==8192 && limits.page_entries==1 && limits.stream_chunk_bytes==1024);
        request=resource_test_request(&fixture,"describe","/",1);RES_CHECK(!tiku_resource_channel_send(client,request,5000) && !tiku_resource_channel_receive(client,&reply,5000));
        result=tiku_msg_find_message(reply,"result",0);RES_CHECK(result);tiku_msg_free(result);tiku_msg_free(reply);tiku_msg_free(request);tiku_resource_channel_free(client);exit(0);
    }
    close(sockets[1]);fixture.channel=tiku_resource_channel_server(sockets[0],tiku_resource_registry_session(fixture.registry),255,&ops,&fixture);close(sockets[0]);RES_CHECK(fixture.channel);
    tiku_resource_protocol_limits_default(&limits);limits.page_entries=1;limits.stream_chunk_bytes=1024;RES_CHECK(!tiku_resource_channel_offer(fixture.channel,&limits));
    RES_CHECK(!tiku_resource_channel_step(fixture.channel,5000) && !tiku_resource_channel_step(fixture.channel,5000));
    RES_CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));tiku_resource_channel_free(fixture.channel);resource_test_close(&fixture);
}

int main(int argc,char **argv)
{
    const struct {const char *name;void(*run)(void);}tests[]={
        {"snapshots",snapshots_test},{"leases",leases_test},{"collections",collections_test},
        {"limits",limits_test},{"event-budget",event_budget_test},{"endpoint",endpoint_test},{"negotiation",negotiation_test}
    };size_t i;
    for(i=0;i<sizeof tests/sizeof tests[0];i++)if(argc<2 || !strcmp(argv[1],tests[i].name)){tests[i].run();printf("PASS resource stream %s\n",tests[i].name);}
    return 0;
}
