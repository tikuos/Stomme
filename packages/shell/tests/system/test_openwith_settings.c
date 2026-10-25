/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_openwith_settings.c - preferred-handler persistence before document launch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/shared/ui/windows/tiku_trk_open_with_window.c"
#include "tiku_state_update.h"
#include "tiku_work.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)

typedef struct {
    int launches;
    char application[TIKU_TRK_APP_NAME_MAX], path[TIKU_PATH_MAX];
    const char *types[2];
    int type_count;
} observed_t;

/** @brief Require the chosen handler on disk before observing a launch. */
static void launched(void *context,const tiku_trk_app_t *app,const char *const *paths,int count)
{
    observed_t *observed=context;tiku_store_t *disk=tiku_store_sidecar_open("openwith");char value[TIKU_TRK_APP_NAME_MAX];
    CHECK(disk && count==1);
    for(int i=0;i<observed->type_count;i++)
        CHECK(!strcmp(tiku_trk_launch_preferred(disk,observed->types[i],value,sizeof value),app->name));
    tiku_store_free(disk);observed->launches++;
    snprintf(observed->application,sizeof observed->application,"%s",app->name);
    snprintf(observed->path,sizeof observed->path,"%s",paths[0]);
}

/** @brief Collect queued updates through the desktop's owner-thread path. */
static void drain(tiku_trk_catalog_settings_t *owner)
{
    for(int i=0;i<200;i++){tiku_trk_catalog_settings_poll(owner);tiku_work_collect();usleep(1000);}
    CHECK(!tiku_work_busy());
}

/** @brief Select a candidate by its native app identity. */
static void choose(tiku_trk_open_with_window_t *chooser,const char *name)
{
    for(int i=0;i<chooser->count;i++)if(!strcmp(chooser->cand[i].app->name,name)){
        tiku_list_click(&chooser->list,i,0);return;
    }
    CHECK(0);
}

/** @brief Press and release the chooser's actual make-preferred control. */
static void prefer(tiku_trk_open_with_window_t *chooser)
{
    tiku_rect_t open,save;tiku_event_t event={.type=TIKU_EVENT_POINTER_DOWN,.button=1};
    buttons(chooser,tiku_window_content(chooser->window),&open,&save);event.x=save.x+8;event.y=save.y+save.h/2;
    CHECK(tiku_window_send(chooser->window,&event));event.type=TIKU_EVENT_POINTER_UP;CHECK(tiku_window_send(chooser->window,&event));
}

int main(void)
{
    char home[]="/tmp/stomme-openwith-XXXXXX",sidecar[512],backup[520],value[64];
    tiku_trk_apps_t *apps=calloc(1,sizeof *apps);tiku_trk_types_t types={0};observed_t observed={.types={"text/plain","text/html"},.type_count=1};
    tiku_trk_open_with_window_t *chooser=calloc(1,sizeof *chooser);tiku_surface_t *surface=tiku_surface_new(1100,800,TIKU_C_PANEL);
    tiku_workspace_t *workspace=tiku_workspace_new(surface);tiku_trk_catalog_settings_t *owner;tiku_store_t *store;int dirty=0;
    const char *mime[]={"text/plain"},*paths[]={"/first.txt"},*next[]={"/replacement.txt"},*mixed[]={"text/plain","text/html"};
    CHECK(apps && chooser && surface && workspace && mkdtemp(home) && !setenv("XDG_DATA_HOME",home,1));
    store=tiku_store_sidecar_open("openwith");CHECK(store);
    CHECK(tiku_trk_launch_add_document(apps,"Editor","/runner","/editor.so","text/plain,text/html")>=0);
    CHECK(tiku_trk_launch_add_document(apps,"Viewer","/runner","/viewer.so","text/plain,text/html")>=0);
    CHECK(tiku_trk_launch_add_document(apps,"Text only","/runner","/text.so","text/plain")>=0);
    CHECK(tiku_trk_types_add(&types,"text/plain","Text","text","")>=0 && tiku_trk_types_add(&types,"text/html","HTML","text","")>=0);
    owner=tiku_trk_catalog_settings_open(store,apps,&types,NULL,NULL);CHECK(owner);
    tiku_trk_open_with_window_init(chooser,workspace,1100,800,&dirty,launched,&observed,NULL,store);
    tiku_trk_open_with_window_bind_settings(chooser,owner);
    tiku_trk_open_with_window_open(chooser,apps,mime,1,paths,1);choose(chooser,"Viewer");prefer(chooser);
    CHECK(chooser->pending && !observed.launches && chooser->window->open);
    CHECK(!strcmp(tiku_trk_launch_preferred(store,mime[0],value,sizeof value),""));
    tiku_trk_open_with_window_key(chooser,TIKU_KEY_RETURN,0);CHECK(!observed.launches);
    drain(owner);CHECK(!chooser->pending && observed.launches==1 && !chooser->window && !strcmp(observed.path,"/first.txt"));
    tiku_trk_open_with_window_open(chooser,apps,mime,1,paths,1);choose(chooser,"Editor");
    snprintf(sidecar,sizeof sidecar,"%s/tracker/devices/openwith.state",home);snprintf(backup,sizeof backup,"%s.bak",sidecar);
    CHECK(!rename(sidecar,backup) && !mkdir(sidecar,0700));prefer(chooser);drain(owner);
    CHECK(!chooser->pending && observed.launches==1 && chooser->window->open && strstr(chooser->status,"Could not save"));
    CHECK(!strcmp(tiku_trk_launch_preferred(store,mime[0],value,sizeof value),"Viewer"));
    CHECK(!rmdir(sidecar) && !rename(backup,sidecar));prefer(chooser);CHECK(chooser->pending);drain(owner);CHECK(observed.launches==2 && !strcmp(observed.application,"Editor"));
    {
        tiku_store_update_t *block=tiku_store_update_begin(store);CHECK(block);
        tiku_trk_open_with_window_open(chooser,apps,mime,1,paths,1);choose(chooser,"Viewer");prefer(chooser);tiku_trk_catalog_settings_poll(owner);CHECK(chooser->pending);
        CHECK(tiku_trk_open_with_window_key(chooser,TIKU_KEY_ESCAPE,0));CHECK(!chooser->window && !chooser->pending);
        tiku_store_update_free(block);drain(owner);CHECK(observed.launches==2);
        CHECK(!strcmp(tiku_trk_launch_preferred(store,mime[0],value,sizeof value),"Viewer"));
    }
    {
        tiku_store_update_t *block=tiku_store_update_begin(store);CHECK(block);
        tiku_trk_open_with_window_open(chooser,apps,mime,1,paths,1);choose(chooser,"Editor");prefer(chooser);
        tiku_trk_open_with_window_open(chooser,apps,mime,1,next,1);CHECK(!chooser->pending);
        tiku_store_update_free(block);drain(owner);CHECK(observed.launches==2 && chooser->window && chooser->window->open);
        CHECK(!strcmp(chooser->paths[0],"/replacement.txt"));
    }
    {
        tiku_store_update_t *block=tiku_store_update_begin(store);CHECK(block);
        choose(chooser,"Viewer");prefer(chooser);tiku_workspace_close(workspace,chooser->window);CHECK(!chooser->window && !chooser->pending);
        tiku_window_t *replacement=tiku_workspace_open(workspace,"Other",(tiku_rect_t){0,0,300,200},NULL,NULL,NULL);CHECK(replacement);
        tiku_store_update_free(block);drain(owner);CHECK(observed.launches==2 && replacement->open);
        tiku_workspace_close(workspace,replacement);
    }
    tiku_trk_open_with_window_open(chooser,apps,mixed,2,paths,1);choose(chooser,"Text only");
    {int can_open,can_prefer;enables(chooser,&can_open,&can_prefer);CHECK(can_open && !can_prefer);}
    CHECK(tiku_trk_catalog_settings_prefer_validate(owner,mixed,2,"Text only")==TIKU_RESOURCE_VALUE);
    choose(chooser,"Viewer");observed.type_count=2;prefer(chooser);CHECK(chooser->pending);drain(owner);CHECK(observed.launches==3);
    tiku_trk_open_with_window_open(chooser,apps,mime,1,paths,1);choose(chooser,"Editor");
    {
        tiku_store_update_t *block=tiku_store_update_begin(store);CHECK(block);prefer(chooser);
        CHECK(!tiku_trk_launch_set_preferred(store,mime[0],"Text only"));tiku_store_update_free(block);drain(owner);
        CHECK(observed.launches==3 && chooser->window->open && strstr(chooser->status,"Associations changed"));
        CHECK(!tiku_trk_launch_set_preferred(store,mime[0],"Viewer") && !tiku_store_flush(store));
    }
    {
        tiku_store_update_t *block=tiku_store_update_begin(store);CHECK(block);prefer(chooser);
        tiku_trk_open_with_window_destroy(chooser);free(chooser);chooser=NULL;
        tiku_store_update_free(block);drain(owner);CHECK(observed.launches==3);
    }
    tiku_trk_catalog_settings_close(owner);tiku_store_free(store);tiku_workspace_free(workspace);tiku_surface_free(surface);free(apps);
    CHECK(!unlink(sidecar));snprintf(sidecar,sizeof sidecar,"%s/tracker/devices",home);CHECK(!rmdir(sidecar));snprintf(sidecar,sizeof sidecar,"%s/tracker",home);CHECK(!rmdir(sidecar) && !rmdir(home));
    puts("PASS Open With settings: durable-before-launch, failure/retry, cancellation, retarget, window reuse, mixed types and owner teardown");return 0;
}
