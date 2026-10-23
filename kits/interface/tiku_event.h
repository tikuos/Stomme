/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_event.h - platform-neutral application input.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_EVENT_H_
#define TIKU_EVENT_H_


#include <stdint.h>

#include "tiku_input.h"

/* Logical modifiers.  A platform adapter maps its physical keyboard onto
 * these; applications never inspect native mask bits. */
#define TIKU_MOD_SHIFT 0x01u
#define TIKU_MOD_CMD 0x02u
#define TIKU_MOD_OPTION 0x04u
#define TIKU_MOD_CAPS 0x08u

/* Printable keys use their character value; the keys that print nothing
 * take the small values below 0x20. */
#define TIKU_KEY_UP 1u
#define TIKU_KEY_DOWN 2u
#define TIKU_KEY_LEFT 3u
#define TIKU_KEY_RIGHT 4u
#define TIKU_KEY_HOME 5u
#define TIKU_KEY_END 6u
#define TIKU_KEY_PAGE_UP 7u
#define TIKU_KEY_PAGE_DOWN 8u
#define TIKU_KEY_BACKSPACE 9u
#define TIKU_KEY_DELETE 10u
#define TIKU_KEY_ESCAPE 11u
#define TIKU_KEY_RETURN 12u
#define TIKU_KEY_TAB 13u
#define TIKU_KEY_MENU 14u
#define TIKU_KEY_F2 15u
#define TIKU_KEY_SHIFT 16u
#define TIKU_KEY_CMD 17u
#define TIKU_KEY_OPTION 18u
#define TIKU_KEY_F1 19u
#define TIKU_KEY_F5 20u
#define TIKU_KEY_F8 21u

/** @brief The application-side name of an input event. */
typedef tiku_input_event_t tiku_event_t;

#endif /* TIKU_EVENT_H_ */
