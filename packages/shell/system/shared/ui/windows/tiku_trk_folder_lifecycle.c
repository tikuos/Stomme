/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_lifecycle.c - opening, finding and closing spatial folder
 * windows, and the query result windows that use the same slots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_sound.h"
#include "tiku_state.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_panel.h"
#include "tiku_trk_task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void
tiku_trk_folder_title_for(tiku_trk_prefs_t *prefs, const char *path,
                          char *out, size_t max)
{
    const char *slash;

    if (path == NULL || path[0] == '\0') {
        snprintf(out, max, "/");
        return;
    }
    if (tiku_trk_prefs_get(prefs, "ShowFullPathInTitleBar")) {
        snprintf(out, max, "%s", path);
        return;
    }
    slash = strrchr(path, '/');
    if (slash == NULL || slash[1] == '\0') {
        snprintf(out, max, "%s", path);   /* "/" or no slash: as-is */
    } else {
        snprintf(out, max, "%s", slash + 1);
    }
}


#define CASCADE_HOME_X 45
#define CASCADE_HOME_Y 40
#define CASCADE_STEP   22

void
tiku_trk_folder_lifecycle_init(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folders, int capacity, int *count,
    int *cascade_x, int *cascade_y, tiku_workspace_t *workspace,
    tiku_backend_t *backend, tiku_store_t *store,
    tiku_trk_prefs_t *prefs, tiku_trk_folder_render_t *render,
    char *status, size_t status_size, int *dirty,
    int screen_w, int screen_h)
{
    *controller = (tiku_trk_folder_lifecycle_t){
        .folders = folders,
        .capacity = capacity,
        .count = count,
        .cascade_x = cascade_x,
        .cascade_y = cascade_y,
        .workspace = workspace,
        .backend = backend,
        .store = store,
        .prefs = prefs,
        .render = render,
        .status = status,
        .status_size = status_size,
        .dirty = dirty,
        .screen_w = screen_w,
        .screen_h = screen_h
    };
}

tiku_trk_folder_window_t *
tiku_trk_folder_lifecycle_find(tiku_trk_folder_lifecycle_t *controller,
                               tiku_window_t *window)
{
    int i;

    if (controller == NULL || window == NULL) {
        return NULL;
    }
    /* The workspace recycles window objects, so a closed folder can still
     * hold the pointer a reopened window was just given.  The open folder
     * answers first; the closed match is a fallback for callers cleaning
     * up after a close. */
    for (i = 0; i < *controller->count; i++) {
        if (controller->folders[i].win == window &&
            controller->folders[i].win->open) {
            return &controller->folders[i];
        }
    }
    for (i = 0; i < *controller->count; i++) {
        if (controller->folders[i].win == window) {
            return &controller->folders[i];
        }
    }
    return NULL;
}

void
tiku_trk_folder_lifecycle_save_frame(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder)
{
    tiku_trk_frame_t frame;

    if (controller == NULL || folder == NULL || folder->win == NULL ||
        folder->gone) {
        return;
    }
    frame = (tiku_trk_frame_t){
        folder->win->frame.x, folder->win->frame.y,
        folder->win->frame.w, folder->win->frame.h
    };
    (void)tiku_trk_frame_save(controller->store, folder->state_path, &frame,
                              folder->had_frame ? &folder->opened_with : NULL);
}

void
tiku_trk_folder_lifecycle_close(tiku_trk_folder_lifecycle_t *controller,
                                tiku_trk_folder_window_t *folder)
{
    if (controller == NULL || folder == NULL || folder->win == NULL) {
        return;
    }
    /* The state and columns of a folder that is gone are not saved. */
    if (!folder->gone) {
        (void)tiku_trk_poseview_save_state(folder->view);
        (void)tiku_trk_poseview_save_columns(folder->view);
    }
    tiku_trk_folder_lifecycle_save_frame(controller, folder);
    tiku_sound_play(TIKU_SOUND_WINDOW_CLOSE);
    tiku_workspace_close(controller->workspace, folder->win);
    /* The kept pixels belong to the window that was showing, so a slot
     * reused for another folder starts from a blank frame. */
    tiku_trk_folder_window_forget(folder);
    /* The window pointer is cleared too: the workspace recycles window
     * objects, so a closed slot still holding one would claim a reopened
     * window that lands at the same address. */
    folder->win = NULL;
    *controller->dirty = 1;
}

/**
 * @brief Where a window opening on @p path goes: its saved frame, else
 *        one inherited from a parent, else the next step of the cascade.
 */
static tiku_rect_t
next_frame(tiku_trk_folder_lifecycle_t *controller, const char *path,
           tiku_trk_folder_window_t *folder)
{
    tiku_rect_t frame;
    tiku_trk_frame_t saved;

    if (*controller->cascade_x == 0 && *controller->cascade_y == 0) {
        *controller->cascade_x = CASCADE_HOME_X;
        *controller->cascade_y = CASCADE_HOME_Y;
    }
    frame = (tiku_rect_t){ *controller->cascade_x,
        *controller->cascade_y, 460, 300 };
    *controller->cascade_x += CASCADE_STEP;
    *controller->cascade_y += CASCADE_STEP;
    folder->had_frame = tiku_trk_frame_load(controller->store, path, &saved);
    if (!folder->had_frame) {
        if (tiku_trk_frame_inherit(controller->store, path, &saved)) {
            tiku_trk_frame_clamp(&saved, controller->screen_w,
                                 controller->screen_h);
            frame = (tiku_rect_t){ saved.x, saved.y, saved.w, saved.h };
        }
    } else {
        tiku_trk_frame_clamp(&saved, controller->screen_w,
                             controller->screen_h);
        frame = (tiku_rect_t){ saved.x, saved.y, saved.w, saved.h };
        folder->opened_with = saved;
    }
    if (*controller->cascade_x + frame.w > controller->screen_w ||
        *controller->cascade_y + frame.h > controller->screen_h) {
        *controller->cascade_x = CASCADE_HOME_X;
        *controller->cascade_y = CASCADE_HOME_Y;
    }
    if (frame.x >= controller->screen_w ||
        frame.y >= controller->screen_h || frame.x + frame.w <= 0 ||
        frame.y + frame.h <= 0) {
        frame.x = CASCADE_HOME_X;
        frame.y = CASCADE_HOME_Y;
    }
    return frame;
}

tiku_trk_folder_window_t *
tiku_trk_folder_lifecycle_open(tiku_trk_folder_lifecycle_t *controller,
                               const char *path)
{
    tiku_trk_folder_window_t *folder;
    tiku_rect_t frame;
    int i, slot;

    if (controller == NULL || path == NULL) {
        return NULL;
    }
    /* With SingleWindowBrowse on, the first open folder window that is
     * not a live query and can switch to path moves there, and no window
     * opens.  This runs before the per-folder lookup. */
    if (controller->prefs != NULL &&
        tiku_trk_prefs_get(controller->prefs, "SingleWindowBrowse")) {
        for (i = 0; i < *controller->count; i++) {
            folder = &controller->folders[i];
            if (folder->win == NULL || !folder->win->open || folder->live ||
                folder->view == NULL) {
                continue;
            }
            (void)tiku_trk_poseview_save_state(folder->view);
            (void)tiku_trk_poseview_save_columns(folder->view);
            if (tiku_trk_poseview_switch_dir(folder->view, path) < 0) {
                continue;
            }
            snprintf(folder->path, sizeof folder->path, "%s", path);
            snprintf(folder->state_path, sizeof folder->state_path, "%s",
                     path);
            tiku_trk_folder_title_for(controller->prefs, path,
                                      folder->win->title,
                                      sizeof folder->win->title);
            tiku_trk_recent_add(controller->store, TIKU_TRK_RECENT_FOLDERS,
                                path);
            tiku_workspace_activate(controller->workspace, folder->win);
            *controller->dirty = 1;
            return folder;
        }
    }
    for (i = 0; i < *controller->count; i++) {
        folder = &controller->folders[i];
        if (strcmp(folder->path, path) == 0 && folder->win != NULL &&
            folder->win->open) {
            tiku_workspace_activate(controller->workspace, folder->win);
            *controller->dirty = 1;
            return folder;
        }
    }
    /* A slot whose window was closed is taken first; its view and query
     * run are released before the slot is cleared. */
    for (slot = 0; slot < *controller->count; slot++) {
        if (controller->folders[slot].win == NULL) {
            break;
        }
    }
    if (slot >= controller->capacity) {
        return NULL;
    }
    folder = &controller->folders[slot];
    tiku_trk_folder_window_forget(folder);
    if (slot < *controller->count) {
        tiku_trk_poseview_free(folder->view);
        if (folder->run != NULL) {
            tiku_query_stop(folder->run);
        }
    }
    memset(folder, 0, sizeof *folder);
    snprintf(folder->path, sizeof folder->path, "%s", path);
    snprintf(folder->state_path, sizeof folder->state_path, "%s", path);
    tiku_trk_recent_add(controller->store, TIKU_TRK_RECENT_FOLDERS, path);
    folder->view = tiku_trk_poseview_new(controller->backend, path);
    if (folder->view == NULL) {
        return NULL;
    }
    tiku_trk_folder_window_configure(folder, controller->render);
    tiku_trk_poseview_set_folders_first(folder->view,
        tiku_trk_prefs_get(controller->prefs, "SortFolderNamesFirst"));
    tiku_trk_poseview_set_hide_dotfiles(folder->view,
        tiku_trk_prefs_get(controller->prefs, "HideDotFiles"));
    if (tiku_trk_poseview_restore_columns(folder->view) == 0) {
        /* A folder with no saved columns takes its parent's layout when a
         * window on the parent has been opened, as the frame is inherited
         * (MA-070). */
        char parent[TIKU_PATH_MAX];
        const char *slash = strrchr(path, '/');
        int i;

        if (slash != NULL && slash != path) {
            snprintf(parent, sizeof parent, "%.*s",
                     (int)(slash - path), path);
            for (i = 0; i < *controller->count; i++) {
                tiku_trk_folder_window_t *pf = &controller->folders[i];

                if (pf != folder && pf->view != NULL &&
                    strcmp(pf->path, parent) == 0) {
                    tiku_trk_poseview_copy_columns(folder->view, pf->view);
                    break;
                }
            }
        }
    }
    (void)tiku_trk_poseview_restore_state(folder->view);
    frame = next_frame(controller, path, folder);
    folder->win = tiku_workspace_open(controller->workspace, path, frame,
        tiku_trk_folder_window_draw, folder, folder->path);
    if (folder->win != NULL) {
        tiku_trk_folder_title_for(controller->prefs, path,
                                  folder->win->title,
                                  sizeof folder->win->title);
    }
    if (folder->win == NULL) {
        tiku_trk_poseview_free(folder->view);
        folder->view = NULL;
        return NULL;
    }
    if (slot == *controller->count) {
        (*controller->count)++;
    }
    *controller->dirty = 1;
    tiku_sound_play(TIKU_SOUND_WINDOW_OPEN);
    return folder;
}

tiku_trk_folder_window_t *
tiku_trk_folder_lifecycle_open_results(
    tiku_trk_folder_lifecycle_t *controller, const char *title,
    tiku_qrun_t *run)
{
    tiku_trk_folder_window_t *folder =
        tiku_trk_folder_lifecycle_open(controller, title);
    tiku_trk_frame_t frame;
    tiku_model_t *results;
    int i, count;

    if (folder == NULL) {
        tiku_query_stop(run);
        return NULL;
    }
    tiku_trk_poseview_set_layout_store(folder->view, controller->store);
    tiku_trk_poseview_set_query_type(folder->view,
                                     tiku_query_search_type(run));
    snprintf(folder->state_path, sizeof folder->state_path, "%s",
             tiku_trk_poseview_layout_path(folder->view));
    if (tiku_trk_frame_load(controller->store, folder->state_path, &frame)) {
        tiku_trk_frame_clamp(&frame, controller->screen_w,
                             controller->screen_h);
        folder->win->frame = (tiku_rect_t){ frame.x, frame.y,
                                                 frame.w, frame.h };
        folder->opened_with = frame;
        folder->had_frame = 1;
    } else {
        folder->had_frame = 0;
    }
    count = tiku_query_count(run);
    results = calloc((size_t)(count > 0 ? count : 1), sizeof *results);
    if (results != NULL) {
        for (i = 0; i < count; i++) {
            results[i] = *tiku_query_at(run, i);
        }
        (void)tiku_trk_poseview_set_results(folder->view, results, count);
        free(results);
    }
    if (folder->run != NULL) {
        tiku_query_stop(folder->run);
    }
    folder->run = run;
    folder->live = 1;
    return folder;
}

/** @brief Put what the run has found into the window. */
static void
show_results(tiku_trk_folder_window_t *folder)
{
    tiku_model_t *results;
    int i, count = tiku_query_count(folder->run);

    results = calloc((size_t)(count > 0 ? count : 1), sizeof *results);
    if (results == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        results[i] = *tiku_query_at(folder->run, i);
    }
    (void)tiku_trk_poseview_apply_results(folder->view, results, count,
                                          tiku_trk_now());
    free(results);
}

int
tiku_trk_folder_lifecycle_refresh_results(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder)
{
    if (controller == NULL || folder == NULL || folder->run == NULL ||
        !folder->live) {
        return 0;
    }
    /* Only begun here: a re-walk can be long on a real disk, and this is
     * the thread that draws.  tiku_trk_folder_lifecycle_step_results()
     * carries it out a slice per turn. */
    if (tiku_query_refreshing(folder->run)) {
        return 0;               /* one is already under way */
    }
    return tiku_query_refresh_begin(folder->run) == 0;
}

int
tiku_trk_folder_lifecycle_step_results(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder, int budget)
{
    tiku_qdiff_t difference;
    int refreshing;

    if (controller == NULL || folder == NULL || folder->run == NULL ||
        !folder->live || tiku_query_done(folder->run)) {
        return 0;
    }
    refreshing = tiku_query_refreshing(folder->run);
    if (tiku_query_step(folder->run, budget) < 0) {
        return 0;
    }
    if (!refreshing) {
        /* The first walk shows each row as it is found; the count line
         * under the rows counts up, and says "live" once the walk is
         * done. */
        show_results(folder);
        /* The first walk leaves the desktop's status line alone; the
         * window's count line shows its progress. */
        *controller->dirty = 1;
        return 1;
    }
    if (!tiku_query_done(folder->run)) {
        return 0;               /* a re-walk shows nothing until it ends */
    }
    if (tiku_query_refresh_end(folder->run, &difference) != 0) {
        return 0;
    }
    show_results(folder);
    if (difference.vanished > 0) {
        snprintf(controller->status, controller->status_size,
                 "%d gone, %d no longer match, %d new", difference.vanished,
                 difference.removed - difference.vanished, difference.added);
    } else {
        snprintf(controller->status, controller->status_size,
                 "%d in, %d out", difference.added, difference.removed);
    }
    *controller->dirty = 1;
    return 1;
}
