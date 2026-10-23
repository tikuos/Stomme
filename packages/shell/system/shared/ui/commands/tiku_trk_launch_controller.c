/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch_controller.c - opening files with an application: the
 * preferred application for a file, the launch and its status line, and
 * the Open with menu built from a folder's selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_drag.h"
#include "tiku_trk_launch_controller.h"
#include "tiku_trk_shortcuts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tiku_trk_launch_controller_init(tiku_trk_launch_controller_t *controller,
    tiku_trk_apps_t *apps, tiku_store_t *store, char *status,
    size_t status_size, tiku_trk_launch_open_path_fn open_path,
    void *open_context)
{
    if (controller == NULL) {
        return;
    }
    *controller = (tiku_trk_launch_controller_t){
        .apps = apps,
        .store = store,
        .status = status,
        .status_size = status_size,
        .open_path = open_path,
        .open_context = open_context
    };
}

int
tiku_trk_launch_controller_run(tiku_trk_launch_controller_t *controller,
    const tiku_trk_app_t *application, const char *const *paths, int count)
{
    tiku_trk_launch_err_t error;

    if (controller == NULL || controller->status == NULL) {
        return 0;
    }
    if (application == NULL) {
        snprintf(controller->status, controller->status_size, "%s",
                 tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NO_APP));
        return 0;
    }
    if (application->module[0]) {
        int opened = 0;
        if (paths && controller->open_document) {
            for (int i = 0; i < count; i++)
                if (paths[i] && *paths[i])
                    opened += !!controller->open_document(
                        controller->open_context, application, paths[i]);
        }
        snprintf(controller->status, controller->status_size,
            "Opened %d of %d with %.40s%s", opened, count, application->name,
            opened == count && count > 0 ? "" :
            "; save device files locally first and check the app is installed");
        return count > 0 && opened == count;
    }
    error = tiku_trk_launch_run(application, paths, count);
    if (error == TIKU_TRK_LAUNCH_OK) {
        snprintf(controller->status, controller->status_size,
                 "opening %d item%s with %.40s", count,
                 count == 1 ? "" : "s", application->name);
    } else if (error == TIKU_TRK_LAUNCH_IS_TRACKER) {
        int i;

        for (i = 0; i < count; i++) {
            if (controller->open_path != NULL) {
                controller->open_path(controller->open_context, paths[i]);
            }
        }
    } else {
        snprintf(controller->status, controller->status_size, "%s",
                 tiku_trk_launch_reason(error));
    }
    return error == TIKU_TRK_LAUNCH_OK;
}

const tiku_trk_app_t *
tiku_trk_launch_controller_handler(tiku_trk_launch_controller_t *controller,
                                   const tiku_model_t *model)
{
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    const char *types[1];
    int count;

    if (controller == NULL || model == NULL || model->type[0] == '\0') {
        return NULL;
    }
    types[0] = model->type;
    count = tiku_trk_launch_candidates(controller->apps, types, 1,
        controller->store, candidates, TIKU_TRK_APP_MAX);
    return count > 0 ? candidates[0].app : NULL;
}

/**
 * @brief The selected non-container poses: their paths, and the distinct
 *        types among them.  @return 0 when there is no view to read, else
 *        the number of poses looked at.
 */
static int
collect_selection(tiku_trk_folder_window_t *folder, const char **types,
                  int *type_count, const char **paths, int *path_count)
{
    int i, count;

    if (folder == NULL || folder->view == NULL) {
        return 0;
    }
    count = tiku_trk_poseview_count(folder->view);
    for (i = 0; i < count; i++) {
        tiku_trk_pose_t *pose = tiku_trk_poseview_at(folder->view, i);
        int k, seen = 0;

        if (pose == NULL || !pose->selected ||
            tiku_model_is_container(&pose->model)) {
            continue;
        }
        if (paths != NULL && *path_count < TIKU_TRK_DRAG_MAX) {
            paths[(*path_count)++] = pose->model.path;
        }
        for (k = 0; k < *type_count; k++) {
            if (strcmp(types[k], pose->model.type) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen && pose->model.type[0] != '\0' &&
            *type_count < TIKU_TRK_APP_TYPES_MAX) {
            types[(*type_count)++] = pose->model.type;
        }
    }
    return count;
}

void
tiku_trk_launch_controller_build_menu(
    tiku_trk_launch_controller_t *controller, tiku_menu_t *menu,
    tiku_trk_folder_window_t *folder)
{
    const char *types[TIKU_TRK_APP_TYPES_MAX];
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    int i, type_count = 0, path_count = 0, count;

    if (menu == NULL) {
        return;
    }
    tiku_menu_clear(menu);
    if (controller == NULL ||
        !collect_selection(folder, types, &type_count, NULL, &path_count) ||
        type_count == 0) {
        return;
    }
    count = tiku_trk_launch_candidates(controller->apps, types, type_count,
        controller->store, candidates, TIKU_TRK_APP_MAX);
    for (i = 0; i < count; i++) {
        char label[TIKU_TRK_APP_NAME_MAX + 40];

        if (i > 0 && candidates[i].rel != candidates[i - 1].rel) {
            /* A separator wherever the relation changes: the candidates
             * come preferred first, then by how well they match
             * (AW-048). */
            (void)tiku_menu_add_separator(menu);
        }
        snprintf(label, sizeof label, "%s (%s)", candidates[i].app->name,
                 tiku_trk_launch_relation_label(candidates[i].rel));
        (void)tiku_menu_add(menu, label,
            TIKU_TRK_CMD_OPEN_WITH_BASE + i, 0, 0u, 1);
        /* Each candidate shows its application's icon, else the generic
         * application icon (IV-054). */
        tiku_menu_set_icon(menu, TIKU_TRK_CMD_OPEN_WITH_BASE + i,
                               candidates[i].app->icon[0] ?
                               candidates[i].app->icon : "application");
        if (candidates[i].preferred) {
            menu->item[menu->count - 1].marked = 1;
        }
    }
    if (menu->count > 0) {
        (void)tiku_menu_add_separator(menu);
    }
    /* The last entry opens the Open with window (AW-047). */
    (void)tiku_menu_add(menu, "Open with…",
                            TIKU_TRK_CMD_OPEN_WITH_WINDOW, 0, 0u, 1);
}

void
tiku_trk_launch_controller_pick(tiku_trk_launch_controller_t *controller,
    tiku_trk_folder_window_t *folder, int index)
{
    const char *types[TIKU_TRK_APP_TYPES_MAX];
    const char *paths[TIKU_TRK_DRAG_MAX];
    tiku_trk_cand_t candidates[TIKU_TRK_APP_MAX];
    int type_count = 0, path_count = 0, count;

    if (controller == NULL || !collect_selection(folder, types, &type_count,
            paths, &path_count)) {
        return;
    }
    if (path_count == 0) {
        snprintf(controller->status, controller->status_size, "%s",
                 tiku_trk_launch_reason(TIKU_TRK_LAUNCH_NOTHING));
        return;
    }
    count = tiku_trk_launch_candidates(controller->apps, types, type_count,
        controller->store, candidates, TIKU_TRK_APP_MAX);
    if (index >= 0 && index < count) {
        (void)tiku_trk_launch_controller_run(controller,
            candidates[index].app, paths, path_count);
    }
}
