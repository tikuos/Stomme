/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_select.h - the Select window's state.
 *
 * The window is a persistent thing, not a dialog: closing it hides it, so
 * the expression, the rule and the checkboxes are still there next time.
 *
 * Only the state and the geometry live here.  Drawing belongs to whichever
 * shell shows the window, so these rules run without a screen.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SELECT_H_
#define TIKU_TRK_SELECT_H_

#include "tiku_field.h"
#include "tiku_trk_match.h"

/** @brief How many rules the popup offers. */
#define TIKU_TRK_SELECT_RULES 5

/** @brief How far the placed window stays from each screen edge, in pixels
 *         (MoveCloseToMouse). */
#define TIKU_TRK_SELECT_MARGIN 20

/** @brief The Select window's state, kept while it is hidden. */
typedef struct {
    tiku_field_t  expr;
    int              rule;          /* index into the rule table          */
    int              ignore_case;
    int              invert;
    int              open;          /* shown now                          */
    int              ever;          /* has been opened at least once      */
    int              x, y;          /* where it was last placed           */
} tiku_trk_select_t;

/**
 * @brief Set the defaults: wildcard, ignore case on, invert off.
 *
 * A wildcard with no metacharacters is an exact match, so a typed name
 * selects that name.
 */
void tiku_trk_select_init(tiku_trk_select_t *w);

/** @brief The rule at index @p i, for the popup and for matching. */
tiku_trk_match_t tiku_trk_select_rule(int i);

/** @brief The label of rule @p i, for the popup; "" past the end. */
const char *tiku_trk_select_rule_label(int i);

/**
 * @brief Show it centred on the pointer, keeping everything it held.
 *
 * @param mouse_x,mouse_y Where the pointer is.
 * @param w,h             The window's size.
 * @param screen_w,screen_h The area it must stay inside.
 */
void tiku_trk_select_show(tiku_trk_select_t *sw, int mouse_x, int mouse_y,
                          int w, int h, int screen_w, int screen_h);

/** @brief Hide it; nothing it holds is discarded. */
void tiku_trk_select_hide(tiku_trk_select_t *sw);

/**
 * @brief Centre a window of @p w by @p h on the pointer, clamped on screen.
 *
 * Clamped, so a pointer near an edge still leaves the window's controls on
 * screen.
 */
void tiku_trk_select_place(int mouse_x, int mouse_y, int w, int h,
                           int screen_w, int screen_h, int *out_x,
                           int *out_y);

/** @brief Set the rule to index @p i; an index out of range is ignored. */
void tiku_trk_select_set_rule(tiku_trk_select_t *sw, int i);

#endif /* TIKU_TRK_SELECT_H_ */
