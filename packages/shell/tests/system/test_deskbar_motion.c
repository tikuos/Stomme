/* Menu motion never changes input geometry or keeps drawing while idle.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "../../system/deskbar/bar/tiku_trk_deskbar_private.h"

/** @brief One frame of the bar at @p now, over a fresh backdrop. */
static void frame(tiku_trk_deskbar_t *db, tiku_surface_t *s, int64_t now)
{
    tiku_fill(s, (tiku_rect_t){0, 0, s->w, s->h}, TIKU_C_BACKDROP);
    tiku_trk_deskbar_set_now(db, now);
    tiku_trk_deskbar_draw(db, s);
}

/** @brief The leaf menu's and submenu's fades at one dock, theme and
 *         scale. */
static void exercise(int dock, int theme, int scale)
{
    tiku_theme_set(theme == 1 ? tiku_theme_dark() :
                   theme == 2 ? tiku_theme_mono() : NULL);
    tiku_surface_t *s = tiku_surface_new(1000, 680, 0); assert(s);
    assert(!tiku_surface_rescale(s, scale, 0));
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL); assert(db);
    tiku_trk_deskbar_set_dock(db, dock);
    frame(db, s, 1000000);
    assert(!tiku_trk_deskbar_motion_damage(db).w);
    tiku_trk_deskbar_open_menu(db);
    frame(db, s, 1000000);
    tiku_rect_t menu = db_leaf_menu_rect(db, tiku_font_plain()->height + 6,
                                         db->leaf_count);
    tiku_rect_t damage = tiku_trk_deskbar_motion_damage(db);
    assert(damage.x == menu.x && damage.y == menu.y && damage.w == menu.w);
    tiku_surface_t *first = tiku_grab(s, menu); assert(first);
    frame(db, s, 1050000);
    assert(db->menu_motion.active);
    frame(db, s, 1100000);
    assert(!tiku_trk_deskbar_motion_damage(db).w);
    int changes = 0;
    for (int y = 0; y < first->h; y++)
        for (int x = 0; x < first->w; x++)
            changes += tiku_peek(first, x, y) !=
                       tiku_peek(s, menu.x + x, menu.y + y);
    assert(changes > 0);
    tiku_surface_free(first);
    int settings = -1;
    for (int i = 0; i < db->leaf_count; i++)
        if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_EXPAND &&
            db->leaf[i].expand == TIKU_TRK_DB_EXPAND_SETTINGS) settings = i;
    assert(settings >= 0);
    db_open_sub(db, settings);
    frame(db, s, 1200000);
    assert(db->sub_motion.active && !db->menu_motion.active);
    /* Pick the child on its first frame, without waiting for the fade. */
    tiku_rect_t sub = db_sub_menu_rect(db, menu, tiku_font_plain()->height+6);
    tiku_trk_db_action_t command = db->sub[0].command;
    tiku_trk_db_pick_t pick = {0};
    assert(tiku_trk_deskbar_click(db, sub.x+12, sub.y+10, 0, 1, 1, &pick));
    assert(pick.what == command && !db->menu_open);
    frame(db, s, 1200001);
    assert(!tiku_trk_deskbar_motion_damage(db).w);
    tiku_trk_deskbar_open_menu(db);
    frame(db, s, 1200002);
    assert(db->menu_motion.active);
    frame(db, s, 0); /* reduce motion takes effect during a transition */
    assert(!db->menu_motion.active && !tiku_trk_deskbar_motion_damage(db).w);
    frame(db, s, 1200003);
    assert(!db->menu_motion.active); /* do not resume cancelled effects */
    tiku_trk_deskbar_close_menu(db);
    tiku_trk_deskbar_open_menu(db);
    frame(db, s, 0);
    assert(!tiku_trk_deskbar_motion_damage(db).w);
    tiku_trk_deskbar_free(db); tiku_surface_free(s);
}

int main(void)
{
    for (int dock = 0; dock < TIKU_TRK_DB_DOCKS; dock++)
        for (int theme = 0; theme < 3; theme++)
            for (int scale = 1; scale <= 2; scale++)
                exercise(dock, theme, scale);
    tiku_theme_set(NULL);
    puts("PASS: Deskbar fades, immediate input, cancellation and idle "
         "across docks, themes and HiDPI");
    return 0;
}
