/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_desktop_navigation.c - the spring navigator a drag resting on a
 * folder opens, following the pointer over it and where a drop on it goes;
 * and the per-turn filling of the navigation menus and the icon hints.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_desktop_private.h"

/** @brief Re-apply the spring menu's enabled states from the live drag. */
static void
spring_nav_states(desktop_t *app)
{
    const char *types[TIKU_TRK_DRAG_TYPES];
    int i;

    for (i = 0; i < app->drag.ntype; i++) {
        types[i] = app->drag.type[i];
    }
    tiku_trk_nav_spring_states(&app->nav,
        (const struct tiku_trk_apps *)&app->apps, types, app->drag.ntype);
}

/**
 * @brief The spring: a drag resting on a folder opens a navigable menu of
 *        its contents (PVS-051).
 */
void
spring_open_nav(void *context, const char *path)
{
    desktop_t *app = context;

    tiku_trk_nav_open(&app->nav, app->backend, path);
    app->nav.hide_dotfiles = tiku_trk_prefs_get(prefs_model(app),
                                                "HideDotFiles");
    app->nav.folders_first = tiku_trk_prefs_get(prefs_model(app),
                                                "SortFolderNamesFirst");
    app->spring_nav_on = 1;
    app->spring_nav_x = app->drag_target.spring.x +
                        app->drag_target.spring.w;
    app->spring_nav_y = app->drag_target.spring.y;
    desktop_damage_all(app);
}

/** @brief Leave every spring level and mark the spring menu closed. */
void
spring_nav_close(desktop_t *app)
{
    while (tiku_trk_nav_leave(&app->nav)) {
    }
    app->nav.depth = 0;
    app->spring_nav_on = 0;
    desktop_damage_all(app);
}

/**
 * @brief The frame of spring level @p k, placed as the drawing places it:
 *        each level beside the selected row of the one before.
 */
static tiku_rect_t
spring_nav_frame(desktop_t *app, int k)
{
    int px = app->spring_nav_x;
    int py = app->spring_nav_y;
    tiku_rect_t r = { 0, 0, 0, 0 };
    int j;

    for (j = 0; j <= k && j < app->nav.depth; j++) {
        tiku_trk_nav_level_t *lv = &app->nav.level[j];

        tiku_trk_nav_place(&app->nav, j, px, py, app->surf->w,
                           app->surf->h, &r.x, &r.y, &r.w, &r.h);
        px = r.x + r.w - 4;
        py = r.y + 2 + ((lv->sel >= 0) ? lv->sel : 0) * TIKU_TRK_NAV_ROW;
    }
    return r;
}

/**
 * @brief Follow the pointer over the spring levels (PVS-051, PVS-054).
 *
 * Hovering a row selects it; an enabled container row opens the next
 * level beside it; leaving every level (and the sprung pose) by more than
 * the margin closes the whole thing.
 */
void
spring_nav_track(desktop_t *app, int x, int y)
{
    int hit_level = -1;
    int hit_item = -1;
    int inside_margin = 0;
    int k;

    for (k = 0; k < app->nav.depth; k++) {
        tiku_rect_t r = spring_nav_frame(app, k);
        int at;

        if (x >= r.x - TIKU_TRK_SPRING_MARGIN &&
            x < r.x + r.w + TIKU_TRK_SPRING_MARGIN &&
            y >= r.y - TIKU_TRK_SPRING_MARGIN &&
            y < r.y + r.h + TIKU_TRK_SPRING_MARGIN) {
            inside_margin = 1;
        }
        at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, x, y);
        if (at >= 0) {
            hit_level = k;
            hit_item = at;
        }
    }
    if (hit_level >= 0) {
        tiku_trk_nav_level_t *lv = &app->nav.level[hit_level];

        if (lv->sel != hit_item) {
            /* A new row: the levels deeper than it were opened from the
             * old row, so they close. */
            while (app->nav.depth > hit_level + 1) {
                (void)tiku_trk_nav_leave(&app->nav);
            }
            lv->sel = hit_item;
            if (lv->item[hit_item].is_container &&
                lv->item[hit_item].enabled) {
                (void)tiku_trk_nav_enter(&app->nav, hit_item);
            }
            desktop_damage_all(app);
        }
        return;
    }
    /* Off every level: the sprung pose keeps the menu open too, so the
     * pointer can move from the pose into the menu (PVS-054). */
    if (!inside_margin &&
        !tiku_trk_springmenu_inside(&app->drag_target.spring, x, y)) {
        spring_nav_close(app);
    }
}

/**
 * @brief What releasing the drag at @p x,@p y over the spring means.
 *
 * @return 1 with @p dest set for an enabled row, 2 for a disabled row or
 *         a point between rows (the release is swallowed), 0 when the menu
 *         was not hit.
 */
int
spring_nav_hit(desktop_t *app, int x, int y, char *dest, size_t max)
{
    int k;

    for (k = app->nav.depth - 1; k >= 0; k--) {
        tiku_rect_t r = spring_nav_frame(app, k);
        int at = tiku_trk_nav_item_at(&app->nav, k, r.x, r.y, x, y);

        if (at >= 0) {
            tiku_trk_nav_level_t *lv = &app->nav.level[k];

            if (!lv->item[at].enabled) {
                return 2;
            }
            snprintf(dest, max, "%s", lv->item[at].path);
            return 1;
        }
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return 2;               /* in the menu, between rows */
        }
    }
    return 0;
}

/**
 * @brief Each turn: fill the open navigation or spring menu by one chunk of
 *        its folder (CW-055), and ask up to HINTS_PER_TURN entries of the
 *        open folders whether they carry a picture of their own.
 */
void
desktop_spring_tick(desktop_t *app)
{
    int i;

    if ((app->nav_on && app->popup != NULL) || app->spring_nav_on) {
        /* A chunk per turn: the menu answers the mouse while a large
         * folder is still arriving (CW-055). */
        if (tiku_trk_nav_fill(&app->nav, app->nav.depth - 1)) {
            if (app->spring_nav_on) {
                /* Whether an entry takes the drop is decided per entry,
                 * so fresh entries get their states as they arrive
                 * (PVS-052). */
                spring_nav_states(app);
            }
            desktop_damage_all(app);
        }
    } else if (app->nav_on && app->popup == NULL) {
        app->nav_on = 0;         /* the menu closed: end the walk */
    }
    {   /* Ask at most HINTS_PER_TURN unasked entries whether they carry
         * a picture of their own; each ask opens the file (IV-005,
         * IV-037). */
        int budget = HINTS_PER_TURN;

        for (i = 0; i < app->folder_count && budget > 0; i++) {
            folder_t *fl = &app->folders[i];
            int k, n;

            if (fl->win == NULL || !fl->win->open) {
                continue;
            }
            n = tiku_trk_poseview_count(fl->view);
            for (k = 0; k < n && budget > 0; k++) {
                tiku_trk_pose_t *p = tiku_trk_poseview_at(fl->view, k);

                if (p == NULL || p->model.facts.icon_hint !=
                                     TIKU_ICONHINT_UNASKED) {
                    continue;
                }
                (void)tiku_trk_nodeicon_hint(app->store, &app->thumbs,
                                             &p->model);
                budget--;
                /* The view marks only the pose's row, which keeps the
                 * damage a notice has recorded in it (PVL-075); the
                 * desktop still asks for a full frame. */
                tiku_trk_poseview_damage_row(fl->view, k);
                desktop_damage_all(app);
            }
        }
    }
}
