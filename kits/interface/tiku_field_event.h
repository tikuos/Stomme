/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_field_event.h - a field driven by events: keys, pointer
 * selection, the clipboard and undo, over the tiku_field model.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FIELD_EVENT_H_
#define TIKU_FIELD_EVENT_H_
#include "tiku_field.h"
#include "tiku_event.h"
#include "tiku_input.h"
#include "tiku_geom.h"
#include "tiku_text_input.h"
/** @brief How many undo steps, and as many redo steps, a field keeps. */
#define TIKU_FIELD_HISTORY 4
/** @brief One undo step: the text, the caret and the selection. */
typedef struct {
    char text[TIKU_FIELD_MAX];
    int caret, a, b;
} tiku_field_snapshot_t;
/**
 * @brief A field's input state beside the model: undo and redo, a drag
 *        and its anchor, the scroll in pixels, and a text commit being
 *        assembled.
 */
typedef struct {
    tiku_field_snapshot_t undo[TIKU_FIELD_HISTORY], redo[TIKU_FIELD_HISTORY];
    int nundo, nredo, dragging, anchor, scroll;
    tiku_text_input_t commit;
} tiku_field_input_t;
/** @brief Reset history and pointer state after loading a different value. */
void tiku_field_input_init(tiku_field_input_t *input);
/**
 * @brief Feed @p ev to the field drawn in @p rect.
 *
 * Return and Escape are reported as TIKU_FIELD_COMMITTED and
 * TIKU_FIELD_CANCELLED without closing the model.  An insert, a paste or
 * an assembled text commit goes in whole, on code point boundaries, or
 * is refused (TIKU_FIELD_REJECTED) with the text and selection as they
 * were.
 * @note Route pointer moves and releases here while a drag is on, even
 *       outside the field.
 */
tiku_field_result_t tiku_field_event(tiku_field_t *field,
    tiku_field_input_t *input, const tiku_input_event_t *ev,
    tiku_rect_t rect, int focused);
#endif
