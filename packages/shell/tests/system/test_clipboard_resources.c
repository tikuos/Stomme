/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_clipboard_resources.c - native content generations and atomic publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_clipboard_resources.h"
#include "tiku_trk_resource_arguments.h"
#include "tiku_resource_jobs.h"
#include "tiku_clipboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static tiku_resource_registry_t *registry;static tiku_resource_caller_t caller={.authenticated=1,.scopes=63};
/** @brief Copy the currently published transfer token. */
static void generation(char token[33])
{
    tiku_resource_snapshot_t value={0};CHECK(!tiku_resource_read(registry,&caller,"/clipboard/generation",&value));CHECK(trk_arguments_text(value.applied,"token",token,33));tiku_resource_snapshot_clear(&value);
}
/** @brief Build a generation-bound bounded native content request. */
static tiku_msg_t *read_value(const char *token,const char *representation,const char *mime,size_t offset)
{
    tiku_msg_t *value=trk_value_container("object");CHECK(!TRK_TEXT(value,"generation",token)&&!TRK_TEXT(value,"representation",representation)&&!TRK_INT(value,"offset",offset)&&!TRK_INT(value,"maximum",2048));
    if(mime)CHECK(!TRK_TEXT(value,"mime",mime));return value;
}
/** @brief Submit one exact current action and return its copied terminal result. */
static tiku_resource_job_info_t run(const char *path,const tiku_msg_t *value,int defer,tiku_resource_error_t expected)
{
    tiku_resource_snapshot_t snapshot={0};tiku_resource_job_info_t job={0},status={0};char request[33];
    CHECK(!tiku_resource_read(registry,&caller,path,&snapshot)&&!tiku_resource_new_id(request));
    tiku_resource_error_t error=tiku_resource_job_submit(registry,&caller,"00000000000000000000000000000002",path,&snapshot.stamp,request,value,1,3000,&job);tiku_resource_snapshot_clear(&snapshot);
    if(expected){CHECK(error==expected);return status;}CHECK(!error);
    if(defer)return job;
    CHECK(tiku_resource_jobs_pump(registry)>=1&&!tiku_resource_job_status(registry,&caller,job.id,&status)&&status.state==TIKU_RESOURCE_JOB_SUCCEEDED);tiku_resource_job_info_clear(&job);return status;
}
int main(void)
{
    unsigned char data[9000],decoded[2048];char encoded[2733],token[33],replacement[33];for(size_t i=0;i<sizeof data;i++)data[i]=(unsigned char)i;
    tiku_clipboard_format_t formats[]={{"application/octet-stream",data,sizeof data},{"text/html","<b>fixture</b>",14}};
    CHECK(tiku_clipboard_import_formats("native fallback",15,formats,2));registry=tiku_resource_registry_new(NULL);CHECK(registry);strcpy(caller.principal,"00000000000000000000000000000001");
    tiku_trk_clipboard_resources_t *owner=tiku_trk_clipboard_resources_open(registry);CHECK(owner);generation(token);
    for(size_t offset=0;offset<sizeof data;) {
        tiku_msg_t *value=read_value(token,"typed","application/octet-stream",offset);tiku_resource_job_info_t result=run("/clipboard/actions/read-content",value,0,0);tiku_msg_free(value);
        CHECK(trk_arguments_text(result.result.applied,"data",encoded,sizeof encoded));int bytes=trk_base64_decode(encoded,decoded,sizeof decoded);CHECK(bytes>0&&!memcmp(data+offset,decoded,(size_t)bytes));offset+=(size_t)bytes;tiku_resource_job_info_clear(&result);
    }
    tiku_msg_t *value=read_value(token,"text",NULL,0);tiku_resource_job_info_t job=run("/clipboard/actions/read-content",value,1,0),status={0};tiku_msg_free(value);
    CHECK(tiku_clipboard_import("new owner",9));CHECK(tiku_resource_jobs_pump(registry)>=0&&!tiku_resource_job_status(registry,&caller,job.id,&status)&&status.error==TIKU_RESOURCE_STALE);
    tiku_resource_job_info_clear(&job);tiku_resource_job_info_clear(&status);tiku_trk_clipboard_resources_poll(owner,1000);generation(replacement);CHECK(strcmp(token,replacement));
    value=trk_value_container("object");CHECK(!TRK_TEXT(value,"generation",replacement)&&!TRK_TEXT(value,"text","published")&&!TRK_TEXT(value,"mime","application/octet-stream")&&!TRK_TEXT(value,"data_base64","AAECAw=="));
    unsigned stamp=tiku_clipboard_stamp();status=run("/clipboard/actions/publish-content",value,0,0);CHECK(tiku_clipboard_stamp()==stamp+1&&!strcmp(tiku_clipboard_text(),"published"));
    size_t size;const unsigned char *bytes=tiku_clipboard_find("application/octet-stream",&size);CHECK(bytes&&size==4&&!memcmp(bytes,"\0\1\2\3",4));tiku_resource_job_info_clear(&status);tiku_msg_free(value);
    value=read_value(replacement,"text",NULL,0);status=run("/clipboard/actions/read-content",value,0,TIKU_RESOURCE_STALE);tiku_msg_free(value);
    tiku_trk_clipboard_resources_poll(owner,2000);generation(token);stamp=tiku_clipboard_stamp();
    value=trk_value_container("object");CHECK(!TRK_TEXT(value,"generation",token)&&!TRK_TEXT(value,"text","bad")&&!TRK_TEXT(value,"mime","text/html")&&!TRK_TEXT(value,"data_base64","AB=="));
    status=run("/clipboard/actions/publish-content",value,0,TIKU_RESOURCE_VALUE);CHECK(tiku_clipboard_stamp()==stamp&&!strcmp(tiku_clipboard_text(),"published"));tiku_msg_free(value);
    tiku_trk_clipboard_resources_close(owner);CHECK(!strcmp(tiku_clipboard_text(),"published"));tiku_resource_registry_free(registry);tiku_clipboard_set(NULL);
    puts("PASS clipboard resources: full chunk transfers, native changes, atomic bytes and stale owner refusal");return 0;
}
