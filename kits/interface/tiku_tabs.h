/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_tabs.h - a horizontal tab strip: a row of named tabs, one of them
 * current, over a body the current one names.
 *
 * The state behind the control: which tabs there are, which one is
 * current, where each sits in a strip, which one a click lands on, and
 * the arrow that moves between them.  An application supplies the names
 * and reads back the current index to decide what to show under the
 * strip; the widget owns the layout, the hit test and, in
 * tiku_tabs_paint.h, the look.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TABS_H_
#define TIKU_TABS_H_

#include "tiku_geom.h"

#define TIKU_TABS_MAX        8   /**< tabs in one strip                */
#define TIKU_TABS_LABEL_MAX 40   /**< bytes of a label, NUL included   */

/** @brief A strip's tabs and the current one. */
typedef struct {
    char label[TIKU_TABS_MAX][TIKU_TABS_LABEL_MAX];
    int  count;
    int  current;                /**< index, or -1 when there are none */
} tiku_tabs_t;

/** @brief Start empty, nothing current yet. */
void tiku_tabs_init(tiku_tabs_t *t);

/**
 * @brief Add a tab named @p label.  The first one added is current.
 * @return its index, or -1 when the strip is full.
 */
int tiku_tabs_add(tiku_tabs_t *t, const char *label);

/** @brief How many tabs there are. */
int tiku_tabs_count(const tiku_tabs_t *t);

/** @brief The current tab's index, or -1 when there are none. */
int tiku_tabs_current(const tiku_tabs_t *t);

/**
 * @brief Make @p index current, clamped to what exists.
 * @return the current index after clamping, or -1 when there are none.
 */
int tiku_tabs_select(tiku_tabs_t *t, int index);

/**
 * @brief Step the current tab by @p dir (-1 or +1), wrapping the ends.
 *
 * The arrow keys of a tabbed window: past the last tab comes the first.
 * @return the current index after the step, or -1 when there are none.
 */
int tiku_tabs_next(tiku_tabs_t *t, int dir);

/**
 * @brief Where tab @p index sits in @p strip.
 *
 * The tabs divide the strip evenly.  An index the strip does not have
 * gets a zero-width rect, so a caller looping past the count draws and
 * hits nothing.
 * @note Keep the strip wider than the count: in a narrower one the
 *       interior tabs have no width and cannot be hit.
 */
tiku_rect_t tiku_tabs_rect(const tiku_tabs_t *t, tiku_rect_t strip,
                           int index);

/**
 * @brief Which tab a point at @p x,@p y lands on in @p strip.
 * @return the tab's index, or -1 for none.
 */
int tiku_tabs_hit(const tiku_tabs_t *t, tiku_rect_t strip, int x, int y);

#endif /* TIKU_TABS_H_ */
