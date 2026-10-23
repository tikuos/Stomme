/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_switcher_window.h - the application switcher: the running
 * applications in a row, the chosen one highlighted and named.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SWITCHER_WINDOW_H_
#define TIKU_TRK_SWITCHER_WINDOW_H_

#include "tiku_trk_deskbar.h"
#include "tiku_trk_switcher.h"
#include "tiku_window.h"
#include <stddef.h>
#include <stdint.h>

/** @brief The switcher window over the Deskbar's teams. */
typedef struct {
    tiku_trk_switcher_t    model;
    tiku_workspace_t *workspace;
    tiku_window_t    *window;
    tiku_trk_deskbar_t    *deskbar;
    int                   *dirty;
    char                  *status;
    size_t                 status_size;
    int                    screen_w;
    int                    screen_h;
} tiku_trk_switcher_window_t;

/**
 * @brief Bind the switcher to its workspace and the bar it lists, and to
 *        the status line the choice is announced in.
 */
void tiku_trk_switcher_window_init(tiku_trk_switcher_window_t *controller,
    tiku_workspace_t *workspace, tiku_trk_deskbar_t *deskbar,
    int screen_w, int screen_h, int *dirty, char *status,
    size_t status_size);

/** @brief Whether the switcher is up. */
int tiku_trk_switcher_window_shown(
    const tiku_trk_switcher_window_t *controller);
/**
 * @brief Bring the switcher up over the bar's teams, or step it once more
 *        while it is up, @p backwards with Shift.
 */
void tiku_trk_switcher_window_open(tiku_trk_switcher_window_t *controller,
                                   int backwards);
/** @brief A key while the switcher is up: the model says what it means. */
tiku_trk_sw_act_t tiku_trk_switcher_window_key(
    tiku_trk_switcher_window_t *controller, const char *key, int shift);
/** @brief The modifier let go: the model says whether that commits. */
tiku_trk_sw_act_t tiku_trk_switcher_window_release(
    tiku_trk_switcher_window_t *controller, int64_t now);
/** @brief Take the switcher down; when @p commit, the choice is announced. */
void tiku_trk_switcher_window_close(tiku_trk_switcher_window_t *controller,
                                    int commit);
/** @brief The chosen application's name, or NULL when none is. */
const char *tiku_trk_switcher_window_selected(
    const tiku_trk_switcher_window_t *controller);

#endif /* TIKU_TRK_SWITCHER_WINDOW_H_ */
