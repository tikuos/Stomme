/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_rename.c - arming an in-place rename on a slow second
 * click on a selected pose's name, and deciding when it falls due.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

void
tiku_trk_poseview_allow_editing(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL) {
        v->allow_editing = on ? 1 : 0;
        if (!on) {
            v->edit_pose = -1;
            v->edit_clicked_at = 0;
        }
    }
}

int
tiku_trk_poseview_name_clicked(tiku_trk_poseview_t *v, int index, int64_t now)
{
    tiku_trk_pose_t *p;

    if (v == NULL || !v->allow_editing) {
        return 0;
    }
    p = tiku_trk_poseview_at(v, index);
    if (p == NULL || !p->selected) {
        /* Clicking a name that was not already selected only selects it. */
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
        return 0;
    }
    if (v->edit_clicked_at != 0 && v->edit_pose == p->index) {
        /* The second click of a pair is a double-click, which opens, so
         * it disarms. */
        v->edit_clicked_at = 0;
        v->edit_pose = -1;
        return 0;
    }
    /* The click that selected the pose does not arm a rename. */
    if (now - p->sel_time < TIKU_TRK_CLICK_SPEED_US) {
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
        return 0;
    }
    v->edit_pose = p->index;
    v->edit_clicked_at = now;
    return 1;
}

void
tiku_trk_poseview_cancel_edit_wait(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
    }
}

int
tiku_trk_poseview_edit_armed(const tiku_trk_poseview_t *v)
{
    return (v != NULL && v->edit_clicked_at != 0) ? 1 : 0;
}

int
tiku_trk_poseview_edit_due(tiku_trk_poseview_t *v, int64_t now)
{
    int idx;

    if (v == NULL || v->edit_clicked_at == 0 || v->edit_pose < 0) {
        return -1;
    }
    /* Disarm when the conditions that armed it stopped holding: a pose that
     * has since been deselected, or has gone, must not start an editor. */
    if (v->edit_pose >= v->count || !v->poses[v->edit_pose].selected ||
        !v->allow_editing) {
        v->edit_pose = -1;
        v->edit_clicked_at = 0;
        return -1;
    }
    if (now - v->edit_clicked_at <= TIKU_TRK_CLICK_SPEED_US) {
        return -1;                  /* still inside the click window       */
    }
    /* Handed back as a row of the list being shown; it is held as a pose
     * index, which survives a filter change while the timer runs. */
    idx = pv_display_of(v, v->edit_pose);
    v->edit_pose = -1;
    v->edit_clicked_at = 0;
    return idx;
}
