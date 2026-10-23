/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drop_controller.h - carrying out a drop: on the Deskbar, back
 * into its source view as a rearrangement, onto an application, or as a
 * copy, move or link into a folder.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DROP_CONTROLLER_H_
#define TIKU_TRK_DROP_CONTROLLER_H_

#include "tiku_fs.h"
#include "tiku_trk_alert.h"
#include "tiku_trk_color.h"
#include "tiku_trk_cwmenu.h"
#include "tiku_trk_deskbar.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_folder_window.h"
#include <stddef.h>

/** @brief Set the desktop colour a colour drop carried. */
typedef void (*tiku_trk_drop_color_fn)(void *context,
                                       const tiku_trk_color_t *color);

struct tiku_trk_apps;
struct tiku_trk_app;
/** @brief Launch @p application with the @p count documents in @p paths. */
typedef int (*tiku_trk_drop_launch_fn)(void *context,
    const struct tiku_trk_app *application, const char *const *paths,
    int count);

/**
 * @brief The shell's parts a drop acts through, and the drag the
 *        secondary-button drop menu acts on.
 */
typedef struct {
    tiku_trk_deskbar_t *deskbar;
    tiku_backend_t *backend;
    tiku_fs_t *fs;
    tiku_trk_desktop_view_t *desktop_view;
    tiku_trk_cwmenu_t *menus;
    tiku_alert_t *alert;
    tiku_trk_drag_t *drag;               /**< the drag being dropped     */
    tiku_trk_folder_window_t **drop_folder; /**< folder under the pointer */
    int *drop_row;              /**< pose index under the pointer, or -1 */
    tiku_trk_drag_t pending;             /**< drag kept for the drop menu */
    char destination[TIKU_PATH_MAX];     /**< where that drag goes       */
    tiku_menu_t **popup;                 /**< the shell's open popup     */
    tiku_menu_t **popup_sub;             /**< its open submenu           */
    tiku_rect_t *popup_frame;            /**< the popup's screen frame   */
    char *progress_destination; /**< the progress window's "To:" folder */
    size_t progress_destination_size;
    const tiku_trk_services_t *services; /**< status lines and frames    */
    tiku_trk_drop_color_fn set_color;    /**< for colour drops           */
    void *color_context;
    /* A drop on an application pose launches it (PVS-044): the services
     * say which poses are applications, and the shell supplies the
     * launch. */
    tiku_trk_drop_launch_fn launch;
    void *launch_context;
    int screen_w;                        /**< bounds for the drop menu   */
    int screen_h;
} tiku_trk_drop_controller_t;

/**
 * @brief Bind the controller to the shell's parts it acts through.  All
 *        of them are borrowed for the shell's lifetime; nothing is owned.
 * @note Clears any launcher: install one afterwards with
 *       tiku_trk_drop_controller_set_launcher().
 */
void tiku_trk_drop_controller_init(tiku_trk_drop_controller_t *controller,
    tiku_trk_deskbar_t *deskbar, tiku_backend_t *backend,
    tiku_fs_t *fs, tiku_trk_desktop_view_t *desktop_view,
    tiku_trk_cwmenu_t *menus, tiku_alert_t *alert,
    tiku_trk_drag_t *drag, tiku_trk_folder_window_t **drop_folder,
    int *drop_row, tiku_menu_t **popup, tiku_menu_t **popup_sub,
    tiku_rect_t *popup_frame, char *progress_destination,
    size_t progress_destination_size, const tiku_trk_services_t *services,
    int screen_w, int screen_h,
    tiku_trk_drop_color_fn set_color, void *color_context);
/**
 * @brief Perform the drop at @p x, @p y with the drag in hand: the Deskbar
 *        first, then a rearrangement within the source view, else
 *        tiku_trk_drop_controller_execute_into() on the pose or folder
 *        under the pointer.
 */
void tiku_trk_drop_controller_execute(tiku_trk_drop_controller_t *controller,
                                      int x, int y, unsigned modifiers,
                                      unsigned button);

/**
 * @brief The drop's acts against an explicit destination (PVS-051).
 *
 * A drop on a file launches it when it is an application; otherwise the
 * drag is copied, moved or linked into @p destination, by the modifiers
 * and refusal rules, and volumes dropped on the Trash are unmounted.  A
 * spring menu's chosen entry calls this with @p in_view zero, which skips
 * what only applies under the pointer: colour payloads, icon landings and
 * the secondary-button menu.
 */
void tiku_trk_drop_controller_execute_into(
    tiku_trk_drop_controller_t *controller, const char *destination, int x,
    int y, unsigned modifiers, unsigned button, int in_view);

/** @brief Install the shell's launch for drops on application poses
 *         (PVS-044). */
void tiku_trk_drop_controller_set_launcher(
    tiku_trk_drop_controller_t *controller, tiku_trk_drop_launch_fn launch,
    void *launch_context);

#endif /* TIKU_TRK_DROP_CONTROLLER_H_ */
