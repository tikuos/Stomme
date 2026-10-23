/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_responder.h - responders: one interface for every surface on the
 * desk that takes pointer and key events, ticks and paints, and the stack
 * that routes events to them in z-order.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_RESPONDER_H_
#define TIKU_TRK_RESPONDER_H_

#include "tiku_event.h"
#include "tiku_gfx.h"
#include <stdint.h>

/** @brief How many surfaces one stack holds. */
#define TIKU_TRK_RESPONDERS_MAX 16

/**
 * @brief A surface that can be pointed at, keyed and drawn: the Deskbar,
 *        a window, a popup, an overlay.
 *
 * Every handler returns 1 when it took the event, and stores in *damage
 * what it changed on the screen: an empty damage for nothing, or
 * tiku_trk_damage_all() when it cannot bound the change.  Any entry may be
 * NULL for a surface that does not answer that event.
 */
typedef struct tiku_trk_responder {
    const char *name;               /**< a label; the stack does not read it */
    void       *self;               /**< handed back to every handler   */
    /** @brief Whether the point is this surface's to press. */
    int (*hit)(void *self, int x, int y);
    /** @brief A pointer press. */
    int (*press)(void *self, const tiku_event_t *ev, tiku_rect_t *damage);
    /** @brief A pointer move. */
    int (*move)(void *self, const tiku_event_t *ev, tiku_rect_t *damage);
    /** @brief A pointer release. */
    int (*release)(void *self, const tiku_event_t *ev, tiku_rect_t *damage);
    /** @brief A key down or up; the handler reads which from the event. */
    int (*key)(void *self, const tiku_event_t *ev, tiku_rect_t *damage);
    /** @brief The turn's clock: hovers that dwell, presses that are held. */
    int (*tick)(void *self, int64_t now, tiku_rect_t *damage);
    /** @brief Draw the surface onto @p s. */
    void (*paint)(void *self, tiku_surface_t *s);
} tiku_trk_responder_t;

/**
 * @brief The surfaces in z-order, the last pushed on top, and the one a
 *        press gave the capture to.
 */
typedef struct {
    const tiku_trk_responder_t *at[TIKU_TRK_RESPONDERS_MAX]; /**< bottom up */
    int                         count;      /**< at[] in use            */
    const tiku_trk_responder_t *capture;    /**< the press holder, or NULL */
} tiku_trk_responders_t;

/** @brief Nothing changed. */
tiku_rect_t tiku_trk_damage_none(void);
/** @brief Everything changed, or nothing bounded can be said. */
tiku_rect_t tiku_trk_damage_all(void);
/** @brief Whether @p r says nothing changed: a zero width or height. */
int tiku_trk_damage_is_none(tiku_rect_t r);
/** @brief Whether @p r says everything changed: a negative width or height. */
int tiku_trk_damage_is_all(tiku_rect_t r);
/** @brief The union of two damages; all swallows anything. */
tiku_rect_t tiku_trk_damage_union(tiku_rect_t a, tiku_rect_t b);

/** @brief Empty the stack, with no capture. */
void tiku_trk_responders_init(tiku_trk_responders_t *r);
/**
 * @brief Add a surface on top of the others; one already in the stack keeps
 *        its place.  @return 0, or -1 when full.
 */
int tiku_trk_responders_push(tiku_trk_responders_t *r,
                             const tiku_trk_responder_t *s);
/** @brief Take a surface out; a capture it held is let go. */
void tiku_trk_responders_remove(tiku_trk_responders_t *r,
                                const tiku_trk_responder_t *s);
/**
 * @brief Deliver one event.  @return 1 when a surface took it.
 *
 * A press goes to the capture holder while its hit() still claims the
 * point (a holder that no longer does lost its release, and the capture
 * ends), else to the topmost surface whose hit() says yes, which then
 * holds the capture until a release.  A move or a release goes to the
 * holder first, then down the order until one takes it; a key goes down
 * the order.  @p damage is set to the union of every asked surface's.
 */
int tiku_trk_responders_deliver(tiku_trk_responders_t *r,
                                const tiku_event_t *ev, tiku_rect_t *damage);
/**
 * @brief Tick every surface, bottom first; @p damage is set to the union of
 *        theirs.  @return 1 when any changed something.
 */
int tiku_trk_responders_tick(tiku_trk_responders_t *r, int64_t now,
                             tiku_rect_t *damage);
/** @brief Paint every surface that paints, bottom first. */
void tiku_trk_responders_paint(tiku_trk_responders_t *r, tiku_surface_t *s);

#endif
