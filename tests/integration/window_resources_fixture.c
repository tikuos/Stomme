/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * window_resources_fixture.c - SDK window owner with a controlled close veto.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_client.h"
#include "tiku_window_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/** @brief Toolkit owner and counts retained independently of the desktop. */
typedef struct {tiku_window_app_t *app;int closes,resizes,width,height;char directory[512];} fixture_t;
/** @brief Publish the native application's received events for its test driver. */
static void record(fixture_t *fixture)
{
    char path[600];FILE *file;snprintf(path,sizeof path,"%s/state",fixture->directory);
    file=fopen(path,"w");if(file){fprintf(file,"%d %d %d %d\n",fixture->closes,fixture->resizes,fixture->width,fixture->height);fclose(file);}
}
/** @brief Keep the document open until the driver supplies the consent file. */
static int request_close(tiku_window_t *window,void *context)
{
    fixture_t *fixture=context;char path[600];(void)window;fixture->closes++;record(fixture);
    snprintf(path,sizeof path,"%s/allow-close",fixture->directory);return !access(path,F_OK);
}
/** @brief Paint the native application surface through its own workspace. */
static void draw(tiku_window_t *window,tiku_surface_t *surface,tiku_rect_t content,void *context)
{
    (void)window;(void)context;tiku_fill(surface,content,0xfff8e0);
}
/** @brief Open and publish a normal toolkit window. */
static int start(void **state,const tiku_app_services_t *services)
{
    fixture_t *fixture=calloc(1,sizeof *fixture);tiku_window_t *window;const char *directory=getenv("STOMME_WINDOW_FIXTURE_DIRECTORY");
    if(!fixture||!directory){free(fixture);return -1;}
    if(getenv("STOMME_LIFECYCLE_FAILURE")){free(fixture);return -1;}
    snprintf(fixture->directory,sizeof fixture->directory,"%s",directory);
    if(services->startup_document) {
        char path[600];snprintf(path,sizeof path,"%s/document",directory);FILE *file=fopen(path,"w");
        if(file){fputs(services->startup_document,file);fclose(file);}
    }
    fixture->app=tiku_window_app_new(services);if(!fixture->app){free(fixture);return -1;}
    window=tiku_workspace_open(tiku_window_app_workspace(fixture->app),"Unsaved fixture",(tiku_rect_t){10,10,320,240},draw,fixture,NULL);
    if(!window){tiku_window_app_free(fixture->app);free(fixture);return -1;}
    tiku_window_set_handlers(window,NULL,request_close,NULL);*state=fixture;
    tiku_window_app_sync(fixture->app,1);record(fixture);return 0;
}
/** @brief Deliver host events through the public toolkit window adapter. */
static int event_in(void *state,uint32_t id,const tiku_event_t *event)
{
    fixture_t *fixture=state;
    if(event->type==TIKU_EVENT_RESIZE){fixture->resizes++;fixture->width=event->width;fixture->height=event->height;record(fixture);}
    tiku_window_app_event(fixture->app,id,event);tiku_window_app_sync(fixture->app,1);
    return !tiku_workspace_count(tiku_window_app_workspace(fixture->app));
}
/** @brief Mirror a host-authorized closed-window notice. */
static void closed(void *state,uint32_t id)
{
    fixture_t *fixture=state;tiku_window_app_closed(fixture->app,id);
}
/** @brief Release the application's private workspace. */
static void stop(void *state)
{
    fixture_t *fixture=state;tiku_window_app_free(fixture->app);free(fixture);
}
static const tiku_app_descriptor_t descriptor={.id="org.stomme.test.window",.name="Window fixture",.start=start,.stop=stop,.event_in=event_in,.closed=closed};
#ifdef TIKU_APP_SO
/** @brief Export the fixture to the native application runner. */
const tiku_app_export_t tiku_app_v1={TIKU_APP_ABI,(uint32_t)sizeof(tiku_app_descriptor_t),&descriptor};
#else
int main(void){return tiku_client_run(&descriptor);}
#endif
