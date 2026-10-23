/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_prefs_window.h - the Desktop preferences window: the
 * picture, how it is laid, the icon size and Reduce motion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKTOP_PREFS_WINDOW_H_
#define TIKU_TRK_DESKTOP_PREFS_WINDOW_H_

#include "tiku_press.h"
#include "tiku_trk_deskprefs.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_prefs.h"
#include "tiku_window.h"

#define TIKU_TRK_DESK_PREFS_W 430    /**< the window's frame width */

/** @brief The Desktop preferences window and the record it edits. */
typedef struct {
    tiku_trk_deskprefs_t          model;
    tiku_trk_deskprefs_pictures_t pictures;  /**< the pictures offered */
    tiku_workspace_t        *workspace;
    tiku_window_t           *window;         /**< while open, else NULL */
    tiku_trk_desktop_view_t      *desktop_view;
    tiku_store_t             *store;
    tiku_trk_prefs_t         *shared_prefs;   /**< where ReduceMotion lives */
    char                     shared_prefs_path[TIKU_PATH_MAX]; /**< its file */
    int                           screen_w;
    int                           screen_h;
    int                          *dirty;     /**< set when a redraw is owed */
    char                          node[TIKU_PATH_MAX];  /**< Desktop folder */
    /** The wallpapers folder beside the Desktop folder. */
    char                          pictures_dir[TIKU_PATH_MAX];
    tiku_press_t                  press; /**< 1 -, 2 +, 3 Defaults, 4 Revert */
} tiku_trk_desktop_prefs_window_t;

/**
 * @brief Bind the controller to the Desktop view, store and workspace and
 *        find the Desktop's own folder.  Opens no window.
 */
void tiku_trk_desktop_prefs_window_init(
    tiku_trk_desktop_prefs_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_desktop_view_t *desktop_view,
    tiku_store_t *store, int screen_w, int screen_h, int *dirty);
/**
 * @brief Give the window the settings Reduce motion lives in and the file
 *        they are saved to; until then its tick box is greyed.
 */
void tiku_trk_desktop_prefs_window_bind_settings(
    tiku_trk_desktop_prefs_window_t *controller,
    tiku_trk_prefs_t *prefs, const char *path);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_desktop_prefs_window_destroy(
    tiku_trk_desktop_prefs_window_t *controller);
/**
 * @brief Open the window centred across the screen in its upper third,
 *        reading the Desktop's record and pictures afresh; an open window
 *        is activated instead.
 */
void tiku_trk_desktop_prefs_window_open(
    tiku_trk_desktop_prefs_window_t *controller);
/** @brief Close the window and ask for a redraw. */
void tiku_trk_desktop_prefs_window_close(
    tiku_trk_desktop_prefs_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_desktop_prefs_window_handle(
    tiku_trk_desktop_prefs_window_t *controller);

#endif /* TIKU_TRK_DESKTOP_PREFS_WINDOW_H_ */
