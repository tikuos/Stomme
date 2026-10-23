/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * resource_client.c - standalone SDK client for a desktop test provider.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_resource_endpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(test) do {if(!(test)){fprintf(stderr,"resource client:%d: %s\n",__LINE__,#test);exit(1);}}while(0)

static tiku_resource_channel_t *channel;

/** @brief Create a correlated request on the negotiated session. */
static tiku_msg_t *request(const char *op,const char *path)
{
    tiku_msg_t *message=tiku_msg_new(TIKU_RESOURCE_MESSAGE_CODE);char id[33];
    CHECK(!tiku_resource_new_id(id));
    CHECK(message && tiku_msg_add_string(message,"op",op) && tiku_msg_add_string(message,"request_id",id) &&
        tiku_msg_add_string(message,"session",tiku_resource_channel_session(channel)));
    if(path)CHECK(tiku_msg_add_string(message,"path",path));
    return message;
}

/** @brief Exchange one request and check its result or expected structured error. */
static tiku_msg_t *call(tiku_msg_t *message,const char *error)
{
    tiku_msg_t *reply=NULL,*result;
    CHECK(!tiku_resource_channel_send(channel,message,5000) && !tiku_resource_channel_receive(channel,&reply,5000));
    CHECK(!strcmp(tiku_msg_find_string(reply,"request_id",0),tiku_msg_find_string(message,"request_id",0)));
    result=tiku_msg_find_message(reply,error?"error":"result",0);CHECK(result);
    if(error)CHECK(!strcmp(tiku_msg_find_string(result,"code",0),error));
    tiku_msg_free(reply);tiku_msg_free(message);return result;
}

/** @brief Copy a result's provider stamp into an expected tuple. */
static void expected(tiku_msg_t *message,const tiku_msg_t *result)
{
    tiku_msg_t *tuple=tiku_msg_new(0);
    CHECK(tuple && tiku_msg_add_string(tuple,"provider",tiku_msg_find_string(result,"provider",0)) &&
        tiku_msg_add_string(tuple,"incarnation",tiku_msg_find_string(result,"incarnation",0)) &&
        tiku_msg_add_string(tuple,"revision",tiku_msg_find_string(result,"revision",0)) && tiku_msg_add_message(message,"expected",tuple));
    tiku_msg_free(tuple);
}

/** @brief Build a complete fixture replacement at one provider revision. */
static tiku_msg_t *set(const char *path,const tiku_msg_t *stamp,int enabled)
{
    tiku_msg_t *message=request("set",path),*value=tiku_resource_value_integer(TIKU_RESOURCE_BOOLEAN,enabled);
    expected(message,stamp);CHECK(value && tiku_msg_add_message(message,"value",value) && tiku_msg_add_string(message,"durability","user") &&
        tiku_msg_add_int32(message,"deadline_ms",3000));tiku_msg_free(value);return message;
}

/** @brief Stop the explicitly enabled fixture through its owner action. */
static void stop(void)
{
    tiku_msg_t *stamp=call(request("describe","/test/stop"),NULL),*message=request("invoke","/test/stop"),*args=tiku_msg_new(0),*result;
    expected(message,stamp);CHECK(args && tiku_msg_add_string(args,"type","object") && tiku_msg_add_message(message,"arguments",args) &&
        tiku_msg_add_int32(message,"deadline_ms",3000));tiku_msg_free(args);tiku_msg_free(stamp);
    result=call(message,NULL);tiku_msg_free(result);
}

/** @brief Exercise discovery, revision checks, events, snapshots and queued work. */
static void workflow(void)
{
    tiku_msg_t *message,*result,*stamp,*current,*event=NULL,*value;
    char lease[33],job[33],prior[33];size_t bytes;int enabled;unsigned i;
    message=request("list","/");CHECK(tiku_msg_add_int32(message,"page_size",4));result=call(message,NULL);CHECK(tiku_msg_count(result,"entry"));tiku_msg_free(result);
    stamp=call(request("describe","/test/enabled"),NULL);
    result=call(request("read","/test/enabled"),NULL);value=tiku_msg_find_message(result,"desired",0);
    CHECK(value && tiku_msg_find_bool(value,"value",0,&enabled));tiku_msg_free(value);tiku_msg_free(result);
    message=request("subscribe","/test");CHECK(tiku_msg_add_string(message,"provider","test"));result=call(message,NULL);tiku_msg_free(result);
    message=set("/test/enabled",stamp,!enabled);strcpy(prior,tiku_msg_find_string(message,"request_id",0));current=call(message,NULL);
    CHECK(!tiku_resource_channel_receive(channel,&event,5000) && !strcmp(tiku_msg_find_string(event,"kind",0),"changed"));tiku_msg_free(event);
    result=call(set("/test/enabled",stamp,enabled),"stale_revision");tiku_msg_free(result);tiku_msg_free(stamp);
    result=call(set("/test/readonly",current,1),"read_only");tiku_msg_free(result);
    result=call(set("/test/fail",current,1),"persistence_failed");tiku_msg_free(result);
    message=set("/test/enabled",current,enabled);CHECK(tiku_msg_add_int64(message,"caller_scopes",INT64_MAX));result=call(message,"invalid_request");tiku_msg_free(result);
    message=request("request-status",NULL);CHECK(tiku_msg_add_string(message,"prior_request_id",prior));result=call(message,NULL);
    CHECK(!strcmp(tiku_msg_find_string(result,"code",0),"ok"));tiku_msg_free(result);
    message=request("list","/test/items");CHECK(tiku_msg_add_int32(message,"page_size",4));result=call(message,NULL);CHECK(tiku_msg_count(result,"entry")==1);tiku_msg_free(result);
    message=request("stream-open","/test/content");expected(message,current);CHECK(tiku_msg_add_string(message,"maximum_bytes","1024"));result=call(message,NULL);
    strcpy(lease,tiku_msg_find_string(result,"lease",0));tiku_msg_free(result);
    message=request("stream-read",NULL);CHECK(tiku_msg_add_string(message,"lease",lease) && tiku_msg_add_string(message,"offset","0") && tiku_msg_add_int32(message,"byte_count",1024));
    result=call(message,NULL);value=NULL;{const void *data=tiku_msg_find_blob(result,"data",0,&bytes);CHECK(data && bytes==25 && !memcmp(data,"Stomme resource snapshot\n",25));}tiku_msg_free(result);
    message=request("stream-close",NULL);CHECK(tiku_msg_add_string(message,"lease",lease));result=call(message,NULL);tiku_msg_free(result);tiku_msg_free(current);
    stamp=call(request("describe","/test-work/enabled"),NULL);result=call(set("/test-work/enabled",stamp,1),NULL);strcpy(job,tiku_msg_find_string(result,"job",0));tiku_msg_free(stamp);tiku_msg_free(result);
    for(i=0;i<100;i++){
        message=request("job-status",NULL);CHECK(tiku_msg_add_string(message,"job",job));result=call(message,NULL);
        {const char *state=tiku_msg_find_string(result,"state",0);CHECK(state);if(!strcmp(state,"succeeded")){tiku_msg_free(result);break;}}
        tiku_msg_free(result);usleep(10000);
    }
    CHECK(i<100);puts("PASS discovery read change watch stale readonly persistence scopes collection stream jobs reconciliation");
}

int main(int argc,char **argv)
{
    tiku_resource_protocol_limits_t limits,selected;
    if(argc<2 || argc>3){fprintf(stderr,"usage: resource-client PRIVATE_DIRECTORY [--stop]\n");return 2;}
    channel=tiku_resource_endpoint_connect(argv[1],5000);CHECK(channel);
    tiku_resource_protocol_limits_default(&limits);limits.frame_bytes=8192;limits.stream_chunk_bytes=4096;limits.page_entries=4;
    CHECK(!tiku_resource_channel_offer(channel,&limits) && !tiku_resource_channel_negotiate(channel,143,64,5000));
    CHECK(!tiku_resource_channel_limits(channel,&selected) && selected.frame_bytes==8192 && selected.page_entries==4);
    if(argc==3){CHECK(!strcmp(argv[2],"--stop"));stop();}else workflow();
    tiku_resource_channel_free(channel);return 0;
}
