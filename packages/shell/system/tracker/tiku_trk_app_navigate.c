/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_navigate.c - navigation: opening, Back, Forward, Up and the
 * location field, and the outline an opened pose grows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/**
 * @brief Start the outline over a pose being opened (PVS-013, PVL-073).
 *
 * Only a mouse open has a clicked rectangle to grow out of.  The open
 * goes ahead at once; the loop's frames draw the outline, through
 * browser_draw_activation().  Without a window, or with Reduce motion on,
 * none starts.
 */
void
browser_open_animation(app_t *a, tiku_rect_t from)
{
    int64_t now = (a->host != NULL && !a->reduce_motion) ? tiku_trk_now()
                                                         : 0;

    a->activation_rect = from;
    tiku_motion_start(&a->activation_motion, now, TIKU_MOTION_ICON_US);
    a->dirty = 1;
}

/**
 * @brief Draw the opened pose's outline over a finished frame, clipped
 *        to the list, and owe the next frame while it runs.
 */
void
browser_draw_activation(app_t *a)
{
    float progress;

    if (!a->activation_motion.active) {
        return;
    }
    /* Turning Reduce motion on while it runs passes a zero time, which
     * retires the outline on this frame. */
    progress = tiku_motion_progress(&a->activation_motion,
                                    (a->host != NULL && !a->reduce_motion)
                                        ? tiku_trk_now() : 0);
    tiku_clip_set(a->surf, browser_list_rect(a));
    tiku_motion_outline(a->surf, a->activation_rect, progress);
    tiku_clip_reset(a->surf);
    if (a->activation_motion.active) {
        a->dirty = 1;
    }
}

/**
 * @brief Show folder @p path in the window: the view's state and columns
 *        are saved, the directory switched and loaded, the layout and
 *        writability settled, and the destination recorded in the
 *        navigator when @p record is set.  @return 1 when the window now
 *        shows @p path (already, or after the switch), 0 when @p path is
 *        empty, not a container, or refused by the store.
 */
int
browser_navigate_to(app_t *a, const char *path, int record)
{
    char dest[TIKU_PATH_MAX];
    tiku_model_t m;

    if (path == NULL || path[0] == '\0' ||
        (a->backend->ops->stat != NULL &&
         (a->backend->ops->stat(a->backend, path, &m) != 0 ||
          !tiku_model_is_container(&m)))) {
        return 0;
    }
    /* @p path may point into the view's poses, which the switch replaces,
     * so it is copied first. */
    snprintf(dest, sizeof dest, "%s", path);
    if (strcmp(dest, tiku_trk_poseview_path(a->view)) == 0) {
        return 1;
    }
    tiku_scroll_detach(&a->sb);
    (void)tiku_trk_poseview_save_state(a->view);
    (void)tiku_trk_poseview_save_columns(a->view);
    /* tiku_trk_poseview_switch_dir() returns the new row count; only a
     * negative value is an error. */
    if (tiku_trk_poseview_switch_dir(a->view, dest) < 0) {
        tiku_scroll_attach(&a->sb);
        return 0;
    }
    (void)tiku_trk_poseview_load_begin(a->view, tiku_trk_now());
    tiku_scroll_attach(&a->sb);
    (void)tiku_trk_poseview_load_step(a->view, tiku_trk_now());
    (void)tiku_trk_poseview_restore_state(a->view);
    (void)tiku_trk_poseview_restore_columns(a->view);
    browser_settle_layout(a);
    browser_settle_writability(a);
    if (record) {
        tiku_trk_navigator_set(&a->nav, dest);
    }
    browser_retitle(a);
    a->dirty = 1;
    return 1;
}

/** @brief Step to the preceding history entry; the history steps forward
 *         again when that folder cannot be shown. */
void
browser_navigate_back(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_back(&a->nav, path, sizeof path) &&
        !browser_navigate_to(a, path, 0)) {
        (void)tiku_trk_navigator_forward(&a->nav, path, sizeof path);
    }
}

/** @brief Step to the history entry most recently left by Back; the
 *         history steps back again when that folder cannot be shown. */
void
browser_navigate_forward(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_forward(&a->nav, path, sizeof path) &&
        !browser_navigate_to(a, path, 0)) {
        (void)tiku_trk_navigator_back(&a->nav, path, sizeof path);
    }
}

/** @brief Navigate to the enclosing folder and record that new branch. */
void
browser_navigate_up(app_t *a)
{
    char path[TIKU_PATH_MAX];

    if (tiku_trk_navigator_parent(tiku_trk_poseview_path(a->view), path,
                                  sizeof path)) {
        (void)browser_navigate_to(a, path, 1);
    }
}

/** @brief Give the location field the keyboard with its path selected. */
void
browser_begin_location(app_t *a)
{
    browser_end_rename(a, 1);
    a->nav_editing = 1;
    tiku_text_input_reset(&a->commit);
    tiku_host_cancel_composition(a->host);
    tiku_field_start(&a->nav_edit,
                        tiku_trk_navigator_current(&a->nav));
    a->dirty = 1;
}

/** @brief Close the location field and, with @p apply, go to the path in
 *         it (an empty field means /); a path that is not a folder is
 *         ignored without a message. */
void
browser_end_location(app_t *a, int apply)
{
    char path[TIKU_PATH_MAX];

    if (!a->nav_editing) {
        return;
    }
    snprintf(path, sizeof path, "%s",
             (a->nav_edit.text[0] != '\0') ? a->nav_edit.text : "/");
    tiku_field_stop(&a->nav_edit);
    a->nav_editing = 0;
    if (apply) {
        (void)browser_navigate_to(a, path, 1);
    }
    a->dirty = 1;
}

/** @brief Open the active pose: a container is shown in the window; any
 *         other file goes to browser_open_image(). */
void
browser_open_selection(app_t *a)
{
    tiku_trk_pose_t *p = tiku_trk_poseview_active(a->view);

    if (p == NULL) {
        return;
    }
    if (tiku_model_is_container(&p->model)) {
        (void)browser_navigate_to(a, p->model.path, 1);
    } else (void)browser_open_image(a, &p->model);
    a->dirty = 1;
}
