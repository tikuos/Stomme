/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_springmenu.h - spring-loaded menus: a drag that rests over a
 * folder for TIKU_TRK_SPRING_DELAY_US opens its menu.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_SPRINGMENU_H_
#define TIKU_TRK_SPRINGMENU_H_

#include <stdint.h>

/** @brief How long a drag rests over a folder before its menu opens. */
#define TIKU_TRK_SPRING_DELAY_US 500000
/** @brief How far outside the area the pointer may wander while armed. */
#define TIKU_TRK_SPRING_MARGIN 20

/** @brief One spring menu's state: the area it is armed over, and when. */
typedef struct {
    int armed;                  /**< a drag rests over the area           */
    int open;                   /**< the menu has opened                  */
    int x, y, w, h;             /**< the area, in pointer coordinates     */
    int64_t entered;            /**< when the pointer entered it          */
} tiku_trk_springmenu_t;

/** @brief Disarm and close. */
void tiku_trk_springmenu_reset(tiku_trk_springmenu_t *m);
/** @brief Arm over the area @p x, @p y, @p w, @p h, entered at @p now. */
void tiku_trk_springmenu_enter(tiku_trk_springmenu_t *m, int x, int y, int w,
                               int h, int64_t now);
/**
 * @brief A tick: once the hold has lasted the spring delay the menu opens.
 *        @return 1 on the tick it opens.
 */
int tiku_trk_springmenu_pulse(tiku_trk_springmenu_t *m, int64_t now);
/** @brief Whether @p x, @p y is over the armed area, margin included. */
int tiku_trk_springmenu_inside(const tiku_trk_springmenu_t *m, int x, int y);
/** @brief The pointer moved to @p x, @p y; outside the area, the arm drops. */
void tiku_trk_springmenu_leave(tiku_trk_springmenu_t *m, int x, int y);
/**
 * @brief Whether a spring menu row can be chosen: a container unless its
 *        link is broken, a file only when something accepts its type.
 */
int tiku_trk_springmenu_item_enabled(int container, int accepts_type,
                                     int broken_link);

#endif
