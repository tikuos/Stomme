/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_fs_generation.c - operation-bound file cancellation and copied outcomes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
int main(void)
{
    char directory[]="/tmp/stomme-file-generation-XXXXXX",source[512],destination[512],copy[512];uint64_t first,second;int busy;tiku_progress_t progress;
    CHECK(mkdtemp(directory));snprintf(source,sizeof source,"%s/source",directory);snprintf(destination,sizeof destination,"%s/destination",directory);CHECK(!mkdir(destination,0700));
    FILE *file=fopen(source,"w");CHECK(file&&fwrite("fixture",1,7,file)==7&&!fclose(file));
    tiku_backend_t *backend=tiku_backend_posix_open();tiku_fs_t *fs=tiku_fs_new(backend);const char *paths[]={source};CHECK(backend&&fs);
    CHECK(!tiku_fs_observe(fs,&first,&progress,&busy));CHECK(!tiku_fs_copy(fs,paths,1,destination,TIKU_CONFLICT_ASK));
    CHECK(tiku_fs_observe(fs,&first,&progress,&busy)&&first&&busy);
    while(tiku_fs_busy(fs))CHECK(tiku_fs_step(fs,1)>=0);
    CHECK(!tiku_fs_cancel_generation(fs,first));CHECK(!tiku_fs_copy(fs,paths,1,destination,TIKU_CONFLICT_ASK));
    CHECK(tiku_fs_observe(fs,&second,&progress,&busy)&&second!=first&&busy);
    CHECK(!tiku_fs_cancel_generation(fs,first));CHECK(tiku_fs_busy(fs));
    CHECK(tiku_fs_cancel_generation(fs,second));CHECK(tiku_fs_observe(fs,&second,&progress,&busy)&&!busy&&progress.cancelled);
    tiku_fs_free(fs);tiku_backend_close(backend);snprintf(copy,sizeof copy,"%s/destination/source",directory);
    CHECK(!unlink(copy)&&!unlink(source)&&!rmdir(destination)&&!rmdir(directory));
    puts("PASS file operation generation: stale cancellation cannot affect a replacement");return 0;
}
