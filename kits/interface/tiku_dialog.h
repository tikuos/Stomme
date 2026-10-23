/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dialog.h - a panel's controls: focus order, keys and
 * release-to-activate.
 *
 * The caller lists the panel's controls between tiku_dialog_begin() and
 * tiku_dialog_end() each time it lays the panel out; the focus and a
 * press in flight are kept by id across the rebuild.  Behaviour only:
 * the caller draws.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DIALOG_H_
#define TIKU_DIALOG_H_
#include "tiku_event.h"
#include "tiku_input.h"
#include "tiku_geom.h"

/** @brief The most controls a panel lists. */
#define TIKU_DIALOG_MAX 96
/** @brief No control: no focus, press, default or cancel. */
#define TIKU_DIALOG_NONE (-1)
/** @brief What a control is; a text input keeps its own keys. */
typedef enum { TIKU_DIALOG_BUTTON, TIKU_DIALOG_TOGGLE,
               TIKU_DIALOG_INPUT } tiku_dialog_kind_t;
/** @brief One listed control; one with an empty rect is disabled. */
typedef struct {
    int id, enabled;
    tiku_dialog_kind_t kind;
    tiku_rect_t rect;
} tiku_dialog_control_t;
/**
 * @brief A panel's controls and its focus.
 *
 * @c focus and @c capture are ids: the focused control and the one a
 * press holds.  @c inside is nonzero while the pointer is on the held
 * control, and @c keyboard while the focus ring shows, after a key.
 * @c dflt and @c cancel are the ids Return and Escape press.
 */
typedef struct {
    tiku_dialog_control_t control[TIKU_DIALOG_MAX];
    int count, focus, capture, inside, keyboard, dflt, cancel;
    int focus_at;                   /* where focus stood before a rebuild */
} tiku_dialog_t;
/** @brief What an event did: whether the panel took it, and the id it
 *         activated or TIKU_DIALOG_NONE. */
typedef struct { int taken, activated; } tiku_dialog_result_t;

/** @brief Initialize with no focus, capture, default or cancel action. */
void tiku_dialog_init(tiku_dialog_t *d);
/** @brief Start listing the controls again, @p dflt and @p cancel being
 *         the ids Return and Escape press; focus and capture are kept by
 *         id. */
void tiku_dialog_begin(tiku_dialog_t *d, int dflt, int cancel);
/** @brief List a control under a unique nonnegative @p id.
 *  @return 0, or -1 for a negative or repeated id, a full panel or an
 *          unknown kind. */
int tiku_dialog_add(tiku_dialog_t *d, int id, tiku_rect_t rect,
                    tiku_dialog_kind_t kind, int enabled);
/** @brief Finish the list: a press on a control no longer enabled is
 *         dropped, and a focus on one moves to the nearest enabled
 *         control before where it stood, else after it. */
void tiku_dialog_end(tiku_dialog_t *d);
/** @brief Focus control @p id.
 *  @return 1, or 0 when @p id is not an enabled control. */
int tiku_dialog_focus(tiku_dialog_t *d, int id);
/** @brief Whether @p id has the focus with its ring showing, for
 *         painting. */
int tiku_dialog_focused(const tiku_dialog_t *d, int id);
/** @brief Whether a press holds @p id with the pointer on it, for
 *         painting it down. */
int tiku_dialog_pressed(const tiku_dialog_t *d, int id);
/**
 * @brief Route @p ev through the panel.
 *
 * Tab and Shift-Tab move the focus; Return presses the focused button,
 * else the default; Escape the cancel; space the focused button or
 * toggle.  A pointer press holds a control, and a release on it activates
 * it.  Other keys, and a click on a text input (which focuses it), are
 * not taken.
 */
tiku_dialog_result_t tiku_dialog_event(tiku_dialog_t *d,
                                       const tiku_input_event_t *ev);
#endif
