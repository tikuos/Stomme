/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_folder_menu.h - the folder menus' controller: the enablement
 * context for a folder and its selection, and the Open with and per-type
 * submenus, rebuilt before a folder is painted.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FOLDER_MENU_H_
#define TIKU_TRK_FOLDER_MENU_H_

#include "tiku_fs.h"
#include "tiku_trk_clip.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_folder_edit.h"
#include "tiku_trk_folder_window.h"
#include "tiku_trk_launch_controller.h"
#include "tiku_trk_types.h"

/** @brief What the folder menus are rebuilt from; everything is borrowed. */
typedef struct {
    tiku_trk_cwmenu_t             *menus;   /**< the menus rebuilt         */
    tiku_trk_clip_t               *clip;    /**< the file clipboard        */
    tiku_backend_t            *backend;     /**< for stat                  */
    tiku_fs_t                 *fs;          /**< for the Trash             */
    tiku_trk_types_t              *types;   /**< the type database         */
    tiku_trk_launch_controller_t  *launcher;    /**< builds Open with      */
    tiku_trk_folder_window_t     **bar_folder;  /**< whose bar menu is open */
    int                           *bar_open;    /**< which menu, or -1     */
    tiku_trk_folder_edit_t        *editor;      /**< the inline editor     */
} tiku_trk_folder_menu_t;

/** @brief Bind live folder-menu models and their supporting services. */
void tiku_trk_folder_menu_init(tiku_trk_folder_menu_t *controller,
    tiku_trk_cwmenu_t *menus, tiku_trk_clip_t *clip,
    tiku_backend_t *backend, tiku_fs_t *fs,
    tiku_trk_types_t *types, tiku_trk_launch_controller_t *launcher,
    tiku_trk_folder_window_t **bar_folder, int *bar_open);

/** @brief Bind the focused inline editor for live text-menu enablement. */
void tiku_trk_folder_menu_set_editor(tiku_trk_folder_menu_t *controller,
                                     tiku_trk_folder_edit_t *editor);

/** @brief Describe which commands apply to a folder and its selection. */
tiku_trk_sc_ctx_t tiku_trk_folder_menu_context(
    tiku_trk_folder_menu_t *controller,
    tiku_trk_folder_window_t *folder, unsigned modifiers);
/** @brief Rebuild the shared Open-with submenu for one folder. */
void tiku_trk_folder_menu_build_open_with(
    tiku_trk_folder_menu_t *controller,
    tiku_trk_folder_window_t *folder);
/** @brief Rebuild per-type attribute submenus for one folder. */
void tiku_trk_folder_menu_build_per_type(
    tiku_trk_folder_menu_t *controller,
    tiku_trk_folder_window_t *folder);
/** @brief Prepare the menu model immediately before a folder is painted. */
void tiku_trk_folder_menu_prepare(void *context,
                                  tiku_trk_folder_window_t *folder);

#endif /* TIKU_TRK_FOLDER_MENU_H_ */
