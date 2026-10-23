/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_input.h - the interface's input event record and its types,
 * apart from the kernel's byte-sized event ids.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_INPUT_H_
#define TIKU_INPUT_H_
#include <stdint.h>

/** @brief What an input event is. */
typedef enum {
    TIKU_EVENT_NONE = 0,
    TIKU_EVENT_EXPOSE,
    TIKU_EVENT_CLOSE,
    TIKU_EVENT_RESIZE,
    TIKU_EVENT_KEY_DOWN,
    TIKU_EVENT_KEY_UP,
    TIKU_EVENT_POINTER_DOWN,
    TIKU_EVENT_POINTER_UP,
    TIKU_EVENT_POINTER_MOVE,
    TIKU_EVENT_WHEEL,
    TIKU_EVENT_ACTIVATED,
    TIKU_EVENT_DEACTIVATED,
    TIKU_EVENT_TICK,
    TIKU_EVENT_CLIPBOARD,
    TIKU_EVENT_TEXT_INPUT,
    TIKU_EVENT_PREEDIT /**< version-2 uncommitted UTF-8 update or cancellation
                        */
} tiku_event_type_t;

/**
 * @brief One input event.
 *
 * @c key names a key or clipboard status; @c button names pointer buttons
 * or text flags. @c dx/@c dy hold wheel travel or text offset/length.
 * On version-2 TEXT_INPUT/PREEDIT, repeat identifies the composition
 * episode (zero is an independent commit). Key events retain key repeat.
 * @c x/@c y are logical; @c width/@c height are resize dimensions.
 * PREEDIT uses x for the marked caret's UTF-8 byte offset.
 */
typedef struct {
    tiku_event_type_t type;
    int64_t                time_us;
    unsigned               modifiers;
    unsigned               key;
    unsigned               button;
    unsigned               repeat;
    char                   text[8];
    int                    x, y;
    int                    dx, dy;
    int                    width, height;
} tiku_input_event_t;
#endif
