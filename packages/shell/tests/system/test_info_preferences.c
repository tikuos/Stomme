/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_info_preferences.c - Get Info preference saves through the session owner.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_panels.c"
#include "tiku_state_update.h"
#include "tiku_work.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)
/** @brief Record a redraw requested by the native completion callback. */
void desktop_damage_all(desktop_t *app){app->dirty=1;}
/** @brief Collect the association controller on its owner thread. */
static void drain(desktop_t *app)
{
    for(int i=0;i<200;i++){tiku_trk_catalog_settings_poll(app->catalog_settings);tiku_work_collect();usleep(1000);}
    CHECK(!tiku_work_busy());
}
int main(void)
{
    char home[]="/tmp/stomme-info-preferences-XXXXXX",sidecar[512],backup[520],value[64];
    desktop_t *app=calloc(1,sizeof *app);tiku_trk_info_t *info;CHECK(app && mkdtemp(home) && !setenv("XDG_DATA_HOME",home,1));
    app->store=tiku_store_sidecar_open("info");app->surf=tiku_surface_new(1100,800,TIKU_C_PANEL);app->desk=tiku_workspace_new(app->surf);CHECK(app->store && app->surf && app->desk);
    CHECK(tiku_trk_launch_add_document(&app->apps,"Editor","/runner","/editor.so","text/plain")>=0);
    CHECK(tiku_trk_launch_add_document(&app->apps,"Viewer","/runner","/viewer.so","text/plain")>=0);
    CHECK(tiku_trk_types_add(&app->types,"text/plain","Text","text","")>=0);
    app->catalog_settings=tiku_trk_catalog_settings_open(app->store,&app->apps,&app->types,NULL,NULL);CHECK(app->catalog_settings);
    info=&app->info_windows.model[0];info->model.kind=TIKU_KIND_FILE;strcpy(info->model.name,"sample.txt");strcpy(info->model.type,"text/plain");
    app->info_windows.window[0]=tiku_workspace_open(app->desk,"Info",(tiku_rect_t){0,0,400,300},NULL,NULL,NULL);CHECK(app->info_windows.window[0]);
    info_opens_with_fill(app,info);CHECK(!strcmp(info->opens_with,"Default application"));
    info_opens_with_next(app,info);CHECK(!strcmp(info->opens_with,"Default application") && strstr(app->status,"Saving"));drain(app);
    CHECK(!strcmp(info->opens_with,"Editor") && strstr(app->status,"saved"));
    {tiku_store_t *disk=tiku_store_sidecar_open("info");CHECK(disk && !strcmp(tiku_trk_launch_preferred(disk,"text/plain",value,sizeof value),"Editor"));tiku_store_free(disk);}
    info_opens_with_next(app,info);drain(app);CHECK(!strcmp(info->opens_with,"Viewer"));
    info_opens_with_next(app,info);drain(app);CHECK(!strcmp(info->opens_with,"Default application"));
    snprintf(sidecar,sizeof sidecar,"%s/tracker/devices/info.state",home);snprintf(backup,sizeof backup,"%s.bak",sidecar);CHECK(!rename(sidecar,backup) && !mkdir(sidecar,0700));
    info_opens_with_next(app,info);drain(app);CHECK(!strcmp(info->opens_with,"Default application") && strstr(app->status,"Could not save"));
    CHECK(!rmdir(sidecar) && !rename(backup,sidecar));info_opens_with_next(app,info);drain(app);CHECK(!strcmp(info->opens_with,"Editor"));
    {
        tiku_store_update_t *block=tiku_store_update_begin(app->store);CHECK(block);info_opens_with_next(app,info);
        CHECK(!tiku_trk_launch_set_preferred(app->store,"text/plain","External choice"));tiku_store_update_free(block);drain(app);
        CHECK(!strcmp(info->opens_with,"External choice") && strstr(app->status,"Associations changed"));
        CHECK(!tiku_trk_launch_set_preferred(app->store,"text/plain","Editor") && !tiku_store_flush(app->store));
    }
    info_opens_with_fill(app,info);info_opens_with_next(app,info);tiku_workspace_close(app->desk,app->info_windows.window[0]);drain(app);
    CHECK(!strcmp(tiku_trk_launch_preferred(app->store,"text/plain",value,sizeof value),"Viewer"));
    tiku_trk_catalog_settings_close(app->catalog_settings);tiku_store_free(app->store);tiku_workspace_free(app->desk);tiku_surface_free(app->surf);free(app);
    CHECK(!unlink(sidecar));snprintf(sidecar,sizeof sidecar,"%s/tracker/devices",home);CHECK(!rmdir(sidecar));snprintf(sidecar,sizeof sidecar,"%s/tracker",home);CHECK(!rmdir(sidecar) && !rmdir(home));
    puts("PASS Get Info preferences: staged save, automatic, failure/retry, stale choice and closed view");return 0;
}
