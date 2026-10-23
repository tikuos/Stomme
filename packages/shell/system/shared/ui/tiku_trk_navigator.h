/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_navigator.h - a browser window's Back and Forward history,
 * bounded at TIKU_TRK_NAV_HISTORY_MAX locations each way, and the path
 * helpers for a folder's parents and a drop's destination.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_NAVIGATOR_H_
#define TIKU_TRK_NAVIGATOR_H_

#include "tiku_model.h"
#include <stddef.h>

/** @brief How many locations each direction keeps; the oldest go first. */
#define TIKU_TRK_NAV_HISTORY_MAX 32

/** @brief The history: the back stack ends with the current location. */
typedef struct {
    char back[TIKU_TRK_NAV_HISTORY_MAX][TIKU_PATH_MAX];    /**< oldest first */
    char forward[TIKU_TRK_NAV_HISTORY_MAX][TIKU_PATH_MAX]; /**< next last    */
    int  nback;                 /**< back[] in use                        */
    int  nforward;              /**< forward[] in use                     */
} tiku_trk_navigator_t;

/** @brief Start a history whose only location is @p path. */
void tiku_trk_navigator_init(tiku_trk_navigator_t *n, const char *path);

/** @brief The location the history is currently showing. */
const char *tiku_trk_navigator_current(const tiku_trk_navigator_t *n);

/**
 * @brief Record a newly visited location and discard the forward branch;
 *        the current location again, or an empty one, is ignored.
 */
void tiku_trk_navigator_set(tiku_trk_navigator_t *n, const char *path);

/** @brief Whether Back has a location to go to. */
int tiku_trk_navigator_can_back(const tiku_trk_navigator_t *n);
/** @brief Whether Forward has a location to go to. */
int tiku_trk_navigator_can_forward(const tiku_trk_navigator_t *n);

/**
 * @brief Step back: the current location moves to the forward history, and
 *        the previous one, copied to @p out, becomes current.
 * @return 1 when there was one to go to.
 */
int tiku_trk_navigator_back(tiku_trk_navigator_t *n, char *out, size_t max);
/**
 * @brief Step forward to the location Back last left, copied to @p out,
 *        which becomes current.  @return 1 when there was one to go to.
 */
int tiku_trk_navigator_forward(tiku_trk_navigator_t *n, char *out,
                               size_t max);

/**
 * @brief The folder enclosing @p path, into @p out; trailing slashes are
 *        ignored.  @return 0 for the root, else 1.
 */
int tiku_trk_navigator_parent(const char *path, char *out, size_t max);

/**
 * @brief Fill @p out with the folders enclosing @p path, nearest first and
 *        ending at the root.  @return how many were written.
 */
int tiku_trk_navigator_hierarchy(const char *path,
                                 char out[][TIKU_PATH_MAX], int max);

/**
 * @brief The folder a drop onto @p model lands in: a container itself, or
 *        a leaf's parent.  @return 0 when there is none.
 */
int tiku_trk_navigator_drop_path(const tiku_model_t *model, char *out,
                                 size_t max);

#endif /* TIKU_TRK_NAVIGATOR_H_ */
