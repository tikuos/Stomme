/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_band_controller.h - the rubber band: arming a sweep on a press,
 * selecting what it covers, and scrolling the view while it is held at an
 * edge.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_BAND_CONTROLLER_H_
#define TIKU_TRK_BAND_CONTROLLER_H_

#include "tiku_trk_folder_window.h"
#include <stdint.h>

/** @brief Retained state for an armed or active selection sweep. */
typedef struct tiku_trk_band_controller {
    tiku_trk_folder_window_t *folder;       /**< the window being swept   */
    tiku_trk_folder_window_t *armed_folder; /**< pressed, not yet a band  */
    int x0;                     /**< the press point                      */
    int y0;
    int x1;                     /**< the pointer now                      */
    int y1;
    unsigned modifiers;         /**< held during the sweep                */
    int64_t scroll_at;          /**< when the band reached an edge, or 0  */
    /* Non-zero when a script drives the shell: the edge hold is counted in
     * loop pulses, so a script's timing does not depend on the machine's
     * load.  An interactive shell leaves it 0. */
    int script_delay_pulses;
    int held_pulses;            /**< pulses held at an edge, scripted     */
    int row_h;                  /**< a row's height: the icon-mode step   */
    int *dirty;                 /**< set when the screen changed          */
} tiku_trk_band_controller_t;

/**
 * @brief Count the edge hold in @p pulses instead of wall time; 0 goes back
 *        to wall time.
 */
void tiku_trk_band_controller_set_script_delay(
    tiku_trk_band_controller_t *controller, int pulses);

/** @brief A controller with the row height and the dirty flag it raises. */
void tiku_trk_band_controller_init(tiku_trk_band_controller_t *controller,
                                   int row_h, int *dirty);
/** @brief A press at @p x, @p y that may become a band in @p folder. */
void tiku_trk_band_controller_arm(tiku_trk_band_controller_t *controller,
    tiku_trk_folder_window_t *folder, int x, int y, unsigned modifiers);
/**
 * @brief The pointer moved: past the slop the press becomes a band, and a
 *        band selects what it covers.  @return 1 while a band is drawn.
 */
int tiku_trk_band_controller_motion(tiku_trk_band_controller_t *controller,
                                    int x, int y, unsigned modifiers);
/**
 * @brief The band ends, and the view's state is saved.  @return 1 when
 *        there was one.
 */
int tiku_trk_band_controller_finish(tiku_trk_band_controller_t *controller);
/**
 * @brief A tick while the band is held at an edge: the view scrolls and
 *        the band follows.  @return 1 when something moved.
 */
int tiku_trk_band_controller_pulse(tiku_trk_band_controller_t *controller,
                                   int64_t now);

#endif /* TIKU_TRK_BAND_CONTROLLER_H_ */
