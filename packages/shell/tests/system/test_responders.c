/* The responder list: hit order, capture, the damage union and removal.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tiku_trk_responder.h"

/* A fake surface: a box that takes what falls in it, or refuses. */
typedef struct {
    tiku_rect_t box;
    int refuse;                 /* say no to every press            */
    int presses, moves, releases, keys, ticks;
    tiku_rect_t reply;          /* the damage each handler reports  */
} fake_t;

static int hit(void *self, int x, int y)
{
    fake_t *f = self;
    return x >= f->box.x && x < f->box.x + f->box.w &&
           y >= f->box.y && y < f->box.y + f->box.h;
}
static int press(void *self, const tiku_event_t *ev, tiku_rect_t *d)
{ fake_t *f = self; (void)ev; f->presses++; *d = f->reply; return !f->refuse; }
static int move(void *self, const tiku_event_t *ev, tiku_rect_t *d)
{ fake_t *f = self; (void)ev; f->moves++; *d = f->reply; return 0; }
static int release(void *self, const tiku_event_t *ev, tiku_rect_t *d)
{ fake_t *f = self; (void)ev; f->releases++; *d = f->reply; return 1; }
static int key(void *self, const tiku_event_t *ev, tiku_rect_t *d)
{ fake_t *f = self; (void)ev; f->keys++; *d = f->reply; return !f->refuse; }
static int tick(void *self, int64_t now, tiku_rect_t *d)
{ fake_t *f = self; (void)now; f->ticks++; *d = f->reply; return 1; }

static tiku_event_t at(tiku_event_type_t type, int x, int y)
{
    tiku_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type; ev.x = x; ev.y = y; ev.button = 1u;
    return ev;
}

int main(void)
{
    tiku_trk_responders_t list;
    fake_t under = { { 0, 0, 100, 100 }, 0, 0, 0, 0, 0, 0, { 0, 0, 10, 10 } };
    fake_t over = { { 50, 50, 100, 100 }, 0, 0, 0, 0, 0, 0, { 60, 60, 5, 5 } };
    tiku_trk_responder_t a = { "under", &under, hit, press, move, release,
                               key, tick, NULL };
    tiku_trk_responder_t b = { "over", &over, hit, press, move, release,
                               key, tick, NULL };
    tiku_rect_t d;
    tiku_event_t ev;

    /* The damage algebra. */
    assert(tiku_trk_damage_is_none(tiku_trk_damage_none()));
    assert(tiku_trk_damage_is_all(tiku_trk_damage_all()));
    d = tiku_trk_damage_union(tiku_trk_damage_none(),
                              (tiku_rect_t){ 1, 2, 3, 4 });
    assert(d.x == 1 && d.y == 2 && d.w == 3 && d.h == 4);
    d = tiku_trk_damage_union((tiku_rect_t){ 0, 0, 10, 10 },
                              (tiku_rect_t){ 5, 5, 10, 10 });
    assert(d.x == 0 && d.y == 0 && d.w == 15 && d.h == 15);
    assert(tiku_trk_damage_is_all(tiku_trk_damage_union(
        (tiku_rect_t){ 0, 0, 10, 10 }, tiku_trk_damage_all())));

    tiku_trk_responders_init(&list);
    assert(tiku_trk_responders_push(&list, &a) == 0);
    assert(tiku_trk_responders_push(&list, &b) == 0);
    assert(tiku_trk_responders_push(&list, &b) == 0 && list.count == 2);

    /* A press where both lie goes to the one on top, which then holds
     * the capture; its damage is what comes back. */
    ev = at(TIKU_EVENT_POINTER_DOWN, 75, 75);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 1);
    assert(over.presses == 1 && under.presses == 0);
    assert(list.capture == &b && d.x == 60 && d.w == 5);
    /* A move anywhere goes to the holder first, then on down since the
     * holder does not take moves; every reply is unioned. */
    ev = at(TIKU_EVENT_POINTER_MOVE, 5, 5);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 0);
    assert(over.moves == 1 && under.moves == 1);
    assert(d.x == 0 && d.y == 0 && d.w == 65 && d.h == 65);
    /* A press while captured goes to the holder while it still claims
     * the point; off its box the release was lost, so the capture ends
     * and the press goes where it falls. */
    ev = at(TIKU_EVENT_POINTER_DOWN, 100, 100);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 1);
    assert(over.presses == 2 && under.presses == 0 && list.capture == &b);
    ev = at(TIKU_EVENT_POINTER_DOWN, 5, 5);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 1);
    assert(over.presses == 2 && under.presses == 1 && list.capture == &a);
    /* The release ends the capture. */
    ev = at(TIKU_EVENT_POINTER_UP, 5, 5);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 1);
    assert(under.releases == 1 && list.capture == NULL);
    under.presses = 0;

    /* A press the top one refuses falls to the one under it. */
    over.refuse = 1;
    ev = at(TIKU_EVENT_POINTER_DOWN, 75, 75);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 1);
    assert(over.presses == 3 && under.presses == 1 && list.capture == &a);
    tiku_trk_responders_remove(&list, &a);
    assert(list.count == 1 && list.capture == NULL);
    /* A press nobody lies under is nobody's. */
    ev = at(TIKU_EVENT_POINTER_DOWN, 200, 200);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 0);
    assert(tiku_trk_damage_is_none(d));

    /* Keys go down the order until one takes them. */
    tiku_trk_responders_push(&list, &a);
    over.refuse = 0;
    ev = at(TIKU_EVENT_KEY_DOWN, 0, 0);
    assert(tiku_trk_responders_deliver(&list, &ev, &d) == 1);
    assert(under.keys == 1 && over.keys == 0);     /* a is on top now */
    /* The tick asks everyone. */
    assert(tiku_trk_responders_tick(&list, 1, &d) == 1);
    assert(under.ticks == 1 && over.ticks == 1 && d.w == 65);
    puts("PASS: responders in order, one capture, the damage unioned");
    return 0;
}
