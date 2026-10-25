/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_volume_requests.c - native save ordering and current cached mount requests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_desktop_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char fixture_path[]="/tmp/stomme-volume-XXXXXX";
static dev_t fixture_device;
#define CHECK(t) do{if(!(t)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#t);exit(1);}}while(0)
static int saved,queued,accept=1;
/** @brief Record the native pre-unmount save boundary. */
void save_before_unmount(dev_t device,void *context){CHECK(device==fixture_device&&context);saved++;}
/** @brief Record worker queue acceptance without calling the host unmount syscall. */
int desktop_wire_unmount(desktop_t *app,const char *mount,const char *name)
{
    CHECK(app&&saved>queued&&!strcmp(mount,fixture_path)&&!strcmp(name,"Fixture"));queued++;return accept;
}
int main(void)
{
    struct stat native;CHECK(mkdtemp(fixture_path)&&!stat(fixture_path,&native));fixture_device=native.st_dev;
    desktop_t *app=calloc(1,sizeof *app);tiku_volumes_t volumes={.n=1};char why[200];CHECK(app);
    strcpy(volumes.v[0].mount,fixture_path);strcpy(volumes.v[0].name,"Fixture");volumes.v[0].dev=fixture_device;volumes.v[0].removable=1;
    tiku_volume_t expected=volumes.v[0];tiku_volumes_cache_set(&volumes,1);
    CHECK(desktop_request_unmount(app,&expected,why,sizeof why)&&saved==1&&queued==1);
    app->unmount_job=1;CHECK(!desktop_request_unmount(app,&expected,why,sizeof why)&&saved==1&&queued==1);app->unmount_job=0;
    volumes.v[0].dev=fixture_device+1;tiku_volumes_cache_set(&volumes,1);CHECK(!desktop_request_unmount(app,&expected,why,sizeof why)&&saved==1&&queued==1);
    volumes.v[0]=expected;volumes.v[0].is_boot=1;tiku_volumes_cache_set(&volumes,1);CHECK(!desktop_request_unmount(app,&expected,why,sizeof why)&&saved==1&&queued==1);
    volumes.v[0]=expected;tiku_volumes_cache_set(&volumes,1);accept=0;CHECK(!desktop_request_unmount(app,&expected,why,sizeof why)&&saved==2&&queued==2&&why[0]);
    free(app);CHECK(!rmdir(fixture_path));puts("PASS volume requests: save before queue, busy refusal, mount replacement and boot policy");return 0;
}

#include "tiku_desktop_volume_commands.c"
