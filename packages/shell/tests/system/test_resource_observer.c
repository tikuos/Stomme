/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_resource_observer.c - complete streams, last successful data and authority.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_resource_observer.h"
#include "tiku_trk_resource_values.h"
#include "tiku_resource_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)
static int rows = 240, broken;
static const tiku_trk_observation_t entries[] = {{"/observe/items",1,1},{"/observe/state",0,0}};
/** @brief Return the fixture's complete stream size. */
static int count_items(void *context, const char *path) { (void)context; (void)path; return rows; }
/** @brief Return one copied object, with a controlled failed capture. */
static tiku_msg_t *capture(void *context, const char *path, int index)
{
    tiku_msg_t *out = trk_value_container("object"); char label[32]; (void)context;
    if (broken && index == 150) { tiku_msg_free(out); return NULL; }
    snprintf(label,sizeof label,"row-%d",index);
    if (trk_value_member(out,"index",tiku_resource_value_integer(TIKU_RESOURCE_INTEGER,index)) ||
        trk_value_member(out,"label",tiku_resource_value_string(TIKU_RESOURCE_TEXT,label)) ||
        trk_value_member(out,"path",tiku_resource_value_string(TIKU_RESOURCE_TEXT,path))) { tiku_msg_free(out); return NULL; }
    return out;
}

int main(void)
{
    tiku_resource_registry_t *registry = tiku_resource_registry_new(NULL);
    tiku_trk_resource_observer_t *observer;
    tiku_resource_caller_t caller = {.authenticated=1,.scopes=63}, untrusted = {0};
    tiku_resource_snapshot_t first={0}, next={0}; tiku_resource_stream_t *stream; size_t length, got, offset=0;
    char bytes[65536];
    strcpy(caller.principal,"00000000000000000000000000000001"); CHECK(registry);
    observer=tiku_trk_resource_observer_open(registry,"observe","/observe",entries,2,count_items,capture,NULL); CHECK(observer);
    CHECK(tiku_resource_read(registry,&untrusted,"/observe/items",&next)==TIKU_RESOURCE_NOT_FOUND);
    CHECK(!tiku_resource_read(registry,&untrusted,"/observe/state",&next)); tiku_resource_snapshot_clear(&next);
    CHECK(!tiku_resource_read(registry,&caller,"/observe/items",&first));
    CHECK(!tiku_resource_stream_open(registry,&caller,"/observe/items",&first.stamp,sizeof bytes,&stream,&length));
    CHECK(length>10000 && length<sizeof bytes);
    while(offset<length) { CHECK(!tiku_resource_stream_read(stream,&caller,offset,bytes+offset,length-offset,&got) && got); offset+=got; }
    bytes[length]=0; CHECK(strstr(bytes,"row-239") && strstr(bytes,"row-0")); tiku_resource_stream_close(stream);
    broken=1; tiku_trk_resource_observer_poll(observer,1000);
    CHECK(!tiku_resource_read(registry,&caller,"/observe/items",&next));
    CHECK(!strcmp(next.apply_state,"last_good_unavailable") && tiku_resource_value_equal(first.desired,next.desired)); tiku_resource_snapshot_clear(&next);
    broken=0; rows=241; tiku_trk_resource_observer_poll(observer,2000);
    CHECK(!tiku_resource_read(registry,&caller,"/observe/items",&next));
    CHECK(!strcmp(next.apply_state,"applied") && !tiku_resource_value_equal(first.desired,next.desired));
    tiku_resource_snapshot_clear(&first); tiku_resource_snapshot_clear(&next);
    rows=100000; tiku_trk_resource_observer_poll(observer,3000);
    CHECK(!tiku_resource_read(registry,&caller,"/observe/items",&next)); CHECK(!strcmp(next.apply_state,"last_good_unavailable")); tiku_resource_snapshot_clear(&next);
    {
        static const char *const paths[]={"/budget/a","/budget/b","/budget/c","/budget/d"};
        tiku_resource_initial_t initial[4]={0}; tiku_resource_stamp_t stamp;
        char *content=calloc(1,TIKU_RESOURCE_STREAM_BYTES); int i;
        CHECK(content); rows=241; tiku_trk_resource_observer_poll(observer,4000);
        for(i=0;i<4;i++) {
            initial[i].spec=(tiku_resource_spec_t){.path=paths[i],.kind=TIKU_RESOURCE_STREAM,.schema_version=1,.schema={.type=TIKU_RESOURCE_OBJECT},
                .read_scopes=TIKU_RESOURCE_READ,.public_read=1,.persistence="none",.controller="budget"};
            CHECK(!trk_value_snapshot(&initial[i].initial,tiku_resource_stream_metadata(0,""),"applied"));
        }
        CHECK(!tiku_resource_register(registry,"budget","/budget",NULL,initial,4,NULL,NULL,&stamp));
        for(i=0;i<4;i++) {
            tiku_resource_snapshot_clear(&initial[i].initial);
            CHECK(!tiku_resource_stream_publish(registry,&stamp,paths[i],content,i==3?3u*1024u*1024u:TIKU_RESOURCE_STREAM_BYTES,&stamp));
        }
        rows=30000; tiku_trk_resource_observer_poll(observer,5000);
        CHECK(!tiku_resource_read(registry,&caller,"/observe/items",&next));
        CHECK(!strcmp(next.apply_state,"last_good_unavailable")); tiku_resource_snapshot_clear(&next);
        CHECK(!tiku_resource_unregister(registry,&stamp)); free(content);
        rows=241; tiku_trk_resource_observer_poll(observer,6000);
        CHECK(!tiku_resource_read(registry,&caller,"/observe/items",&next));
        CHECK(!strcmp(next.apply_state,"applied")); tiku_resource_snapshot_clear(&next);
    }
    tiku_trk_resource_observer_close(observer);
    CHECK(tiku_resource_read(registry,&caller,"/observe/items",&next)==TIKU_RESOURCE_NOT_FOUND); tiku_resource_registry_free(registry);
    puts("PASS observations: complete large streams, unavailable feedback, recovery, limits and private reads"); return 0;
}
