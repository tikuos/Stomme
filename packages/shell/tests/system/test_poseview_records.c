/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_poseview_records.c - explicit record clearing and subsequent native saves.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_poseview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#test); exit(1); } } while (0)

/** @brief Retain a copied-list request until the test delivers its reply. */
static int hold_listing(void *context, tiku_trk_poseview_t *view, int generation, int kind)
{
    (void)context; (void)view; (void)generation; (void)kind; return 1;
}

int main(void)
{
    char directory[]="/tmp/stomme-view-records-XXXXXX",node[512],bytes[512]; FILE *file;
    tiku_backend_t *backend; tiku_store_t *store; tiku_trk_poseview_t *view; tiku_trk_pose_t *pose;
    CHECK(mkdtemp(directory)); snprintf(node,sizeof node,"%s/item",directory);
    file=fopen(node,"w"); CHECK(file && !fclose(file)); backend=tiku_backend_posix_open(); CHECK(backend);
    store=backend->ops->state_store(backend); view=tiku_trk_poseview_new(backend,directory); CHECK(view && tiku_trk_poseview_count(view)==1);
    pose=tiku_trk_poseview_at(view,0); tiku_trk_poseview_set_mode(view,TIKU_TRK_MODE_ICON); tiku_trk_poseview_set_icon_size(view,64);
    tiku_trk_poseview_move_pose(view,pose,100,120,0);
    CHECK(!tiku_state_write(store,directory,TIKU_ATTR_VIEW_STATE,"",0));
    CHECK(tiku_trk_poseview_apply_record(view,TIKU_ATTR_VIEW_STATE,directory,NULL,0));
    CHECK(tiku_trk_poseview_save_state(view)==1);
    CHECK(tiku_state_read(store,directory,TIKU_ATTR_VIEW_STATE,bytes,sizeof bytes)==0);
    CHECK(tiku_state_read(store,node,TIKU_ATTR_POSE_INFO,bytes,sizeof bytes)>0);
    tiku_trk_poseview_set_icon_size(view,96); CHECK(tiku_trk_poseview_save_state(view)==1);
    CHECK(tiku_state_read(store,directory,TIKU_ATTR_VIEW_STATE,bytes,sizeof bytes)>0);
    CHECK(tiku_trk_poseview_resize_column(view,TIKU_TRK_COL_NAME,260));
    CHECK(!tiku_state_write(store,directory,TIKU_ATTR_COLUMNS,"",0));
    CHECK(tiku_trk_poseview_apply_record(view,TIKU_ATTR_COLUMNS,directory,NULL,0));
    CHECK(tiku_trk_poseview_save_columns(view)==0);
    CHECK(tiku_state_read(store,directory,TIKU_ATTR_COLUMNS,bytes,sizeof bytes)==0);
    {
        tiku_model_t model = pose->model; int generation;
        tiku_trk_poseview_set_lister(hold_listing,NULL);
        CHECK(tiku_trk_poseview_load_begin(view,0)==0);
        generation=tiku_trk_poseview_generation(view); CHECK(tiku_trk_poseview_loading(view));
        CHECK(tiku_trk_poseview_set_models(view,&model,1)==1);
        CHECK(!tiku_trk_poseview_loading(view));
        CHECK(tiku_trk_poseview_load_landed(view,generation,&model,1)<0);
        tiku_trk_poseview_set_lister(NULL,NULL);
    }
    tiku_trk_poseview_free(view); tiku_backend_close(backend); CHECK(!unlink(node) && !rmdir(directory));
    puts("PASS view records: explicit clears survive unrelated saves and subsequent view changes persist"); return 0;
}
