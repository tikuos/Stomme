/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * workspace.c - borrowed workspace storage, stacking, damage and teardown.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_window.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static union { max_align_t align; unsigned char bytes[256*1024]; } storage;
static tiku_rgb_t pixels[320*240], reference[320*240];
static unsigned destroyed, captured, released;
extern unsigned tiku_service_heap_calls;
static tiku_surface_t snapshot;
/** @brief Paint each window with its supplied content colour. */
static void paint(tiku_window_t *w,tiku_surface_t *s,tiku_rect_t r,void *context)
{ (void)w;tiku_fill(s,r,*(tiku_rgb_t *)context); }
/** @brief Paint the content, then, after a clip ended by a reset, a band
 *  reaching 8 pixels below the content. */
static void overhang(tiku_window_t *w,tiku_surface_t *s,tiku_rect_t r,void *context)
{
    (void)w;tiku_fill(s,r,*(tiku_rgb_t *)context);
    tiku_clip_set(s,(tiku_rect_t){r.x,r.y,10,10});tiku_clip_reset(s);
    tiku_fill(s,(tiku_rect_t){r.x,r.y+r.h-4,r.w,12},0x20c040);
}
/** @brief Count close notifications without changing workspace state. */
static void destroy(tiku_window_t *w,void *context)
{ (void)w;(void)context;++destroyed; }
/** @brief Keep the window open when its owner refuses a close. */
static int refuse(tiku_window_t *w,void *context)
{ (void)w;(void)context;return 0; }
/** @brief Lend one test snapshot to the workspace. */
static tiku_surface_t *capture(void *context,const tiku_surface_t *s,tiku_rect_t r)
{ (void)context;(void)s;(void)r;++captured;return &snapshot; }
/** @brief Count release of the test snapshot. */
static void release(void *context,tiku_surface_t *s)
{ (void)context;if(s==&snapshot)++released; }
/** @brief Lend pixels for the single window's client cache. */
static tiku_surface_t *client_cache(void *context, int width, int height,
                                    int scale, tiku_rgb_t ground)
{
    static tiku_rgb_t client_pixels[200 * 120];
    static tiku_surface_t client;
    (void)context;
    (void)ground;
    if (scale != 1 || width > 200 || height > 120) return NULL;
    client = (tiku_surface_t){ .w = width, .h = height, .scale = scale, .px = client_pixels };
    tiku_clip_reset(&client);
    return &client;
}

/** @brief Exercise native window behavior without native heap entry points. */
int main(void)
{
    tiku_surface_t s={.w=320,.h=240,.scale=1,.px=pixels};
    tiku_clip_reset(&s);memset(storage.bytes,0xa5,sizeof storage.bytes);
    CHECK(tiku_workspace_size()<=sizeof storage.bytes);
    CHECK((uintptr_t)storage.bytes%tiku_workspace_align()==0);
    CHECK(!tiku_workspace_init(storage.bytes,1,&s,NULL));
    CHECK(!tiku_workspace_init(storage.bytes+1,sizeof storage.bytes-1,&s,NULL));
    tiku_workspace_surfaces_t ops={.capture=capture};
    CHECK(!tiku_workspace_init(storage.bytes,sizeof storage.bytes,&s,&ops));
    CHECK(storage.bytes[0]==0xa5);
    tiku_workspace_t *ws=tiku_workspace_init(storage.bytes,sizeof storage.bytes,NULL,NULL);
    CHECK(ws&&tiku_workspace_count(ws)==0&&!tiku_workspace_surface(ws));
    tiku_workspace_draw(ws);tiku_workspace_deinit(ws);
    ws=tiku_workspace_init(storage.bytes,sizeof storage.bytes,&s,NULL);
    tiku_rgb_t red=0xff2010,blue=0x2040ff;
    tiku_window_t *a=tiku_workspace_open(ws,"Folder",(tiku_rect_t){10,10,200,150},paint,&red,&red);
    tiku_window_t *b=tiku_workspace_open(ws,"Information",(tiku_rect_t){100,70,200,150},paint,&blue,&blue);
    CHECK(a&&b&&tiku_workspace_count(ws)==2&&tiku_workspace_focused(ws)==b);
    CHECK(tiku_workspace_open(ws,"Ignored",(tiku_rect_t){0},NULL,NULL,&red)==a);
    CHECK(tiku_workspace_count(ws)==2&&tiku_workspace_focused(ws)==a);
    tiku_workspace_activate(ws,b);tiku_workspace_draw(ws);
    CHECK(pixels[120*320+140]==blue);
    memcpy(reference,pixels,sizeof pixels);memset(pixels,0x55,sizeof pixels);
    for(int y=0;y<240;y+=7){tiku_rect_t damage={0,y,320,y+7>240?240-y:7};
        tiku_clip_damage(&s,&damage);tiku_workspace_draw(ws);
        for(int yy=y+damage.h;yy<240;++yy)for(int x=0;x<320;++x)CHECK(pixels[yy*320+x]==0x55555555);
    }
    tiku_clip_damage(&s,NULL);CHECK(!memcmp(reference,pixels,sizeof pixels));
    tiku_window_t *hit=NULL;CHECK(tiku_workspace_hit(ws,140,120,&hit)==TIKU_HIT_CONTENT&&hit==b);
    tiku_workspace_minimize(ws,b,(tiku_rect_t){300,0,20,20});
    CHECK(b->minimized);
    CHECK(tiku_workspace_hit(ws,140,120,&hit)==TIKU_HIT_CONTENT&&hit==a);
    tiku_workspace_activate(ws,a);
    tiku_workspace_restore(ws,b);tiku_workspace_activate(ws,b);
    CHECK(!b->minimized&&tiku_workspace_focused(ws)==b);
    tiku_workspace_begin_track(ws,b,TIKU_HIT_TAB,140,75);
    CHECK(tiku_workspace_tracking(ws)&&tiku_workspace_track(ws,160,95));
    CHECK(b->frame.x==120&&b->frame.y==90);tiku_workspace_end_track(ws);
    CHECK(!tiku_workspace_tracking(ws));
    tiku_workspace_begin_track(ws,b,TIKU_HIT_RESIZE,320,240);
    CHECK(tiku_workspace_track(ws,280,200));CHECK(b->frame.w==180&&b->frame.h==110);
    tiku_workspace_end_track(ws);
    tiku_rect_t saved=b->frame;tiku_workspace_set_reserved(ws,(tiku_rect_t){260,0,60,240});
    CHECK(tiku_workspace_zoom(ws,b,310,200));CHECK(b->frame.x+b->frame.w<=260);
    CHECK(tiku_workspace_zoom(ws,b,310,200));CHECK(!memcmp(&saved,&b->frame,sizeof saved));
    tiku_window_set_handlers(b,NULL,refuse,destroy);CHECK(!tiku_workspace_request_close(ws,b));
    tiku_window_set_handlers(a,NULL,NULL,destroy);
    tiku_workspace_set_now(ws,1000);tiku_workspace_cache_mode(ws,TIKU_CACHE_REFRESH);
    tiku_workspace_draw(ws);tiku_workspace_deinit(ws);CHECK(destroyed==2);
    tiku_workspace_deinit(ws);CHECK(destroyed==2);
    ops.release=release;ws=tiku_workspace_init(storage.bytes,sizeof storage.bytes,&s,&ops);
    a=tiku_workspace_open(ws,"Folder",(tiku_rect_t){10,10,200,150},paint,&red,NULL);
    tiku_workspace_set_now(ws,1000);tiku_workspace_close(ws,a);
    CHECK(captured==1&&released==0);
    CHECK(tiku_workspace_open(ws,"Second",(tiku_rect_t){10,10,200,150},paint,&red,NULL));
    tiku_workspace_deinit(ws);CHECK(captured==1&&released==1);
    /* A callback that resets its own clip still draws only in its content,
     * so a frame that reuses the window's cache matches a full frame. */
    ops=(tiku_workspace_surfaces_t){.create=client_cache,.release=release};
    ws=tiku_workspace_init(storage.bytes,sizeof storage.bytes,&s,&ops);
    a=tiku_workspace_open(ws,"Overhang",(tiku_rect_t){40,40,200,120},overhang,&red,NULL);
    tiku_rect_t content=tiku_window_content(a),below={content.x,content.y+content.h-4,content.w,12};
    tiku_workspace_cache_mode(ws,TIKU_CACHE_REFRESH);tiku_workspace_draw(ws);
    CHECK(pixels[(content.y+content.h-1)*320+content.x+5]==0x20c040);
    CHECK(pixels[(content.y+content.h+2)*320+content.x+5]!=0x20c040);
    memcpy(reference,pixels,sizeof pixels);
    tiku_workspace_cache_mode(ws,TIKU_CACHE_REUSE);tiku_clip_damage(&s,&below);tiku_workspace_draw(ws);
    tiku_clip_damage(&s,NULL);CHECK(!memcmp(reference,pixels,sizeof pixels));
    tiku_workspace_begin_track(ws,a,TIKU_HIT_TAB,60,50);
    CHECK(tiku_workspace_track(ws,76,57));
    tiku_workspace_draw(ws);
    memcpy(reference,pixels,sizeof pixels);
    uint64_t hits;
    tiku_workspace_cache_stats(ws,NULL,&hits,NULL);
    CHECK(hits >= 2);
    tiku_workspace_cache_mode(ws,TIKU_CACHE_OFF);
    tiku_workspace_draw(ws);
    CHECK(!memcmp(reference,pixels,sizeof pixels));
    tiku_workspace_deinit(ws);
    ws=tiku_workspace_init(storage.bytes,sizeof storage.bytes,&s,NULL);
    for(int i=0;i<TIKU_WINDOW_MAX;++i)CHECK(tiku_workspace_open(ws,"Window",(tiku_rect_t){0,0,180,90},NULL,NULL,NULL));
    CHECK(!tiku_workspace_open(ws,"Full",(tiku_rect_t){0},NULL,NULL,NULL));
    tiku_workspace_deinit(ws);CHECK(!tiku_service_heap_calls);puts("PASS: borrowed window workspace");return 0;
}
