/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * host_resources_fixture.c - SDK semantic publications and user print preparation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_client.h"
#include "tiku_window_app.h"
#include "tiku_access.h"
#include "tiku_remote.h"
#include "tiku_print_rpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Private application state with the copied public host services. */
typedef struct {
    tiku_app_services_t services;tiku_window_app_t *app;tiku_window_t *window;
    unsigned generation,actions;char directory[512];
} fixture_t;
/** @brief Store an application-owned result for its independent driver. */
static void record(fixture_t *fixture,const char *name,int value)
{
    char path[600];snprintf(path,sizeof path,"%s/%s",fixture->directory,name);
    FILE *file=fopen(path,"w");if(file){fprintf(file,"%d\n",value);fclose(file);}
}
/** @brief Publish Unicode text and scalar geometry through the native codec. */
static void publish(fixture_t *fixture)
{
    tiku_rect_t boxes[4]={{1,2,7,12},{8,2,7,12},{15,2,7,12},{22,2,7,12}};
    uint32_t lines[4]={0,0,0,0};tiku_access_tree_t *tree=NULL;
    tiku_access_node_t node={.parent=TIKU_ACCESS_NO_PARENT,.role=TIKU_ACCESS_DOCUMENT,
        .state=TIKU_ACCESS_VISIBLE|TIKU_ACCESS_FOCUSED|TIKU_ACCESS_EDITABLE,
        .bounds={0,0,320,240},.name="Semantic fixture",.text="Aé中😀",.description="Unicode geometry",.target="",
        .selection_start=1,.selection_end=3,.caret=3,.character_bounds=boxes,.character_lines=lines,.character_count=4};
    if(tiku_access_create(101,fixture->generation,&node,1,&tree))return;
    void *wire=NULL;size_t bytes=0;char path[600];
    if(!tiku_access_encode(tree,&wire,&bytes)) {
        snprintf(path,sizeof path,"%s/tree-%u.bin",fixture->directory,fixture->generation);
        FILE *file=fopen(path,"wb");if(file){fwrite(wire,1,bytes,file);fclose(file);}free(wire);
    }
    tiku_app_access(&fixture->services,tiku_window_app_id(fixture->app,fixture->window),tree);tiku_access_release(tree);
}
/** @brief Paint the application's own logical canvas. */
static void draw(tiku_window_t *window,tiku_surface_t *surface,tiku_rect_t content,void *context)
{
    (void)window;(void)context;tiku_fill(surface,content,0xf8f8ff);
}
/** @brief Start an ordinary SDK window and its immutable semantic publication. */
static int start(void **state,const tiku_app_services_t *services)
{
    const char *directory=getenv("STOMME_HOST_FIXTURE_DIRECTORY");fixture_t *fixture=calloc(1,sizeof *fixture);
    if(!directory||!fixture){free(fixture);return -1;}fixture->services=*services;fixture->generation=1;
    snprintf(fixture->directory,sizeof fixture->directory,"%s",directory);fixture->app=tiku_window_app_new(services);
    if(!fixture->app){free(fixture);return -1;}
    fixture->window=tiku_workspace_open(tiku_window_app_workspace(fixture->app),"Host services fixture",(tiku_rect_t){20,20,320,240},draw,fixture,NULL);
    if(!fixture->window){tiku_window_app_free(fixture->app);free(fixture);return -1;}
    *state=fixture;tiku_window_app_sync(fixture->app,1);publish(fixture);return 0;
}
/** @brief Request printing from the normal focused keyboard route. */
static int event_in(void *state,uint32_t id,const tiku_event_t *event)
{
    fixture_t *fixture=state;
    if(event->type==TIKU_EVENT_KEY_DOWN&&event->key=='p'&&(event->modifiers&TIKU_MOD_CMD))
        tiku_app_print_prepare(&fixture->services,id,55,"Host services fixture");
    tiku_window_app_event(fixture->app,id,event);tiku_window_app_sync(fixture->app,1);
    return !tiku_workspace_count(tiku_window_app_workspace(fixture->app));
}
/** @brief Accept current semantic messages and retain native print acknowledgments. */
static void message_in(void *state,const tiku_msg_t *message)
{
    fixture_t *fixture=state;int32_t value=0;int64_t document=0,generation=0;
    if(tiku_msg_what(message)==TIKU_MSG_ACCESS_ACTION) {
        tiku_msg_find_int64(message,"document",0,&document);tiku_msg_find_int64(message,"generation",0,&generation);
        if(document!=101||generation!=fixture->generation)return;
        fixture->actions++;record(fixture,"actions",fixture->actions);fixture->generation++;publish(fixture);
    } else if(tiku_msg_what(message)==TIKU_MSG_PRINT_READY) {
        tiku_msg_find_int32(message,"accepted",0,&value);record(fixture,"print-ready",value);
    } else if(tiku_msg_what(message)==TIKU_MSG_PRINT_DONE) {
        tiku_msg_find_int32(message,"status",0,&value);record(fixture,"print-done",value);
    }
}
/** @brief Release the application-owned workspace. */
static void stop(void *state)
{
    fixture_t *fixture=state;tiku_window_app_free(fixture->app);free(fixture);
}
static const tiku_app_descriptor_t descriptor={.id="org.stomme.test.host",.name="Host fixture",.start=start,.stop=stop,.event_in=event_in,.message=message_in};
#ifdef TIKU_APP_SO
/** @brief Export the fixture to the native application runner. */
const tiku_app_export_t tiku_app_v1={TIKU_APP_ABI,(uint32_t)sizeof(tiku_app_descriptor_t),&descriptor};
#else
int main(void){return tiku_client_run(&descriptor);}
#endif
