/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_responder.c - the responder stack: damage rectangles, z-order,
 * the press capture, and routing each event to the surface that takes it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_responder.h"
#include <string.h>

tiku_rect_t
tiku_trk_damage_none(void)
{
    return (tiku_rect_t){ 0, 0, 0, 0 };
}

tiku_rect_t
tiku_trk_damage_all(void)
{
    /* A negative width: no surface returns one as a real rectangle. */
    return (tiku_rect_t){ 0, 0, -1, -1 };
}

int
tiku_trk_damage_is_none(tiku_rect_t r)
{
    return r.w == 0 || r.h == 0;
}

int
tiku_trk_damage_is_all(tiku_rect_t r)
{
    return r.w < 0 || r.h < 0;
}

tiku_rect_t
tiku_trk_damage_union(tiku_rect_t a, tiku_rect_t b)
{
    if (tiku_trk_damage_is_all(a) || tiku_trk_damage_is_all(b)) {
        return tiku_trk_damage_all();
    }
    if (tiku_trk_damage_is_none(a)) {
        return b;
    }
    if (tiku_trk_damage_is_none(b)) {
        return a;
    }
    return tiku_rect_union(a, b);
}

void
tiku_trk_responders_init(tiku_trk_responders_t *r)
{
    if (r != NULL) {
        memset(r, 0, sizeof *r);
    }
}

int
tiku_trk_responders_push(tiku_trk_responders_t *r,
                         const tiku_trk_responder_t *s)
{
    int i;

    if (r == NULL || s == NULL || r->count >= TIKU_TRK_RESPONDERS_MAX) {
        return -1;
    }
    for (i = 0; i < r->count; i++) {
        if (r->at[i] == s) {
            return 0;           /* already on the list, where it was */
        }
    }
    r->at[r->count++] = s;
    return 0;
}

void
tiku_trk_responders_remove(tiku_trk_responders_t *r,
                           const tiku_trk_responder_t *s)
{
    int i;

    if (r == NULL || s == NULL) {
        return;
    }
    for (i = 0; i < r->count; i++) {
        if (r->at[i] == s) {
            memmove(&r->at[i], &r->at[i + 1],
                    (size_t)(r->count - i - 1) * sizeof r->at[0]);
            r->count--;
            break;
        }
    }
    if (r->capture == s) {
        r->capture = NULL;
    }
}

/** @brief Ask one surface one event; its damage joins the turn's. */
static int
ask(const tiku_trk_responder_t *s, const tiku_event_t *ev,
    tiku_rect_t *damage)
{
    tiku_rect_t mine = tiku_trk_damage_none();
    int taken = 0;

    switch (ev->type) {
    case TIKU_EVENT_POINTER_DOWN:
        taken = (s->press != NULL) ? s->press(s->self, ev, &mine) : 0;
        break;
    case TIKU_EVENT_POINTER_MOVE:
        taken = (s->move != NULL) ? s->move(s->self, ev, &mine) : 0;
        break;
    case TIKU_EVENT_POINTER_UP:
        taken = (s->release != NULL) ? s->release(s->self, ev, &mine) : 0;
        break;
    case TIKU_EVENT_KEY_DOWN:
    case TIKU_EVENT_KEY_UP:
        taken = (s->key != NULL) ? s->key(s->self, ev, &mine) : 0;
        break;
    default:
        break;
    }
    *damage = tiku_trk_damage_union(*damage, mine);
    return taken;
}

int
tiku_trk_responders_deliver(tiku_trk_responders_t *r,
                            const tiku_event_t *ev, tiku_rect_t *damage)
{
    int i;

    if (damage != NULL) {
        *damage = tiku_trk_damage_none();
    }
    if (r == NULL || ev == NULL || damage == NULL) {
        return 0;
    }
    if (ev->type == TIKU_EVENT_POINTER_DOWN) {
        if (r->capture != NULL) {
            const tiku_trk_responder_t *holder = r->capture;

            /* A press while captured is a second button or a double
             * click when the holder still claims the point; when it no
             * longer does, the release that would have ended the capture
             * was never delivered, and the capture is over. */
            if (holder->hit == NULL ||
                holder->hit(holder->self, ev->x, ev->y)) {
                return ask(holder, ev, damage);
            }
            r->capture = NULL;
        }
        for (i = r->count - 1; i >= 0; i--) {
            const tiku_trk_responder_t *s = r->at[i];

            if (s->hit == NULL || !s->hit(s->self, ev->x, ev->y)) {
                continue;
            }
            if (ask(s, ev, damage)) {
                r->capture = s;
                return 1;
            }
        }
        return 0;
    }
    if (ev->type == TIKU_EVENT_POINTER_MOVE ||
        ev->type == TIKU_EVENT_POINTER_UP) {
        const tiku_trk_responder_t *holder = r->capture;

        if (holder != NULL) {
            int taken = ask(holder, ev, damage);

            if (ev->type == TIKU_EVENT_POINTER_UP) {
                r->capture = NULL;
            }
            if (taken) {
                return 1;
            }
        }
        for (i = r->count - 1; i >= 0; i--) {
            if (r->at[i] != holder && ask(r->at[i], ev, damage)) {
                return 1;
            }
        }
        return 0;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN || ev->type == TIKU_EVENT_KEY_UP) {
        for (i = r->count - 1; i >= 0; i--) {
            if (ask(r->at[i], ev, damage)) {
                return 1;
            }
        }
    }
    return 0;
}

int
tiku_trk_responders_tick(tiku_trk_responders_t *r, int64_t now,
                         tiku_rect_t *damage)
{
    int i, changed = 0;

    if (damage != NULL) {
        *damage = tiku_trk_damage_none();
    }
    if (r == NULL || damage == NULL) {
        return 0;
    }
    for (i = 0; i < r->count; i++) {
        const tiku_trk_responder_t *s = r->at[i];
        tiku_rect_t mine = tiku_trk_damage_none();

        if (s->tick != NULL && s->tick(s->self, now, &mine)) {
            changed = 1;
        }
        *damage = tiku_trk_damage_union(*damage, mine);
    }
    return changed;
}

void
tiku_trk_responders_paint(tiku_trk_responders_t *r, tiku_surface_t *s)
{
    int i;

    if (r == NULL || s == NULL) {
        return;
    }
    for (i = 0; i < r->count; i++) {
        if (r->at[i]->paint != NULL) {
            r->at[i]->paint(r->at[i]->self, s);
        }
    }
}
