/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_app_fileops.c - what follows a file operation, and the file
 * work left at exit.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_app_private.h"

/**
 * @brief Let queued file work finish before the process exits, for about
 *        five seconds at most.
 *
 * Work already done stays done: the engine has no transaction above one
 * item.
 */
void
browser_drain_fs(tiku_fs_t *fs)
{
    int guard = 0;

    if (fs == NULL) {
        return;
    }
    /* At most 50 turns of 100 ms. */
    while (tiku_fs_busy(fs) && guard++ < 50) {
        tiku_fs_moved_pump(fs);
        if (tiku_fs_step(fs, 16) == 0) {
            const tiku_progress_t *pr = tiku_fs_progress(fs);

            /* A question stops the queue and nobody is left to answer
             * it: a conflict is answered with Rename, an error with keep
             * going. */
            if (pr->conflict) {
                tiku_fs_resolve(fs, TIKU_CONFLICT_RENAME);
            } else if (pr->error_ask) {
                tiku_fs_resolve_error(fs, 1);
            } else {
                break;
            }
        }
        usleep(100000);
    }
    tiku_fs_moved_pump(fs);         /* what the last step moved */
}

/**
 * @brief After a file operation: reload the view, restore its state,
 *        select the row that took a deleted one's place when one was
 *        asked for (FS-050), settle the layout and mark the frame dirty.
 */
void
browser_after_op(app_t *a)
{
    (void)tiku_trk_poseview_reload(a->view);
    (void)tiku_trk_poseview_restore_state(a->view);
    if (a->select_next >= 0) {
        /* The row that has taken the deleted one's place, so a run of
         * deletes can be done with one key held down (FS-050). */
        (void)tiku_trk_poseview_select_after_removal(a->view, a->select_next);
        a->select_next = -1;
    }
    browser_settle_layout(a);
    a->dirty = 1;
}
