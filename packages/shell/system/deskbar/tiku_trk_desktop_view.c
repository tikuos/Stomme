/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_view.c - the Desktop pose-view controller: the poses
 * on the screen's backdrop, their sources, and their placement around the
 * Deskbar.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_state.h"
#include "tiku_trk_deskicons.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_volume.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief The workspace backdrop: the Desktop folder's ground and its icons. */
static void
draw(tiku_surface_t *surface, void *context)
{
    tiku_trk_desktop_view_t *controller = context;

    if (controller != NULL && controller->folder.view != NULL) {
        tiku_rect_t content =
            tiku_trk_folder_window_content(&controller->folder);

        controller->folder.bg_drawn =
            tiku_trk_folder_window_draw_background(&controller->folder,
                                                   surface, content);
        tiku_trk_folder_window_draw_icons(&controller->folder, surface,
                                          content);
    }
}

tiku_trk_folder_window_t *
tiku_trk_desktop_view_folder(tiku_trk_desktop_view_t *controller)
{
    return controller != NULL ? &controller->folder : NULL;
}

int
tiku_trk_desktop_view_shows_disks(
    const tiku_trk_desktop_view_t *controller)
{
    return controller != NULL ? controller->disks_shown : 0;
}

/** @brief Project the current Deskbar frame onto a resized screen. */
static tiku_rect_t
deskbar_frame_at(const tiku_trk_desktop_view_t *controller,
                 int screen_w, int screen_h)
{
    tiku_rect_t frame;
    tiku_trk_db_dock_t dock;

    if (controller == NULL || controller->deskbar == NULL) {
        return (tiku_rect_t){ 0, 0, 0, 0 };
    }
    frame = tiku_trk_deskbar_frame(controller->deskbar);
    dock = tiku_trk_deskbar_dock(controller->deskbar);
    if (tiku_trk_deskbar_vertical(dock)) {
        frame.x = (dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                   dock == TIKU_TRK_DB_LEFT_MINI)
                      ? 0 : screen_w - frame.w;
        frame.y = 0;
    } else {
        frame.x = 0;
        frame.w = screen_w;
        frame.y = (dock == TIKU_TRK_DB_TOP) ? 0 : screen_h - frame.h;
    }
    return frame;
}

/** @brief Usable Desktop area at an arbitrary screen size. */
static tiku_trk_rect_t
desktop_area_at(const tiku_trk_desktop_view_t *controller,
                int screen_w, int screen_h)
{
    tiku_rect_t frame;
    tiku_trk_rect_t area;
    tiku_trk_db_dock_t dock;

    if (controller == NULL || controller->deskbar == NULL ||
        screen_w <= 0 || screen_h <= 0) {
        return (tiku_trk_rect_t){ 0, 0, 0, 0 };
    }
    frame = deskbar_frame_at(controller, screen_w, screen_h);
    area = (tiku_trk_rect_t){ 0, controller->top_inset, screen_w,
                              screen_h - controller->top_inset };
    if (frame.w <= 0 || frame.h <= 0) {
        return area;
    }
    dock = tiku_trk_deskbar_dock(controller->deskbar);
    if (tiku_trk_deskbar_vertical(dock)) {
        if (dock == TIKU_TRK_DB_RIGHT_EXPANDED ||
            dock == TIKU_TRK_DB_RIGHT_MINI) {
            area.w = frame.x - TIKU_TRK_DESK_BAR_MARGIN;
        } else {
            area.x = frame.x + frame.w + TIKU_TRK_DESK_BAR_MARGIN;
            area.w = screen_w - area.x;
        }
    } else if (dock == TIKU_TRK_DB_BOTTOM) {
        area.h = frame.y - TIKU_TRK_DESK_BAR_MARGIN;
    } else {
        area.y = frame.y + frame.h + TIKU_TRK_DESK_BAR_MARGIN;
        area.h = screen_h - area.y;
    }
    return area;
}

tiku_trk_rect_t
tiku_trk_desktop_view_area(const tiku_trk_desktop_view_t *controller)
{
    if (controller == NULL) {
        return (tiku_trk_rect_t){ 0, 0, 0, 0 };
    }
    return desktop_area_at(controller, controller->screen_w,
                           controller->screen_h);
}

/**
 * @brief Adopt @p path as a pose on the Desktop, at the slot a watched
 *        entry would take; a path the backend cannot stat is skipped.
 */
static void
add_pose(tiku_trk_desktop_view_t *controller, const char *path)
{
    tiku_model_t model;

    if (path == NULL || path[0] == '\0' ||
        controller->backend->ops->stat(controller->backend, path,
                                       &model) != 0) {
        return;
    }
    /* The pose is left unplaced, so the next placement pass puts it in
     * the next free slot, as it does a new Desktop entry. */
    (void)tiku_trk_poseview_adopt(controller->folder.view, &model);
}

int
tiku_trk_desktop_view_ensure_visible(tiku_trk_desktop_view_t *controller)
{
    tiku_trk_rect_t screen;
    tiku_trk_rect_t area;
    tiku_rect_t frame;
    tiku_trk_rect_t bar;
    int i, count, moved = 0;

    if (controller == NULL || controller->folder.view == NULL) {
        return 0;
    }
    /* The strip of the top inset is not part of the screen a pose may
     * use. */
    screen = (tiku_trk_rect_t){ 0, controller->top_inset,
                                controller->screen_w,
                                controller->screen_h - controller->top_inset };
    area = tiku_trk_desktop_view_area(controller);
    frame = deskbar_frame_at(controller, controller->screen_w,
                            controller->screen_h);
    bar = (tiku_trk_rect_t){ frame.x, frame.y, frame.w, frame.h };
    count = tiku_trk_poseview_count(controller->folder.view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(controller->folder.view,
                                                     i);
        tiku_trk_rect_t rect;
        int x, y, w, h;

        if (pose == NULL || !pose->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(controller->folder.view, pose,
                                    &x, &y, &w, &h);
        rect = (tiku_trk_rect_t){ x, y, w, h };
        if (tiku_trk_deskicons_valid(rect, screen, bar, NULL, 0)) {
            continue;
        }
        if (tiku_trk_deskicons_pull_back(&rect, area)) {
            int location_x, location_y;

            /* A pose location is the art anchor, while its rectangle also
             * contains the label; preserve that offset when pulling it in. */
            tiku_trk_poseview_pose_location(controller->folder.view, pose,
                                             &location_x, &location_y);
            tiku_trk_poseview_move_pose(controller->folder.view, pose,
                location_x + rect.x - x, location_y + rect.y - y, 0);
            moved++;
        }
    }
    return moved;
}

/**
 * @brief Move every placed pose from its place in @p old_area to the same
 *        relative place in the current area, a taken place giving way to
 *        the nearest free grid slot, then bring any off the screen back.
 * @return How many poses moved.
 */
static int
reflow_poses(tiku_trk_desktop_view_t *controller, tiku_trk_rect_t old_area)
{
    tiku_trk_rect_t *occupied;
    tiku_trk_rect_t new_area = tiku_trk_desktop_view_area(controller);
    int grid_x, grid_y, off_x, off_y;
    int i, count, occupied_count = 0, moved = 0;

    count = tiku_trk_poseview_count(controller->folder.view);
    occupied = calloc((size_t)count, sizeof *occupied);
    tiku_trk_poseview_layout_info(controller->folder.view,
                                  &grid_x, &grid_y, &off_x, &off_y);
    (void)off_x;
    (void)off_y;
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(controller->folder.view,
                                                     i);
        tiku_trk_rect_t before, after;
        tiku_trk_rect_t anchor, anchor_area;
        int location_x, location_y;
        int slot_w, slot_h;
        int was_auto;

        if (pose == NULL || !pose->placed) {
            continue;
        }
        tiku_trk_poseview_pose_rect(controller->folder.view, pose,
            &before.x, &before.y, &before.w, &before.h);
        tiku_trk_poseview_pose_location(controller->folder.view, pose,
                                        &location_x, &location_y);
        after = before;
        tiku_trk_deskicons_reflow(&after, old_area, new_area);
        slot_w = grid_x - 2 * TIKU_TRK_DESK_OTHER_MARGIN;
        slot_h = grid_y - 2 * TIKU_TRK_DESK_OTHER_MARGIN;
        if (slot_w < 1) { slot_w = 1; }
        if (slot_h < 1) { slot_h = 1; }
        anchor = (tiku_trk_rect_t){
            location_x + after.x - before.x,
            location_y + after.y - before.y, slot_w, slot_h
        };
        anchor_area = (tiku_trk_rect_t){
            new_area.x + location_x - before.x,
            new_area.y + location_y - before.y,
            new_area.w - before.w + slot_w,
            new_area.h - before.h + slot_h
        };
        if (occupied != NULL &&
            !tiku_trk_deskicons_valid(anchor, anchor_area,
                (tiku_trk_rect_t){ 0, 0, 0, 0 }, occupied,
                occupied_count)) {
            tiku_trk_rect_t wanted = anchor;

            if (tiku_trk_deskicons_find_free(&anchor, anchor_area,
                    occupied, occupied_count, grid_x, grid_y)) {
                after.x += anchor.x - wanted.x;
                after.y += anchor.y - wanted.y;
            }
        }
        if (occupied != NULL) {
            occupied[occupied_count++] = anchor;
        }
        if (after.x == before.x && after.y == before.y) {
            continue;
        }
        was_auto = pose->auto_placed;
        tiku_trk_poseview_move_pose(controller->folder.view, pose,
            location_x + after.x - before.x,
            location_y + after.y - before.y, 0);
        pose->auto_placed = was_auto;
        moved++;
    }
    free(occupied);
    moved += tiku_trk_desktop_view_ensure_visible(controller);
    return moved;
}

int
tiku_trk_desktop_view_resize(tiku_trk_desktop_view_t *controller,
                             int screen_w, int screen_h)
{
    tiku_trk_rect_t old_area;

    if (controller == NULL || controller->folder.view == NULL ||
        screen_w <= 0 || screen_h <= 0) {
        return 0;
    }
    if (screen_w == controller->screen_w &&
        screen_h == controller->screen_h) {
        return 0;
    }
    old_area = tiku_trk_desktop_view_area(controller);
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    tiku_trk_poseview_set_viewport(controller->folder.view,
                                   screen_w, screen_h);
    return reflow_poses(controller, old_area);
}

void
tiku_trk_desktop_view_rebuild(tiku_trk_desktop_view_t *controller)
{
    tiku_volumes_t volumes;
    tiku_model_t *models;
    char trash[TIKU_PATH_MAX];
    int count = 0, have_volumes;

    if (controller == NULL || controller->folder.view == NULL) {
        return;
    }
    models = calloc(TIKU_TRK_POSES_MAX, sizeof *models);
    if (models == NULL) {
        return;
    }
    have_volumes = tiku_volumes_scan(&volumes) >= 0;
    if (have_volumes) {
        tiku_trk_deskicons_sources(&volumes, controller->backend,
            controller->folder.path, &controller->sources);
    }
    count = tiku_vdir_list(&controller->sources, controller->backend,
        models, TIKU_TRK_POSES_MAX);
    if (count < 0) {
        free(models);
        return;
    }
    (void)tiku_trk_poseview_set_models(controller->folder.view, models,
                                       count);
    free(models);
    (void)tiku_trk_poseview_restore_state(controller->folder.view);
    tiku_trk_poseview_set_mode(controller->folder.view,
        tiku_trk_deskicons_mode(
            tiku_trk_poseview_mode(controller->folder.view)));
    (void)tiku_trk_poseview_place_unplaced(controller->folder.view,
        tiku_trk_desktop_view_area(controller).w);
    controller->disks_shown = tiku_trk_deskicons_show_disks(
        tiku_trk_prefs_get(controller->prefs, "ShowDisksIcon"),
        tiku_trk_prefs_get(controller->prefs, "MountVolumesOntoDesktop"));
    if (controller->disks_shown) {
        add_pose(controller, "/");
    }
    if (tiku_fs_trash_dir(controller->fs, controller->folder.path,
                              trash, sizeof trash) == 0) {
        add_pose(controller, trash);
    }
    if (have_volumes &&
        tiku_trk_prefs_get(controller->prefs, "MountVolumesOntoDesktop")) {
        /* A pose for every volume that earns one, from the mount table
         * scanned for this rebuild (PVN-040). */
        int shared_too = tiku_trk_prefs_get(controller->prefs,
                             "MountSharedVolumesOntoDesktop") != 0;
        int i;

        for (i = 0; i < volumes.n; i++) {
            if (tiku_trk_deskicons_volume_pose(&volumes, &volumes.v[i], 1,
                                               shared_too)) {
                add_pose(controller, volumes.v[i].mount);
            }
        }
    }
    (void)tiku_trk_poseview_place_unplaced(controller->folder.view,
        tiku_trk_desktop_view_area(controller).w);
    (void)tiku_trk_desktop_view_ensure_visible(controller);
}

int
tiku_trk_desktop_view_init(tiku_trk_desktop_view_t *controller,
    tiku_backend_t *backend, tiku_fs_t *fs,
    tiku_trk_prefs_t *prefs, tiku_trk_deskbar_t *deskbar,
    tiku_trk_folder_render_t *render, int screen_w, int screen_h)
{
    const char *home;

    if (controller == NULL || backend == NULL || fs == NULL ||
        prefs == NULL || deskbar == NULL || render == NULL) {
        return -1;
    }
    memset(controller, 0, sizeof *controller);
    controller->backend = backend;
    controller->fs = fs;
    controller->prefs = prefs;
    controller->deskbar = deskbar;
    controller->screen_w = screen_w;
    controller->screen_h = screen_h;
    /* Reserve the configured menu strip before restoring saved positions. */
    controller->top_inset = tiku_trk_prefs_get(prefs,"GlobalMenuBar") && render->menus ? render->menus->bar.height : 0;
    tiku_trk_deskbar_set_top_inset(deskbar,controller->top_inset);
    home = getenv("HOME");
    (void)home;
    (void)tiku_desktop_dir(controller->folder.path,
                               sizeof controller->folder.path);
    controller->folder.view = tiku_trk_poseview_new(
        backend, controller->folder.path);
    if (controller->folder.view == NULL) {
        return -1;
    }
    /* The Desktop's own state slot: the backdrop's view state (mode, icon
     * size, scroll and sort) is kept apart from that of a folder window
     * opened on the same directory (MA-064).  Icon positions are kept on
     * each entry, so both views share them. */
    tiku_trk_poseview_set_state_slot(controller->folder.view,
                                     TIKU_KEY_DESKTOP);
    tiku_trk_poseview_set_no_autoscroll(controller->folder.view, 1);
    tiku_trk_folder_window_configure(&controller->folder, render);
    tiku_trk_poseview_set_viewport(controller->folder.view,
                                   screen_w, screen_h);
    tiku_trk_poseview_set_mode(controller->folder.view,
                               TIKU_TRK_MODE_ICON);
    if (backend->ops->state_store != NULL) {
        /* Once: a desktop the user has set, or cleared, stays as it is
         * across boots (AW-016). */
        (void)tiku_trk_bg_install_default(backend->ops->state_store(backend),
                                          controller->folder.path, screen_w,
                                          screen_h);
    }
    tiku_trk_desktop_view_rebuild(controller);
    tiku_vdir_watch_init(&controller->watch, &controller->sources);
    tiku_workspace_set_backdrop(render->workspace, draw, controller);
    return 0;
}

/** @brief Whether @p path is a directory with anything in it. */
static int
dv_dir_has_entries(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int any = 0;

    if (d == NULL) {
        return 0;
    }
    while (!any && (e = readdir(d)) != NULL) {
        any = (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0);
    }
    (void)closedir(d);
    return any;
}

void
tiku_trk_desktop_view_set_top_inset(tiku_trk_desktop_view_t *controller,
                                    int inset)
{
    tiku_trk_rect_t old_area;

    if (controller == NULL || controller->top_inset == inset) {
        return;
    }
    old_area = tiku_trk_desktop_view_area(controller);
    controller->top_inset = inset;
    /* The poses move out from under the strip (or reclaim it): the same
     * reflow a screen resize runs. */
    if (controller->folder.view != NULL) {
        (void)reflow_poses(controller, old_area);
    }
}

int
tiku_trk_desktop_view_trash_check(tiku_trk_desktop_view_t *controller)
{
    tiku_trk_poseview_t *v;
    int i, n;

    if (controller == NULL || controller->fs == NULL) {
        return 0;
    }
    v = controller->folder.view;
    n = tiku_trk_poseview_count(v);
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(v, i);
        char dirs[TIKU_VOLUME_MAX][TIKU_PATH_MAX];
        int k, nd, full = 0;

        if (pose == NULL || pose->model.kind != TIKU_KIND_TRASH) {
            continue;
        }
        nd = tiku_fs_trash_dirs(controller->fs, dirs,
                                    TIKU_VOLUME_MAX);
        for (k = 0; k < nd && !full; k++) {
            full = dv_dir_has_entries(dirs[k]);
        }
        if (full != pose->model.facts.trash_full) {
            pose->model.facts.trash_full = full;
            return 1;
        }
        return 0;
    }
    return 0;
}

int
tiku_trk_desktop_view_poll(tiku_trk_desktop_view_t *controller,
    int64_t now, const tiku_model_change_t *changes, int change_count,
    int complete)
{
    unsigned flags;
    int gone = -1;
    int changed = 0;
    int want_disks;

    if (controller == NULL || controller->folder.view == NULL) {
        return 0;
    }
    flags = tiku_trk_poseview_poll(controller->folder.view, now, changes,
                                   change_count, complete);
    if (flags != TIKU_TRK_NOTIFY_NOTHING) {
        (void)tiku_trk_poseview_place_unplaced(controller->folder.view,
            tiku_trk_desktop_view_area(controller).w);
        (void)tiku_trk_desktop_view_ensure_visible(controller);
        changed++;
    }
    want_disks = tiku_trk_deskicons_show_disks(
        tiku_trk_prefs_get(controller->prefs, "ShowDisksIcon"),
        tiku_trk_prefs_get(controller->prefs, "MountVolumesOntoDesktop"));
    if (want_disks != controller->disks_shown) {
        tiku_trk_desktop_view_rebuild(controller);
        changed++;
    }
    if (tiku_vdir_poll(&controller->sources, &controller->watch,
                           &gone) != TIKU_VDIR_CH_NONE) {
        tiku_trk_desktop_view_rebuild(controller);
        changed++;
    }
    return changed;
}

void
tiku_trk_desktop_view_destroy(tiku_trk_desktop_view_t *controller)
{
    if (controller == NULL || controller->folder.view == NULL) {
        return;
    }
    (void)tiku_trk_poseview_save_state(controller->folder.view);
    tiku_trk_poseview_free(controller->folder.view);
    controller->folder.view = NULL;
}
