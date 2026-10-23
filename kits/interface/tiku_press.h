/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_press.h - a button held down under the pointer.
 *
 * What every pressable thing shares: a press arms it and draws it down, a
 * move draws it up when the pointer leaves and down again when it
 * returns, and only a release on it acts, so a release anywhere else
 * cancels the press.  tiku_dialog keeps this rule for a whole window of
 * controls; this is the same rule for a window that draws its own few
 * buttons.  It knows no rectangles: the window's own hit test says what
 * is under the pointer, and this remembers what was pressed.  It draws
 * nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_PRESS_H
#define TIKU_PRESS_H

/** @brief One press in flight: what was pressed, and whether the pointer
 *         is on it. */
typedef struct {
    int id;         /**< what was pressed, 0 when nothing is */
    int inside;     /**< the pointer is on it now            */
} tiku_press_t;

/**
 * @brief A press landed on @p id (0 for nothing pressable).
 * @return nonzero when a press was taken and a repaint is owed.
 */
int tiku_press_arm(tiku_press_t *p, int id);

/**
 * @brief The pointer moved; @p under is what the hit test finds there now.
 * @return nonzero when the look changed and a repaint is owed.
 */
int tiku_press_track(tiku_press_t *p, int under);

/**
 * @brief The button was released over @p under.  Disarms either way.
 * @return the id that acts, when released on what was pressed, or 0.
 */
int tiku_press_release(tiku_press_t *p, int under);

/** @brief Whether @p id is held down right now, for drawing it so. */
int tiku_press_held(const tiku_press_t *p, int id);

#endif /* TIKU_PRESS_H */
