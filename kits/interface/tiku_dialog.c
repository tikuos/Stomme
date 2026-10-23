/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dialog.c - focus order and release-to-activate, for a panel.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_dialog.h"
#include <string.h>

/** @brief Where control @p id is in the list, or -1. */
static int index_of(const tiku_dialog_t *d, int id)
{
    for (int i = 0; i < d->count; i++)
        if (d->control[i].id == id) return i;
    return -1;
}
/** @brief Whether @p id is a listed, enabled control. */
static int live(const tiku_dialog_t *d, int id)
{
    int i = index_of(d, id);
    return i >= 0 && d->control[i].enabled;
}
/** @brief Whether @p e's point is inside @p r. */
static int contains(tiku_rect_t r, const tiku_input_event_t *e)
{
    return e->x >= r.x && e->y >= r.y &&
           e->x < r.x + r.w && e->y < r.y + r.h;
}
void tiku_dialog_init(tiku_dialog_t *d)
{
    memset(d, 0, sizeof *d);
    d->focus = d->capture = d->dflt = d->cancel = TIKU_DIALOG_NONE;
}
void tiku_dialog_begin(tiku_dialog_t *d, int dflt, int cancel)
{
    /* Where the focus stood before the list is rebuilt, so that
     * tiku_dialog_end() can hand a focus whose control disabled itself to
     * a neighbour. */
    d->focus_at = index_of(d, d->focus);
    d->count = 0; d->dflt = dflt; d->cancel = cancel;
}
int tiku_dialog_add(tiku_dialog_t *d, int id, tiku_rect_t rect,
                    tiku_dialog_kind_t kind, int enabled)
{
    if (id < 0 || d->count == TIKU_DIALOG_MAX || index_of(d, id) >= 0 ||
        (unsigned)kind > TIKU_DIALOG_INPUT) return -1;
    d->control[d->count++] = (tiku_dialog_control_t){id,
        enabled && rect.w > 0 && rect.h > 0, kind, rect};
    return 0;
}
void tiku_dialog_end(tiku_dialog_t *d)
{
    int at = d->focus_at;

    if (!live(d, d->capture)) d->capture = TIKU_DIALOG_NONE;
    if (live(d, d->focus)) return;
    /* A focus moved off a control that disabled itself shows its ring
     * where it lands; the first placing does not, so a panel opens
     * without a ring. */
    if (d->focus != TIKU_DIALOG_NONE) d->keyboard = 1;
    d->focus = TIKU_DIALOG_NONE;
    if (at > d->count - 1) at = d->count - 1;
    /* Back from where the focus stood first, then forward: a pager that
     * greys itself hands the focus to the arrow beside it. */
    for (int i = at; i >= 0; i--) {
        if (d->control[i].enabled) { d->focus = d->control[i].id; return; }
    }
    for (int i = (at < 0) ? 0 : at; i < d->count; i++) {
        if (d->control[i].enabled) { d->focus = d->control[i].id; return; }
    }
}
int tiku_dialog_focus(tiku_dialog_t *d, int id)
{
    if (!live(d, id)) return 0;
    d->focus = id;
    return 1;
}
int tiku_dialog_focused(const tiku_dialog_t *d, int id)
{ return d->keyboard && d->focus == id && live(d, id); }
int tiku_dialog_pressed(const tiku_dialog_t *d, int id)
{ return d->capture == id && d->inside && live(d, id); }
tiku_dialog_result_t tiku_dialog_event(tiku_dialog_t *d,
                                       const tiku_input_event_t *ev)
{
    tiku_dialog_result_t r = {0, TIKU_DIALOG_NONE};
    int at = index_of(d, d->focus);
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        d->capture = TIKU_DIALOG_NONE;
        return r;
    }
    if (ev->type == TIKU_EVENT_KEY_DOWN) {
        d->keyboard = 1;
        if (ev->key == TIKU_KEY_TAB) {
            int step = ev->modifiers & TIKU_MOD_SHIFT ? -1 : 1;
            for (int n = 0; n < d->count; n++) {
                at = (at + step + d->count) % d->count;
                if (d->control[at].enabled) {
                    d->focus = d->control[at].id; break;
                }
            }
            r.taken = 1;
        } else if (ev->key == TIKU_KEY_ESCAPE) {
            r.taken = 1;
            if (live(d, d->cancel)) r.activated = d->cancel;
        } else if (ev->key == TIKU_KEY_RETURN) {
            r.taken = 1;
            int id = at >= 0 && d->control[at].kind == TIKU_DIALOG_BUTTON
                     ? d->focus : d->dflt;
            if (live(d, id) && !ev->repeat) r.activated = id;
        } else if (ev->key == ' ' && at >= 0 &&
                   !(ev->modifiers & (TIKU_MOD_CMD | TIKU_MOD_OPTION)) &&
                   d->control[at].kind != TIKU_DIALOG_INPUT) {
            r.taken = 1;
            if (live(d, d->focus) && !ev->repeat) r.activated = d->focus;
        }
    } else if (ev->type == TIKU_EVENT_POINTER_DOWN && ev->button == 1) {
        d->keyboard = 0;
        d->capture = TIKU_DIALOG_NONE;
        for (int i = d->count - 1; i >= 0; i--) {
            if (!contains(d->control[i].rect, ev)) continue;
            r.taken = 1;
            if (d->control[i].enabled) {
                d->focus = d->control[i].id;
                if (d->control[i].kind == TIKU_DIALOG_INPUT) r.taken = 0;
                else { d->capture = d->focus; d->inside = 1; }
            }
            break;
        }
    } else if (d->capture >= 0 &&
               (ev->type == TIKU_EVENT_POINTER_MOVE ||
                (ev->type == TIKU_EVENT_POINTER_UP && ev->button == 1))) {
        int i = index_of(d, d->capture);
        r.taken = 1;
        d->inside = i >= 0 && contains(d->control[i].rect, ev);
        if (ev->type == TIKU_EVENT_POINTER_UP) {
            if (d->inside && live(d, d->capture)) r.activated = d->capture;
            d->capture = TIKU_DIALOG_NONE;
        }
    }
    return r;
}
