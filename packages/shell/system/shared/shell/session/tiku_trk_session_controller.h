/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_session_controller.h - the desktop session: the store it is kept
 * in, the folders and Deskbar it restores at startup, and what it saves and
 * finishes at shutdown.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SESSION_CONTROLLER_H_
#define TIKU_TRK_SESSION_CONTROLLER_H_

#include "tiku_fs.h"
#include "tiku_model.h"
#include "tiku_state.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_folder_lifecycle.h"
#include "tiku_trk_frame.h"

/** @brief What the session reads and writes: the store, the Deskbar, the
 *         Desktop and the folder windows. */
typedef struct {
    tiku_store_t *store;
    tiku_trk_deskbar_t *deskbar;
    tiku_trk_desktop_view_t *desktop_view;
    tiku_trk_folder_lifecycle_t *folders;
    int preserve_restore; /**< explicit next-start record supersedes shutdown capture */
} tiku_trk_session_controller_t;

/**
 * @brief The sidecar store for @p backend's device, or the local one; an
 *        in-memory store when neither can be opened.
 */
tiku_store_t *tiku_trk_session_open_store(tiku_backend_t *backend);
/** @brief Bind the controller to the store, bar, Desktop and folders. */
void tiku_trk_session_controller_init(
    tiku_trk_session_controller_t *controller, tiku_store_t *store,
    tiku_trk_deskbar_t *deskbar, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_folder_lifecycle_t *folders);
/** @brief Put the Deskbar back where the saved session had it. */
void tiku_trk_session_restore_deskbar(
    tiku_trk_session_controller_t *controller);
/**
 * @brief Open the folders the session starts with: @p paths when given,
 *        else the saved session (none in safe mode), else the home or,
 *        on a device, its memory and devices when @p fallback is enabled.
 */
void tiku_trk_session_restore_folders(
    tiku_trk_session_controller_t *controller, const char *const *paths,
    int path_count, int safe_mode, int device_backend, int fallback);
/** @brief Copy the current restorable folder entries without filesystem I/O.
 *  @return Count, or -1 when the supplied array cannot hold the complete set.
 * */
int tiku_trk_session_controller_entries(const tiku_trk_session_controller_t *,
    tiku_trk_session_entry_t *entries, int capacity);
/**
 * @brief Save folder views, columns, frames and Deskbar docking; capture the
 *        open folders unless an explicit next-start record supersedes capture.
 *        Flush the session store afterwards.
 */
void tiku_trk_session_controller_save(
    tiku_trk_session_controller_t *controller);
/**
 * @brief Finish the file work under way before quitting, answering any
 *        conflict by renaming and any error by skipping, for at most 50
 *        rounds of 100 ms.
 */
void tiku_trk_session_drain_filesystem(tiku_fs_t *filesystem);

#endif /* TIKU_TRK_SESSION_CONTROLLER_H_ */
