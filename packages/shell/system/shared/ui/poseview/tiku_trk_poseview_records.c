/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_records.c - apply copied saved records to a loaded view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_trk_poseview_private.h"
#include "tiku_trk_native_record.h"
#include "tiku_trk_resource_values.h"

/** @brief Read an optional validated native integer field. */
static int record_integer(const tiku_msg_t *value, const char *key, int *out)
{
    tiku_msg_t *member = trk_value_find(value,key); int64_t number;
    int found = member && tiku_msg_find_int64(member,"value",0,&number);
    if (found) *out = (int)number;
    tiku_msg_free(member); return found;
}

int
tiku_trk_poseview_apply_record(tiku_trk_poseview_t *view, const char *attribute,
    const char *node, const void *bytes, size_t length)
{
    tiku_trk_native_record_kind_t kind;
    tiku_msg_t *value; char state_node[TIKU_PATH_MAX]; const char *own; int i;
    if (!view || !attribute || !node || (length && !bytes)) return 0;
    own = pv_state_node(view,state_node,sizeof state_node);
    if (!strcmp(attribute,TIKU_ATTR_POSE_INFO)) {
        for (i = 0; i < view->count; i++) if (!strcmp(view->poses[i].model.path,node)) {
            tiku_trk_pose_t *pose = &view->poses[i]; int flags = 0;
            value = tiku_trk_native_record_decode(TIKU_TRK_RECORD_POSE,bytes,length); if (!value) return 0;
            if (length) {
                record_integer(value,"x",&pose->loc_x); record_integer(value,"y",&pose->loc_y);
                record_integer(value,"flags",&flags); pose->invisible = (flags & POSE_INFO_INVISIBLE) != 0;
                pose->placed = 1; pose->auto_placed = 0;
            } else { pose->placed = 0; pose->invisible = 0; }
            tiku_msg_free(value); view->state_dirty = 1; pv_extent_clear(view); pv_apply_visibility(view); return 1;
        }
        return 0;
    }
    if (strcmp(node,own)) return 0;
    if (!strcmp(attribute,view->state_key[0] ? TIKU_ATTR_DESK_COLUMNS : !strcmp(view->path,"/") ? TIKU_ATTR_DISKS_COLUMNS : TIKU_ATTR_COLUMNS)) kind = TIKU_TRK_RECORD_COLUMNS;
    else if (!strcmp(attribute,view->state_key[0] ? TIKU_ATTR_DESK_VIEW_STATE : !strcmp(view->path,"/") ? TIKU_ATTR_DISKS_VIEW_STATE : TIKU_ATTR_VIEW_STATE)) kind = TIKU_TRK_RECORD_VIEW;
    else return 0;
    value = tiku_trk_native_record_decode(kind,bytes,length); if (!value) return 0;
    if (kind == TIKU_TRK_RECORD_COLUMNS) {
        if (!length) { view->columns_dirty = 0; tiku_msg_free(value); return 1; }
        i = length ? tiku_trk_poseview_apply_columns(view,bytes,length) : 0;
        tiku_msg_free(value); return i > 0;
    }
    if (!length) {
        view->view_cleared = pv_view_record(view,view->cleared_view,sizeof view->cleared_view) >= 0;
        tiku_msg_free(value); return view->view_cleared;
    }
    if (length) {
        view->view_cleared = 0;
        tiku_msg_t *mode = trk_value_find(value,"mode"); const char *word = tiku_msg_find_string(mode,"value",0); int last;
        view->mode = !strcmp(word,"list") ? TIKU_TRK_MODE_LIST : !strcmp(word,"mini") ? TIKU_TRK_MODE_MINI : TIKU_TRK_MODE_ICON;
        tiku_msg_free(mode);
        record_integer(value,"icon_size",&view->icon_size); record_integer(value,"scroll",&view->scroll);
        record_integer(value,"icon_x",&view->icon_x); record_integer(value,"icon_y",&view->icon_y);
        record_integer(value,"mini_x",&view->mini_x); record_integer(value,"mini_y",&view->mini_y);
        record_integer(value,"sort_field",&view->sort_field); record_integer(value,"sort2_field",&view->sort2_field);
        record_integer(value,"sort_reverse",&view->sort_reverse);
        if (record_integer(value,"last_icon",&last)) view->last_icon = last ? TIKU_TRK_MODE_MINI : TIKU_TRK_MODE_ICON;
        pv_resort(view); pv_extent_clear(view);
    }
    tiku_msg_free(value); return 1;
}
