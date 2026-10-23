/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_session_controller.c - restoring and saving the desktop session.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_session_controller.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

tiku_store_t *
tiku_trk_session_open_store(tiku_backend_t *backend)
{
    tiku_store_t *store = tiku_store_sidecar_open(
        backend != NULL && backend->devid[0] != '\0' ? backend->devid
                                                      : "local");

    return store != NULL ? store : tiku_store_memory_open();
}

void
tiku_trk_session_controller_init(
    tiku_trk_session_controller_t *controller, tiku_store_t *store,
    tiku_trk_deskbar_t *deskbar, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_folder_lifecycle_t *folders)
{
    *controller = (tiku_trk_session_controller_t){
        .store = store,
        .deskbar = deskbar,
        .desktop_view = desktop_view,
        .folders = folders
    };
}

void
tiku_trk_session_restore_deskbar(tiku_trk_session_controller_t *controller)
{
    int32_t record[2];

    if (tiku_state_read(controller->store, TIKU_KEY_SESSION,
            TIKU_ATTR_DOCK, record, sizeof record) ==
            (int)sizeof record) {
        tiku_trk_deskbar_set_dock(controller->deskbar,
                                  (tiku_trk_db_dock_t)record[0]);
        (void)tiku_trk_deskbar_set_width(controller->deskbar,
                                         (int)record[1]);
    }
}

/**
 * @brief Reopen the saved session's folders, minimised as they were; none
 *        in safe mode, and not the root unless the Desktop shows Disks.
 *
 * @return how many opened.
 */
static int
restore_saved(tiku_trk_session_controller_t *controller, int safe_mode)
{
    tiku_trk_session_entry_t entries[TIKU_TRK_SESSION_MAX];
    int count = safe_mode ? 0 : tiku_trk_session_load_entries(
        controller->store, entries, TIKU_TRK_SESSION_MAX);
    int opened = 0;
    int i;

    for (i = 0; i < count; i++) {
        tiku_trk_folder_window_t *folder;

        if (strcmp(entries[i].path, "/") == 0 &&
            !tiku_trk_desktop_view_shows_disks(controller->desktop_view)) {
            continue;
        }
        folder = tiku_trk_folder_lifecycle_open(controller->folders,
                                                entries[i].path);
        if (folder != NULL) {
            folder->win->minimized = entries[i].minimized;
            opened++;
        }
    }
    return opened;
}

void
tiku_trk_session_restore_folders(
    tiku_trk_session_controller_t *controller, const char *const *paths,
    int path_count, int safe_mode, int device_backend, int fallback)
{
    int opened = 0;
    int i;

    for (i = 0; i < path_count; i++) {
        if (tiku_trk_folder_lifecycle_open(controller->folders,
                                           paths[i]) != NULL) {
            opened++;
        }
    }
    if (path_count > 0) {
        return;
    }
    opened = restore_saved(controller, safe_mode);
    if (opened != 0 || !fallback) {
        return;
    }
    if (device_backend) {
        (void)tiku_trk_folder_lifecycle_open(controller->folders,
                                             "/sys/mem");
        (void)tiku_trk_folder_lifecycle_open(controller->folders, "/dev");
    } else {
        const char *home = getenv("HOME");

        (void)tiku_trk_folder_lifecycle_open(controller->folders,
                                             home != NULL ? home : "/");
    }
}

void
tiku_trk_session_controller_save(tiku_trk_session_controller_t *controller)
{
    tiku_trk_session_entry_t entries[TIKU_TRK_SESSION_MAX];
    int32_t dock[2];
    int entry_count = 0;
    int i;

    for (i = 0; i < *controller->folders->count; i++) {
        tiku_trk_folder_window_t *folder = &controller->folders->folders[i];

        if (folder->win != NULL && folder->win->open && !folder->gone &&
            !tiku_trk_poseview_is_query(folder->view) &&
            entry_count < TIKU_TRK_SESSION_MAX) {
            snprintf(entries[entry_count].path, TIKU_PATH_MAX, "%s",
                     folder->path);
            entries[entry_count].minimized = folder->win->minimized;
            entry_count++;
        }
        if (!folder->gone) {
            (void)tiku_trk_poseview_save_state(folder->view);
            (void)tiku_trk_poseview_save_columns(folder->view);
            tiku_trk_folder_lifecycle_save_frame(controller->folders,
                                                  folder);
        }
    }
    (void)tiku_trk_session_save_entries(controller->store, entries,
                                         entry_count);
    dock[0] = (int32_t)tiku_trk_deskbar_dock(controller->deskbar);
    dock[1] = (int32_t)tiku_trk_deskbar_width(controller->deskbar);
    (void)tiku_state_write(controller->store, TIKU_KEY_SESSION,
                               TIKU_ATTR_DOCK, dock, sizeof dock);
    (void)tiku_store_flush(controller->store);
}

void
tiku_trk_session_drain_filesystem(tiku_fs_t *filesystem)
{
    int guard = 0;

    if (filesystem == NULL) {
        return;
    }
    while (tiku_fs_busy(filesystem) && guard++ < 50) {
        if (tiku_fs_step(filesystem, 16) == 0) {
            const tiku_progress_t *progress =
                tiku_fs_progress(filesystem);

            if (progress->conflict) {
                tiku_fs_resolve(filesystem, TIKU_CONFLICT_RENAME);
            } else if (progress->error_ask) {
                tiku_fs_resolve_error(filesystem, 1);
            } else {
                break;
            }
        }
        usleep(100000);
    }
    tiku_fs_moved_pump(filesystem);         /* what the last step moved */
}
