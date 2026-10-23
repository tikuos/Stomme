/* tiku_circuit_preferences.c -- Save View Settings: a document's units,
 * panels and each layer's colour, opacity and visibility, kept in a file
 * named by a hash of its path and applied when it is opened again.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_atomic_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** @brief One layer's saved look.  Its root and path are matched against
 *         the open stack's layers and never opened. */
typedef struct {
    char root[CV_PATH_MAX], path[CV_PATH_MAX];
    uint32_t colour, opacity, visible;
} saved_layer_t;
/** @brief The settings file's record, in fixed-width fields: "TCVIEW1",
 *         the document's real path, the units, the panels and the
 *         layers. */
typedef struct {
    char magic[8], path[CV_PATH_MAX];
    uint32_t units, sheets, details, count;
    saved_layer_t layer[TIKU_CIRCUIT_LAYERS_MAX];
} saved_view_t;
/** @brief One save: the record and where it goes. */
typedef struct {
    tiku_circuitview_t *owner;
    saved_view_t data;
    char directory[CV_PATH_MAX], destination[CV_PATH_MAX];
    int result;
} save_job_t;

void tiku_circuitview_settings(tiku_circuitview_t *v, const char *directory)
{
    if(!v) return;
    v->settings_dir[0]=0;
    if(directory && directory[0]=='/' && strlen(directory)<sizeof v->settings_dir)
        snprintf(v->settings_dir,sizeof v->settings_dir,"%s",directory);
}

/** @brief The settings file for @p path, <directory>/<hash>.view, the hash
 *         of its real path, which goes into @p canonical for a reader to
 *         check.  @return 1, or 0 when there is none. */
static int filename(const char *directory, const char *path, char *out,
                    size_t cap, char *canonical)
{
    if(!directory[0]) return 0;
    char *resolved=realpath(path,NULL);
    if(!resolved) return 0;
    if(strlen(resolved)>=CV_PATH_MAX) { free(resolved); return 0; }
    strcpy(canonical,resolved); free(resolved);
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)canonical;*p;p++)
        hash=(hash^*p)*UINT64_C(1099511628211);
    int n=snprintf(out,cap,"%s/%016llx.view",directory,(unsigned long long)hash);
    return n>0 && (size_t)n<cap;
}

/** @brief Make each missing folder of @p path; a part that exists and is
 *         not a folder fails it.  @return 1 when all are folders. */
static int directories(char *path)
{
    for(char *p=path+1;;p++) if(*p=='/' || !*p) {
        char c=*p; *p=0;
        int result=mkdir(path,0700); struct stat st;
        int ok=(!result || errno==EEXIST) && !stat(path,&st) && S_ISDIR(st.st_mode);
        *p=c; if(!ok) return 0; if(!c) return 1;
    }
}
/** @brief Write the record to @p f for tiku_atomic_file(). */
static int produce(FILE *f,void *ctx)
{
    save_job_t *j=ctx;
    return fwrite(&j->data,1,sizeof j->data,f)==sizeof j->data ? 0 : -1;
}
/** @brief Prepare the configuration directory and replace the settings file. */
static void run(void *ctx)
{
    save_job_t *j=ctx;
    j->result=-1;
    if(filename(j->directory,j->data.path,j->destination,sizeof j->destination,j->data.path) &&
       directories(j->directory)) j->result=tiku_atomic_file(j->destination,produce,j);
}
/** @brief On the loop: say in the status line whether the settings were
 *         saved. */
static void done(void *ctx)
{
    save_job_t *j=ctx;
    if(j->owner->window) tiku_circuitview_note(j->owner,j->result ?
        "View settings could not be saved. Source files are unchanged." :
        "View settings saved for this document. They apply when you reopen it.");
}
/** @brief Release the settings snapshot and worker's viewer reference. */
static void dispose(void *ctx)
{
    save_job_t *j=ctx; tiku_circuitview_t *v=j->owner;
    v->saving_view=0;
    if(v->window) tiku_circuitview_menus(v);
    tiku_circuitview_unref(v); free(j);
}
void cv_save_view(tiku_circuitview_t *v)
{
    if(v->saving_view || !v->path[0] || !v->settings_dir[0]) return;
    save_job_t *j=calloc(1,sizeof *j);
    if(!j) { tiku_circuitview_note(v,"Not enough memory to save view settings."); return; }
    j->owner=v; memcpy(j->data.magic,"TCVIEW1",8);
    snprintf(j->directory,sizeof j->directory,"%s",v->settings_dir);
    snprintf(j->data.path,sizeof j->data.path,"%s",v->path);
    j->data.units=(uint32_t)v->units; j->data.sheets=!!v->show_sheets;
    j->data.details=!!v->show_details;
    j->data.count=(uint32_t)tiku_circuit_layer_count(v->document);
    for(size_t i=0;i<j->data.count;i++) {
        const tiku_circuit_layer_t *l=tiku_circuit_layer(v->document,i);
        saved_layer_t *d=&j->data.layer[i];
        snprintf(d->root,sizeof d->root,"%s",l->root);
        snprintf(d->path,sizeof d->path,"%s",l->path);
        d->colour=l->colour; d->opacity=l->opacity; d->visible=!!l->visible;
    }
    tiku_work_job_t work={.run=run,.done=done,.free=dispose,.ctx=j};
    v->refs++; v->saving_view=1;
    if(tiku_work_post(&work)<0) { dispose(j); tiku_circuitview_note(v,"Worker queue is full; settings were not saved."); }
    else tiku_circuitview_note(v,"Saving view settings...");
    tiku_circuitview_menus(v);
}

tiku_circuit_err_t cv_restore_view(const char *directory,const char *path,
    tiku_circuit_t **document,int *units,int *sheets,int *details)
{
    char file[CV_PATH_MAX], canonical[CV_PATH_MAX];
    if(!filename(directory,path,file,sizeof file,canonical)) return TIKU_CIRCUIT_OK;
    int fd=open(file,O_RDONLY|O_NONBLOCK|O_NOFOLLOW);
    if(fd<0) return TIKU_CIRCUIT_OK;
    struct stat st;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size!=(off_t)sizeof(saved_view_t)) {
        close(fd); return TIKU_CIRCUIT_OK;
    }
    FILE *f=fdopen(fd,"rb"); if(!f) { close(fd); return TIKU_CIRCUIT_OK; }
    saved_view_t *data=calloc(1,sizeof *data);
    if(!data) { fclose(f); return TIKU_CIRCUIT_NOMEM; }
    int valid=fread(data,1,sizeof *data,f)==sizeof *data && fgetc(f)==EOF && !ferror(f);
    fclose(f);
    valid=valid && !memcmp(data->magic,"TCVIEW1",8) &&
        memchr(data->path,0,sizeof data->path) && !strcmp(data->path,canonical) &&
        data->units<=2 && data->sheets<=1 && data->details<=1 && data->count<=TIKU_CIRCUIT_LAYERS_MAX;
    for(size_t i=0;valid && i<data->count;i++) valid=
        memchr(data->layer[i].root,0,sizeof data->layer[i].root) &&
        memchr(data->layer[i].path,0,sizeof data->layer[i].path) &&
        data->layer[i].opacity<=255 && data->layer[i].visible<=1;
    tiku_circuit_err_t result=TIKU_CIRCUIT_OK;
    if(valid) {
        size_t count=tiku_circuit_layer_count(*document), used=0;
        tiku_circuit_layer_t layers[TIKU_CIRCUIT_LAYERS_MAX];
        unsigned char matched[TIKU_CIRCUIT_LAYERS_MAX]={0};
        for(size_t i=0;i<data->count;i++) for(size_t k=0;k<count;k++) {
            const tiku_circuit_layer_t *l=tiku_circuit_layer(*document,k);
            saved_layer_t *d=&data->layer[i];
            if(!matched[k] && !strcmp(l->path,d->path) && !strcmp(l->root,d->root)) {
                layers[used]=*l; layers[used].colour=d->colour;
                layers[used].opacity=d->opacity; layers[used++].visible=(int)d->visible;
                matched[k]=1; break;
            }
        }
        for(size_t k=0;k<count;k++) if(!matched[k]) layers[used++]=*tiku_circuit_layer(*document,k);
        if(count) {
            tiku_circuit_t *next=NULL; result=tiku_circuit_stack(layers,count,&next);
            if(!result) { tiku_circuit_release(*document); *document=next; }
        }
        if(!result) { *units=(int)data->units; *sheets=(int)data->sheets; *details=(int)data->details; }
    }
    free(data); return result;
}
