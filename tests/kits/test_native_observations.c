/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_native_observations.c - copied native facts and asynchronous print metadata.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_host.h"
#include "tiku_sound.h"
#include "tiku_volume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int opens,writes,closes,completions,fail;
/** @brief Record backend opens without accessing an audio service. */
static int sound_open(void *context,unsigned rate){(void)context;CHECK(rate==TIKU_SOUND_RATE);opens++;return fail?-1:0;}
/** @brief Offer bounded nonblocking mock audio room. */
static int sound_room(void *context){(void)context;return 32;}
/** @brief Consume mock frames without producing audio. */
static int sound_write(void *context,const int16_t *pcm,size_t frames){(void)context;CHECK(pcm&&frames);writes++;return (int)frames;}
/** @brief Record native engine release. */
static void sound_close(void *context){(void)context;closes++;}
/** @brief Retain a native job completion from an unavailable isolated bus. */
static void printed(void *context,uint64_t request,int status,const char *reason)
{
    (void)context;CHECK(request==9&&status<=0&&reason);completions++;
}
int main(void)
{
    tiku_host_observation_t host;memset(&host,1,sizeof host);tiku_host_observe(NULL,&host);
    CHECK(!host.available&&!host.scale&&host.backend[0]);CHECK(!tiku_host_print_observe(NULL,NULL,0));
    tiku_sound_backend_t backend={.name="observation-fixture",.open=sound_open,.room=sound_room,.write=sound_write,.close=sound_close};
    int16_t clip[16]={1};tiku_sound_observation_t sound;tiku_sound_init(&backend);tiku_sound_observe(&sound);
    CHECK(sound.configured&&!sound.opened&&!sound.last_open_known&&!opens&&!strcmp(sound.backend,"observation-fixture"));
    tiku_sound_set_clip(TIKU_SOUND_BEEP,clip,16);tiku_sound_set_volume(35);tiku_sound_play(TIKU_SOUND_BEEP);tiku_sound_observe(&sound);
    CHECK(sound.event==TIKU_SOUND_BEEP&&sound.frames==16&&sound.volume==35&&!opens);
    tiku_sound_pump();tiku_sound_observe(&sound);CHECK(opens==1&&writes==1&&sound.opened&&sound.last_open_known&&sound.last_open_ok);
    tiku_sound_free();CHECK(closes==1);tiku_sound_init(&backend);fail=1;
    CHECK(tiku_sound_open_now()<0);tiku_sound_opened(0);tiku_sound_observe(&sound);CHECK(sound.last_open_known&&!sound.last_open_ok&&!sound.opened);
    tiku_sound_free();
    tiku_volumes_t volumes={0},copy;uint64_t first,second;CHECK(tiku_volumes_cached(&copy,&first)<0);
    volumes.n=1;strcpy(volumes.v[0].mount,"/fixture");volumes.v[0].dev=77;volumes.v[0].total=100;volumes.v[0].avail=40;
    tiku_volumes_cache_set(&volumes,1);CHECK(tiku_volumes_cached(&copy,&first)==1&&first&&copy.v[0].avail==40);
    copy.v[0].avail=1;CHECK(tiku_volumes_cached(&copy,&second)==1&&copy.v[0].avail==40&&first==second);
    volumes.v[0].dev=78;tiku_volumes_cache_set(&volumes,1);CHECK(tiku_volumes_cached(&copy,&second)==1&&second>first&&copy.v[0].dev==78);
#ifdef __linux__
    CHECK(!setenv("DBUS_SESSION_BUS_ADDRESS","unix:path=/nonexistent/stomme-observation-test-bus",1));
    tiku_print_native_t *native=tiku_print_native_open("",NULL);tiku_print_job_observation_t job;CHECK(native);
    CHECK(tiku_print_native_begin(native,9,"%PDF-x",6,"Isolated fixture",printed,NULL));
    CHECK(tiku_print_native_observe(native,&job,1)==1&&job.request==9&&job.bytes==6&&!strcmp(job.title,"Isolated fixture"));
    tiku_print_native_cancel(native,9);CHECK(tiku_print_native_observe(native,&job,1)==1&&job.cancellation_requested);
    for(int i=0;i<10000&&!completions;i++){tiku_print_native_poll(native);usleep(1000);}
    CHECK(completions==1&&!tiku_print_native_observe(native,&job,1));tiku_print_native_close(native);
#endif
    puts("PASS native observations: no device query, copied cache, engine facts and owned print metadata");return 0;
}
