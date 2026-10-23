/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drag.c - drag arming, payload and drop rules.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_drag.h"
#include <stdio.h>
#include <string.h>

int
tiku_trk_click_press(tiku_trk_click_t *click, const void *pose,
                     int x, int y, unsigned button, unsigned flags,
                     int64_t now)
{
    int dx, dy, match;

    if (click == NULL) {
        return 0;
    }
    if (pose == NULL || button != 1u ||
        (flags & (TIKU_TRK_CLICK_EXTEND | TIKU_TRK_CLICK_CONTROL)) != 0u) {
        memset(click, 0, sizeof *click);
        return 0;
    }
    dx = x - click->x;
    dy = y - click->y;
    match = click->pose == pose && click->button == button &&
            dx > -TIKU_TRK_DOUBLE_CLICK_SLOP &&
            dx < TIKU_TRK_DOUBLE_CLICK_SLOP &&
            dy > -TIKU_TRK_DOUBLE_CLICK_SLOP &&
            dy < TIKU_TRK_DOUBLE_CLICK_SLOP &&
            now - click->at < TIKU_TRK_DOUBLE_CLICK_US;
    if (match) {
        /* A completed pair is forgotten: a triple click is one pair and a
         * new first click. */
        memset(click, 0, sizeof *click);
        return 1;
    }
    click->pose = pose;
    click->x = x;
    click->y = y;
    click->button = button;
    click->at = now;
    return 0;
}

/** @brief The parent directory of @p path, into @p out. */
static void
parent_of(const char *path, char *out, size_t max)
{
    const char *slash = strrchr(path, '/');
    size_t n;

    if (slash == NULL || slash == path) {
        snprintf(out, max, "/");
        return;
    }
    n = (size_t)(slash - path);
    if (n >= max) {
        n = max - 1u;
    }
    memcpy(out, path, n);
    out[n] = '\0';
}

void
tiku_trk_drag_reset(tiku_trk_drag_t *d)
{
    if (d != NULL) {
        memset(d, 0, sizeof *d);
    }
}

void
tiku_trk_drag_name_at(tiku_trk_drag_t *d, int i, const char *name)
{
    if (d == NULL || i < 0 || i >= TIKU_TRK_DRAG_MAX) {
        return;
    }
    snprintf(d->name[i], sizeof d->name[i], "%s",
             (name != NULL) ? name : "");
}

void
tiku_trk_drag_arm(tiku_trk_drag_t *d, int x, int y, unsigned buttons,
                  unsigned mods, int clicks, void *src_win,
                  const char *src_dir)
{
    if (d == NULL) {
        return;
    }
    tiku_trk_drag_reset(d);
    /* Only a single click arms: the second press of a double-click opens
     * instead. */
    if (clicks != 1) {
        return;
    }
    d->armed = 1;
    d->x0 = x;
    d->y0 = y;
    d->x = x;
    d->y = y;
    d->buttons = buttons;
    d->mods = mods;
    d->src_win = src_win;
    if (src_dir != NULL) {
        snprintf(d->src_dir, sizeof d->src_dir, "%s", src_dir);
    }
}

void
tiku_trk_drag_set_source_virtual(tiku_trk_drag_t *d, int virtual_)
{
    if (d != NULL) {
        d->src_virtual = virtual_ ? 1 : 0;
    }
}

void
tiku_trk_drag_set_foreign_refs(tiku_trk_drag_t *d, int foreign)
{
    if (d != NULL) {
        d->foreign_refs = foreign ? 1 : 0;
    }
}

int
tiku_trk_drag_set_payload(tiku_trk_drag_t *d, const char *type,
                          const void *data, size_t size)
{
    if (d == NULL || type == NULL || type[0] == '\0' || data == NULL ||
        size > TIKU_TRK_DRAG_PAYLOAD_MAX) {
        return -1;
    }
    snprintf(d->payload_type, sizeof d->payload_type, "%s", type);
    memcpy(d->payload, data, size);
    d->payload_size = size;
    d->foreign_refs = 1;
    return 0;
}

int
tiku_trk_drag_add(tiku_trk_drag_t *d, const char *path)
{
    if (d == NULL || path == NULL || path[0] == '\0' ||
        d->count >= TIKU_TRK_DRAG_MAX) {
        return -1;
    }
    snprintf(d->path[d->count], sizeof d->path[0], "%s", path);
    d->count++;
    return 0;
}

int
tiku_trk_drag_moved(tiku_trk_drag_t *d, int x, int y)
{
    int dx, dy;

    if (d == NULL || !d->armed) {
        return 0;
    }
    d->x = x;
    d->y = y;
    if (d->active) {
        return 0;                   /* already started: only tracked  */
    }
    /* Squared, against the squared threshold: the comparison is the same
     * one and costs no square root. */
    dx = x - d->x0;
    dy = y - d->y0;
    if (dx * dx + dy * dy < TIKU_TRK_DRAG_SLOP * TIKU_TRK_DRAG_SLOP) {
        return 0;
    }
    /* Nothing to carry is not a drag.  A press on empty space arms the
     * rubber band, which is a different gesture with the same opening. */
    if (d->count == 0) {
        return 0;
    }
    d->active = 1;
    return 1;
}

int
tiku_trk_drag_active(const tiku_trk_drag_t *d)
{
    return (d != NULL && d->active);
}

int
tiku_trk_drag_carries(const tiku_trk_drag_t *d, const char *path)
{
    int i;

    if (d == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < d->count; i++) {
        if (strcmp(d->path[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_drag_can_drop(const tiku_trk_drag_t *d, const tiku_model_t *over)
{
    if (d == NULL || over == NULL || !d->active) {
        return 0;
    }
    /* A pose that is itself in the drag is not a target: dropping the
     * selection on one of its own members means the window behind it
     * (PVS-042). */
    if (tiku_trk_drag_carries(d, over->path)) {
        return 0;
    }
    if (!tiku_model_is_container(over)) {
        /* An executable is a target too: dropping documents on an
         * application launches it with them (PVS-044), as the source's
         * Model::IsDropTarget answers. */
        return (over->facts.perm & TIKU_P_EXEC) != 0u &&
               over->kind == TIKU_KIND_FILE;
    }
    /* A container that will not accept new entries (a read-only store, a
     * device node's virtual directory) is not a target either, so the hover
     * already shows the refusal. */
    return tiku_model_is_mutable_container(over);
}

tiku_trk_drop_t
tiku_trk_drag_action(const tiku_trk_drag_t *d, const char *dst_dir,
                     unsigned mods, int same_volume)
{
    char from[TIKU_PATH_MAX];

    if (d == NULL || dst_dir == NULL || d->count == 0) {
        return TIKU_TRK_DROP_NOTHING;
    }
    /* Control asks for a link whatever else is true, and Option forces a
     * copy; both are read at the drop, not at the press. */
    if ((mods & TIKU_TRK_DRAG_MOD_LINK) != 0u) {
        return TIKU_TRK_DROP_LINK;
    }
    /* A merged namespace has no single mutable parent to move from.  Its
     * default drag operation is therefore a link to the resolved node;
     * Option/copy remains an explicit snapshot request (FS-078). */
    if (d->src_virtual && (mods & TIKU_TRK_DRAG_MOD_COPY) == 0u) {
        return TIKU_TRK_DROP_LINK;
    }
    /* A drop into the first item's own parent does nothing unless Option
     * asks for a copy. */
    parent_of(d->path[0], from, sizeof from);
    if (strcmp(from, dst_dir) == 0) {
        return ((mods & TIKU_TRK_DRAG_MOD_COPY) != 0u) ? TIKU_TRK_DROP_COPY
                                                       : TIKU_TRK_DROP_NOTHING;
    }
    if ((mods & TIKU_TRK_DRAG_MOD_COPY) != 0u) {
        return TIKU_TRK_DROP_COPY;
    }
    /* Same volume moves, different volume copies. */
    return same_volume ? TIKU_TRK_DROP_MOVE : TIKU_TRK_DROP_COPY;
}

int
tiku_trk_drag_add_at(tiku_trk_drag_t *d, const char *path, const char *type,
                     int x, int y, int from_list)
{
    int at;

    if (d == NULL) {
        return -1;
    }
    at = d->count;
    if (tiku_trk_drag_add(d, path) != 0) {
        return -1;
    }
    d->ox[at] = x;
    d->oy[at] = y;
    d->from_list = from_list;
    if (type != NULL && type[0] != '\0' && !d->types_built &&
        d->ntype < TIKU_TRK_DRAG_TYPES) {
        int i;

        for (i = 0; i < d->ntype; i++) {
            if (strcmp(d->type[i], type) == 0) {
                return 0;
            }
        }
        snprintf(d->type[d->ntype], sizeof d->type[0], "%s", type);
        d->ntype++;
    }
    return 0;
}

void
tiku_trk_drag_landing(const tiku_trk_drag_t *d, int i, int dx, int dy,
                      int *x, int *y)
{
    if (d == NULL || i < 0 || i >= d->count) {
        return;
    }
    if (d->from_list) {
        if (d->foreign_refs) {
            int col = i % TIKU_TRK_DRAG_FOREIGN_COLS;
            int row = i / TIKU_TRK_DRAG_FOREIGN_COLS;

            if (x != NULL) { *x = dx + col * TIKU_TRK_DRAG_FOREIGN_CELL_W; }
            if (y != NULL) { *y = dy + row * TIKU_TRK_DRAG_FOREIGN_CELL_H; }
            return;
        }
        /* A list has no positions to preserve: a column at the drop
         * point. */
        if (x != NULL) { *x = dx; }
        if (y != NULL) { *y = dy + i * TIKU_TRK_DRAG_STACK; }
        return;
    }
    /* One delta for the whole selection, so it keeps its shape. */
    if (x != NULL) { *x = dx + (d->ox[i] - d->ox[0]); }
    if (y != NULL) { *y = dy + (d->oy[i] - d->oy[0]); }
}

int
tiku_trk_drag_types(tiku_trk_drag_t *d)
{
    if (d == NULL) {
        return 0;
    }
    /* Sealed on the first ask, so tiku_trk_drag_add_at() adds no more
     * types and re-entering a view does not rebuild the list. */
    d->types_built = 1;
    return d->ntype;
}
