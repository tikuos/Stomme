/*
 * TikuOS SDK example.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * htmlview.c - a standalone offline HTML view using exported kit APIs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tiku_app.h"
#include "tiku_client.h"
#include "tiku_html_view.h"
#include "tiku_image.h"
#include "tiku_font.h"
#include "tiku_text_layout_paint.h"

static const char document[] =
    "<style>.card{padding:12px;background:#eef4fa;border:1px solid #345678;max-width:580px}"
    "td,th{padding:6px;border:1px solid #7799bb}figcaption{color:#345678}</style>"
    "<h1>Offline HTML view</h1><div class='card'><p>This reusable renderer displays local documents. "
    "Drag to select text. Use Command/Ctrl+C to copy, Tab and Enter to follow links, "
    "and the wheel or navigation keys to scroll.</p><p><a href='#details'>Jump to details</a> "
    "or <a href='local-topic.html'>send a local link to the host</a>.</p></div>"
    "<h2>Shared table layout</h2><table><caption>Document capabilities</caption>"
    "<tr><th>Area</th><th>Behavior</th></tr><tr><td rowspan='2'>Text</td>"
    "<td>Measured fonts, selection and find</td></tr><tr><td>Wrapping and bounded CSS boxes</td></tr></table>"
    "<figure><img width='160' src='data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAABQAAAAICAYAAAD5nd/tAAAAFklEQVR4nGM4wSXyn5qYYdTAUQNJxwCGyS8wuJRVjAAAAABJRU5ErkJggg=='>"
    "<figcaption>An embedded PNG supplied through a resource callback.</figcaption></figure>"
    "<h2 id='details'>Details</h2><p>The renderer does not fetch resources. "
    "The host supplies image pixels and handles links.</p><pre>HTML from memory\nA reusable view\nAn exported SDK</pre>";

typedef struct {
    const tiku_app_services_t *services;
    tiku_html_view_t view;
    tiku_surface_t *surface;
    tiku_image_t image;
    uint32_t window;
    char status[160];
} pane_t;

/** @brief Inspect the authored embedded PNG without reading a file. */
static int image_size(void *ctx, const char *src, int *w, int *h)
{
    (void)ctx;return tiku_image_data_png_size(src,strlen(src),w,h);
}

/** @brief Decode and scale the example's opaque PNG into the current surface. */
static void image_draw(void *ctx, const char *src, tiku_rect_t where)
{
    pane_t *p = ctx;
    if (!p->image.px && tiku_image_data_png(src,strlen(src),&p->image)) return;
    tiku_surface_t image = {.px=p->image.px,.w=p->image.w,.h=p->image.h,.scale=1};
    tiku_blend_scaled(p->surface,where,&image,1);
}

/** @brief Display a host-handled link without opening another resource. */
static void link_open(void *ctx, const char *href)
{
    pane_t *p = ctx;snprintf(p->status,sizeof p->status,"Host received: %s",href);
}

/** @brief Paint the view and its host status, then publish the completed frame. */
static void paint(pane_t *p)
{
    tiku_surface_t *s = p->surface;
    tiku_fill(s,(tiku_rect_t){0,0,s->w,s->h},TIKU_C_DOC);
    tiku_html_view_draw(&p->view,s,(tiku_rect_t){8,8,s->w-16,s->h-36});
    tiku_text_line_draw(s,(tiku_rect_t){8,s->h-24,s->w-16,20},TIKU_MEASURE_PLAIN,
                         p->status,TIKU_C_TEXT);
    if (p->services) p->services->frame(p->services->ctx,p->window,s->px,s->w,s->h);
}

/** @brief Allocate one pane and load its memory-backed document. */
static pane_t *pane_new(void)
{
    pane_t *p = calloc(1,sizeof *p);if (!p) return NULL;
    tiku_html_view_init(&p->view,NULL);
    p->surface = tiku_surface_new(700,520,TIKU_C_DOC);
    if (!p->surface || !tiku_html_view_load(&p->view,document,NULL)) {
        tiku_html_view_free(&p->view);tiku_surface_free(p->surface);free(p);return NULL;
    }
    tiku_html_view_resources(&p->view,image_size,image_draw,p);
    p->view.link = link_open;p->view.link_ctx = p;
    snprintf(p->status,sizeof p->status,"F1 finds renderer; Escape closes the window.");return p;
}

/** @brief Create the application's single window. */
static int start(void **state, const tiku_app_services_t *services)
{
    pane_t *p = pane_new();if (!p) return -1;
    p->services = services;p->window = services->open(services->ctx,"HTML view",700,520);
    *state = p;paint(p);return 0;
}

/** @brief Release the view, decoded resource and surface. */
static void stop(void *state)
{
    pane_t *p = state;if (!p) return;
    tiku_html_view_free(&p->view);tiku_image_free(&p->image);
    tiku_surface_free(p->surface);free(p);
}

/** @brief Route input and resize events into the public view. */
static int event(void *state, const tiku_event_t *e)
{
    pane_t *p = state;
    if (e->type == TIKU_EVENT_CLOSE || (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_ESCAPE)) return 1;
    if (e->type == TIKU_EVENT_RESIZE && e->width >= 40 && e->height >= 60) {
        tiku_surface_t *surface = tiku_surface_new(e->width,e->height,TIKU_C_DOC);
        if (surface) { tiku_surface_free(p->surface);p->surface = surface; }
    }
    if (e->type == TIKU_EVENT_KEY_DOWN && e->key == TIKU_KEY_F1)
        tiku_html_view_find(&p->view,"renderer",0);
    else tiku_html_view_event(&p->view,e);
    paint(p);return 0;
}

static const tiku_app_descriptor_t descriptor = {
    .id="org.kintsugi.example.htmlview",.name="HTML view",.start=start,.stop=stop,.event=event
};

#ifdef TIKU_APP_SO
const tiku_app_export_t tiku_app_v1 = {TIKU_APP_ABI,sizeof(tiku_app_descriptor_t),&descriptor};
#else
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1],"--smoke")) {
        pane_t *p = pane_new();if (!p) return 1;
        paint(p);
        int ok = p->image.px && tiku_doc_check(p->view.doc) &&
            tiku_html_view_find(&p->view,"renderer",0) && p->view.matches == 2;
        if (argc > 2) ok &= !tiku_surface_png(p->surface,argv[2]);
        printf("HTML SDK smoke: %s\n",ok ? "passed" : "failed");stop(p);return !ok;
    }
    return tiku_client_run(&descriptor);
}
#endif
