/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs_window.h - the Deskbar preferences window.
 *
 * A row per setting, a monitor that picks the dock, and the Defaults,
 * Revert and Edit in TikuTracker buttons.  A change to a row is applied to
 * the bar and saved at once.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PREFS_WINDOW_H_
#define TIKU_TRK_DESKBAR_PREFS_WINDOW_H_

#include "tiku_press.h"
#include "tiku_trk_deskbar_prefs.h"
#include "tiku_window.h"

#define TIKU_TRK_DB_PREFS_W 470    /**< the window's frame width */
#define TIKU_TRK_DB_PREFS_H 580    /**< the window's frame height */

/** @brief Opens @p path, the menu folder, in a Tracker window. */
typedef void (*tiku_trk_deskbar_prefs_open_fn)(void *context,
                                                const char *path);

/** @brief The Deskbar preferences window and the settings it edits. */
typedef struct {
    tiku_trk_deskbar_prefs_t model;   /**< the value, and as opened */
    tiku_workspace_t *workspace;
    tiku_window_t *window;            /**< while open, else NULL */
    tiku_trk_deskbar_t *deskbar;      /**< the bar the settings apply to */
    tiku_store_t *store;              /**< the settings and the frame */
    int screen_w;
    int screen_h;
    int *dirty;                       /**< set when a redraw is owed */
    void *save_context;
    int (*submit_save)(void *);
    const char *(*save_status)(void *);
    char menu_dir[TIKU_PATH_MAX];     /**< the Deskbar's menu folder */
    tiku_trk_deskbar_prefs_open_fn open_path;  /**< opens menu_dir */
    void *open_context;
    tiku_press_t press;     /**< 1 Defaults, 2 Revert, 3 the menu folder */
} tiku_trk_deskbar_prefs_window_t;

/**
 * @brief Bind the controller to its bar, store and workspace, load the
 *        saved settings and apply them to the bar.  Opens no window.
 */
void tiku_trk_deskbar_prefs_window_init(
    tiku_trk_deskbar_prefs_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_store_t *store, int screen_w, int screen_h, const char *menu_dir,
    int *dirty, tiku_trk_deskbar_prefs_open_fn open_path,
    void *open_context);
/** @brief Close the window if open; nothing else is held. */
void tiku_trk_deskbar_prefs_window_destroy(
    tiku_trk_deskbar_prefs_window_t *controller);
/**
 * @brief Open the window at its remembered frame, reloading the settings
 *        first; an open window is activated instead.
 */
void tiku_trk_deskbar_prefs_window_open(
    tiku_trk_deskbar_prefs_window_t *controller);
/** @brief Close the window, remembering its position in the session. */
void tiku_trk_deskbar_prefs_window_close(
    tiku_trk_deskbar_prefs_window_t *controller);
/** @brief The window while it is open, else NULL. */
tiku_window_t *tiku_trk_deskbar_prefs_window_handle(
    tiku_trk_deskbar_prefs_window_t *controller);
/**
 * @brief A key while the window is open: a dock key moves the bar.
 *        @return 1 when the key was one.
 */
int tiku_trk_deskbar_prefs_window_key(
    tiku_trk_deskbar_prefs_window_t *controller, const char *key);

#endif /* TIKU_TRK_DESKBAR_PREFS_WINDOW_H_ */
