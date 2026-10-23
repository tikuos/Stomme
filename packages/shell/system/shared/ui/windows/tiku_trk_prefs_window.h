/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs_window.h - the Tracker preferences window: the list of
 * pages, the settings of the page shown, and Defaults and Revert.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PREFS_WINDOW_H_
#define TIKU_TRK_PREFS_WINDOW_H_

#include "tiku_model.h"
#include "tiku_press.h"
#include "tiku_trk_prefs.h"
#include "tiku_window.h"

/** @brief Preferences state together with the one window that edits it. */
typedef struct {
    tiku_trk_prefs_t       model;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    tiku_trk_page_t        page;       /**< the page shown */
    char                   path[TIKU_PATH_MAX];  /**< the settings file */
    int                   *dirty;
    const int             *blocked;    /**< set: no pointer events taken */
    int                    screen_w;
    int                    screen_h;
    tiku_press_t           press;      /* 1 Defaults, 2 Revert, while held */
} tiku_trk_prefs_window_t;

/**
 * @brief Bind the controller to its workspace and the settings file, which
 *        it loads, and to @p blocked, under which the window takes no
 *        pointer events.  Opens no window.
 */
void tiku_trk_prefs_window_init(tiku_trk_prefs_window_t *controller,
    tiku_workspace_t *workspace, int screen_w, int screen_h,
    const char *settings_path, int *dirty, const int *blocked);
/** @brief Close the window if open and free the preferences model. */
void tiku_trk_prefs_window_destroy(tiku_trk_prefs_window_t *controller);

/** @brief The shared preferences model read by the rest of Tracker. */
tiku_trk_prefs_t *tiku_trk_prefs_window_model(
    tiku_trk_prefs_window_t *controller);

/** @brief The window, or NULL before it is opened and after it is
 *         closed. */
tiku_window_t *tiku_trk_prefs_window_handle(
    tiku_trk_prefs_window_t *controller);

/** @brief Open the preferences window or activate its existing instance. */
void tiku_trk_prefs_window_open(tiku_trk_prefs_window_t *controller);
/** @brief Close the window, if open. */
void tiku_trk_prefs_window_close(tiku_trk_prefs_window_t *controller);

#endif /* TIKU_TRK_PREFS_WINDOW_H_ */
