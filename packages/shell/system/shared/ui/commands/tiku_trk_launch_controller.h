/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_launch_controller.h - opening files with an application: the
 * preferred application for a file, the launch and its status line, and
 * the Open with menu built from a folder's selection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_LAUNCH_CONTROLLER_H_
#define TIKU_TRK_LAUNCH_CONTROLLER_H_

#include "tiku_menu.h"
#include "tiku_state.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_launch.h"
#include <stddef.h>

/** @brief Open @p path in a Tracker window. */
typedef void (*tiku_trk_launch_open_path_fn)(void *context,
                                             const char *path);

/**
 * @brief The application registry and type store a launch reads, the
 *        status line it writes, and the shell's ways of opening a file.
 */
typedef struct {
    tiku_trk_apps_t              *apps;
    tiku_store_t             *store;
    char                         *status;  /**< the shell's status line */
    size_t                        status_size;
    tiku_trk_launch_open_path_fn  open_path; /**< when the app is Tracker */
    void                         *open_context;
    /** Open one document in a hosted application (one with a module);
     *  nonzero when it opened.  Called with open_context. */
    int (*open_document)(void *context, const tiku_trk_app_t *app,
                          const char *path);
} tiku_trk_launch_controller_t;

/**
 * @brief Bind the application registry, type store, status line and
 *        @p open_path.
 * @note Clears open_document: set it after this call.
 */
void tiku_trk_launch_controller_init(tiku_trk_launch_controller_t *controller,
    tiku_trk_apps_t *apps, tiku_store_t *store, char *status,
    size_t status_size, tiku_trk_launch_open_path_fn open_path,
    void *open_context);

/**
 * @brief The first launch candidate for @p model's type: the preferred
 *        application, else the best match.  NULL when the model has no
 *        type or no application takes it.
 */
const tiku_trk_app_t *tiku_trk_launch_controller_handler(
    tiku_trk_launch_controller_t *controller,
    const tiku_model_t *model);
/**
 * @brief Open the @p count @p paths with @p application and write the
 *        outcome to the status line.
 *
 * A hosted application opens each document through open_document, and
 * Tracker itself opens each path in a window through open_path.
 *
 * @return 1 when the launch succeeded (for a hosted application, every
 *         document opened), 0 otherwise and always for Tracker.
 */
int tiku_trk_launch_controller_run(tiku_trk_launch_controller_t *controller,
    const tiku_trk_app_t *application, const char *const *paths, int count);
/**
 * @brief Rebuild @p menu as the Open with list for @p folder's selected
 *        files: candidates grouped by how well they match, the preferred
 *        one marked, then "Open with…" for the window.  Left empty when the
 *        selection holds no file with a type.
 */
void tiku_trk_launch_controller_build_menu(
    tiku_trk_launch_controller_t *controller, tiku_menu_t *menu,
    tiku_trk_folder_window_t *folder);
/**
 * @brief Open @p folder's selected files with Open with entry @p index.
 * @note The candidates are computed again from the selection: @p index
 *       must come from a menu built for the same selection.
 */
void tiku_trk_launch_controller_pick(
    tiku_trk_launch_controller_t *controller,
    tiku_trk_folder_window_t *folder, int index);

#endif /* TIKU_TRK_LAUNCH_CONTROLLER_H_ */
