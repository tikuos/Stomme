/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_work_observe.c - bounded copied work identities and terminal retention.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_work.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static atomic_int gate,runs,done,frees;
/** @brief Hold one serialized worker until cancellation has been observed. */
static void run(void *context){(void)context;runs++;while(!gate)usleep(1000);}
/** @brief Count delivered owner completions. */
static void complete(void *context){(void)context;done++;}
/** @brief Count contexts released after collection. */
static void release(void *context){(void)context;frees++;}
int main(void)
{
    tiku_work_job_t job={.run=run,.done=complete,.free=release};tiku_work_observation_t rows[128];size_t count;
    strcpy(job.key,"observed-serial");
    for(int i=0;i<64;i++)CHECK(!tiku_work_post(&job));
    CHECK(tiku_work_post(&job)<0);count=tiku_work_observe(rows,128);CHECK(count==64);
    for(size_t i=0;i<count;i++){CHECK(rows[i].id&&rows[i].state<=1);for(size_t j=0;j<i;j++)CHECK(rows[j].id!=rows[i].id);}
    CHECK(tiku_work_cancel(job.key)==64);gate=1;
    for(int i=0;i<10000&&tiku_work_busy();i++){tiku_work_collect();usleep(1000);}
    CHECK(!tiku_work_busy()&&done==0&&frees==64);count=tiku_work_observe(rows,128);CHECK(count==64);
    unsigned long long maximum=0;
    for(size_t i=0;i<count;i++){CHECK(rows[i].state==3&&rows[i].cancellation_requested);if(rows[i].id>maximum)maximum=rows[i].id;}
    CHECK(!tiku_work_post(&job));
    for(int i=0;i<10000&&tiku_work_busy();i++){tiku_work_collect();usleep(1000);}
    CHECK(!tiku_work_busy()&&done==1&&frees==65);count=tiku_work_observe(rows,128);CHECK(count==64);
    int found=0;for(size_t i=0;i<count;i++)if(rows[i].id>maximum){CHECK(rows[i].state==2&&!rows[i].cancellation_requested);found=1;}
    CHECK(found);puts("PASS work observations: copied identities, bounded history and native cancellation outcomes");return 0;
}
