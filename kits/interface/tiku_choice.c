/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_choice.c - the popup choice's behaviour: selection, keys, the
 * pointer and where the popup goes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_choice.h"
#include "tiku_measure.h"
#include <string.h>

/** @brief Whether @p e's point is inside @p r. */
static int hit(tiku_rect_t r, const tiku_input_event_t *e)
{ return e->x >= r.x && e->y >= r.y &&
         e->x < r.x + r.w && e->y < r.y + r.h; }
void tiku_choice_init(tiku_choice_t *c)
{
    memset(c, 0, sizeof *c);
    c->selected = c->hot = c->pressed = -1;
    c->enabled = 1;
}
int tiku_choice_add(tiku_choice_t *c, const char *label, int enabled)
{
    if (!label || c->count == TIKU_CHOICE_MAX ||
        strlen(label) >= TIKU_CHOICE_LABEL) return -1;
    int at = c->count++;
    strcpy(c->item[at].label, label);
    c->item[at].enabled = !!enabled;
    if (c->selected < 0 && enabled) c->selected = at;
    return at;
}
void tiku_choice_close(tiku_choice_t *c)
{ c->open = 0; c->pressed = -1; c->hot = c->selected; }
int tiku_choice_select(tiku_choice_t *c, int index)
{
    if (index < 0 || index >= c->count || !c->item[index].enabled) return 0;
    c->selected = index;
    tiku_choice_close(c);
    return 1;
}
tiku_rect_t tiku_choice_popup(const tiku_choice_t *c, tiku_rect_t a,
                              tiku_rect_t s)
{
    int row = tiku_measure_row_h();
    int below = s.y + s.h - a.y - a.h, above = a.y - s.y;
    int h = (c->count < 8 ? c->count : 8) * row + 4;
    int down = below >= h || below >= above;
    int room = down ? below : above;
    if (s.w <= 0 || s.h <= 0) return (tiku_rect_t){s.x, s.y, 0, 0};
    if (h > room) h = room;
    if (h < row + 4) h = s.h < row + 4 ? s.h : row + 4;
    if (h >= row + 4) h = (h - 4) / row * row + 4;
    tiku_rect_t r = {a.x, down ? a.y + a.h : a.y - h, a.w, h};
    if (r.w > s.w) r.w = s.w;
    if (r.x + r.w > s.x + s.w) r.x = s.x + s.w - r.w;
    if (r.x < s.x) r.x = s.x;
    if (r.y + r.h > s.y + s.h) r.y = s.y + s.h - r.h;
    if (r.y < s.y) r.y = s.y;
    return r;
}
/** @brief Scroll the popup so that the hot row shows. */
static void reveal(tiku_choice_t *c, tiku_rect_t popup)
{
    int rows = (popup.h - 4) / tiku_measure_row_h();
    if (rows < 1) rows = 1;
    if (c->hot < c->top) c->top = c->hot;
    if (c->hot >= c->top + rows) c->top = c->hot - rows + 1;
    if (c->top < 0) c->top = 0;
}
/** @brief Move the hot row by @p dir to the next enabled option,
 *         wrapping round; it stays put when none is enabled. */
static void step(tiku_choice_t *c, int dir)
{
    int at = c->hot;
    for (int i = 0; i < c->count; i++) {
        at = (at + dir + c->count) % c->count;
        if (c->item[at].enabled) { c->hot = at; return; }
    }
}
/** @brief Take the hot row as the selection when it is enabled, then
 *         close the popup. */
static tiku_choice_result_t accept(tiku_choice_t *c)
{
    int valid_hot = c->hot >= 0 && c->hot < c->count &&
                    c->item[c->hot].enabled;
    int changed = valid_hot && c->hot != c->selected;
    if (valid_hot) c->selected = c->hot;
    tiku_choice_close(c);
    return changed ? TIKU_CHOICE_CHANGED : TIKU_CHOICE_HANDLED;
}
tiku_choice_result_t tiku_choice_event(tiku_choice_t *c,
    const tiku_input_event_t *e, tiku_rect_t a, tiku_rect_t screen, int focused)
{
    tiku_rect_t popup = tiku_choice_popup(c, a, screen);
    if (!c->enabled || e->type == TIKU_EVENT_DEACTIVATED) {
        tiku_choice_close(c); return TIKU_CHOICE_IGNORED;
    }
    /* An empty list ignores input; a list with nothing selected still
     * opens. */
    if (!c->count) return TIKU_CHOICE_IGNORED;
    if (e->type == TIKU_EVENT_KEY_DOWN && (focused || c->open)) {
        if (c->open && (e->key == TIKU_KEY_ESCAPE || e->key == TIKU_KEY_TAB)) {
            tiku_choice_close(c);
            return e->key == TIKU_KEY_TAB ? TIKU_CHOICE_IGNORED : TIKU_CHOICE_HANDLED;
        }
        if (e->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION))
            return TIKU_CHOICE_IGNORED;
        if (e->key == ' ' || e->key == TIKU_KEY_RETURN) {
            if (e->repeat) return TIKU_CHOICE_HANDLED;
            if (c->open) return accept(c);
            c->open = 1; c->hot = c->selected;
        } else if (e->key == TIKU_KEY_UP || e->key == TIKU_KEY_DOWN ||
                   e->key == TIKU_KEY_HOME || e->key == TIKU_KEY_END) {
            if (!c->open) { c->open = 1; c->hot = c->selected; }
            if (e->key == TIKU_KEY_HOME) { c->hot = c->count - 1; step(c, 1); }
            else if (e->key == TIKU_KEY_END) { c->hot = 0; step(c, -1); }
            else step(c, e->key == TIKU_KEY_UP ? -1 : 1);
        } else return TIKU_CHOICE_IGNORED;
        reveal(c, popup);
        return TIKU_CHOICE_HANDLED;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1 && !c->open &&
        hit(a, e)) {
        c->open = 1; c->hot = c->selected; c->pressed = -1;
        reveal(c, popup); return TIKU_CHOICE_HANDLED;
    }
    if (!c->open) return TIKU_CHOICE_IGNORED;
    if (e->type == TIKU_EVENT_WHEEL && hit(popup, e)) {
        step(c, e->dy < 0 ? -1 : 1); reveal(c, popup);
        return TIKU_CHOICE_HANDLED;
    }
    if (e->type == TIKU_EVENT_POINTER_DOWN || e->type == TIKU_EVENT_POINTER_UP ||
        e->type == TIKU_EVENT_POINTER_MOVE) {
        int at = hit(popup, e) && e->y >= popup.y + 2 &&
                 e->y < popup.y + popup.h - 2
                 ? c->top + (e->y - popup.y - 2) / tiku_measure_row_h() : -1;
        if (at >= c->count || (at >= 0 && !c->item[at].enabled)) at = -1;
        if (at >= 0) c->hot = at;
        if (e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1) {
            if (!hit(popup, e)) tiku_choice_close(c);
            else c->pressed = at;
        } else if (e->type == TIKU_EVENT_POINTER_UP && e->button == 1) {
            if (at >= 0 && at == c->pressed) return accept(c);
            c->pressed = -1;
        }
        return TIKU_CHOICE_HANDLED;
    }
    return TIKU_CHOICE_IGNORED;
}
