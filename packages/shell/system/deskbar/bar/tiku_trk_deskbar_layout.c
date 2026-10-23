/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_layout.c - where the bar's parts go (the list, the tray
 * and its icons, the theme switch), the leaf menu's programs, and opening
 * and closing the bar's popups.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/**
 * @brief The height in pixels the application list wants: a team row per
 *        team, plus a window row per window of each expanded team, each
 *        with its seam.
 */
int
db_list_rows(const tiku_trk_deskbar_t *db)
{
    int i, h = 0;
    int team_h = (db->settings.icon_size + 4 > db_team_row())
                     ? db->settings.icon_size + 4 : db_team_row();

    for (i = 0; i < db->team_count; i++) {
        h += team_h + SEAM;
        if (db->teams[i].expanded) {
            h += db->teams[i].win_count * (db_win_row() + SEAM);
        }
    }
    return h;
}

/** @brief Where the application list starts: below the leaf row. */
int
db_list_y(const tiku_trk_deskbar_t *db)
{
    /* Measured from the frame's top, which sits lower while the global
     * bar is on. */
    return db->frame.y + db_leaf_row() + PAD;
}

/** @brief Where the tray starts: below the list's rows. */
int
db_tray_y(const tiku_trk_deskbar_t *db)
{
    return db_list_y(db) + db_list_rows(db) + PAD;
}

/** @brief The width tray icon @p t takes: the icon, its text, the gap. */
static int
icon_cell_w(const tiku_trk_trayicon_t *t)
{
    int w = 16 + ICON_GAP;

    if (t->text[0] != '\0') {
        w += 3 + tiku_text_width(tiku_font_plain(), t->text);
    }
    return w;
}

/**
 * @brief Where tray icon @p i goes in a strip starting at @p x0, @p y0 and
 *        @p w wide: left to right, a new row where the next would not fit.
 * @return its box (empty for no such icon), and the rows all of them take
 *         into @p rows when given.
 */
static tiku_rect_t
icon_place(const tiku_trk_deskbar_t *db, int x0, int y0, int w, int i,
           int *rows)
{
    tiku_rect_t at = { 0, 0, 0, 0 };
    int x = x0, y = y0, n = db->trayicon_count > 0;

    for (int k = 0; k < db->trayicon_count; k++) {
        int cw = icon_cell_w(&db->trayicons[k]);

        if (x > x0 && x + cw - ICON_GAP > x0 + w) {
            x = x0;
            y += ICON_ROW;
            n++;
        }
        if (k == i) {
            at = (tiku_rect_t){ x, y, cw - ICON_GAP, ICON_ROW };
        }
        x += cw;
    }
    if (rows != NULL) {
        *rows = n;
    }
    return at;
}

/** @brief The width all the tray icons take in one row. */
int
db_trayicons_w(const tiku_trk_deskbar_t *db)
{
    int w = 0;

    for (int k = 0; k < db->trayicon_count; k++) {
        w += icon_cell_w(&db->trayicons[k]);
    }
    return w;
}

/** @brief How many replicant rows the tray shows: none while the
 *         replicants are hidden (DB-007). */
int
db_tray_rows(const tiku_trk_deskbar_t *db)
{
    return db->settings.show_replicants ? db->tray_count : 0;
}

/** @brief The tray's height: the rows of application icons (down a side
 *         edge only), then a row per shown replicant. */
int
db_tray_h(const tiku_trk_deskbar_t *db)
{
    int rows = 0;

    if (tiku_trk_deskbar_vertical(db->dock)) {
        (void)icon_place(db, 0, 0, tiku_trk_deskbar_width(db) - 8, -1,
                         &rows);
    }
    return rows * ICON_ROW + db_tray_rows(db) * TRAY_ROW;
}

/** @brief Where the tray starts on a bar docked down a side edge, measured
 *         up from the frame's bottom past the clock row and the switch
 *         strip; it lies below the list, or below a mini dock's
 *         application button. */
int
db_tray_top(const tiku_trk_deskbar_t *db)
{
    return db->frame.y + db->frame.h - PAD -
           (db->settings.show_clock ? db_clock_row() : 0) - SWITCH_ROW -
           db_tray_h(db);
}

/** @brief Where the application list ends on a bar docked down a side
 *         edge: PAD above the tray.  No row is drawn or hit below it. */
int
db_list_bottom(const tiku_trk_deskbar_t *db)
{
    return db_tray_top(db) - PAD;
}

/** @brief The order[] index of the first application row shown: the
 *         scrolled-to row while the list overflows, else 0. */
int
db_list_first(const tiku_trk_deskbar_t *db)
{
    return db->overflow ? db->scroll : 0;
}

tiku_rect_t
tiku_trk_deskbar_trayicon_rect(const tiku_trk_deskbar_t *db, int i)
{
    tiku_rect_t none = { 0, 0, 0, 0 };

    if (db == NULL || i < 0 || i >= db->trayicon_count ||
        !tiku_trk_deskbar_shown(db)) {
        return none;
    }
    if (tiku_trk_deskbar_vertical(db->dock)) {
        return icon_place(db, db->frame.x + 4, db_tray_top(db),
                          tiku_trk_deskbar_width(db) - 8, i, NULL);
    }
    /* Along an edge: one row, left of the clock and the switch. */
    return icon_place(db, db->frame.x + db->frame.w - (SWITCH_W + 14) -
                      db_hclock_w(db) - db_trayicons_w(db), db->frame.y +
                      (db->frame.h - ICON_ROW) / 2, db->frame.w, i, NULL);
}

/** @brief The tray icon under @p x, @p y, or -1. */
int
db_trayicon_at(const tiku_trk_deskbar_t *db, int x, int y)
{
    for (int i = 0; i < db->trayicon_count; i++) {
        tiku_rect_t r = tiku_trk_deskbar_trayicon_rect(db, i);

        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

tiku_rect_t
tiku_trk_deskbar_frame(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t r = { 0, 0, 0, 0 };

    return (db != NULL) ? db->frame : r;
}

tiku_rect_t
tiku_trk_deskbar_switch_rect(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t r = { 0, 0, SWITCH_W, SWITCH_H };

    if (db == NULL) {
        return r;
    }
    if (tiku_trk_deskbar_vertical(db->dock)) {
        /* Its own strip between the tray and the clock, measured up from
         * the frame's bottom as the clock row is. */
        r.x = db->frame.x + (db->frame.w - SWITCH_W) / 2;
        r.y = db->frame.y + db->frame.h - PAD -
              (db->settings.show_clock ? db_clock_row() : 0) - SWITCH_ROW +
              (SWITCH_ROW - SWITCH_H) / 2;
    } else {
        /* Along an edge: the strip's far end, past the last button. */
        r.x = db->frame.x + db->frame.w - SWITCH_W - 8;
        r.y = db->frame.y + (db->frame.h - SWITCH_H) / 2;
    }
    return r;
}

/**
 * @brief The theme switch: a recessed track, filled with the tab colour
 *        while the dark theme is on, and a raised knob with grip lines.
 *
 * The knob has three positions: light at the left, dark at the right, no
 * hue in the middle.
 */
void
db_draw_switch(const tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    tiku_rect_t t = tiku_trk_deskbar_switch_rect(db);
    tiku_rgb_t well = (db->theme == 1)
        ? TIKU_C_TAB
        : tiku_tint(TIKU_C_PANEL,
                         (db->theme == 2) ? 1.28f : 1.10f);
    tiku_rect_t knob;
    int slot;
    int k;

    /* The well: filled, framed, and shadowed along its top edge. */
    tiku_fill(s, t, well);
    tiku_frame(s, t, tiku_tint(TIKU_C_PANEL, 1.45f));
    tiku_hline(s, t.x + 1, t.y + 1, t.w - 2,
                    tiku_tint(well, TIKU_DARKEN_2));
    tiku_hline(s, t.x + 1, t.y + t.h, t.w,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX));

    /* The knob reaches one pixel past the track at the top and at the
     * bottom, and sits at the left end, the right end or the middle. */
    slot = (db->theme == 1) ? t.x + t.w - SWITCH_H - 1
         : (db->theme == 2) ? t.x + (t.w - SWITCH_H) / 2
                            : t.x + 1;
    knob = (tiku_rect_t){ slot, t.y - 1, SWITCH_H, t.h + 2 };
    tiku_fill(s, knob, TIKU_C_PANEL);
    tiku_frame(s, knob, tiku_tint(TIKU_C_PANEL, 1.45f));
    tiku_bevel(s, tiku_inset(knob, 1),
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX),
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    for (k = 0; k < 3; k++) {
        tiku_vline(s, knob.x + 3 + 3 * k, knob.y + 4, knob.h - 8,
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
    }
}

void
tiku_trk_deskbar_set_theme(tiku_trk_deskbar_t *db, int which)
{
    if (db != NULL) {
        db->theme = (which >= 0 && which <= 2) ? which : 0;
    }
}

int
tiku_trk_deskbar_add_app(tiku_trk_deskbar_t *db, const char *label,
                         const char *command, const char *icon)
{
    return tiku_trk_deskbar_add_app_in(db, TIKU_TRK_DB_GROUP_APPS,
                                       label, command, icon);
}

int
tiku_trk_deskbar_add_app_in(tiku_trk_deskbar_t *db, int group,
                            const char *label, const char *command,
                            const char *icon)
{
    if (db == NULL || label == NULL || command == NULL ||
        group < 0 || group >= TIKU_TRK_DB_GROUP_COUNT ||
        db->app_count >= (int)(sizeof db->app / sizeof db->app[0])) {
        return 0;
    }
    snprintf(db->app[db->app_count].label,
             sizeof db->app[db->app_count].label, "%s", label);
    snprintf(db->app[db->app_count].command,
             sizeof db->app[db->app_count].command, "%s", command);
    snprintf(db->app[db->app_count].icon,
             sizeof db->app[db->app_count].icon, "%s",
             (icon != NULL) ? icon : "");
    db->app[db->app_count].group = group;
    db->app_count++;
    return 1;
}

void
tiku_trk_deskbar_set_top_inset(tiku_trk_deskbar_t *db, int px)
{
    if (db != NULL) {
        db->top_inset = (px > 0) ? px : 0;
    }
}

int
tiku_trk_deskbar_hit(const tiku_trk_deskbar_t *db, int x, int y)
{
    tiku_rect_t f;

    if (db == NULL) {
        return 0;
    }
    f = db->frame;
    return (x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h);
}

int
tiku_trk_deskbar_menu_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? (db->menu_open || db->calendar_open ||
                           db->clock_menu_open || db->win_menu_team >= 0 ||
                           db->apps_menu_open) : 0;
}

void
tiku_trk_deskbar_open_menu(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        /* The same state a click on the leaf sets, for the Menu key
         * (DB-011). */
        db_build_leaf(db);         /* rebuilt from the namespace (DB-005) */
        db->menu_seen = 0;
        db->sub_seen = -1;
        db->menu_open = 1;
        db_reset_menu_hots(db);
        db->win_menu_team = -1;
    }
}

void
tiku_trk_deskbar_close_menu(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        db->menu_open = 0;
        db->calendar_open = 0;
        db->clock_menu_open = 0;
        db->win_menu_team = -1;
        db->apps_menu_open = 0;
        db->sub_of = -1;
        db->sub_count = 0;
        db_reset_menu_hots(db);
    }
}

int
tiku_trk_deskbar_calendar_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->calendar_open : 0;
}

int
tiku_trk_deskbar_clock_visible(const tiku_trk_deskbar_t *db)
{
    return db != NULL && db->settings.show_clock;
}

void
tiku_trk_deskbar_set_clock_visible(tiku_trk_deskbar_t *db, int visible)
{
    if (db == NULL) { return; }
    db->settings.show_clock = !!visible;
    if (!db->settings.show_clock) {
        db->calendar_open = 0;
    }
}

void
tiku_trk_deskbar_close_calendar(tiku_trk_deskbar_t *db)
{
    if (db != NULL) {
        db->calendar_open = 0;
        db->clock_menu_open = 0;
    }
}
