/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_status_overlay.h - the alert and the file-job progress panel
 * drawn over everything else, and the pointer and keys they take while
 * they are up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_STATUS_OVERLAY_H_
#define TIKU_TRK_STATUS_OVERLAY_H_

#include "tiku_event.h"
#include "tiku_gfx.h"
#include "tiku_trk_alert.h"
#include <stdint.h>

/** @brief Take the alert's answer: @p alert->chosen is the button. */
typedef void (*tiku_trk_status_alert_fn)(void *context,
                                         const tiku_alert_t *alert);
/** @brief The progress panel's stop button was pressed. */
typedef void (*tiku_trk_status_stop_fn)(void *context);

/** @brief The alert, the progress panel, and where their answers go. */
typedef struct {
    tiku_alert_t          alert;
    tiku_trk_progress_win_t   progress;
    tiku_surface_t      *surface;
    int                       screen_w;
    int                       screen_h;
    int                      *dirty;   /**< set to 1 to ask for a redraw */
    int64_t                   progress_opened; /**< when it opened, or 0 */
    tiku_trk_status_alert_fn  answer;
    tiku_trk_status_stop_fn   stop;
    void                     *callback_context;
} tiku_trk_status_overlay_t;

/**
 * @brief Set up the overlay over @p surface: alert answers go to
 *        @p answer and the stop button to @p stop, both with
 *        @p callback_context.  Nothing is owned.
 */
void tiku_trk_status_overlay_init(tiku_trk_status_overlay_t *controller,
    tiku_surface_t *surface, int screen_w, int screen_h, int *dirty,
    tiku_trk_status_alert_fn answer, tiku_trk_status_stop_fn stop,
    void *callback_context);

/** @brief The alert model, to open or read. */
tiku_alert_t *tiku_trk_status_overlay_alert(
    tiku_trk_status_overlay_t *controller);
/** @brief The progress model, to fill or read. */
tiku_trk_progress_win_t *tiku_trk_status_overlay_progress(
    tiku_trk_status_overlay_t *controller);

/**
 * @brief Record @p now as the time the progress panel opened, unless a
 *        time is already recorded.
 */
void tiku_trk_status_overlay_progress_start(
    tiku_trk_status_overlay_t *controller, int64_t now);
/** @brief Clear the recorded opening time, at the end of a run. */
void tiku_trk_status_overlay_progress_finish(
    tiku_trk_status_overlay_t *controller);
/** @brief The recorded opening time, or 0 when none is recorded. */
int64_t tiku_trk_status_overlay_progress_opened(
    const tiku_trk_status_overlay_t *controller);

/** @brief Draw the progress panel, then the alert above it. */
void tiku_trk_status_overlay_draw(tiku_trk_status_overlay_t *controller);

/**
 * @brief A press at @p x, @p y: an open alert takes every press and arms
 *        the button under it; otherwise the progress panel's pause and
 *        stop buttons act.  @return 1 when the press was taken.
 */
int tiku_trk_status_overlay_pointer(tiku_trk_status_overlay_t *controller,
                                    int x, int y);

/**
 * @brief The pointer moved: an open alert's pressed button shows down
 *        under the pointer and up off it.  @return 1 while an alert is
 *        open, 0 otherwise.
 */
int tiku_trk_status_overlay_pointer_move(tiku_trk_status_overlay_t *c,
                                         int x, int y);

/**
 * @brief The pointer was released: over an open alert's pressed button
 *        it answers with that button, anywhere else it cancels the press.
 *        @return 1 while an alert is open, 0 otherwise.
 */
int tiku_trk_status_overlay_pointer_up(tiku_trk_status_overlay_t *c,
                                       int x, int y);

/**
 * @brief A key while an alert is open: Escape answers with the cancel
 *        button, Return with the default button (else the last), and a
 *        letter with the first button whose label starts with it.
 *        @return 1 while an alert is open, 0 otherwise.
 */
int tiku_trk_status_overlay_key(tiku_trk_status_overlay_t *controller,
                                unsigned key);

#endif /* TIKU_TRK_STATUS_OVERLAY_H_ */
