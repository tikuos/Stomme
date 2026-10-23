/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview_iconmode.c - the modes and icon sizes, the icon grid
 * and auto-placement, the geometry a pose is drawn and hit-tested in, the
 * rubber band, and where a dropped entry lands.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_poseview_private.h"

int
tiku_trk_poseview_mode(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->mode : TIKU_TRK_MODE_LIST;
}

int
tiku_trk_poseview_icon_size(const tiku_trk_poseview_t *v)
{
    return (v != NULL) ? v->icon_size : 32;
}

void
tiku_trk_poseview_set_icon_size(tiku_trk_poseview_t *v, int px)
{
    if (v != NULL && px >= 16 && px <= 128) {
        v->icon_size = px;
        v->state_dirty = 1;
    }
}

/** @brief The size ladder, in the order Cmd-+ walks it (PVL-003). */
static const int icon_ladder[] = { 32, 40, 48, 64, 96, 128 };

int
tiku_trk_poseview_step_icon_size(tiku_trk_poseview_t *v, int up)
{
    int i, n = (int)(sizeof icon_ladder / sizeof icon_ladder[0]);

    if (v == NULL || v->mode != TIKU_TRK_MODE_ICON) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (icon_ladder[i] == v->icon_size) {
            break;
        }
    }
    if (i >= n) {
        /* Not on the ladder: the step starts from the first rung at or
         * above the size. */
        for (i = 0; i < n - 1 && icon_ladder[i] < v->icon_size; i++) {
        }
    }
    /* The ends are a no-op, not a wrap. */
    {
        /* The point at the middle of the window, in the unscaled space
         * positions are stored in, which a size change does not move. */
        float was = pv_mode_scale(v->mode, v->icon_size);
        float cx = ((float)v->icon_x + (float)v->view_w / 2.0f) / was;
        float cy = ((float)v->icon_y + (float)v->view_h / 2.0f) / was;

        if (up && i + 1 < n) {
            v->icon_size = icon_ladder[i + 1];
        } else if (!up && i > 0) {
            v->icon_size = icon_ladder[i - 1];
        } else {
            return 0;
        }
        /* That point goes back to the middle of the window: the origin is
         * in pixels, and every pixel position moved (PVL-010). */
        pv_extent_clear(v);
        {
            float now = pv_mode_scale(v->mode, v->icon_size);

            tiku_trk_poseview_set_origin(v,
                (int)(cx * now - (float)v->view_w / 2.0f),
                (int)(cy * now - (float)v->view_h / 2.0f));
        }
    }
    v->state_dirty = 1;
    return 1;
}

/** @brief The factor icon-mode positions are scaled by: the icon size over
 *         32; every other mode is 1. */
float
pv_mode_scale(int mode, int size)
{
    return (mode == TIKU_TRK_MODE_ICON) ? (float)size / 32.0f : 1.0f;
}

/**
 * @brief Carry existing positions from one icon grid into another (PVL-011).
 *
 * Each position is pinned to the old grid, and the cell count and the
 * offset within the cell are converted separately, so the arrangement and
 * any nudge off the grid carry across a mode whose cell is a different
 * shape.
 */
static void
map_icons(tiku_trk_poseview_t *v, int old_mode, int old_size)
{
    int ogx, ogy, oox, ooy, ngx, ngy, nox, noy, i;
    float oscale = pv_mode_scale(old_mode, old_size);
    float nscale = pv_mode_scale(v->mode, v->icon_size);
    int saved_mode = v->mode, saved_size = v->icon_size;

    v->mode = old_mode;
    v->icon_size = old_size;
    tiku_trk_poseview_layout_info(v, &ogx, &ogy, &oox, &ooy);
    v->mode = saved_mode;
    v->icon_size = saved_size;
    tiku_trk_poseview_layout_info(v, &ngx, &ngy, &nox, &noy);
    if (ogx <= 0 || ogy <= 0 || ngx <= 0 || ngy <= 0) {
        return;
    }
    for (i = 0; i < v->count; i++) {
        tiku_trk_pose_t *p = &v->poses[i];
        int x, y, px, py, dx, dy;

        if (!p->placed) {
            continue;
        }
        x = (int)((float)p->loc_x * oscale);
        y = (int)((float)p->loc_y * oscale);
        px = x;
        py = y;
        {   /* Pin to the old grid to find the cell and the nudge. */
            float fx = (float)(px - oox) / (float)ogx;
            float fy = (float)(py - ooy) / (float)ogy;
            px = (int)((fx >= 0.0f) ? (fx + 0.5f) : (fx - 0.5f)) * ogx + oox;
            py = (int)((fy >= 0.0f) ? (fy + 0.5f) : (fy - 0.5f)) * ogy + ooy;
        }
        dx = x - px;
        dy = y - py;
        px -= oox;
        py -= ooy;
        px = (px >= 0) ? (px / ogx) * ngx : -(((-px) + ogx - 1) / ogx) * ngx;
        py = (py >= 0) ? (py / ogy) * ngy : -(((-py) + ogy - 1) / ogy) * ngy;
        if (dx != 0 || dy != 0) {
            /* The nudge is scaled by the ratio of the cells, rounded to the
             * nearest pixel. */
            float sx = (float)dx * (float)ngx / (float)ogx;
            float sy = (float)dy * (float)ngy / (float)ogy;

            dx = (int)((sx >= 0.0f) ? (sx + 0.5f) : (sx - 0.5f));
            dy = (int)((sy >= 0.0f) ? (sy + 0.5f) : (sy - 0.5f));
            px += dx;
            py += dy;
        }
        px += nox;
        py += noy;
        p->loc_x = (int)((float)px / nscale);
        p->loc_y = (int)((float)py / nscale);
    }
    v->state_dirty = 1;
}

void
tiku_trk_poseview_hint(const tiku_trk_poseview_t *v, int *x, int *y)
{
    if (v == NULL) {
        return;
    }
    if (x != NULL) { *x = v->hint_x; }
    if (y != NULL) { *y = v->hint_y; }
}

/** @brief The first grid line at or past @p origin plus the offset @p o,
 *         on a grid of pitch @p g; the offset itself with no grid. */
static int
first_cell(int origin, int g, int o)
{
    int k;

    if (g <= 0) {
        return o;
    }
    k = (origin >= 0) ? (origin + g - 1) / g : -((-origin) / g);
    return k * g + o;
}

/** @brief The view's horizontal and vertical scroll origin; 0, 0 in list
 *         mode, which places from the top-left of the document. */
static void
place_origin(const tiku_trk_poseview_t *v, int *x, int *y)
{
    *x = 0;
    *y = 0;
    if (v->mode != TIKU_TRK_MODE_LIST) {
        tiku_trk_poseview_origin(v, x, y);
    }
}

void
tiku_trk_poseview_reset_hint(tiku_trk_poseview_t *v)
{
    int gx, gy, ox, oy, sx, sy;

    if (v == NULL) {
        return;
    }
    /* PVL-017: back to the top-left of what is on screen, so the next
     * folder's auto-placement does not carry on from the last folder's
     * last icon.  The hint is the first grid point at or past the view's
     * top-left plus the layout offset. */
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    place_origin(v, &sx, &sy);
    v->hint_x = first_cell(sx, gx, ox);
    v->hint_y = first_cell(sy, gy, oy);
}

void
tiku_trk_poseview_set_mode(tiku_trk_poseview_t *v, int mode)
{
    int old_mode;

    if (v == NULL || v->mode == mode ||
        (v->is_query && mode != TIKU_TRK_MODE_LIST)) {
        return;
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* Leaving list mode cancels type-ahead filtering, which only makes
         * sense against column text (PVL-005). */
        tiku_trk_poseview_type_clear(v);
        tiku_trk_poseview_set_filtering(v, 0);
    }
    old_mode = v->mode;
    v->mode = mode;
    v->state_dirty = 1;
    /* Positions are carried into the new icon grid; list mode has no grid.
     * Leaving list mode, they are mapped from the icon grid last live
     * (MA-061). */
    if (mode != TIKU_TRK_MODE_LIST) {
        int from = (old_mode != TIKU_TRK_MODE_LIST) ? old_mode
                                                    : v->last_icon;

        if (from != mode) {
            map_icons(v, from, v->icon_size);
        }
        v->last_icon = mode;
    }
    /* Every rect changed shape, so the box is forgotten, and the mode's
     * origin is clamped against the rebuilt one. */
    pv_extent_clear(v);
    {
        int x, y;

        tiku_trk_poseview_origin(v, &x, &y);
        tiku_trk_poseview_set_origin(v, x, y);
    }
    tiku_trk_poseview_reset_hint(v);
}

void
tiku_trk_poseview_layout_info(const tiku_trk_poseview_t *v, int *gx, int *gy,
                              int *ox, int *oy)
{
    int s;

    if (v == NULL) {
        return;
    }
    s = tiku_trk_poseview_art_size(v);
    switch (v->mode) {
    case TIKU_TRK_MODE_MINI:
        /* PVL-013: a wide, short cell, because a mini-icon row is a label.
         * Its height is the taller of the art and the label, since the two
         * sit side by side (PVL-007). */
        *gx = s * 6;
        *gy = (v->font_h > 0 && v->font_h + 1 > s) ? v->font_h + 1 : s;
        *ox = (int)(s * 0.6f + 0.999f);
        *oy = (int)(s * 0.3f + 0.999f);
        break;
    case TIKU_TRK_MODE_ICON:
        *gx = s + (int)(s * 0.875f + 0.999f);
        /* The label sits under the icon, so the cell is as tall as both
         * (PVL-007). */
        *gy = (v->font_h > 0) ? s + v->font_h + 1 : *gx;
        *ox = (int)(s * 0.625f + 0.999f);
        *oy = *ox;
        break;
    default:
        *gx = 0;                  /* no grid in list mode */
        *gy = 0;
        *ox = 3;
        *oy = 3;
        break;
    }
}

void
tiku_trk_poseview_pin_to_grid(const tiku_trk_poseview_t *v, int *x, int *y)
{
    int gx, gy, ox, oy;

    if (v == NULL) {
        return;
    }
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    if (gx == 0 || gy == 0) {
        return;                   /* PVL-014: a no-op with no grid */
    }
    /* Round to the nearest cell, negative coordinates included. */
    {
        float fx = (float)(*x - ox) / (float)gx;
        float fy = (float)(*y - oy) / (float)gy;
        *x = (int)((fx >= 0.0f) ? (fx + 0.5f) : (fx - 0.5f)) * gx + ox;
        *y = (int)((fy >= 0.0f) ? (fy + 0.5f) : (fy - 0.5f)) * gy + oy;
    }
}

int
tiku_trk_poseview_open_anim_rect(const tiku_trk_poseview_t *v, int row,
                                 int row_h, int *x, int *y, int *w, int *h)
{
    const tiku_trk_pose_t *p;

    if (v == NULL || row < 0 || row >= tiku_trk_poseview_count(v)) {
        return 0;
    }
    p = v->filtering ? &v->poses[v->filtered[row]] : &v->poses[row];
    if (v->mode != TIKU_TRK_MODE_LIST) {
        int cell = (v->mode == TIKU_TRK_MODE_MINI) ? 16 : v->icon_size;
        int px = 0;
        int py = 0;

        tiku_trk_poseview_pose_location(v, p, &px, &py);
        *x = px;
        *y = py;
        *w = cell;
        *h = cell;
        return 1;
    }
    *x = 2;
    *y = 2 + (row - v->scroll) * row_h;
    /* The first column's measured width (PVS-013). */
    *w = (v->column_count > 0) ? v->columns[0].width : 200;
    *h = row_h;
    return 1;
}

void
tiku_trk_poseview_pose_location(const tiku_trk_poseview_t *v,
                                const tiku_trk_pose_t *p, int *x, int *y)
{
    float scale;

    if (v == NULL || p == NULL) {
        return;
    }
    /* Stored unscaled at 32 px and scaled on the way out, so one arrangement
     * serves every icon size (PVL-012).  Mini and list mode do not scale. */
    scale = (v->mode == TIKU_TRK_MODE_ICON)
            ? (float)v->icon_size / 32.0f : 1.0f;
    *x = (int)((float)p->loc_x * scale);
    *y = (int)((float)p->loc_y * scale);
}

void
tiku_trk_poseview_move_pose(tiku_trk_poseview_t *v, tiku_trk_pose_t *p,
                            int x, int y, int snap)
{
    float scale;

    if (v == NULL || p == NULL) {
        return;
    }
    if (snap) {
        tiku_trk_poseview_pin_to_grid(v, &x, &y);
    }
    scale = (v->mode == TIKU_TRK_MODE_ICON)
            ? (float)v->icon_size / 32.0f : 1.0f;
    p->loc_x = (int)((float)x / scale);
    p->loc_y = (int)((float)y / scale);
    p->placed = 1;
    p->auto_placed = 0;           /* the user put it there */
    /* Grown in place.  A move only pushes the box outwards; shrinking it
     * needs the walk that removal does. */
    pv_extent_add(v, p);
    v->state_dirty = 1;
}

/** @brief The grid cell one pose takes, or the icon size where there is no
 *         grid. */
static void
pose_extent(const tiku_trk_poseview_t *v, int *w, int *h)
{
    int gx, gy, ox, oy;

    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    *w = (gx > 0) ? gx : v->icon_size;
    *h = (gy > 0) ? gy : v->icon_size;
}

/**
 * @brief Whether the candidate slot at @p x, @p y is taken by a placed pose
 *        other than @p self.
 *
 * Poses that do not overlap the candidate's vertical span are skipped
 * before the horizontal test (PVL-016).
 */
static int
slot_occupied(tiku_trk_poseview_t *v, int x, int y, const tiku_trk_pose_t *self)
{
    int i, w, h;

    pose_extent(v, &w, &h);
    for (i = 0; i < v->count; i++) {
        tiku_trk_pose_t *p = &v->poses[i];
        int px, py;

        if (p == self || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_location(v, p, &px, &py);
        if (py + h <= y || py >= y + h) {
            continue;             /* not in this band */
        }
        /* The footprint is widened by TIKU_TRK_PLACE_GAP on each side, so
         * two placed icons never touch (PVL-015). */
        if (px + w + TIKU_TRK_PLACE_GAP > x &&
            px - TIKU_TRK_PLACE_GAP < x + w) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_poseview_art_size(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return TIKU_TRK_MINI_ICON;
    }
    /* Mini-icon and list modes draw at the mini size whatever the icon
     * size is set to. */
    return (v->mode == TIKU_TRK_MODE_ICON) ? v->icon_size
                                           : TIKU_TRK_MINI_ICON;
}

/** @brief Advance the hint to the next slot, wrapping at the right edge of
 *         the view to its left edge. */
static void
next_slot(tiku_trk_poseview_t *v, int view_w)
{
    int gx, gy, ox, oy, w, h, sx, sy;

    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    pose_extent(v, &w, &h);
    place_origin(v, &sx, &sy);
    v->hint_x += (gx > 0) ? gx : w;
    if (v->hint_x + w > sx + view_w) {
        v->hint_x = first_cell(sx, gx, ox);
        v->hint_y += (gy > 0) ? gy : h;
        tiku_trk_poseview_pin_to_grid(v, &v->hint_x, &v->hint_y);
    }
}

/** @brief Place one pose at the first free slot from the hint. */
static void
place_pose(tiku_trk_poseview_t *v, tiku_trk_pose_t *p, int view_w)
{
    int tries = 0;
    float scale = (v->mode == TIKU_TRK_MODE_ICON)
                  ? (float)v->icon_size / 32.0f : 1.0f;

    while (slot_occupied(v, v->hint_x, v->hint_y, p) && tries < 4096) {
        next_slot(v, view_w);
        tries++;
    }
    if (tries >= 4096) {
        /* PVL-019: nowhere free, so the icon goes to the grid origin. */
        int gx, gy;

        tiku_trk_poseview_layout_info(v, &gx, &gy, &v->hint_x, &v->hint_y);
    }
    p->loc_x = (int)((float)v->hint_x / scale);
    p->loc_y = (int)((float)v->hint_y / scale);
    p->placed = 1;
    p->auto_placed = 1;
    next_slot(v, view_w);
}

int
tiku_trk_poseview_place_unplaced(tiku_trk_poseview_t *v, int view_w)
{
    int i, gx, gy, ox, oy, n = 0;

    if (v == NULL) {
        return 0;
    }
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    (void)gx;
    (void)gy;
    /* The hint restarts at the top-left of the view (PVL-017).  A hint
     * still at (0,0) has never been set and starts at the layout offset; a
     * mode change, a directory switch, a reload and a resize reset it. */
    if (v->hint_x == 0 && v->hint_y == 0) {
        v->hint_x = ox;
        v->hint_y = oy;
    }
    for (i = 0; i < v->count; i++) {
        int lx, ly;

        if (v->poses[i].placed) {
            continue;
        }
        /* A landing reserved before the entry existed (where a drop
         * landed, or the click that made a folder) is taken here as well
         * as by a creation notice, so an entry that arrives by a reload
         * lands there too (PVL-025, PVL-098). */
        if (v->mode != TIKU_TRK_MODE_LIST &&
            pv_take_landing(v, v->poses[i].model.path, &lx, &ly)) {
            float scale = pv_mode_scale(v->mode, v->icon_size);

            v->poses[i].loc_x = (int)((float)lx / scale);
            v->poses[i].loc_y = (int)((float)ly / scale);
            v->poses[i].placed = 1;
            v->poses[i].auto_placed = 0;
            pv_extent_add(v, &v->poses[i]);
            n++;
            continue;
        }
        place_pose(v, &v->poses[i], view_w);
        pv_extent_add(v, &v->poses[i]);
        n++;
    }
    return n;
}

void
tiku_trk_poseview_cleanup(tiku_trk_poseview_t *v, int view_w)
{
    int i, gx, gy, ox, oy;

    if (v == NULL) {
        return;
    }
    /* List mode has no grid: laying out there would stack every icon at
     * the origin and lose the icon-mode arrangement. */
    if (v->mode == TIKU_TRK_MODE_LIST) {
        return;
    }
    pv_resort(v);
    tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
    for (i = 0; i < v->count; i++) {
        v->poses[i].placed = 0;
    }
    /* Every icon moves, so the box is rebuilt from the new layout. */
    pv_extent_clear(v);
    v->hint_x = ox;
    v->hint_y = oy;
    (void)tiku_trk_poseview_place_unplaced(v, view_w);
    v->state_dirty = 1;
}

int
tiku_trk_poseview_snap_to_grid(tiku_trk_poseview_t *v)
{
    int i, moved = 0;

    if (v == NULL || v->mode == TIKU_TRK_MODE_LIST) {
        return 0;
    }
    {
        /* Snapped in the unscaled space positions are stored in, where
         * snapping an icon already on the grid leaves it in place. */
        int gx, gy, ox, oy;
        float sc = pv_mode_scale(v->mode, v->icon_size);
        float ugx, ugy, uox, uoy;

        tiku_trk_poseview_layout_info(v, &gx, &gy, &ox, &oy);
        if (gx <= 0 || gy <= 0 || sc <= 0.0f) {
            return 0;
        }
        ugx = (float)gx / sc;
        ugy = (float)gy / sc;
        uox = (float)ox / sc;
        uoy = (float)oy / sc;
        for (i = 0; i < v->count; i++) {
            tiku_trk_pose_t *p = &v->poses[i];
            int nx, ny;

            if (!p->placed) {
                continue;
            }
            nx = (int)(((float)p->loc_x - uox) / ugx + 0.5f) *
                 (int)(ugx + 0.5f) + (int)(uox + 0.5f);
            ny = (int)(((float)p->loc_y - uoy) / ugy + 0.5f) *
                 (int)(ugy + 0.5f) + (int)(uoy + 0.5f);
            if (nx == p->loc_x && ny == p->loc_y) {
                continue;
            }
            p->loc_x = nx;
            p->loc_y = ny;
            pv_extent_add(v, p);
            moved++;
        }
    }
    if (moved > 0) {
        v->state_dirty = 1;
    }
    return moved;
}

void
tiku_trk_poseview_set_measure(tiku_trk_poseview_t *v, tiku_trk_measure_fn fn,
                              void *ctx)
{
    if (v != NULL) {
        v->measure = fn;
        v->measure_ctx = ctx;
    }
}

void
tiku_trk_poseview_set_font_metrics(tiku_trk_poseview_t *v, int height,
                                   int ascent)
{
    if (v != NULL && height > 0) {
        v->font_h = height;
        v->font_a = ascent;
    }
}

int
tiku_trk_poseview_row_height(const tiku_trk_poseview_t *v)
{
    if (v == NULL) {
        return 20;              /* the reference face's row */
    }
    if (v->row_h > 0) {
        /* What the owner draws with, so a band sweeps rows at the pitch
         * drawn on screen. */
        return v->row_h;
    }
    if (v->font_h <= 0) {
        return 20;
    }
    /* ceil(font height * 1.65), which leaves a mini icon room in the
     * row. */
    return (int)((float)v->font_h * 1.65f + 0.999f);
}

void
tiku_trk_poseview_set_folders_first(tiku_trk_poseview_t *v, int on)
{
    if (v != NULL && v->folders_first != (on ? 1 : 0)) {
        v->folders_first = on ? 1 : 0;
        pv_resort(v);
    }
}

void
tiku_trk_poseview_set_hide_dotfiles(tiku_trk_poseview_t *v, int on)
{
    int value;

    if (v == NULL) {
        return;
    }
    value = on ? 1 : 0;
    if (v->hide_dotfiles == value) {
        return;
    }
    v->hide_dotfiles = value;
    (void)tiku_trk_poseview_reload(v);
}

int
tiku_trk_poseview_tab_next(tiku_trk_poseview_t *v, int backwards)
{
    tiku_trk_pose_t *active;
    const char *needle = "";
    int best = -1, i, n;
    const char *best_name = NULL;

    if (v == NULL || v->filtering || v->count <= 0) {
        return 0;
    }
    active = tiku_trk_poseview_active(v);
    if (active != NULL) {
        needle = active->model.name;
    }
    n = v->count;
    for (i = 0; i < n; i++) {
        const char *name = v->poses[i].model.name;
        int cmp = strcasecmp(name, needle);

        if ((!backwards && cmp > 0) || (backwards && cmp < 0)) {
            if (best < 0 || (!backwards && strcasecmp(name, best_name) < 0) ||
                (backwards && strcasecmp(name, best_name) > 0)) {
                best = i;
                best_name = name;
            }
        }
    }
    if (best < 0) {
        for (i = 0; i < n; i++) {
            const char *name = v->poses[i].model.name;

            if (best < 0 || (!backwards && strcasecmp(name, best_name) < 0) ||
                (backwards && strcasecmp(name, best_name) > 0)) {
                best = i;
                best_name = name;
            }
        }
    }
    if (best < 0) {
        return 0;
    }
    tiku_trk_poseview_click(v, best, 0u);
    return 1;
}

void
tiku_trk_poseview_set_row_height(tiku_trk_poseview_t *v, int h)
{
    if (v != NULL) {
        v->row_h = (h > 0) ? h : 0;
    }
}

void
tiku_trk_poseview_set_icon_hit(tiku_trk_poseview_t *v, tiku_trk_hit_fn fn,
                               void *ctx)
{
    if (v != NULL) {
        v->icon_hit = fn;
        v->icon_hit_ctx = ctx;
    }
}

int
tiku_trk_poseview_fit_label(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p,
                            char *out, size_t max)
{
    const char *name;

    if (v == NULL || p == NULL || out == NULL || max == 0u) {
        return 0;
    }
    name = tiku_model_display_name(&p->model);
    if (v->measure == NULL) {
        snprintf(out, max, "%s", name);
        return (int)strlen(out);
    }
    return tiku_text_fit(name, TIKU_TRK_LABEL_CHARS * v->measure("M",
                                 v->measure_ctx),
                             TIKU_TRUNC_MIDDLE, v->measure,
                             v->measure_ctx, out, max);
}

/** @brief The row a pose is shown on in list mode: its place among the
 *         filtered rows while filtering, and its index when the view is
 *         not filtering or the filter leaves it out. */
static int
list_row(const tiku_trk_poseview_t *v, const tiku_trk_pose_t *p)
{
    int row = pv_display_of(v, p->index);

    return (row >= 0) ? row : p->index;
}

void
tiku_trk_poseview_pose_parts(tiku_trk_poseview_t *v,
                             const tiku_trk_pose_t *p, int *ix, int *iy,
                             int *iw, int *ih, int *lx, int *ly, int *lw,
                             int *lh)
{
    int px, py, cw, ch, art, tw = 0;

    if (v == NULL || p == NULL) {
        return;
    }
    pose_extent(v, &cw, &ch);
    art = tiku_trk_poseview_art_size(v);
    tiku_trk_poseview_pose_location(v, p, &px, &py);
    if (v->measure != NULL) {
        /* Measured from the label as it is drawn, elided (IV-047). */
        char fit[TIKU_NAME_MAX * 2];

        if (v->mode != TIKU_TRK_MODE_LIST &&
            tiku_trk_poseview_fit_label(v, p, fit, sizeof fit) > 0) {
            tw = v->measure(fit, v->measure_ctx);
        } else {
            tw = v->measure(tiku_model_display_name(&p->model),
                            v->measure_ctx);
        }
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* In list mode a pose's place is its row as shown; the icon-mode
         * location it also carries is ignored. */
        int rh = tiku_trk_poseview_row_height(v);

        py = list_row(v, p) * rh;
        if (ix != NULL) { *ix = 2; *iy = py; *iw = art; *ih = rh; }
        if (lx != NULL) {
            *lx = 2 + art + 4;
            *ly = py;
            *lw = tw;
            *lh = rh;
        }
        return;
    }
    if (v->mode == TIKU_TRK_MODE_MINI) {
        if (ix != NULL) { *ix = px; *iy = py; *iw = art; *ih = art; }
        if (lx != NULL) {
            *lx = px + art + 4;
            *ly = py;
            *lw = tw;
            *lh = (v->font_h > 0) ? v->font_h : art;
        }
        return;
    }
    /* Icon mode: the art centred in the cell, the label under it. */
    if (ix != NULL) {
        *ix = px + ((cw > art) ? (cw - art) / 2 : 0);
        *iy = py;
        *iw = art;
        *ih = art;
    }
    if (lx != NULL) {
        int h = (v->font_h > 0) ? v->font_h : 0;

        *lx = px + ((cw - tw) / 2);
        *ly = py + art + 1;
        *lw = tw;
        *lh = h;
    }
}

void
tiku_trk_poseview_pose_rect(tiku_trk_poseview_t *v, const tiku_trk_pose_t *p,
                            int *x, int *y, int *w, int *h)
{
    int ix, iy, iw, ih, lx, ly, lw, lh, x0, y0, x1, y1;

    if (v == NULL || p == NULL) {
        return;
    }
    if (v->mode == TIKU_TRK_MODE_LIST) {
        /* In list mode the whole row: a click anywhere across a row hits
         * its pose, and a band sweeps the same box. */
        int rh = tiku_trk_poseview_row_height(v);

        *x = 0;
        *y = list_row(v, p) * rh;
        *w = (v->view_w > 0) ? v->view_w : 1;
        *h = rh;
        return;
    }
    /* The union of the parts, derived from pose_parts so a band and a
     * click work from one piece of arithmetic. */
    tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih, &lx, &ly, &lw,
                                 &lh);
    x0 = (lw > 0 && lx < ix) ? lx : ix;
    y0 = (lh > 0 && ly < iy) ? ly : iy;
    x1 = (lw > 0 && lx + lw > ix + iw) ? lx + lw : ix + iw;
    y1 = (lh > 0 && ly + lh > iy + ih) ? ly + lh : iy + ih;
    {
        /* Never narrower than the cell in mini mode, so a band through
         * the empty end of the cell still catches a pose with a short
         * name. */
        int cw, ch;

        pose_extent(v, &cw, &ch);
        if (v->mode == TIKU_TRK_MODE_MINI && x1 - x0 < cw) {
            x1 = x0 + cw;
        }
    }
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
}

/** @brief Whether a point is inside a rectangle. */
static int
in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

tiku_trk_pose_t *
tiku_trk_poseview_pose_at(tiku_trk_poseview_t *v, int x, int y)
{
    int i, n;

    if (v == NULL) {
        return NULL;
    }
    n = tiku_trk_poseview_count(v);
    /* Last drawn wins, so the topmost icon under the point is the one hit. */
    for (i = n - 1; i >= 0; i--) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int ix, iy, iw, ih, lx, ly, lw, lh;

        if (p == NULL || !p->placed) {
            continue;
        }
        tiku_trk_poseview_pose_parts(v, p, &ix, &iy, &iw, &ih, &lx, &ly,
                                     &lw, &lh);
        if (in_rect(x, y, lx, ly, lw, lh)) {
            return p;           /* the name is hit where it is drawn */
        }
        if (!in_rect(x, y, ix, iy, iw, ih)) {
            /* The gap between an icon and its name, and the space beside a
             * short label, belong to no pose, so a band can start there
             * (PVS-019). */
            continue;
        }
        if (v->icon_hit != NULL) {
            /* Opaque pixels only: an icon's transparent corners belong to
             * the gap, so a band can start between two adjacent icons
             * (IV-026). */
            if (v->icon_hit(&p->model, iw, x - ix, y - iy,
                            v->icon_hit_ctx)) {
                return p;
            }
            continue;
        }
        return p;               /* no hook: the icon square */
    }
    return NULL;
}

int
tiku_trk_poseview_select_rect(tiku_trk_poseview_t *v, int x0, int y0, int x1,
                              int y1, unsigned mods)
{
    int i, n, hits = 0;
    int lox = (x0 < x1) ? x0 : x1, hix = (x0 < x1) ? x1 : x0;
    int loy = (y0 < y1) ? y0 : y1, hiy = (y0 < y1) ? y1 : y0;

    if (v == NULL) {
        return 0;
    }
    n = tiku_trk_poseview_count(v);
    /* Only the sliver between where the band was and where it is now is
     * repainted (PVL-070). */
    {
        /* Full width, whatever the band's shape: what is repainted is
         * whole rows. */
        tiku_trk_repaint_rect_t now = { 0, loy,
                                        (v->view_w > 0) ? v->view_w : 1,
                                        (hiy > loy) ? hiy - loy : 1 };

        (void)lox; (void)hix;

        if (v->band_seen) {
            tiku_trk_repaint_band(&v->repaint, v->band_rect, now);
        } else {
            tiku_trk_repaint_add(&v->repaint, now);
        }
        v->band_rect = now;
        v->band_seen = 1;
    }
    if (v->single_only) {
        /* In a single-selection view the additive modifiers are dropped
         * and the sweep stops at the first pose it touches (PVS-009). */
        mods &= ~(TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD);
    }
    /* Outside a tracked band the rectangle adds to the selection with a
     * modifier, and replaces it without one. */
    if (!v->band && !(mods & (TIKU_TRK_MOD_SHIFT | TIKU_TRK_MOD_CMD))) {
        pv_clear_selection(v);
    }
    for (i = 0; i < n; i++) {
        tiku_trk_pose_t *p = tiku_trk_poseview_at(v, i);
        int px, py, pw, ph, inside;

        if (p == NULL ||
            (!p->placed && v->mode != TIKU_TRK_MODE_LIST)) {
            /* An unplaced pose has no rectangle to sweep -- except in a
             * list, where the row is the place and nothing was ever
             * placed. */
            continue;
        }
        tiku_trk_poseview_pose_rect(v, p, &px, &py, &pw, &ph);
        inside = (px + pw > lox && px < hix && py + ph > loy && py < hiy);
        if (v->single_only) {
            pv_set_selected(p, inside && hits == 0);
            if (inside && hits == 0) {
                hits++;
                v->pivot = p->index;
            }
        } else if (v->band) {
            /* PVS-027: within a band each pose is toggled against what it
             * was when the band started, so a pose the band leaves again
             * returns to that state. */
            int was = v->band_was[p->index];

            pv_set_selected(p, inside ? !was : was);
            if (inside) {
                hits++;
                if (v->pivot < 0 && !was) {
                    v->pivot = p->index;
                }
            }
        } else if (inside) {
            pv_set_selected(p, 1);
            hits++;
            if (v->single_only) {
                break;
            }
            if (v->pivot < 0) {
                v->pivot = p->index;
            }
        }
    }
    return hits;
}

void
tiku_trk_poseview_set_drop_point(tiku_trk_poseview_t *v, int x, int y)
{
    if (v != NULL) {
        v->drop_x = x;
        v->drop_y = y;
        v->drop_valid = 1;
    }
}

void
tiku_trk_poseview_clear_drop_point(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->drop_valid = 0;
    }
}

void
tiku_trk_poseview_drop_land(tiku_trk_poseview_t *v, const char *name, int x,
                            int y)
{
    int i;

    if (v == NULL || name == NULL || name[0] == '\0') {
        return;
    }
    for (i = 0; i < v->land_count; i++) {
        if (strcmp(v->land[i].name, name) == 0) {
            break;              /* a second drop of the same name wins */
        }
    }
    if (i == v->land_count) {
        if (v->land_count >= TIKU_TRK_LAND_MAX) {
            /* More names than the table holds: the rest are auto-placed,
             * and the reservations already made stand. */
            return;
        }
        v->land_count++;
        snprintf(v->land[i].name, sizeof v->land[i].name, "%s", name);
    }
    v->land[i].x = x;
    v->land[i].y = y;
}

void
tiku_trk_poseview_clear_landings(tiku_trk_poseview_t *v)
{
    if (v != NULL) {
        v->land_count = 0;
    }
}

/**
 * @brief Take the landing reserved for @p path's name, if there is one.
 *
 * The reservation is consumed, so a name that arrives a second time is
 * auto-placed.
 *
 * @return 1 with the point in @p x and @p y when one was reserved.
 */
int
pv_take_landing(tiku_trk_poseview_t *v, const char *path, int *x, int *y)
{
    const char *name;
    int i;

    if (v == NULL || v->land_count <= 0 || path == NULL) {
        return 0;
    }
    name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;
    for (i = 0; i < v->land_count; i++) {
        if (strcmp(v->land[i].name, name) == 0) {
            *x = v->land[i].x;
            *y = v->land[i].y;
            v->land[i] = v->land[v->land_count - 1];
            v->land_count--;
            return 1;
        }
    }
    return 0;
}
