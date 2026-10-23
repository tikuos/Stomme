/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_bar.c - the bar itself: creation, dock, width, auto-hide,
 * auto-raise and scrolling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

tiku_trk_deskbar_t *
tiku_trk_deskbar_new(tiku_backend_t *backend)
{
    tiku_trk_deskbar_t *db = calloc(1, sizeof *db);

    if (db != NULL) {
        db->backend = backend;
        db->pointer_x = db->pointer_y = -1;
        db->dock = TIKU_TRK_DB_RIGHT_EXPANDED;
        db->win_menu_team = -1;
        db_reset_menu_hots(db);
        db->expander_press = -1;
        db->tip_y = -1;
        db->trayicon_hot = -1;
        tiku_trk_deskbar_settings_default(&db->settings);
    }
    return db;
}

void
tiku_trk_deskbar_free(tiku_trk_deskbar_t *db)
{
    free(db);
}

void
tiku_trk_deskbar_set_dock(tiku_trk_deskbar_t *db, tiku_trk_db_dock_t dock)
{
    if (db != NULL && dock >= 0 && dock < TIKU_TRK_DB_DOCKS) {
        db->dock = dock;
    }
}

int
tiku_trk_deskbar_vertical(tiku_trk_db_dock_t d)
{
    return (d != TIKU_TRK_DB_TOP && d != TIKU_TRK_DB_BOTTOM);
}

int
tiku_trk_deskbar_mini(tiku_trk_db_dock_t d)
{
    return (d == TIKU_TRK_DB_RIGHT_MINI || d == TIKU_TRK_DB_LEFT_MINI);
}

tiku_trk_db_dock_t
tiku_trk_deskbar_dock_at(int x, int y, int sw, int sh,
                         tiku_trk_db_dock_t now)
{
    int dl = x, dr = sw - x, dt = y, db_ = sh - y;
    int nearest;

    if (sw <= 0 || sh <= 0) {
        return now;
    }
    nearest = dl;
    if (dr < nearest) { nearest = dr; }
    if (dt < nearest) { nearest = dt; }
    if (db_ < nearest) { nearest = db_; }
    /* Farther than a quarter of the screen from every edge: the dock
     * stays as it is (DB-048). */
    if (nearest > sw / 4 && nearest > sh / 4) {
        return now;
    }
    if (nearest == dt) { return TIKU_TRK_DB_TOP; }
    if (nearest == db_) { return TIKU_TRK_DB_BOTTOM; }
    {
        /* Down the top third of a side edge is the expanded form; further
         * down is the collapsed one. */
        int mini = (y > sh / 3);

        if (nearest == dl) {
            return mini ? TIKU_TRK_DB_LEFT_MINI : TIKU_TRK_DB_LEFT_EXPANDED;
        }
        return mini ? TIKU_TRK_DB_RIGHT_MINI : TIKU_TRK_DB_RIGHT_EXPANDED;
    }
}

int
tiku_trk_deskbar_set_width(tiku_trk_deskbar_t *db, int w)
{
    if (db == NULL) {
        return 0;
    }
    if (w < TIKU_TRK_DB_MIN_W) { w = TIKU_TRK_DB_MIN_W; }
    if (w > TIKU_TRK_DB_MAX_W) { w = TIKU_TRK_DB_MAX_W; }
    db->width = w;
    return w;
}

int
tiku_trk_deskbar_width(const tiku_trk_deskbar_t *db)
{
    if (db == NULL || db->width < TIKU_TRK_DB_MIN_W) {
        return TIKU_TRK_DB_MIN_W;
    }
    return db->width;
}

void
tiku_trk_deskbar_set_autohide(tiku_trk_deskbar_t *db, int on)
{
    if (db != NULL) {
        db->autohide = !!on;
        db->settings.auto_hide = !!on;
    }
}

void
tiku_trk_deskbar_set_autoraise(tiku_trk_deskbar_t *db, int on)
{
    if (db != NULL) {
        db->autoraise = !!on;
        db->settings.always_on_top = on ? 0 :
                                      db->settings.always_on_top;
        db->settings.auto_raise = !!on;
    }
}

int
tiku_trk_deskbar_pointer(tiku_trk_deskbar_t *db, int x, int y)
{
    int was, on;

    if (db == NULL) {
        return 0;
    }
    was = db->pointer_on;
    /* Judged against the frame as last laid out: the whole bar while it
     * shows, its sliver while it hides. */
    on = (x >= db->frame.x && x < db->frame.x + db->frame.w &&
          y >= db->frame.y && y < db->frame.y + db->frame.h);
    if (!on && db->autohide) {
        /* With auto-hide on, the whole screen edge the bar hides against
         * counts as the bar. */
        if (tiku_trk_deskbar_vertical(db->dock)) {
            on = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                  db->dock == TIKU_TRK_DB_LEFT_MINI)
                     ? (x <= TIKU_TRK_DB_SLIVER)
                     : (x >= db->frame.x + db->frame.w -
                            TIKU_TRK_DB_SLIVER);
        } else {
            on = (db->dock == TIKU_TRK_DB_TOP)
                     ? (y <= TIKU_TRK_DB_SLIVER)
                     : (y >= db->frame.y + db->frame.h -
                            TIKU_TRK_DB_SLIVER);
        }
    }
    db->pointer_on = on;
    return (was != on);
}

void
tiku_trk_deskbar_arrows(const tiku_trk_deskbar_t *db, int *up, int *down)
{
    if (up != NULL) { *up = 0; }
    if (down != NULL) { *down = 0; }
    if (db == NULL || !db->overflow) {
        return;
    }
    /* Overflowing at all means something is out of sight: above when the
     * list has been scrolled, below until it has been scrolled to its
     * end. */
    if (up != NULL) { *up = db->scroll > 0; }
    if (down != NULL) {
        *down = db->scroll < db->team_count - 1;
    }
}

int
tiku_trk_deskbar_scroll(tiku_trk_deskbar_t *db, int step)
{
    int was;

    if (db == NULL || !db->overflow) {
        /* Nothing hidden: the step changes nothing (DB-034). */
        return 0;
    }
    was = db->scroll;
    db->scroll += step;
    if (db->scroll < 0) {
        db->scroll = 0;
    }
    if (db->scroll > db->team_count - 1) {
        db->scroll = (db->team_count > 0) ? db->team_count - 1 : 0;
    }
    return (was != db->scroll);
}

int
tiku_trk_deskbar_scroll_at(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->scroll : 0;
}

int
tiku_trk_deskbar_scrollable(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->overflow : 0;
}

int
tiku_trk_deskbar_shown(const tiku_trk_deskbar_t *db)
{
    if (db == NULL) {
        return 0;
    }
    /* An open leaf menu, calendar or clock menu keeps the bar shown
     * wherever the pointer is. */
    return (!db->autohide || db->pointer_on || db->menu_open ||
            db->calendar_open || db->clock_menu_open);
}

int
tiku_trk_deskbar_raised(const tiku_trk_deskbar_t *db)
{
    if (db == NULL) {
        return 1;
    }
    return (db->settings.always_on_top || (db->autoraise && db->pointer_on) ||
            tiku_trk_deskbar_menu_open(db));
}
