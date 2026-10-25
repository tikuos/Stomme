/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_catalog_window.c - staged association window saves and live model updates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include "tiku_work.h"
#include "../../system/desktop/tiku_trk_desktop_associations.c"
/** @brief Mark the desktop for repaint after a controller completion. */
void desktop_damage_all(desktop_t *app){app->dirty=1;}
/** @brief Send the native button press/release pair. */
static void click(tiku_window_t *window,tiku_rect_t r)
{
    tiku_event_t event={.type=TIKU_EVENT_POINTER_DOWN,.button=1,.x=r.x+8,.y=r.y+r.h/2};
    assert(tiku_window_send(window,&event));event.type=TIKU_EVENT_POINTER_UP;assert(tiku_window_send(window,&event));
}
/** @brief Collect the same queued saves used by the desktop loop. */
static void drain(desktop_t *app)
{
    for(int i=0;i<200;i++){tiku_trk_catalog_settings_poll(app->catalog_settings);tiku_work_collect();usleep(1000);}assert(!tiku_work_busy());
}
int main(void)
{
    char home[]="/tmp/stomme-catalog-window-XXXXXX",path[512],backup[520],name[64];
    desktop_t *app=calloc(1,sizeof *app);assert(app && mkdtemp(home) && !setenv("XDG_DATA_HOME",home,1));
    app->store=tiku_store_sidecar_open("window");app->surf=tiku_surface_new(1100,800,TIKU_C_PANEL);app->desk=tiku_workspace_new(app->surf);
    assert(app->store && app->surf && app->desk);
    assert(tiku_trk_launch_add_document(&app->apps,"Editor","/runner","/editor.so","text/plain")>=0);
    assert(tiku_trk_launch_add_document(&app->apps,"Viewer","/runner","/viewer.so","text/plain")>=0);
    assert(tiku_trk_types_add(&app->types,"text/plain","Plain text","text","")>=0);
    app->catalog_settings=tiku_trk_catalog_settings_open(app->store,&app->apps,&app->types,NULL,NULL);assert(app->catalog_settings);
    desktop_associations_open(app);struct desktop_associations *p=app->associations;assert(p && p->model.count==1);
    strcpy(p->model.row[0].chosen,"Viewer");
    click(p->window,assoc_layout(tiku_window_content(p->window)).save);
    assert(p->saving && desktop_associations_handle(app));
    assert(!strcmp(tiku_trk_launch_preferred(app->store,"text/plain",name,sizeof name),""));
    drain(app);assert(!p->saving && !desktop_associations_handle(app));
    assert(!strcmp(tiku_trk_launch_preferred(app->store,"text/plain",name,sizeof name),"Viewer"));
    desktop_associations_open(app);
    assert(!tiku_trk_launch_set_preferred(app->store,"text/plain","Editor"));
    assoc_sync(p);assert(!strcmp(p->model.row[0].chosen,"Editor"));
    strcpy(p->model.row[0].chosen,"Viewer");assert(!tiku_trk_launch_set_preferred(app->store,"text/plain",""));
    assoc_sync(p);assert(!strcmp(p->model.row[0].saved,"Editor") && !strcmp(p->model.row[0].chosen,"Viewer"));
    click(p->window,assoc_layout(tiku_window_content(p->window)).save);drain(app);assert(desktop_associations_handle(app) && strstr(p->status,"Associations changed"));
    desktop_associations_close(app);desktop_associations_open(app);strcpy(p->model.row[0].chosen,"Editor");
    assert(!tiku_store_flush(app->store));snprintf(path,sizeof path,"%s/tracker/devices/window.state",home);snprintf(backup,sizeof backup,"%s.bak",path);
    assert(!rename(path,backup) && !mkdir(path,0700));
    click(p->window,assoc_layout(tiku_window_content(p->window)).save);drain(app);assert(desktop_associations_handle(app) && strstr(p->status,"Could not save"));
    assert(!strcmp(tiku_trk_launch_preferred(app->store,"text/plain",name,sizeof name),""));
    assert(!rmdir(path) && !rename(backup,path));
    click(p->window,assoc_layout(tiku_window_content(p->window)).save);
    desktop_associations_close(app);desktop_associations_open(app);drain(app);assert(desktop_associations_handle(app));
    assoc_sync(p);assert(!strcmp(p->model.row[0].chosen,"Editor"));
    tiku_trk_catalog_settings_close(app->catalog_settings);app->catalog_settings=NULL;
    desktop_associations_destroy(app);tiku_workspace_free(app->desk);tiku_surface_free(app->surf);tiku_store_free(app->store);free(app);
    assert(!unlink(path));snprintf(path,sizeof path,"%s/tracker/devices",home);assert(!rmdir(path));snprintf(path,sizeof path,"%s/tracker",home);assert(!rmdir(path) && !rmdir(home));
    puts("PASS catalog window: pending save, live refresh, stale draft, failed save and reopened window lifetime");return 0;
}
