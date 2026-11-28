/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_prefs_window.h - the Desktop preferences window: the
 * wallpaper gallery, placement and desktop icon size.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKTOP_PREFS_WINDOW_H_
#define TIKU_TRK_DESKTOP_PREFS_WINDOW_H_

#include "tiku_press.h"
#include "tiku_trk_deskprefs.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_image.h"
#include "tiku_window.h"

#define TIKU_TRK_DESK_PREFS_W 430 /**< the window's frame width */

/** @brief The Desktop preferences window and the record it edits. */
typedef struct {
    tiku_trk_deskprefs_t model;
    tiku_trk_deskprefs_pictures_t pictures; /**< the pictures offered */
    tiku_workspace_t *workspace;
    tiku_window_t *window; /**< while open, else NULL */
    tiku_trk_desktop_view_t *desktop_view;
    tiku_store_t *store;
    tiku_image_t
        previews[5]; /**< four stock thumbnails and the custom picture */
    char custom_path[TIKU_PATH_MAX];
    void *pick_context;
    void (*pick_file)(void *);
    int64_t custom_click;
    int editing_size;
    int replace_size;
    char digits[4];
    const char *notice_key;
    void *save_context;         /**< context for native persistence callbacks */
    int (*submit_save)(void *); /**< enqueue background and icon proposals */
    const char *(*save_status)(void *); /**< pending or failed save feedback */
    int screen_w;
    int screen_h;
    int *dirty;               /**< set when a redraw is owed */
    char node[TIKU_PATH_MAX]; /**< Desktop folder */
    /** Product directory containing stock wallpapers. */
    char pictures_dir[TIKU_PATH_MAX];
    tiku_press_t press;
} tiku_trk_desktop_prefs_window_t;

/**
 * @brief Bind the controller to the Desktop view, store and workspace and
 *        find the Desktop's own folder.  Opens no window.
 */
void tiku_trk_desktop_prefs_window_init(
    tiku_trk_desktop_prefs_window_t *controller, tiku_workspace_t *workspace,
    tiku_trk_desktop_view_t *desktop_view, tiku_store_t *store, int screen_w,
    int screen_h, int *dirty);
/** @brief Apply the image returned by the desktop file panel. */
void tiku_trk_desktop_prefs_window_chosen(
    tiku_trk_desktop_prefs_window_t *controller, const char *path);
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
