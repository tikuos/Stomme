/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_choice.h - a popup choice: a bounded list of options, one of them
 * selected, picked from a popup under or over the control.
 *
 * Behaviour only, so it builds for a board without a display;
 * tiku_choice_paint.h draws it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CHOICE_H_
#define TIKU_CHOICE_H_
#include "tiku_event.h"
#include "tiku_input.h"
#include "tiku_geom.h"
/** @brief The most options a choice holds. */
#define TIKU_CHOICE_MAX 32
/** @brief Room for an option's label, its terminator included. */
#define TIKU_CHOICE_LABEL 64
/** @brief One option: its label, and whether it can be chosen. */
typedef struct {
    char label[TIKU_CHOICE_LABEL];
    int enabled;
} tiku_choice_item_t;
/**
 * @brief A choice control's state.
 *
 * @c selected is the chosen option and @c pressed the popup row a press
 * went down on, -1 for none; @c hot is the row the popup highlights and
 * @c top the first row it shows; @c open is nonzero while the popup is up
 * and @c enabled while the control takes input.
 */
typedef struct {
    tiku_choice_item_t item[TIKU_CHOICE_MAX];
    int count, selected, hot, top, open, enabled, pressed;
} tiku_choice_t;
/** @brief What an event did: nothing, something, or changed the
 *         selection. */
typedef enum { TIKU_CHOICE_IGNORED, TIKU_CHOICE_HANDLED,
               TIKU_CHOICE_CHANGED } tiku_choice_result_t;
/** @brief Empty @p c: no options, none selected, the popup closed and the
 *         control enabled. */
void tiku_choice_init(tiku_choice_t *c);
/** @brief Append an option; the first enabled one becomes the selection
 *         while there is none.
 *  @return its index, or -1 when the list is full or the label does not
 *          fit (it is never cut short). */
int tiku_choice_add(tiku_choice_t *c, const char *label, int enabled);
/** @brief Select option @p index and close any popup.
 *  @return 1, or 0 when @p index is out of range or disabled. */
int tiku_choice_select(tiku_choice_t *c, int index);
/** @brief Close the popup and drop any press, keeping the selection. */
void tiku_choice_close(tiku_choice_t *c);
/** @brief Where the popup goes: under @p anchor when it fits there or
 *         there is more room below than above, else over it; at most
 *         eight rows tall, and inside @p screen. */
tiku_rect_t tiku_choice_popup(const tiku_choice_t *c, tiku_rect_t anchor,
                              tiku_rect_t screen);
/**
 * @brief Feed @p ev to the choice at @p anchor.
 *
 * Keys count while @p focused or the popup is open: space or Return opens
 * the popup and accepts the hot row, the arrows, Home and End move it,
 * Escape or Tab closes it.  Input only changes the selection; acting on
 * it is the caller's, when the result is TIKU_CHOICE_CHANGED.
 */
tiku_choice_result_t tiku_choice_event(tiku_choice_t *c,
    const tiku_input_event_t *ev, tiku_rect_t anchor, tiku_rect_t screen, int focused);
#endif
