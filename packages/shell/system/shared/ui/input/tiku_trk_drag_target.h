/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag_target.h - the target under a drag: the folder window and
 * row it is over, auto-scroll at a window's edge, and spring-opening a
 * folder the drag rests on.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DRAG_TARGET_H_
#define TIKU_TRK_DRAG_TARGET_H_

#include "tiku_trk_deskbar.h"
#include "tiku_trk_desktop_view.h"
#include "tiku_trk_drag.h"
#include "tiku_trk_springmenu.h"
#include <stdint.h>

/** @brief Open @p path, the folder a drag rested on long enough. */
typedef void (*tiku_trk_drag_open_fn)(void *context, const char *path);

/** @brief Dependencies and state for resolving the target under a drag. */
typedef struct tiku_trk_drag_target {
    tiku_workspace_t *workspace;            /**< borrowed                 */
    tiku_trk_deskbar_t *deskbar;            /**< borrowed                 */
    tiku_trk_desktop_view_t *desktop_view;  /**< borrowed                 */
    tiku_trk_drag_t *drag;                  /**< the drag in hand         */
    tiku_trk_folder_window_t *folders;      /**< the shell's windows      */
    int *folder_count;                      /**< folders[] in use         */
    int row_h;                  /**< a row's height: the icon-mode step   */
    int *dirty;                 /**< set when the screen changed          */
    tiku_trk_drag_open_fn open;             /**< opens a sprung folder    */
    void *open_context;                     /**< handed back to open      */
    tiku_trk_folder_window_t *folder;       /**< the target window, or NULL */
    int row;                    /**< the droppable row under it, or -1    */
    int64_t hover_at;           /**< when the current target was entered  */
    int64_t scroll_at;          /**< when the drag reached an edge, or 0  */
    int sprung;                 /**< the target's folder has been opened  */
    /* Non-zero when a script drives the shell: both holds here, the
     * spring's and the auto-scroll's, are then counted in pulses. */
    int spring_script_pulses;
    int spring_held;            /**< pulses held over the target          */
    int scroll_held;            /**< pulses spent in the edge band        */
    tiku_trk_springmenu_t spring;           /**< the spring hold          */
} tiku_trk_drag_target_t;

/**
 * @brief Count the spring and auto-scroll holds in @p pulses instead of
 *        wall time; 0 goes back to wall time.
 */
void tiku_trk_drag_target_set_script_delay(
    tiku_trk_drag_target_t *controller, int pulses);

/** @brief A target over the shell's windows; everything is borrowed. */
void tiku_trk_drag_target_init(tiku_trk_drag_target_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    tiku_trk_desktop_view_t *desktop_view, tiku_trk_drag_t *drag,
    tiku_trk_folder_window_t *folders, int *folder_count, int row_h,
    int *dirty, tiku_trk_drag_open_fn open, void *open_context);
/**
 * @brief The pointer moved with a drag in hand: the folder and row under
 *        it become the target, and a hold arms the spring.  @return 1 when
 *        the target changed.
 */
int tiku_trk_drag_target_motion(tiku_trk_drag_target_t *controller,
                                int x, int y, int64_t now);
/**
 * @brief A tick during a drag: auto-scroll at an edge, and the spring-open
 *        of a folder held over.  @return 1 when something changed.
 */
int tiku_trk_drag_target_pulse(tiku_trk_drag_target_t *controller,
                               int64_t now);
/** @brief No target, no spring, no drag on the Deskbar. */
void tiku_trk_drag_target_reset(tiku_trk_drag_target_t *controller);

#endif /* TIKU_TRK_DRAG_TARGET_H_ */
