/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskicons.c - Desktop mode, placement and the union source.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskicons.h"
#include "tiku_trk_view_defs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
tiku_trk_deskicons_mode(int saved)
{
    /* Coerced when read: a state file from another build may hold list
     * mode. */
    return (saved == TIKU_TRK_MODE_LIST) ? TIKU_TRK_MODE_ICON : saved;
}

/** @brief Whether two rectangles share a pixel; edges that only meet do not
 *         count. */
static int
overlaps(tiku_trk_rect_t a, tiku_trk_rect_t b)
{
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

/** @brief Grow @p r by @p n on every side. */
static tiku_trk_rect_t
grow(tiku_trk_rect_t r, int n)
{
    r.x -= n;
    r.y -= n;
    r.w += 2 * n;
    r.h += 2 * n;
    return r;
}

int
tiku_trk_deskicons_valid(tiku_trk_rect_t r, tiku_trk_rect_t screen,
                         tiku_trk_rect_t deskbar,
                         const tiku_trk_rect_t *others, int nothers)
{
    int i;

    if (r.x < screen.x || r.y < screen.y ||
        r.x + r.w > screen.x + screen.w ||
        r.y + r.h > screen.y + screen.h) {
        return 0;
    }
    if (deskbar.w > 0 && deskbar.h > 0 &&
        overlaps(r, grow(deskbar, TIKU_TRK_DESK_BAR_MARGIN))) {
        return 0;
    }
    for (i = 0; i < nothers; i++) {
        if (overlaps(r, grow(others[i], TIKU_TRK_DESK_OTHER_MARGIN))) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_deskicons_pull_back(tiku_trk_rect_t *r, tiku_trk_rect_t screen)
{
    int ox, oy;

    if (r == NULL) {
        return 0;
    }
    ox = r->x;
    oy = r->y;
    if (r->x + r->w > screen.x + screen.w) {
        r->x = screen.x + screen.w - r->w;
    }
    if (r->y + r->h > screen.y + screen.h) {
        r->y = screen.y + screen.h - r->h;
    }
    /* The near edges are applied last, so a rectangle larger than the
     * screen ends against its top-left corner. */
    if (r->x < screen.x) { r->x = screen.x; }
    if (r->y < screen.y) { r->y = screen.y; }
    return (r->x != ox || r->y != oy);
}

void
tiku_trk_deskicons_reflow(tiku_trk_rect_t *r, tiku_trk_rect_t old_area,
                          tiku_trk_rect_t new_area)
{
    int old_span, new_span, offset;

    if (r == NULL || old_area.w <= 0 || old_area.h <= 0 ||
        new_area.w <= 0 || new_area.h <= 0) {
        return;
    }
    old_span = old_area.w - r->w;
    new_span = new_area.w - r->w;
    if (old_span > 0 && new_span > 0) {
        offset = r->x - old_area.x;
        if (offset < 0) { offset = 0; }
        if (offset > old_span) { offset = old_span; }
        r->x = new_area.x + (int)((int64_t)offset * new_span / old_span);
    } else {
        r->x = new_area.x;
    }
    old_span = old_area.h - r->h;
    new_span = new_area.h - r->h;
    if (old_span > 0 && new_span > 0) {
        offset = r->y - old_area.y;
        if (offset < 0) { offset = 0; }
        if (offset > old_span) { offset = old_span; }
        r->y = new_area.y + (int)((int64_t)offset * new_span / old_span);
    } else {
        r->y = new_area.y;
    }
}

int
tiku_trk_deskicons_find_free(tiku_trk_rect_t *r, tiku_trk_rect_t area,
                             const tiku_trk_rect_t *others, int nothers,
                             int step_x, int step_y)
{
    tiku_trk_rect_t candidate, best = { 0, 0, 0, 0 };
    int64_t best_distance = INT64_MAX;
    int x, y;

    if (r == NULL || area.w < r->w || area.h < r->h) {
        return 0;
    }
    if (step_x <= 0) { step_x = r->w + TIKU_TRK_DESK_OTHER_MARGIN; }
    if (step_y <= 0) { step_y = r->h + TIKU_TRK_DESK_OTHER_MARGIN; }
    candidate = *r;
    for (y = area.y; y + r->h <= area.y + area.h; y += step_y) {
        for (x = area.x; x + r->w <= area.x + area.w; x += step_x) {
            int64_t dx = x - r->x;
            int64_t dy = y - r->y;
            int64_t distance = dx * dx + dy * dy;

            candidate.x = x;
            candidate.y = y;
            if (distance >= best_distance ||
                !tiku_trk_deskicons_valid(candidate, area,
                    (tiku_trk_rect_t){ 0, 0, 0, 0 }, others, nothers)) {
                continue;
            }
            best = candidate;
            best_distance = distance;
        }
    }
    if (best_distance == INT64_MAX) {
        return 0;
    }
    *r = best;
    return 1;
}

void
tiku_trk_deskicons_remember(tiku_trk_deskpos_set_t *s, int sw, int sh,
                            int x, int y)
{
    int i;

    if (s == NULL) {
        return;
    }
    /* One position per screen size, so each size gets back the
     * arrangement made on it. */
    for (i = 0; i < s->n; i++) {
        if (s->at[i].sw == sw && s->at[i].sh == sh) {
            s->at[i].x = x;
            s->at[i].y = y;
            return;
        }
    }
    if (s->n >= TIKU_TRK_DESK_POS_MAX) {
        return;
    }
    s->at[s->n].sw = sw;
    s->at[s->n].sh = sh;
    s->at[s->n].x = x;
    s->at[s->n].y = y;
    s->n++;
}

int
tiku_trk_deskicons_recall(const tiku_trk_deskpos_set_t *s, int sw, int sh,
                          int *x, int *y)
{
    int i;

    if (s == NULL) {
        return 0;
    }
    for (i = 0; i < s->n; i++) {
        if (s->at[i].sw == sw && s->at[i].sh == sh) {
            if (x != NULL) { *x = s->at[i].x; }
            if (y != NULL) { *y = s->at[i].y; }
            return 1;
        }
    }
    return 0;
}

void
tiku_trk_deskicons_sources(const tiku_volumes_t *vs,
                           tiku_backend_t *b, const char *home,
                           tiku_vdir_t *out)
{
    int i;

    if (out == NULL) {
        return;
    }
    tiku_vdir_init(out);
    if (vs == NULL || b == NULL) {
        return;
    }
    if (home != NULL && home[0] != '\0') {
        tiku_model_t hm;

        /* First, so what the user put on their own Desktop is what shows
         * when another volume holds the same name. */
        if (b->ops->stat(b, home, &hm) == 0 &&
            tiku_model_is_container(&hm)) {
            (void)tiku_vdir_add(out, home);
        }
    }
    for (i = 0; i < vs->n; i++) {
        char path[TIKU_PATH_MAX];
        tiku_model_t m;

        /* Only volumes that show an icon contribute (PVN-041, PVN-051): a
         * volume mounted inside another shows none, so its Desktop folder
         * is left out. */
        if (!tiku_volume_shows_icon(vs, &vs->v[i])) {
            continue;
        }
        if (snprintf(path, sizeof path, "%s%sDesktop", vs->v[i].mount,
                     (vs->v[i].mount[strlen(vs->v[i].mount) - 1u] == '/')
                         ? "" : "/") < 0) {
            continue;
        }
        if (b->ops->stat(b, path, &m) == 0 &&
            tiku_model_is_container(&m)) {
            (void)tiku_vdir_add(out, path);
        }
    }
}

int
tiku_trk_deskicons_volume_pose(const tiku_volumes_t *vs,
                               const tiku_volume_t *v,
                               int volumes_on_desktop, int shared_too)
{
    if (vs == NULL || v == NULL || !volumes_on_desktop) {
        return 0;
    }
    if (v->is_boot) {
        return 0;               /* the boot volume is the Disks pose */
    }
    if (!v->persistent) {
        return 0;
    }
    if (v->shared && !shared_too) {
        return 0;
    }
    return tiku_volume_shows_icon(vs, v);
}

int
tiku_trk_deskicons_show_disks(int show_disks_icon, int volumes_on_desktop)
{
    /* Either setting alone puts the root on the Desktop. */
    return (show_disks_icon || volumes_on_desktop) ? 1 : 0;
}
