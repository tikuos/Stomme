/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_lifecycle.h - opening, finding and closing spatial folder
 * windows, and the query result windows that use the same slots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_LIFECYCLE_H_
#define TIKU_TRK_FOLDER_LIFECYCLE_H_

#include "tiku_trk_folder_window.h"
#include <stddef.h>

/** @brief Services and storage for spatial folder windows. */
typedef struct {
    tiku_trk_folder_window_t *folders;     /**< the shell's window slots */
    int                       capacity;
    int                      *count;       /**< slots taken; closed: win NULL */
    int                      *cascade_x;   /**< the cascade's next x */
    int                      *cascade_y;   /**< the cascade's next y */
    tiku_workspace_t    *workspace;
    tiku_backend_t       *backend;
    tiku_store_t         *store;           /**< frames and recent folders */
    tiku_trk_prefs_t         *prefs;
    tiku_trk_folder_render_t *render;      /**< shared drawing services */
    char                     *status;      /**< the shell's status line */
    size_t                    status_size;
    int                      *dirty;       /**< set when a redraw is owed */
    int                       screen_w;
    int                       screen_h;
} tiku_trk_folder_lifecycle_t;

/** @brief Bind folder storage and the services required for its lifetime. */
void tiku_trk_folder_lifecycle_init(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folders, int capacity, int *count,
    int *cascade_x, int *cascade_y, tiku_workspace_t *workspace,
    tiku_backend_t *backend, tiku_store_t *store,
    tiku_trk_prefs_t *prefs, tiku_trk_folder_render_t *render,
    char *status, size_t status_size, int *dirty,
    int screen_w, int screen_h);

/**
 * @brief Open a window on @p path, or raise the one already showing it.
 *
 * With SingleWindowBrowse on, an open folder window moves to @p path
 * instead.
 * @return the folder, or NULL when no slot is free or the window or its
 *         view cannot be made.
 */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_open(
    tiku_trk_folder_lifecycle_t *controller, const char *path);

/**
 * @brief The folder slot that owns @p window, an open one first.
 * @return the slot, or NULL.
 */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_find(
    tiku_trk_folder_lifecycle_t *controller, tiku_window_t *window);

/**
 * @brief Save one folder window's state, columns and frame, unless its
 *        folder is gone, and close it, emptying its slot.
 */
void tiku_trk_folder_lifecycle_close(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder);

/** @brief Save one folder's frame, unless its folder or volume is gone. */
void tiku_trk_folder_lifecycle_save_frame(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder);

/**
 * @brief Give a live query run a result window of its own.
 * @note Takes @p run: it is stopped when no window can be opened.
 */
tiku_trk_folder_window_t *tiku_trk_folder_lifecycle_open_results(
    tiku_trk_folder_lifecycle_t *controller, const char *title,
    tiku_qrun_t *run);

/**
 * @brief Begin a re-walk of a live result window; the turns carry it out
 *        through tiku_trk_folder_lifecycle_step_results().
 * @return nonzero when a re-walk was begun.
 */
int tiku_trk_folder_lifecycle_refresh_results(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder);

/**
 * @brief Give a live query window its turn: the walk, or the re-walk.
 *
 * Steps the walk by at most @p budget directories.  The first walk shows
 * rows as they are found.  A re-walk shows nothing until it ends, since
 * its partial results lack the rows not yet found again; at its end the
 * status line says what came and went.
 *
 * @return non-zero when what the window shows has changed.
 */
int tiku_trk_folder_lifecycle_step_results(
    tiku_trk_folder_lifecycle_t *controller,
    tiku_trk_folder_window_t *folder, int budget);

/**
 * @brief The title a folder window shows for @p path (CW-016).
 *
 * The whole path when ShowFullPathInTitleBar is on, the folder's own
 * name otherwise; the root and an empty path show "/".
 */
void tiku_trk_folder_title_for(tiku_trk_prefs_t *prefs,
                               const char *path, char *out, size_t max);

#endif /* TIKU_TRK_FOLDER_LIFECYCLE_H_ */
