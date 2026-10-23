/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_scroll.h - the state behind tiku_ui_scrollbar().
 *
 * It holds a range, a thumb, and two states in which a new range is
 * deferred: tracking, while a button is held on the bar, and detached,
 * while the view is rebuilt.  It draws nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SCROLL_H_
#define TIKU_SCROLL_H_

/** @brief The smallest thumb that can still be grabbed. */
#define TIKU_SCROLL_MIN_THUMB 16

/** @brief A scrollbar: its range, its position and the press on it. */
typedef struct {
    int total;            /**< the whole content, in units             */
    int visible;          /**< how much of it is on screen             */
    int value;            /**< the first unit shown                    */
    int step;             /**< one line, in units                      */
    /* A range handed over while the bar was busy, applied when it is free,
     * and whether there is one. */
    int pend_total, pend_visible;
    int pending;
    int tracking;         /**< a button is held on the bar             */
    /* A held arrow repeats its step (NeXT UIG ch3), counted in loop
     * pulses so a script can hold an arrow deterministically. */
    int arrow_dir;        /**< -1 back, +1 on, 0 when no arrow is held */
    int arrow_pulses;
    int detached;         /**< a bulk re-layout is in progress         */
    int grab_off;         /**< where in the thumb the grab landed      */
} tiku_scroll_t;

/**
 * @brief Start empty: no content, nothing shown, at the top, with a line
 *        of @p step units, at least 1.
 */
void tiku_scroll_init(tiku_scroll_t *sb, int step);

/**
 * @brief Tell it what there is to scroll (PVL-052).
 *
 * Applied at once when the bar is idle; held while a button is down or
 * while the view is being rebuilt, and applied when that ends.
 *
 * @return 1 when it took effect now.
 */
int tiku_scroll_set_range(tiku_scroll_t *sb, int total, int visible);

/**
 * @brief Move to @p value, clamped to what there is to show.
 *
 * @return The new value.
 */
int tiku_scroll_to(tiku_scroll_t *sb, int value);

/** @brief An arrow was pressed and is now held: @p dir below 0 is back. */
void tiku_scroll_arrow_press(tiku_scroll_t *sb, int dir);

/**
 * @brief One loop pulse with the arrow still held.
 *
 * The first repeat is due on the twelfth pulse, the rest every fourth.
 *
 * @return the direction to step, or 0 when no repeat is due.
 */
int tiku_scroll_arrow_repeat(tiku_scroll_t *sb);

/** @brief The largest value that still shows content. */
int tiku_scroll_max(const tiku_scroll_t *sb);

/**
 * @brief Where the thumb sits in a track @p len long (PVL-051).
 *
 * Sized by the visible fraction, but never under TIKU_SCROLL_MIN_THUMB,
 * so a long listing's thumb can still be caught.
 *
 * @return 1 when there is anything to scroll; otherwise 0, with the
 *         thumb filling the track.
 */
int tiku_scroll_thumb(const tiku_scroll_t *sb, int len, int *pos,
                          int *thick);

/** @brief One click of a stepper: half a line, rounded up (PVL-051). */
int tiku_scroll_line(const tiku_scroll_t *sb);

/** @brief One page: what is visible, less an overlap of one line. */
int tiku_scroll_page(const tiku_scroll_t *sb);

/** @brief Where a press on the track lands. */
typedef enum {
    TIKU_SCROLL_HIT_NONE = 0,
    TIKU_SCROLL_HIT_UP,       /**< before the thumb: a page back */
    TIKU_SCROLL_HIT_DOWN,     /**< after it: a page on           */
    TIKU_SCROLL_HIT_THUMB
} tiku_scroll_hit_t;

/**
 * @brief Where a press at @p at in a track @p len long lands: none when
 *        there is nothing to scroll or @p at is off the track.
 */
tiku_scroll_hit_t tiku_scroll_hit(const tiku_scroll_t *sb,
                                          int len, int at);

/**
 * @brief Press at @p at: page, or take hold of the thumb.
 *
 * @return the new value.
 */
int tiku_scroll_press(tiku_scroll_t *sb, int len, int at);

/** @brief Drag to @p at while holding the thumb.  @return the new value. */
int tiku_scroll_drag(tiku_scroll_t *sb, int len, int at);

/**
 * @brief Let go: the press and any held arrow end, and a range that
 *        arrived while held is applied unless the bar is detached.
 */
void tiku_scroll_release(tiku_scroll_t *sb);

/**
 * @brief Detach for a bulk re-layout (PVL-060), deferring every range
 *        until tiku_scroll_attach().
 *
 * A mode switch, a clean-up or a folder switch moves the content through
 * intermediate shapes, which a detached bar does not take.
 */
void tiku_scroll_detach(tiku_scroll_t *sb);

/**
 * @brief Attach after a bulk re-layout, applying the range deferred
 *        meanwhile unless a button is still held.
 */
void tiku_scroll_attach(tiku_scroll_t *sb);

/** @brief Whether it is detached or held, i.e. not taking ranges. */
int tiku_scroll_busy(const tiku_scroll_t *sb);

#endif /* TIKU_SCROLL_H_ */
