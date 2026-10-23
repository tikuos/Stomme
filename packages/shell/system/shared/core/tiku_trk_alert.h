/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_alert.h - what Tracker's alerts mean, and the progress a running
 * job reports.
 *
 * The alert itself (state, geometry, keys and look) is the Interface kit's,
 * in tiku_alert.h.  This file builds the alert a collision or a failed item
 * asks, maps the chosen button to the engine's answer, and keeps the rows of
 * the shared progress window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ALERT_H_
#define TIKU_TRK_ALERT_H_

#include "tiku_alert.h"
#include "tiku_fs.h"

/** @brief Which question an alert asks; the shell routes the answer by it. */
#define TIKU_TRK_ALERT_TAG_NONE     0
#define TIKU_TRK_ALERT_TAG_CONFLICT 1
#define TIKU_TRK_ALERT_TAG_ERROR    2
#define TIKU_TRK_ALERT_TAG_REFUSAL  3
#define TIKU_TRK_ALERT_TAG_DELETE   4
#define TIKU_TRK_ALERT_TAG_BROKEN_OPEN 5
#define TIKU_TRK_ALERT_TAG_OPEN_ANYWAY 6
#define TIKU_TRK_ALERT_TAG_SHUTDOWN 7
#define TIKU_TRK_ALERT_TAG_SAVE_REPLACE 8   /* a save would overwrite */
#define TIKU_TRK_ALERT_TAG_EMPTY_TRASH 9    /* asks before emptying   */

/**
 * @brief Build the alert a collision asks, from the progress record.
 *
 * A lone file-versus-file collision gets two buttons; a folder collision, or
 * any collision while more than one is pending, gets the four-way form.  The
 * engine makes that choice and records it in conflict_many (FS-018, FS-019).
 */
void tiku_trk_alert_for_conflict(tiku_alert_t *a,
                                 const tiku_progress_t *pr);

/**
 * @brief Build the alert a failed item asks (FS-035).
 *
 * It offers Cancel and OK when the engine can go on past the item, and
 * Cancel alone when it cannot.
 */
void tiku_trk_alert_for_error(tiku_alert_t *a,
                              const tiku_progress_t *pr);

/**
 * @brief Map the button chosen on a collision alert to the engine's answer.
 *
 * @return the conflict answer to hand to tiku_fs_resolve(), or
 *         TIKU_CONFLICT_ASK when nothing has been chosen.
 * @note Cancel is the caller's to handle, with tiku_fs_cancel(); this maps
 *       it to a skip.
 */
tiku_conflict_t tiku_trk_alert_conflict_answer(const tiku_alert_t *a);

/*---------------------------------------------------------------------------*/
/* The progress window                                                        */
/*---------------------------------------------------------------------------*/

/** @brief A link job shows a window only with more items than this (FS-070). */
#define TIKU_TRK_PROGRESS_LINK_MIN 10

/** @brief How long the status line shows speed, then the estimate. */
#define TIKU_TRK_PROGRESS_SPEED_US    8000000
#define TIKU_TRK_PROGRESS_ESTIMATE_US 4000000

/** @brief How many samples the speed is measured across. */
#define TIKU_TRK_PROGRESS_SLOTS 4

/** @brief How many jobs the shared window can show at once. */
#define TIKU_TRK_PROGRESS_ROWS 4

/** @brief One row of the shared status window. */
typedef struct {
    int   used;
    char  caption[48];              /* "Copying: ", "Moving: " ...        */
    char  current[TIKU_PATH_MAX];   /* the item being worked on           */
    char  dest[TIKU_PATH_MAX];
    int   done, total;              /* items handled so far, and in all   */
    int   paused;
    /* The last byte counts and when each was taken, newest first; the
     * speed is measured across all of them. */
    int64_t bytes[TIKU_TRK_PROGRESS_SLOTS];
    int64_t at[TIKU_TRK_PROGRESS_SLOTS];
    int     nsample;
    int64_t total_bytes;            /* bytes written so far, latest sample */
} tiku_trk_progress_row_t;

/** @brief The shared status window: its rows, and whether it is open. */
typedef struct {
    tiku_trk_progress_row_t row[TIKU_TRK_PROGRESS_ROWS];
    int                     open;
} tiku_trk_progress_win_t;

/** @brief Clear a progress window's state to nothing in flight. */
void tiku_trk_progress_reset(tiku_trk_progress_win_t *p);

/**
 * @brief The window's jobs as one line of text; the shells answer the
 *        "operation" query with it.
 *
 * "idle" when nothing runs; otherwise each row as its caption, the leaf of
 * the current item and "done of total", each ended by '|'.
 *
 * @return the line's length as snprintf counts it; @p max or more means the
 *         line was cut.
 */
size_t tiku_trk_progress_say(const tiku_trk_progress_win_t *p,
                             char *out, size_t max);

/**
 * @brief Whether a job of @p op over @p items should show a window at all.
 */
int tiku_trk_progress_wanted(tiku_op_t op, int items);

/**
 * @brief Record that row @p i has written @p bytes in all, at @p now.
 */
void tiku_trk_progress_sample(tiku_trk_progress_win_t *p, int i,
                              int64_t bytes, int64_t now);

/** @brief Bytes per second across the samples.  0 when unknown. */
int64_t tiku_trk_progress_speed(const tiku_trk_progress_win_t *p, int i);

/**
 * @brief Seconds still to go, or -1 when it cannot be estimated.
 *
 * The bytes per item so far, times the items left, over the speed.  A job
 * with fewer than two samples or no finished item has no estimate.
 */
int64_t tiku_trk_progress_eta(const tiku_trk_progress_win_t *p, int i);

/**
 * @brief Whether the status line should show speed rather than the estimate.
 *
 * TIKU_TRK_PROGRESS_SPEED_US of speed, then TIKU_TRK_PROGRESS_ESTIMATE_US of
 * the estimate, on one cycle counted from @p since_open, so every row
 * changes at the same moment (FS-071).
 */
int tiku_trk_progress_show_speed(int64_t since_open);

/** @brief The caption a job of kind @p op shows. */
const char *tiku_trk_progress_caption(tiku_op_t op);

/** @brief What a click on a progress row landed on. */
typedef enum {
    TIKU_TRK_PROGRESS_HIT_NONE = 0,
    TIKU_TRK_PROGRESS_HIT_PAUSE,
    TIKU_TRK_PROGRESS_HIT_STOP
} tiku_trk_progress_hit_t;

/**
 * @brief The pause or stop button of row @p i, in window coordinates.
 *
 * @param which TIKU_TRK_PROGRESS_HIT_PAUSE or TIKU_TRK_PROGRESS_HIT_STOP.
 * @param w     The window's width.
 */
void tiku_trk_progress_button_rect(int i, int which, int w, int *bx, int *by,
                                   int *bw, int *bh);

/**
 * @brief What a point hits inside a @p w wide progress window.
 *
 * @param row Receives which row was hit.
 */
tiku_trk_progress_hit_t tiku_trk_progress_hit(const tiku_trk_progress_win_t *p,
                                              int w, int x, int y, int *row);

/**
 * @brief The text a row shows: its current item, or for a paused row how to
 *        resume or stop it.
 */
const char *tiku_trk_progress_row_text(const tiku_trk_progress_win_t *p,
                                       int i);

/**
 * @brief Take the engine's state into row @p i, or clear the row when
 *        @p busy is 0.
 *
 * The window is open while any row is in use, so it opens with the first
 * job and closes when the last one ends (FS-072).
 */
void tiku_trk_progress_update(tiku_trk_progress_win_t *p, int i,
                              const tiku_progress_t *pr, int busy,
                              const char *dest);

#endif /* TIKU_TRK_ALERT_H_ */
