/* tiku_circuit_export.c -- Export View as PNG...: the rendered view, its
 * measurement and a caption marking it an inspection preview, written on a
 * worker; an existing file is replaced only when it is a PNG.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_app_product.h"
#include "tiku_circuit_window_private.h"
#include "tiku_atomic_file.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/** @brief One export: the picture to write and where. */
typedef struct {
    tiku_circuitview_t *owner;
    tiku_surface_t *surface;
    char path[CV_PATH_MAX];
    int result;
} export_job_t;

int tiku_circuitview_picking_export(const tiku_circuitview_t *v)
{ return v && v->picker_action==CIRCUIT_EXPORT; }

/** @brief Write the picture to @p f as PNG for tiku_atomic_file(); a
 *         cancel returns -1, which leaves the destination as it was. */
static int produce(FILE *f,void *ctx)
{
    export_job_t *j=ctx; tiku_surface_t *s=j->surface;
    if(tiku_work_cancelled(j)) return -1;
    int result=tiku_png_write_stream(f,s->px,s->w,s->h,0);
    return tiku_work_cancelled(j) ? -1 : result;
}
/** @brief On the worker: check the destination, then write the picture
 *         through tiku_atomic_file(). */
static void run(void *ctx)
{
    export_job_t *j=ctx; j->result=-1;
    /* An existing file is replaced only when it starts with the PNG
     * signature, whatever its name or the path or link that reaches it. */
    int fd=open(j->path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);
    if(fd>=0) {
        struct stat st;
        unsigned char sig[8];
        int png = !fstat(fd,&st) && S_ISREG(st.st_mode) &&
            read(fd,sig,8)==8 && !memcmp(sig,"\211PNG\r\n\032\n",8);
        close(fd); if(!png) { j->result=-2; return; }
    } else if(errno!=ENOENT) return;
    if(!tiku_work_cancelled(j)) j->result=tiku_atomic_file(j->path,produce,j);
}
/** @brief Report the result on the UI thread, if the window still exists. */
static void done(void *ctx)
{
    export_job_t *j=ctx;
    if(j->owner->window) tiku_circuitview_note(j->owner,j->result==-2 ?
        "Export refused: choose a new .png file or an existing PNG; "
        "other files are protected." :
        j->result ? "PNG export failed. The previous destination was kept." :
        "PNG exported with the current view, measurement and preview warnings.");
}
/** @brief Release the snapshot and worker's viewer reference. */
static void dispose(void *ctx)
{
    export_job_t *j=ctx; tiku_circuitview_t *v=j->owner;
    v->exporting=0; tiku_surface_free(j->surface);
    if(v->window) tiku_circuitview_menus(v);
    tiku_circuitview_unref(v); free(j);
}
void cv_export(tiku_circuitview_t *v,const char *path)
{
    if(!tiku_circuitview_enabled(v,CIRCUIT_EXPORT) ||
       v->generation!=v->picker_generation) {
        tiku_circuitview_note(v,"The view changed. Wait for rendering, "
                                "then choose Export again.");
        return;
    }
    const char *ext=strrchr(path,'.');
    if(!ext || strcasecmp(ext,".png") || strlen(path)>=CV_PATH_MAX) {
        tiku_circuitview_note(v,"Choose a filename ending in .png."); return;
    }
    char measure[512]="",caption[2048];
    if(v->measure && v->measure_points==2)
        cv_measure_text(v,measure,sizeof measure);
    snprintf(caption,sizeof caption,
        TIKU_PRODUCT_CIRCUIT_TIKUCIRCUIT_INSPECTION_PREVIEW_NOT_FABRICATION_OUTPUT_N_S_S_S_S,
        tiku_circuit_project_incomplete(v->project) ?
            "Incomplete project: some sheets are unavailable.\n" : "",
        tiku_circuit_warning(v->document),*measure ? "\n" : "",measure);
    int width=v->image.w, height=v->image.h;
    int line_height=tiku_font_plain()->height+2;
    if(width<80) {
        tiku_circuitview_note(v,"Enlarge the window before exporting.");
        return;
    }
    const char *remaining=caption; char line[TIKU_TEXT_LINE_MAX]; int lines=0;
    while(tiku_text_wrap_next(TIKU_MEASURE_PLAIN,&remaining,width-16,
                              line,sizeof line,0)) lines++;
    int footer=lines*line_height+16;
    if(footer<16 || footer>4096 ||
       (size_t)width*(size_t)(height+footer)>TIKU_IMAGE_PIXELS_MAX) {
        tiku_circuitview_note(v,
            "The exported preview would exceed the image size limit.");
        return;
    }
    export_job_t *j=calloc(1,sizeof *j);
    if(j) j->surface=tiku_surface_new(width,height+footer,CV_PAPER);
    if(!j || !j->surface) {
        free(j);
        tiku_circuitview_note(v,"Not enough memory to export this view.");
        return;
    }
    j->owner=v; snprintf(j->path,sizeof j->path,"%s",path);
    memcpy(j->surface->px,v->image.px,
           (size_t)width*(size_t)height*sizeof(uint32_t));
    if(v->measure_points==2)
        cv_measure_draw(v,j->surface,(tiku_rect_t){0,0,width,height},
                        &v->cached_view);
    tiku_hline(j->surface,0,height,width,0xffa09080);
    tiku_text_block_draw(j->surface,
        (tiku_rect_t){8,height+8,width-16,footer-16},
        TIKU_MEASURE_PLAIN,caption,0xff684820,2);
    tiku_work_job_t work={.run=run,.done=done,.free=dispose,.ctx=j};
    memcpy(work.key,v->export_key,sizeof work.key);
    v->refs++; v->exporting=1;
    if(tiku_work_post(&work)<0) {
        dispose(j);
        tiku_circuitview_note(v,"Worker queue is full; PNG was not exported.");
    }
    else tiku_circuitview_note(v,"Exporting PNG...");
    tiku_circuitview_menus(v);
}
