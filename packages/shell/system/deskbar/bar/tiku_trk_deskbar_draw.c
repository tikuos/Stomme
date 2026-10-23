/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_draw.c - drawing the bar and its popups, the hover and
 * release handling that changes what is drawn, and the submenus' rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_deskbar_private.h"
#include <strings.h>

/* Used before they are defined. */
static void clock_text(const tiku_trk_deskbar_t *db, char *clock,
                       size_t cap);
static void fit_label(const tiku_font_t *f, const char *src, int budget,
                      char *out, size_t max);
void db_draw_calendar(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                      const tiku_font_t *f, const tiku_font_t *fb,
                      tiku_rgb_t dark);
void db_draw_tip(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_switch_strip(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_clock_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s);
void db_draw_win_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                      const tiku_font_t *f);
void db_draw_leaf_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                       const tiku_font_t *f);
void db_draw_clock(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                   const tiku_font_t *f, int *y);
void db_draw_tray(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f, int *y);
void db_draw_teams(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                   const tiku_font_t *f, int team_h, int *y);
void db_draw_mini(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f);
void db_draw_leaf(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *fb, tiku_rgb_t dark, tiku_rect_t *leaf);
void db_draw_frame(tiku_trk_deskbar_t *db, tiku_surface_t *s, int *rows);
/* End of the forward declarations. */

/** @brief A window's state icon: filled when shown, hollow when minimised,
 *         and ringed in the selection colour when @p current. */
static void
draw_win_icon(tiku_surface_t *s, int x, int y, int minimized,
              int current)
{
    tiku_rect_t b = { x, y, 9, 9 };
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);

    tiku_fill(s, b, minimized ? TIKU_C_PANEL : TIKU_C_DOC);
    tiku_frame(s, b, dark);
    if (!minimized) {
        /* A shown window's glyph carries a title bar. */
        tiku_hline(s, b.x + 1, b.y + 1, b.w - 2, TIKU_C_TAB);
        tiku_hline(s, b.x + 1, b.y + 2, b.w - 2, TIKU_C_TAB);
    }
    if (current) {
        tiku_frame(s, (tiku_rect_t){ b.x - 2, b.y - 2, b.w + 4,
                                               b.h + 4 }, TIKU_C_SELECT);
    }
}

/**
 * @brief Where the leaf menu goes: right of the bar when it is docked
 *        left, otherwise left of it, clamped at x 0 (DB-010).  It opens
 *        below the leaf button, or above it on a bottom dock.
 */
tiku_rect_t
db_leaf_menu_rect(const tiku_trk_deskbar_t *db, int rowh, int rows)
{
    tiku_rect_t m;

    m.w = 176;
    m.h = rows * rowh + 4;
    m.y = (db->dock == TIKU_TRK_DB_BOTTOM)
              ? db->frame.y + 2 - m.h
              : db->frame.y + db_leaf_row() + 2;
    m.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           db->dock == TIKU_TRK_DB_LEFT_MINI)
          ? db->frame.x + db->frame.w + 2
          : db->frame.x - m.w - 2;
    if (m.x < 0) {
        m.x = 0;
    }
    return m;
}

tiku_rect_t
db_leaf_button_rect(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t r;

    r.x = db->frame.x + 2;
    r.y = db->frame.y + 2;
    r.w = tiku_trk_deskbar_vertical(db->dock) ? db->frame.w - 4 : HLEAF_W;
    r.h = db_leaf_row();
    return r;
}

/** @brief Whether the last pointer position is over the shown button. */
static int
leaf_button_hovered(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t r = db_leaf_button_rect(db);

    return tiku_trk_deskbar_shown(db) && r.w > 0 && r.h > 0 &&
           db->pointer_x >= r.x && db->pointer_x < r.x + r.w &&
           db->pointer_y >= r.y && db->pointer_y < r.y + r.h;
}

void
tiku_trk_deskbar_set_menu_icon_painter(tiku_trk_deskbar_t *db,
                                       tiku_trk_db_icon_fn fn, void *context)
{
    if (db != NULL) {
        db->menu_icon = fn;
        db->menu_icon_ctx = context;
    }
}

/** @brief Row @p index of the running-apps menu: that team's icon. */
static void
apps_menu_icon(tiku_surface_t *s, int index, int x, int y, int size,
               void *context)
{
    tiku_trk_deskbar_t *db = context;

    (void)tiku_icon_paint(s, tiku_trk_deskbar_team_icon(db,
                              db->order[index]), x, y, size);
}

/** @brief The menu's icon slot, handed to the painter the shell registered. */
static void
relay_menu_icon(tiku_surface_t *s, int index, int x, int y, int size,
                void *context)
{
    struct menu_icon_relay *relay = context;

    (void)relay->db->menu_icon(relay->db->menu_icon_ctx, s,
                               &relay->items[index], x, y, size);
}

int
tiku_trk_deskbar_release(tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_font_t *font = tiku_font_plain();
    int team = (db != NULL) ? db->expander_press : -1;
    int i, ry, changed = 0;

    if (db == NULL || team < 0) {
        return 0;
    }
    db->expander_press = -1;
    /* Still over the same team's strip: the press commits (DB-022). */
    if (db->settings.show_expander &&
        x > db->frame.x + db->frame.w - EXPANDER_W - 6) {
        ry = db_list_y(db);
        for (i = db_list_first(db); i < db->team_count; i++) {
            int at = db->order[i];
            tiku_trk_team_t *t = &db->teams[at];
            int team_h = (db->settings.icon_size + 4 > db_team_row())
                             ? db->settings.icon_size + 4 : db_team_row();

            if (ry + team_h > db_list_bottom(db)) {
                break;
            }
            if (y >= ry && y < ry + team_h) {
                if (at == team) {
                    t->expanded = !t->expanded;
                    changed = 1;
                }
                break;
            }
            ry += team_h + SEAM;
            if (t->expanded) {
                int j;

                for (j = 0; j < t->win_count; j++) {
                    ry += db_win_row() + SEAM;
                }
            }
        }
    }
    (void)font;
    return changed;
}

int
tiku_trk_deskbar_team_expanded(const tiku_trk_deskbar_t *db, int team)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return 0;
    }
    return db->teams[team].expanded;
}

/** @brief Add @p r to what this hover changed on screen. */
static void
hover_touch(tiku_trk_deskbar_t *db, tiku_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) { return; }
    db->hover_damage = (db->hover_damage.w > 0)
                           ? tiku_rect_union(db->hover_damage, r) : r;
}

/**
 * @brief Light the application tray icon under @p x, @p y, and write its
 *        tip into @p tip and its top edge into @p tip_y; an icon with no
 *        tip gives its id.  While the leaf, window or clock menu is open
 *        no icon is lit.
 * @return 1 when a different icon (or none) is lit now.
 */
static int
tray_hover(tiku_trk_deskbar_t *db, int x, int y, char *tip, size_t cap,
           int *tip_y)
{
    int ti = (!db->menu_open && db->win_menu_team < 0 &&
              !db->clock_menu_open) ? db_trayicon_at(db, x, y) : -1;
    int changed = ti != db->trayicon_hot;

    if (changed) {
        if (db->trayicon_hot >= 0) {
            hover_touch(db, tiku_trk_deskbar_trayicon_rect(db,
                                db->trayicon_hot));
        }
        db->trayicon_hot = ti;
        if (ti >= 0) {
            hover_touch(db, tiku_trk_deskbar_trayicon_rect(db, ti));
        }
    }
    if (ti >= 0) {
        const tiku_trk_trayicon_t *t = &db->trayicons[ti];

        snprintf(tip, cap, "%s", t->tip[0] ? t->tip : t->id);
        *tip_y = tiku_trk_deskbar_trayicon_rect(db, ti).y;
    }
    return changed;
}

/** @brief The tooltip's box as db_draw_tip() places it, or empty. */
static tiku_rect_t
tip_box(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t b = { 0, 0, 0, 0 };

    if (db->tip_y >= 0 && db->tip[0] != '\0') {
        b = tiku_ui_tip_size(db->tip);
        b.x = db->frame.x - b.w - 4;
        if (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
            db->dock == TIKU_TRK_DB_LEFT_MINI) {
            b.x = db->frame.x + db->frame.w + 4;
        }
        b.y = db->tip_y;
        if (b.x < 0) { b.x = 0; }
    }
    return b;
}

tiku_rect_t
tiku_trk_deskbar_hover_area(tiku_trk_deskbar_t *db)
{
    tiku_rect_t r = { 0, 0, 0, 0 };

    if (db != NULL) {
        r = db->hover_damage;
        db->hover_damage = (tiku_rect_t){ 0, 0, 0, 0 };
    }
    return r;
}

int
tiku_trk_deskbar_motion(tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_font_t *font = tiku_font_plain();
    int rowh, changed = 0;
    int leaf = -1, sub = -1, win = -1, clock = -1;

    if (db == NULL) {
        return 0;
    }
    db->pointer_x = x;
    db->pointer_y = y;
    if (leaf_button_hovered(db) != db->leaf_hover) {
        db->leaf_hover = !db->leaf_hover;
        hover_touch(db, db_leaf_button_rect(db));
        changed = 1;
    }
    rowh = font->height + 6;
    if (db->menu_open) {
        tiku_rect_t m = db_leaf_menu_rect(db, rowh, db->leaf_count);

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + db->leaf_count * rowh) {
            leaf = (y - m.y - 2) / rowh;
            if (db->leaf[leaf].kind == TIKU_TRK_DB_ITEM_SEP ||
                db->leaf[leaf].kind == TIKU_TRK_DB_ITEM_DISABLED) {
                leaf = -1;      /* nothing to choose there */
            }
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

            if (x >= sm.x && x < sm.x + sm.w && y >= sm.y + 2 &&
                y < sm.y + 2 + db->sub_count * rowh) {
                sub = (y - sm.y - 2) / rowh;
                leaf = -1; /* a point in both menus is the submenu's */
            }
        }
    }
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);
        tiku_rect_t m = db_win_menu_rect(db, n, rowh);

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + n * rowh) {
            win = (y - m.y - 2) / rowh;
            if (win >= n || !enabled[win]) {
                win = -1;       /* a separator or a refused row */
            }
        }
    }
    if (db->apps_menu_open) {
        const tiku_font_t *fm = tiku_font_plain();
        int mrowh = fm->height + 6;
        int nn = (db->team_count > 0) ? db->team_count : 1;
        tiku_rect_t am;
        int ahot = -1;

        am.w = 176;
        am.h = nn * mrowh + 4;
        am.y = db->frame.y + 2 + db_leaf_row() + 2;
        am.x = (db->frame.x > 0) ? db->frame.x - am.w - 2
                                 : db->frame.x + db->frame.w + 2;
        if (x >= am.x && x < am.x + am.w && y >= am.y + 2 &&
            y < am.y + 2 + nn * mrowh) {
            ahot = (y - am.y - 2) / mrowh;
            if (ahot >= db->team_count) {
                ahot = -1;      /* the disabled empty row */
            }
        }
        if (ahot != db->apps_hot) {
            db->apps_hot = ahot;
            hover_touch(db, am);
            changed = 1;
        }
    }
    if (db->clock_menu_open) {
        tiku_rect_t m = db_clock_menu_rect(db);

        if (x >= m.x && x < m.x + m.w && y >= m.y + 2 &&
            y < m.y + 2 + 4 * 28) {
            clock = (y - m.y - 2) / 28;
        }
    }
    {
        /* The tooltip (DB-032): over a row whose label is cut, or any
         * application row while names are hidden, the tip is the whole
         * name.  No tip shows while the leaf, window or clock menu is
         * open. */
        char tip[TIKU_TRK_DB_LABEL_MAX] = "";
        int tip_y = -1;
        /* A tray icon under the pointer takes the tip first. */
        changed |= tray_hover(db, x, y, tip, sizeof tip, &tip_y);
        if (tip_y < 0 && !db->menu_open && db->win_menu_team < 0 &&
            !db->clock_menu_open && db->settings.show_clock &&
            !db->calendar_open && x >= db->frame.x &&
            x < db->frame.x + db->frame.w &&
            y >= db->frame.y + db->frame.h - db_clock_row() - PAD &&
            y < db->frame.y + db->frame.h) {
            /* The clock's tip is the long-form date (DB-044), from the
             * clock the display reads, frozen or not. */
            time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
            struct tm tmv;

            (void)localtime_r(&now, &tmv);
            (void)strftime(tip, sizeof tip, "%A, %B %-e, %Y", &tmv);
            tip_y = db->frame.y + db->frame.h - db_clock_row() - PAD;
        } else if (tip_y < 0 && !db->menu_open && db->win_menu_team < 0 &&
            !db->clock_menu_open && x >= db->frame.x &&
            x < db->frame.x + db->frame.w) {
            int i, ry = db_list_y(db);
            int team_h = (db->settings.icon_size + 4 > db_team_row())
                             ? db->settings.icon_size + 4 : db_team_row();

            for (i = db_list_first(db); i < db->team_count && tip_y < 0;
                 i++) {
                tiku_trk_team_t *t = &db->teams[db->order[i]];
                int j;

                if (ry + team_h > db_list_bottom(db)) {
                    break;
                }
                if (y >= ry && y < ry + team_h) {
                    char cut[TIKU_TRK_DB_LABEL_MAX];

                    fit_label(font, t->name,
                              db->frame.w - 4 - db->settings.icon_size -
                              EXPANDER_W - 14, cut, sizeof cut);
                    if (db->settings.hide_app_names ||
                        strcmp(cut, t->name) != 0) {
                        snprintf(tip, sizeof tip, "%s", t->name);
                        tip_y = ry;
                    }
                    break;
                }
                ry += team_h + SEAM;
                if (!t->expanded) {
                    continue;
                }
                for (j = 0; j < t->win_count; j++) {
                    if (ry + db_win_row() > db_list_bottom(db)) {
                        break;
                    }
                    if (y >= ry && y < ry + db_win_row()) {
                        char cut[TIKU_TRK_DB_LABEL_MAX];

                        fit_label(font, t->wins[j].title,
                                  db->frame.w - 4 - WIN_INDENT - 20, cut,
                                  sizeof cut);
                        if (strcmp(cut, t->wins[j].title) != 0) {
                            snprintf(tip, sizeof tip, "%s",
                                     t->wins[j].title);
                            tip_y = ry;
                        }
                        break;
                    }
                    ry += db_win_row() + SEAM;
                }
            }
        }
        if (strcmp(tip, db->tip) != 0 || tip_y != db->tip_y) {
            /* Both the old and the new tip box are repainted. */
            hover_touch(db, tip_box(db));
            snprintf(db->tip, sizeof db->tip, "%s", tip);
            db->tip_y = tip_y;
            hover_touch(db, tip_box(db));
            changed = 1;
        }
    }
    if (leaf != db->leaf_hot) {
        db->leaf_hot = leaf;
        db->sub_pending = -1;
        hover_touch(db, db_leaf_menu_rect(db, rowh, db->leaf_count));
        changed = 1;
    }
    if (sub != db->sub_hot) {
        db->sub_hot = sub;
        hover_touch(db, db_sub_menu_rect(db,
            db_leaf_menu_rect(db, rowh, db->leaf_count), rowh));
        changed = 1;
    }
    if (win != db->win_hot) {
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);

        db->win_hot = win;
        hover_touch(db, db_win_menu_rect(db, n, rowh));
        changed = 1;
    }
    if (clock != db->clock_hot) {
        tiku_rect_t m = db_clock_menu_rect(db);

        db->clock_hot = clock;
        hover_touch(db, m);
        changed = 1;
    }
    return changed;
}

/** @brief Whether a row of @p kind expands, by a click or by hover. */
static int item_expands(int kind)
{
    return kind == TIKU_TRK_DB_ITEM_EXPAND;
}

int tiku_trk_deskbar_menu_tick(tiku_trk_deskbar_t *db, int64_t now)
{
    if (!db) return 0;
    int row = db->leaf_hot;
    if (!db->menu_open || row < 0 || row >= db->leaf_count ||
        db->leaf[row].kind == TIKU_TRK_DB_ITEM_DISABLED ||
        db->leaf[row].kind == TIKU_TRK_DB_ITEM_SEP) {
        db->sub_pending = -1;
        return 0;
    }
    int wanted = item_expands(db->leaf[row].kind) ? row : -1;
    if (wanted == db->sub_of) {
        db->sub_pending = -1;
        return 0;
    }
    if (db->sub_pending == row && db->sub_entered == -1) return 0;
    if (db->sub_pending != row || now < db->sub_entered) {
        db->sub_pending = row;
        db->sub_entered = now;
        return 0;
    }
    if (now-db->sub_entered < TIKU_TRK_DB_SUBMENU_DELAY_US) return 0;
    /* The submenu is filled once, after the dwell; hovering never
     * chooses a row's command. */
    int before = db->sub_of;
    db_open_sub(db, wanted);
    db->sub_pending = row;
    /* -1 marks the row as tried: a submenu that came out empty is not
     * built again on every tick while the row stays hot. */
    db->sub_entered = -1;
    return before != db->sub_of;
}

int
tiku_trk_deskbar_menu_keeps_open(const tiku_trk_deskbar_t *db, int x, int y)
{
    const tiku_font_t *font;
    tiku_rect_t m;
    int rowh;

    if (db == NULL || !db->menu_open) {
        return 0;
    }
    font = tiku_font_plain();
    rowh = font->height + 6;
    m = db_leaf_menu_rect(db, rowh, db->leaf_count);
    /* The 20 px tracking margin lies outside the visible menu: the
     * pointer may cross the gap into a submenu without the menu closing
     * (DB-036). */
    if (x >= m.x - 20 && x < m.x + m.w + 20 &&
        y >= m.y - 20 && y < m.y + m.h + 20) {
        return 1;
    }
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

        if (x >= sm.x - 20 && x < sm.x + sm.w + 20 &&
            y >= sm.y - 20 && y < sm.y + sm.h + 20) {
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_menu_labels(const tiku_trk_deskbar_t *db, int sub,
                            char *out, size_t cap)
{
    int i, n;

    if (db == NULL || out == NULL || cap == 0) { return 0; }
    out[0] = '\0';
    if (!db->menu_open) { return 0; }
    n = sub ? ((db->sub_of >= 0) ? db->sub_count : 0) : db->leaf_count;
    for (i = 0; i < n; i++) {
        size_t used = strlen(out);

        snprintf(out + used, cap - used, "%s|",
                 sub ? db->sub[i].label : db->leaf[i].label);
    }
    return n;
}

/** @brief The clock menu's four rows, each worded from its setting. */
static void
clock_menu_items(const tiku_trk_deskbar_t *db, const char **items)
{
    items[0] = tiku_str(db->settings.show_clock ? "menu.clock.hide"
                                                : "menu.clock.show");
    items[1] = tiku_str(db->settings.show_seconds ? "menu.seconds.hide"
                                                  : "menu.seconds.show");
    items[2] = tiku_str(db->settings.show_day_of_week ? "menu.day.hide"
                                                      : "menu.day.show");
    items[3] = tiku_str(db->settings.show_timezone ? "menu.zone.hide"
                                                   : "menu.zone.show");
}

int
tiku_trk_deskbar_hot_label(const tiku_trk_deskbar_t *db, char *out,
                           size_t cap)
{
    const char *label = NULL;
    const char *items[4];
    int enabled[4];

    if (db == NULL || out == NULL || cap == 0) { return 0; }
    if (db->menu_open) {
        if (db->sub_of >= 0 && db->sub_hot >= 0 &&
            db->sub_hot < db->sub_count) {
            label = db->sub[db->sub_hot].label;
        } else if (db->leaf_hot >= 0 && db->leaf_hot < db->leaf_count) {
            label = db->leaf[db->leaf_hot].label;
        }
    } else if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);

        if (db->win_hot >= 0 && db->win_hot < n) {
            label = items[db->win_hot];
        }
    } else if (db->clock_menu_open) {
        clock_menu_items(db, items);
        if (db->clock_hot >= 0 && db->clock_hot < 4) {
            label = items[db->clock_hot];
        }
    } else if (db->apps_menu_open) {
        if (db->apps_hot >= 0 && db->apps_hot < db->team_count) {
            /* Row k of the menu is team order[k], as it is drawn. */
            label = db->teams[db->order[db->apps_hot]].name;
        }
    }
    snprintf(out, cap, "%s", (label != NULL) ? label : "");
    return label != NULL;
}

const char *
tiku_trk_deskbar_tip(const tiku_trk_deskbar_t *db)
{
    return (db != NULL && db->tip_y >= 0) ? db->tip : "";
}

int
tiku_trk_deskbar_menu_bottom(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t m;

    if (db == NULL || !db->menu_open) {
        return -1;
    }
    m = db_leaf_menu_rect(db, tiku_font_plain()->height + 6, db->leaf_count);
    return m.y + m.h;
}

int
tiku_trk_deskbar_menu_point(const tiku_trk_deskbar_t *db, const char *label,
                            int *x, int *y)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t m;
    int rowh, i;

    if (db == NULL || label == NULL || x == NULL || y == NULL ||
        !db->menu_open) {
        return 0;
    }
    rowh = f->height + 6;
    m = db_leaf_menu_rect(db, rowh, db->leaf_count);
    /* The open submenu is searched first, so a label in both menus
     * resolves to the submenu's row.  The submenu is placed beside the
     * leaf menu; the two overlap only when the screen's left edge pushes
     * the submenu to x = 0, as it does with a top or bottom dock. */
    if (db->sub_of >= 0 && db->sub_count > 0) {
        tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

        for (i = 0; i < db->sub_count; i++) {
            if (strstr(db->sub[i].label, label) != NULL) {
                *x = sm.x + 8;
                *y = sm.y + 2 + i * rowh + rowh / 2;
                return 1;
            }
        }
    }
    for (i = 0; i < db->leaf_count; i++) {
        if (db->leaf[i].kind != TIKU_TRK_DB_ITEM_SEP &&
            strstr(db->leaf[i].label, label) != NULL) {
            *x = m.x + 8;
            *y = m.y + 2 + i * rowh + rowh / 2;
            return 1;
        }
    }
    return 0;
}

int
tiku_trk_deskbar_quit_policy(int prefs_open, int shortcut_quit)
{
    /* The bar is hosted by the desktop process, so it has no independent
     * close/minimize operation.  A quit shortcut is consumed by preferences
     * when that window is open; otherwise it is refused by the bar itself. */
    return (shortcut_quit && prefs_open) ? 1 : 0;
}

/** @brief Where the submenu goes: level with its leaf row, right of the
 *         leaf menu with a left dock and left of it otherwise, clamped at
 *         x 0. */
tiku_rect_t
db_sub_menu_rect(const tiku_trk_deskbar_t *db, tiku_rect_t m, int rowh)
{
    tiku_rect_t r;

    r.w = 210;
    r.h = db->sub_count * rowh + 4;
    r.y = m.y + 2 + db->sub_of * rowh;
    r.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
           db->dock == TIKU_TRK_DB_LEFT_MINI)
              ? m.x + m.w + 2 : m.x - r.w - 2;
    if (r.x < 0) {
        r.x = 0;
    }
    return r;
}

/** @brief Append a submenu row that acts by @p command, with the icon
 *         named @p icon.  @return the row, or NULL when the submenu is
 *         full. */
static tiku_trk_db_item_t *
sub_row(tiku_trk_deskbar_t *db, int command, const char *label,
        const char *icon)
{
    tiku_trk_db_item_t *si;

    if (db->sub_count >= TIKU_TRK_DB_SUB_MAX) {
        return NULL;
    }
    si = &db->sub[db->sub_count++];
    memset(si, 0, sizeof *si);
    si->kind = TIKU_TRK_DB_ITEM_ROW;
    si->command = command;
    snprintf(si->label, sizeof si->label, "%s", (label != NULL) ? label : "");
    snprintf(si->icon, sizeof si->icon, "%s", (icon != NULL) ? icon : "");
    return si;
}

/** @brief Fill the submenu for leaf row @p at, or close it. */
void
db_open_sub(tiku_trk_deskbar_t *db, int at)
{
    const tiku_trk_db_item_t *it;

    db->sub_count = 0;
    db->sub_of = -1;
    db->sub_seen = -1;
    db->sub_hot = -1;
    db->sub_pending = -1;
    if (at < 0 || at >= db->leaf_count) {
        return;
    }
    db->leaf_hot = at; /* A click supersedes any previously hovered row. */
    it = &db->leaf[at];
    if (it->kind != TIKU_TRK_DB_ITEM_EXPAND || !db_leaf_enabled(db, it)) {
        return;
    }
    switch (it->expand) {
    case TIKU_TRK_DB_EXPAND_SETTINGS:
        sub_row(db, TIKU_TRK_DB_PREFERENCES, tiku_str("menu.deskbar_prefs"),
                "folder_config");
        sub_row(db, TIKU_TRK_DB_TRACKER_PREFERENCES,
                tiku_str("menu.tracker_prefs"), "folder_prefs");
        sub_row(db, TIKU_TRK_DB_DESKTOP_PREFERENCES,
                tiku_str("menu.desktop_prefs"), "desktop");
        sub_row(db, TIKU_TRK_DB_FONT_PREFERENCES, tiku_str("menu.fonts"),
                "folder_fonts");
        sub_row(db, TIKU_TRK_DB_SOUND_PREFERENCES, tiku_str("menu.sounds"),
                "");
#if TIKU_SHELL_BOARD_SERVICES

        sub_row(db, TIKU_TRK_DB_DEVICE_PREFERENCES, "Devices Preferences…",
                "microcontroller");
#endif

        sub_row(db, TIKU_TRK_DB_STARTUP_PREFERENCES, "Startup Preferences…",
                "");
        sub_row(db, TIKU_TRK_DB_ASSOC_PREFERENCES, "File Associations…",
                "folder_prefs");
        /* The label names the action: the opposite of the current
         * state. */
        sub_row(db, TIKU_TRK_DB_TOGGLE_REPLICANTS,
                tiku_str(db->settings.show_replicants
                             ? "menu.hide_replicants"
                             : "menu.show_replicants"),
                "package");
        break;
    case TIKU_TRK_DB_EXPAND_APPS: {
        int i;

        for (i = 0; i < db->app_count && db->sub_count < TIKU_TRK_DB_SUB_MAX;
             i++) {
            tiku_trk_db_item_t *si;

            if (db->app[i].group != it->which) {
                continue;
            }
            si = sub_row(db, TIKU_TRK_DB_LAUNCH_APP, db->app[i].label,
                         db->app[i].icon[0] != '\0' ? db->app[i].icon
                                                     : "application");
            if (si != NULL) {
                snprintf(si->path, sizeof si->path, "%s", db->app[i].command);
            }
        }
        /* Sorted by label, ignoring case, by moving whole rows so each
         * keeps its icon and launch path; equal labels keep their order. */
        for (i = 1; i < db->sub_count; i++) {
            tiku_trk_db_item_t item = db->sub[i];
            int j = i;
            while (j > 0 && strcasecmp(db->sub[j-1].label, item.label) > 0) {
                db->sub[j] = db->sub[j-1]; j--;
            }
            db->sub[j] = item;
        }
        break;
    }
    case TIKU_TRK_DB_EXPAND_RECENT: {
        int i;

        for (i = 0; i < db->recent_count[it->which] &&
                    i < db->settings.recent_count[it->which] &&
                    i < TIKU_TRK_DB_SUB_MAX; i++) {
            /* Each row shows the entry's name; its path goes with the
             * pick. */
            tiku_trk_db_item_t *si = sub_row(db, TIKU_TRK_DB_OPEN_PATH,
                db_leaf_name(db->recent[it->which][i]),
                (it->which == 1) ? "folder"
                                 : (it->which == 2) ? "application" : "file");

            if (si != NULL) {
                memcpy(si->path, db->recent[it->which][i], sizeof si->path);
                si->path[sizeof si->path - 1u] = '\0';
            }
        }
        break;
    }
    case TIKU_TRK_DB_EXPAND_DIR:
        if (db->backend != NULL && db->backend->ops->list != NULL) {
            struct menu_walk w;

            w.db = db;
            w.n = 0;
            w.into = db->sub;
            w.max = TIKU_TRK_DB_SUB_MAX;
            (void)db->backend->ops->list(db->backend, it->path, db_menu_entry,
                                         &w);
            db->sub_count = w.n;
        }
        break;
    default:
        break;
    }
    if (db->sub_count > 0) {
        db->sub_of = at;
        db->sub_hot = -1;
    }
}

/**
 * @brief The expander arrow beside a team: pointing down when its windows
 *        are shown, right when they are not.
 */
static void
draw_expander(tiku_surface_t *s, int x, int y, int expanded)
{
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);
    int k;

    /* An open three-point polyline, stroked with an EXPANDER_PEN-pixel
     * pen. */
    for (k = 0; k < EXPANDER_PEN; k++) {
        int i;

        for (i = 0; i < EXPANDER_ARM; i++) {
            if (expanded) {           /* pointing down: \ then /            */
                tiku_pixel(s, x + i, y + i + k, dark);
                tiku_pixel(s, x + 2 * EXPANDER_ARM - 2 - i, y + i + k,
                                dark);
            } else {                  /* pointing right: / then \           */
                tiku_pixel(s, x + i + k, y + i, dark);
                tiku_pixel(s, x + i + k, y + 2 * EXPANDER_ARM - 2 - i,
                                dark);
            }
        }
    }
}

/**
 * @brief A list row's background.
 *
 * An idle row is bevelled on every edge over a light gradient; a selected
 * one is a darker gradient with a single dark line on top and no side
 * bevel.  The label colour is the same either way.
 */
static void
draw_row_bg(tiku_surface_t *s, tiku_rect_t r, int selected)
{
    int i;

    for (i = 0; i < r.h; i++) {
        float t = (r.h > 1) ? (float)i / (float)(r.h - 1) : 0.0f;
        float tint = selected ? (1.25f + (1.20f - 1.25f) * t)
                              : (0.69f + (1.03f - 0.69f) * t);

        tiku_hline(s, r.x, r.y + i, r.w,
                        tiku_tint(TIKU_C_PANEL, tint));
    }
    if (selected) {
        tiku_hline(s, r.x, r.y, r.w,
                        tiku_tint(TIKU_C_PANEL, 1.40f));
        return;
    }
    tiku_hline(s, r.x, r.y, r.w,
                    tiku_tint(TIKU_C_PANEL, 0.50f));
    tiku_vline(s, r.x, r.y, r.h,
                    tiku_tint(TIKU_C_PANEL, 0.70f));
    tiku_vline(s, r.x + r.w - 1, r.y, r.h,
                    tiku_tint(TIKU_C_PANEL, 1.08f));
    tiku_hline(s, r.x, r.y + r.h - 1, r.w,
                    tiku_tint(TIKU_C_PANEL, 1.08f));
}

/** @brief Truncate to a width budget, with an ellipsis (DB-032). */
static void
fit_label(const tiku_font_t *f, const char *src, int budget, char *out,
          size_t max)
{
    size_t n;

    snprintf(out, max, "%s", src);
    if (tiku_text_width(f, out) <= budget) {
        return;
    }
    n = strlen(out);
    while (n > 1u) {
        out[--n] = '\0';
        if (n > 3u) {
            out[n - 1u] = '.';
            out[n - 2u] = '.';
            out[n - 3u] = '.';
        }
        if (tiku_text_width(f, out) <= budget) {
            return;
        }
    }
}

void
tiku_trk_deskbar_draw(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    const tiku_font_t *f = tiku_font_plain();
    const tiku_font_t *fb = tiku_font_bold();
    tiku_rgb_t dark = tiku_tint(TIKU_C_PANEL, 1.40f);
    tiku_rect_t leaf;
    int y, rows;
    int team_h;

    if (db == NULL || s == NULL) {
        return;
    }
    db->motion_damage = (tiku_rect_t){ 0, 0, 0, 0 };
    if (!db->menu_open) {
        db->menu_seen = 0;
        db->sub_seen = -1;
        db->menu_motion.active = 0;
        db->sub_motion.active = 0;
    }
    team_h = (db->settings.icon_size + 4 > db_team_row())
                 ? db->settings.icon_size + 4 : db_team_row();
    db->screen_h = s->h;
    rows = tiku_trk_deskbar_mini(db->dock) ? 0 : db_list_rows(db);
    db_draw_frame(db, s, &rows);

    /* A shadow beneath, a hairline frame, square corners. */
    tiku_shadow(s, db->frame, 12, 0, 0.35f);
    tiku_fill(s, db->frame, TIKU_C_PANEL);
    tiku_frame(s, db->frame, tiku_tint(TIKU_C_PANEL, 1.40f));

    db_draw_leaf(db, s, fb, dark, &leaf);

    db_draw_mini(db, s, f);

    db_draw_teams(db, s, f, team_h, &y);

    db_draw_tray(db, s, f, &y);
    db_draw_switch(db, s);
    db_draw_clock(db, s, f, &y);

    db_draw_leaf_menu(db, s, f);
    db_draw_win_menu(db, s, f);
    db_draw_clock_menu(db, s);
    db_draw_switch_strip(db, s);
    db_draw_tip(db, s);
    db_draw_calendar(db, s, f, fb, dark);
}

void
tiku_trk_deskbar_set_now(tiku_trk_deskbar_t *db, int64_t now)
{
    if (db != NULL) {
        db->motion_now = now;
    }
}

tiku_rect_t
tiku_trk_deskbar_motion_damage(const tiku_trk_deskbar_t *db)
{
    tiku_rect_t none = { 0, 0, 0, 0 };

    return (db != NULL) ? db->motion_damage : none;
}

/**
 * @brief The clock's calendar, while it is open: the month and year over a
 *        grid of its days, the chosen day lit (DB-045, DB-046).
 */
void
db_draw_calendar(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                 const tiku_font_t *f, const tiku_font_t *fb, tiku_rgb_t dark)
{
    if (db->calendar_open) {
        /* The four heading quarters step the month and year. */
        struct tm tmv;
        tiku_rect_t c = db_calendar_rect(db);
        int col, rowi, first, days;

        memset(&tmv, 0, sizeof tmv);
        tmv.tm_year = db->calendar_year - 1900;
        tmv.tm_mon = db->calendar_month - 1;
        tmv.tm_mday = 1;
        tmv.tm_isdst = -1;
        (void)mktime(&tmv);
        tiku_fill(s, c, TIKU_C_PANEL);
        tiku_frame(s, c, dark);
        tiku_bevel(s, tiku_inset(c, 1),
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_LIGHTEN_MAX),
                        tiku_tint(TIKU_C_PANEL,
                                       TIKU_DARKEN_2));
        {
            char head[32];
            fit_label(fb, tiku_str((const char *[]){ "month.1", "month.2",
                       "month.3", "month.4", "month.5", "month.6", "month.7",
                       "month.8", "month.9", "month.10", "month.11",
                       "month.12" }[tmv.tm_mon]), 60, head, sizeof head);
            (void)tiku_text_centered(s, fb,
                (tiku_rect_t){ c.x + 18, c.y + 2, 60, 18 }, head, TIKU_C_TEXT);
            snprintf(head, sizeof head, "%d", 1900 + tmv.tm_year);
            (void)tiku_text_centered(s, fb,
                (tiku_rect_t){ c.x + 108, c.y + 2, 60, 18 }, head, TIKU_C_TEXT);
            for (int button = 0; button < 4; button++) {
                int x = c.x + (button < 2 ? 8 : 98) + (button % 2) * 78;
                for (int d = 0; d < 4; d++) {
                    int px = x + (button % 2 ? -d : d);
                    tiku_vline(s, px, c.y + 10 - d, 2 * d + 1, TIKU_C_TEXT);
                }
            }
        }
        first = tmv.tm_wday;
        days = db_calendar_days(db->calendar_year, db->calendar_month);
        for (rowi = 0; rowi < 6; rowi++) {
            for (col = 0; col < 7; col++) {
                int day = rowi * 7 + col - first + 1;
                char num[12];
                tiku_rect_t cell = { c.x + 6 + col * 25,
                                          c.y + 22 + rowi * 20, 24, 18 };
                if (day < 1 || day > days) {
                    continue;
                }
                if (day == db->calendar_day) {
                    tiku_fill(s, cell, TIKU_C_SELECT);
                }
                snprintf(num, sizeof num, "%d", day);
                (void)tiku_text_centered(s, f, cell, num,
                    (day == db->calendar_day) ? TIKU_C_SELTEXT
                                         : TIKU_C_TEXT);
            }
        }
    }
}

/**
 * @brief The tooltip beside the bar, on the side facing the desktop.
 */
void
db_draw_tip(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    if (db->tip_y >= 0 && db->tip[0] != '\0') {
        /* The tip tiku_trk_deskbar_motion() chose: a cut or hidden name
         * (DB-032), a tray icon's tip, or the clock's long date
         * (DB-044). */
        tiku_rect_t b = tip_box(db);
        tiku_ui_tip(s, b, db->tip);
    }
}

/**
 * @brief The application list's scroll arrows (DB-034).
 */
void
db_draw_switch_strip(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    {
        /* Each arrow shows while something is out of sight in the
         * direction it points (DB-034). */
        int up = 0, down = 0, k;
        int ly = db_list_y(db);
        int lb = db_list_bottom(db);
        int cx = db->frame.x + db->frame.w / 2;

        tiku_trk_deskbar_arrows(db, &up, &down);
        for (k = 0; k < 4 && up; k++) {
            tiku_hline(s, cx - k, ly + 1 + k, 2 * k + 1,
                            TIKU_C_TEXT);
        }
        for (k = 0; k < 4 && down; k++) {
            tiku_hline(s, cx - (3 - k), lb - 5 + k, 2 * (3 - k) + 1,
                            TIKU_C_TEXT);
        }
    }
}

/**
 * @brief The clock's four-row menu, while it is open.
 */
void
db_draw_clock_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s)
{
    if (db->clock_menu_open) {
        const char *items[4];
        tiku_rect_t m = db_clock_menu_rect(db);

        clock_menu_items(db, items);
        tiku_ui_menu(s, m, items, 4, db->clock_hot);
    }
}

/**
 * @brief One application's window menu, while it is open, its refused rows
 *        dimmed.
 */
void
db_draw_win_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                 const tiku_font_t *f)
{
    /* Hide all, Show all and Close all; an application with no windows
     * gets No windows and Force Quit… instead (DB-028). */
    if (db->win_menu_team >= 0 && db->win_menu_team < db->team_count) {
        const char *items[4];
        int enabled[4];
        int n = db_win_menu_items(db, db->win_menu_team, items, enabled);
        int rowh = f->height + 6;
        tiku_rect_t m = db_win_menu_rect(db, n, rowh);
        int k;

        tiku_ui_menu(s, m, items, n, db->win_hot);
        for (k = 0; k < n; k++) {
            /* A refused row is redrawn dimmed over the menu's own
             * drawing (DB-004, DB-028). */
            if (!enabled[k] && items[k][0] != '-') {
                tiku_rect_t r = { m.x + 2, m.y + 2 + k * rowh,
                                       m.w - 4, rowh };

                tiku_fill(s, r, TIKU_C_PANEL);
                tiku_text(s, f, r.x + 8,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               items[k],
                               tiku_tint(TIKU_C_PANEL, 1.55f));
            }
        }
    }
}

/**
 * @brief The leaf menu and its open submenu, each fading in as it opens.
 */
void
db_draw_leaf_menu(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f)
{
    int i;

    /* The rows db_build_leaf() made: Find, Force Quit, Settings and the
     * application groups, the mounted stores and their forms, the recent
     * lists, the Deskbar folder's entries and Shut down. */
    if (db->menu_open) {
        const char *items[TIKU_TRK_DB_LEAF_MAX];
        int rowh = f->height + 6;
        tiku_rect_t m = db_leaf_menu_rect(db, rowh, db->leaf_count);
        tiku_rect_t captured = { 0, 0, 0, 0 };
        tiku_surface_t *under;
        float progress;

        if (!db->menu_seen) {
            tiku_motion_start(&db->menu_motion, db->motion_now,
                              TIKU_MOTION_MENU_US);
            db->menu_seen = 1;
        }
        progress = tiku_motion_progress(&db->menu_motion, db->motion_now);
        under = tiku_motion_fade_begin(s, m, progress, &captured);
        for (i = 0; i < db->leaf_count; i++) {
            items[i] = (db->leaf[i].kind == TIKU_TRK_DB_ITEM_SEP)
                           ? "-" : db->leaf[i].label;
        }
        if (db->menu_icon != NULL) {
            struct menu_icon_relay relay = { db, db->leaf };

            tiku_ui_menu_icons(s, m, items, db->leaf_count,
                                    db->leaf_hot, relay_menu_icon, &relay);
        } else {
            tiku_ui_menu(s, m, items, db->leaf_count, db->leaf_hot);
        }
        for (i = 0; i < db->leaf_count; i++) {
            /* A disabled row is redrawn dimmed over the menu's own
             * drawing (DB-004). */
            if (db->leaf[i].kind != TIKU_TRK_DB_ITEM_SEP &&
                !db_leaf_enabled(db, &db->leaf[i])) {
                tiku_rect_t r = { m.x + 2, m.y + 2 + i * rowh,
                                       m.w - 4, rowh };

                tiku_fill(s, r, TIKU_C_PANEL);
                tiku_text(s, f, r.x + 10,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               db->leaf[i].label,
                               tiku_tint(TIKU_C_PANEL, 1.55f));
            } else if (item_expands(db->leaf[i].kind)) {
                /* An expanding row carries the drawn submenu arrow the
                 * other menus use (DB-002). */
                tiku_ui_submenu_arrow(s,
                    (tiku_rect_t){ m.x, m.y + 2 + i * rowh, m.w, rowh },
                    TIKU_C_TEXT);
            }
        }
        tiku_motion_fade_end(s, under, captured, progress);
        if (db->menu_motion.active) {
            db->motion_damage = m;
        }
        if (db->sub_of >= 0 && db->sub_count > 0) {
            const char *sitems[TIKU_TRK_DB_SUB_MAX];
            tiku_rect_t sm = db_sub_menu_rect(db, m, rowh);

            if (db->sub_seen != db->sub_of) {
                tiku_motion_start(&db->sub_motion, db->motion_now,
                                  TIKU_MOTION_MENU_US);
                db->sub_seen = db->sub_of;
            }
            progress = tiku_motion_progress(&db->sub_motion, db->motion_now);
            under = tiku_motion_fade_begin(s, sm, progress, &captured);
            for (i = 0; i < db->sub_count; i++) {
                sitems[i] = db->sub[i].label;
            }
            if (db->menu_icon != NULL) {
                struct menu_icon_relay relay = { db, db->sub };

                tiku_ui_menu_icons(s, sm, sitems, db->sub_count,
                                        db->sub_hot, relay_menu_icon,
                                        &relay);
            } else {
                tiku_ui_menu(s, sm, sitems, db->sub_count, db->sub_hot);
            }
            tiku_motion_fade_end(s, under, captured, progress);
            if (db->sub_motion.active) {
                db->motion_damage = db->motion_damage.w > 0
                    ? tiku_rect_union(db->motion_damage, sm) : sm;
            }
        } else {
            db->sub_seen = -1;
            db->sub_motion.active = 0;
        }
    }
}

/**
 * @brief The clock: in its own row down a side edge, before the theme
 *        switch along a top or bottom edge.  Down a side edge @p y is
 *        moved past the switch's strip first.
 */
void
db_draw_clock(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f,
              int *y)
{
    char clock[64];
    int cw;

    if (tiku_trk_deskbar_vertical(db->dock)) {
        (*y) += SWITCH_ROW;
    }
    if (!db->settings.show_clock) {
        return;
    }
    clock_text(db, clock, sizeof clock);
    cw = tiku_text_width(f, clock);
    if (!tiku_trk_deskbar_vertical(db->dock)) {
        /* Along an edge: inside the bar, after the tray's icons and
         * before the theme switch. */
        tiku_text(s, f, db->frame.x + db->frame.w - (SWITCH_W + 14) -
                  db_hclock_w(db) + 6, db->frame.y +
                  (db->frame.h - f->height) / 2 + f->ascent, clock,
                  TIKU_C_TEXT);
        return;
    }
    tiku_text(s, f, db->frame.x + (db->frame.w - cw) / 2,
              (*y) + (db_clock_row() - f->height) / 2 + f->ascent,
              clock, TIKU_C_TEXT);
}

/** @brief The clock's words: the date, the time and, when asked for, the
 *         seconds, the weekday and the zone, from the bar's own clock. */
static void
clock_text(const tiku_trk_deskbar_t *db, char *clock, size_t cap)
{
    time_t now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
    struct tm tmv;
    char date[32];
    char time_text[32];
    char time_fmt[16] = "%H:%M";

    (void)localtime_r(&now, &tmv);
    if (db->settings.show_seconds) {
        snprintf(time_fmt, sizeof time_fmt, "%%H:%%M:%%S");
    }
    if (strftime(date, sizeof date,
                 db->settings.show_day_of_week ? "%a %e %b" : "%e %b",
                 &tmv) == 0) {
        snprintf(date, sizeof date, "%d", tmv.tm_mday);
    }
    if (strftime(time_text, sizeof time_text, time_fmt, &tmv) == 0) {
        snprintf(time_text, sizeof time_text, "%02d:%02d",
                 tmv.tm_hour, tmv.tm_min);
    }
    if (db->settings.show_timezone) {
        char zone[16];
        size_t used;

        if (strftime(zone, sizeof zone, "%Z", &tmv) == 0 ||
            zone[0] == '\0') {
            snprintf(zone, sizeof zone, "local");
        }
        snprintf(clock, cap, "%s %s", date, time_text);
        used = strlen(clock);
        if (used + 1u < cap) {
            snprintf(clock + used, cap - used, " %s", zone);
        }
    } else {
        snprintf(clock, cap, "%s %s", date, time_text);
    }
}

/** @brief The room the clock takes along a top or bottom edge, with its
 *         margins; none down a side edge, where it has a row of its own,
 *         or when it is off. */
int
db_hclock_w(const tiku_trk_deskbar_t *db)
{
    char clock[64];

    if (tiku_trk_deskbar_vertical(db->dock) || !db->settings.show_clock) {
        return 0;
    }
    clock_text(db, clock, sizeof clock);
    return tiku_text_width(tiku_font_plain(), clock) + 12;
}

/**
 * @brief The applications' own tray icons, each with its text after it,
 *        the one under the pointer lit.
 */
static void
db_draw_trayicons(tiku_trk_deskbar_t *db, tiku_surface_t *s,
                  const tiku_font_t *f)
{
    for (int i = 0; i < db->trayicon_count; i++) {
        const tiku_trk_trayicon_t *t = &db->trayicons[i];
        tiku_rect_t r = tiku_trk_deskbar_trayicon_rect(db, i);

        if (r.w <= 0) {
            continue;
        }
        if (i == db->trayicon_hot) {
            tiku_fill(s, (tiku_rect_t){ r.x - 2, r.y, r.w + 4, r.h },
                      tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
        }
        /* DB-018: an icon the set does not have leaves its slot as it
         * is; the text, if any, is still drawn. */
        (void)tiku_icon_paint(s, t->icon, r.x, r.y + (r.h - 16) / 2, 16);
        if (t->text[0] != '\0') {
            tiku_text(s, f, r.x + 19, r.y + (r.h - f->height) / 2 +
                      f->ascent, t->text, TIKU_C_TEXT);
        }
    }
}

/**
 * @brief The tray: the applications' tray icons, then down a side edge a
 *        row per replicant while they are shown, leaving @p y at the
 *        tray's foot.
 */
void
db_draw_tray(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f,
             int *y)
{
    int i;

    /* Down a side edge the tray is placed up from the bar's foot, as the
     * switch and the clock are, so it sits below the mini dock's
     * application button. */
    (*y) = tiku_trk_deskbar_vertical(db->dock) ? db_tray_top(db)
                                               : db_tray_y(db);
    if (!tiku_trk_deskbar_vertical(db->dock)) {
        /* Along a top or bottom edge the tray is its icons only; the
         * replicant rows and their rule are not drawn. */
        db_draw_trayicons(db, s, f);
        return;
    }
    tiku_hline(s, db->frame.x + 4, (*y) - 3, db->frame.w - 8,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    tiku_hline(s, db->frame.x + 4, (*y) - 2, db->frame.w - 8,
                    tiku_tint(TIKU_C_PANEL,
                                   TIKU_LIGHTEN_MAX));
    db_draw_trayicons(db, s, f);
    (*y) += db_tray_h(db) - db_tray_rows(db) * TRAY_ROW;
    for (i = 0; i < db_tray_rows(db); i++) {
        const tiku_trk_replicant_t *r = &db->tray[i];
        tiku_rect_t saved_clip = s->clip;
        tiku_rect_t ink = {db->frame.x, *y + (TRAY_ROW - r->height) / 2,
                           db->frame.w, r->height};
        int ty = (*y) + (TRAY_ROW - f->height) / 2 + f->ascent;
        char val[32];
        int vw;

        /* Each reading is formatted by its node's description. */
        (void)tiku_attr_format(r->desc_known ? &r->desc : NULL,
                                   r->valid ? r->value : NULL,
                                   val, sizeof val);
        vw = tiku_text_width(f, val);

        int right = ink.x + ink.w, bottom = ink.y + ink.h;
        if (right > saved_clip.x + saved_clip.w)
            right = saved_clip.x + saved_clip.w;
        if (bottom > saved_clip.y + saved_clip.h)
            bottom = saved_clip.y + saved_clip.h;
        if (ink.x < saved_clip.x) ink.x = saved_clip.x;
        if (ink.y < saved_clip.y) ink.y = saved_clip.y;
        ink.w = right > ink.x ? right - ink.x : 0;
        ink.h = bottom > ink.y ? bottom - ink.y : 0;
        tiku_clip_set(s, ink);
        tiku_text(s, f, db->frame.x + 6, ty, r->label, TIKU_C_TEXT);
        tiku_text(s, f, db->frame.x + db->frame.w - 6 - vw, ty, val,
                       TIKU_C_TEXT);
        tiku_clip_set(s, saved_clip);
        (*y) += TRAY_ROW;
    }
}

/**
 * @brief The application list: along a top or bottom edge a strip of
 *        buttons, down a side edge a row per application with its windows
 *        beneath it when expanded.
 */
void
db_draw_teams(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f,
              int team_h, int *y)
{
    int i, j;

    if (!tiku_trk_deskbar_mini(db->dock) &&
        !tiku_trk_deskbar_vertical(db->dock)) {
        /* The horizontal strip: as many buttons as fit, each between a
         * font-derived minimum and 1.25x it, shrinking as applications
         * launch and growing back as they quit.  When they do not all fit
         * at the minimum they keep it, an arrow is drawn at the strip's
         * right end, and one at its left end once it is scrolled
         * (DB-033). */
        int n = db->team_count;
        int min_w = 24 + tiku_text_width(f, "abcdefgh");
        int max_w = min_w + min_w / 4;
        int left = db->frame.x + 2 + HLEAF_W + 4;
        int right = db->frame.x + db->frame.w - (SWITCH_W + 14) -
                    db_hclock_w(db) - db_trayicons_w(db);  /* tray icons */
        int avail = right - left;
        int bw = (n > 0) ? avail / n : max_w;
        int bx, i2, k;

        if (bw > max_w) { bw = max_w; }
        db->hoverflow = (n > 0 && bw < min_w);
        if (db->hoverflow) {
            bw = min_w;
            left += 12;
            right -= 12;
        } else {
            db->scroll = 0;
        }
        db->overflow = db->hoverflow;
        db->hbutton_w = bw;
        db->hleft = left;
        bx = left;
        for (i2 = (db->hoverflow ? db->scroll : 0); i2 < n; i2++) {
            tiku_trk_team_t *t = &db->teams[db->order[i2]];
            tiku_rect_t b = { bx, db->frame.y + 2, bw - 2, db_leaf_row() };
            char label[TIKU_TRK_DB_LABEL_MAX];

            if (bx + bw > right) {
                break;
            }
            draw_row_bg(s, b, t->active);
            (void)tiku_icon_paint(s,
                tiku_trk_deskbar_team_icon(db, db->order[i2]),
                b.x + 3, b.y + (db_leaf_row() - 16) / 2, 16);
            fit_label(f, t->name, b.w - 16 - 10, label, sizeof label);
            tiku_text(s, f, b.x + 16 + 6,
                           b.y + (db_leaf_row() - f->height) / 2 + f->ascent,
                           label, TIKU_C_TEXT);
            bx += bw;
        }
        if (db->hoverflow) {
            int ay = db->frame.y + db->frame.h / 2;

            for (k = 0; k < 4; k++) {
                if (db->scroll > 0) {
                    tiku_vline(s, left - 10 + k, ay - k, 2 * k + 1,
                                    TIKU_C_TEXT);
                }
                tiku_vline(s, right + 3 + k, ay - (3 - k),
                                2 * (3 - k) + 1, TIKU_C_TEXT);
            }
        }
    }
    if (!tiku_trk_deskbar_mini(db->dock) &&
        tiku_trk_deskbar_vertical(db->dock)) {
        int lb = db_list_bottom(db);

        /* From the first row shown, and only the rows that end above the
         * list's bottom (DB-034). */
        (*y) = db_list_y(db);
        for (i = db_list_first(db); i < db->team_count; i++) {
            int team = db->order[i];
            tiku_trk_team_t *t = &db->teams[team];
            tiku_rect_t row = { db->frame.x + 2, (*y), db->frame.w - 4,
                                     team_h };
            int icon_size = db->settings.icon_size;
            tiku_rect_t icon = { row.x + 4,
                                      (*y) + (team_h - icon_size) / 2,
                                      icon_size, icon_size };
            char label[TIKU_TRK_DB_LABEL_MAX];

            if ((*y) + team_h > lb) {
                break;
            }
            /* A hovering drag lights the row it would be handed to
             * (DB-035). */
            if (db->drop_team == team) {
                tiku_fill(s, row, TIKU_C_SELECT);
            } else {
                draw_row_bg(s, row, t->active);
            }
            /* The application's icon: its own, or the generic one
             * (tiku_trk_deskbar_team_icon).  DB-018: when even that is
             * missing, the slot is left as it is. */
            (void)tiku_icon_paint(s, tiku_trk_deskbar_team_icon(db, team),
                                      icon.x, icon.y, icon_size);

            if (!db->settings.hide_app_names) {
                fit_label(f, t->name,
                          row.w - icon_size - EXPANDER_W - 14, label,
                          sizeof label);
                tiku_text(s, f, icon.x + icon_size + 5,
                               (*y) + (team_h - f->height) / 2 + f->ascent,
                               label, TIKU_C_TEXT);
            }
            if (db->settings.show_expander) {
                draw_expander(s, row.x + row.w - EXPANDER_W - 2,
                              (*y) + (team_h - 9) / 2,
                              t->expanded ||
                              db->expander_press == db->order[i]);
            }
            (*y) += team_h + SEAM;

            if (!t->expanded) {
                continue;
            }
            for (j = 0; j < t->win_count; j++) {
                tiku_trk_dbwin_t *w = &t->wins[j];
                tiku_rect_t wr = { db->frame.x + 2, (*y), db->frame.w - 4,
                                        db_win_row() };
                char wl[TIKU_TRK_DB_LABEL_MAX];

                if ((*y) + db_win_row() > lb) {
                    break;
                }
                draw_row_bg(s, wr, w->current);
                draw_win_icon(s, wr.x + WIN_INDENT,
                              (*y) + (db_win_row() - 9) / 2,
                              w->state == TIKU_TRK_DBW_MINIMIZED,
                              0);
                fit_label(f, w->title, wr.w - WIN_INDENT - 20, wl,
                          sizeof wl);
                tiku_text(s, f, wr.x + WIN_INDENT + 14,
                          (*y) + (db_win_row() - f->height) / 2 + f->ascent,
                          wl, TIKU_C_TEXT);
                (*y) += db_win_row() + SEAM;
            }
        }
    }
}

/**
 * @brief The mini dock's application button and, while it is open, its
 *        running-apps menu (DB-037).
 */
void
db_draw_mini(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *f)
{
    if (tiku_trk_deskbar_mini(db->dock)) {
        /* The mini dock's second icon: one button under the leaf that
         * opens the running-apps menu (DB-037). */
        tiku_rect_t ab = { db->frame.x + 2,
                                db->frame.y + 2 + db_leaf_row() + 2,
                                db->frame.w - 4, db_team_row() };

        draw_row_bg(s, ab, db->apps_menu_open);
        (void)tiku_icon_paint(s, "application",
                                  ab.x + (ab.w - 16) / 2,
                                  ab.y + (ab.h - 16) / 2, 16);
        if (db->apps_menu_open) {
            const char *items[TIKU_TRK_DB_TEAMS_MAX];
            int n = db->team_count, k;
            int rowh = f->height + 6;
            tiku_rect_t am;

            if (n == 0) {
                items[0] = "no application running";
                n = 1;
            } else {
                for (k = 0; k < n; k++) {
                    items[k] = db->teams[db->order[k]].name;
                }
            }
            am.w = 176;
            am.h = n * rowh + 4;
            am.y = ab.y;
            am.x = (db->frame.x > 0) ? db->frame.x - am.w - 2
                                     : db->frame.x + db->frame.w + 2;
            if (db->team_count == 0) {
                tiku_ui_menu(s, am, items, n, db->apps_hot);
            } else {                /* each with its team's icon */
                tiku_ui_menu_icons(s, am, items, n, db->apps_hot,
                                   apps_menu_icon, db);
            }
            if (db->team_count == 0) {
                /* The empty menu's one row, dimmed (DB-004). */
                tiku_rect_t r = { am.x + 2, am.y + 2, am.w - 4,
                                       rowh };

                tiku_fill(s, r, TIKU_C_PANEL);
                tiku_text(s, f, r.x + 10,
                               r.y + (rowh - f->height) / 2 + f->ascent,
                               items[0],
                               tiku_tint(TIKU_C_PANEL, 1.55f));
            }
        }
    }
}

#define LEAF_HELD      1.22f    /* the face while the menu is out */
#define LEAF_HELD_EDGE 1.55f    /* its top and left edges, held   */
#define LEAF_HOVER     0.72f    /* the face under the pointer     */

/**
 * @brief The leaf button: the TikuOS mark and name on the tab colour, held
 *        down while its menu is out.  Sets @p leaf to its rectangle.
 */
void
db_draw_leaf(tiku_trk_deskbar_t *db, tiku_surface_t *s, const tiku_font_t *fb,
             tiku_rgb_t dark, tiku_rect_t *leaf)
{
    /* The leaf is filled with the tab colour.  While its menu is out it is
     * drawn pressed: a deeper face, the bevel dark on the top and left
     * edges and light on the far ones, and the mark and label one pixel
     * down and right. */
    int down = db->menu_open;
    tiku_rgb_t face;

    (*leaf) = db_leaf_button_rect(db);
    /* The button moves when a hidden bar comes back or the bar changes
     * edge, so the hover is worked out again from the last pointer. */
    db->leaf_hover = leaf_button_hovered(db);
    face = down             ? tiku_tint(TIKU_C_TAB, LEAF_HELD)
         : db->leaf_hover   ? tiku_tint(TIKU_C_TAB, LEAF_HOVER)
         :                    TIKU_C_TAB;
    tiku_fill(s, (*leaf), face);
    tiku_frame(s, (*leaf), dark);
    tiku_bevel(s, tiku_inset((*leaf), 1),
                    tiku_tint(TIKU_C_TAB, down ? LEAF_HELD_EDGE
                                               : TIKU_LIGHTEN_1),
                    tiku_tint(TIKU_C_TAB, down ? TIKU_LIGHTEN_2
                                               : TIKU_DARKEN_2));
    {
        /* The mark, drawn without a ground of its own, then the name
         * beside it. */
        int mark = db_leaf_row() - 10;  /* 24 at the default face */
        tiku_rect_t label = (*leaf);
        /*
         * The mark's shade follows the leaf's colour in each theme: 1.0 in
         * light (near-black edges on amber), 0.35 in dark (lighter edges
         * on the darker amber), 1.15 without hue (on the mid-grey tab).
         */
        float shade = (db->theme == 1) ? 0.35f
                    : (db->theme == 2) ? 1.15f : 1.0f;

        tiku_logo_paint_with(s, (tiku_rect_t){ (*leaf).x + 4 + down,
                                               (*leaf).y + down +
                                                   ((*leaf).h - mark) / 2,
                                               mark, mark }, 0u, NULL,
                             shade);
        label.x += mark + 8 + down;
        label.y += down;
        label.w -= mark + 8;
        (void)tiku_text_centered(s, fb, label, TIKU_SHELL_PRODUCT_NAME,
                                 TIKU_C_TABTEXT);
    }
}

/**
 * @brief Lay the frame out for the dock, capping @p rows at the room the
 *        list has; while the bar is hidden, shrink it to its sliver and
 *        paint that.
 */
void
db_draw_frame(tiku_trk_deskbar_t *db, tiku_surface_t *s, int *rows)
{
    {
        /* The room the list has once the leaf, the tray, the switch and
         * the clock have taken theirs.  A longer list makes the bar
         * scrollable (DB-034). */
        int room = s->h - db->top_inset - (db_leaf_row() + PAD) -
                   db_tray_h(db) - SWITCH_ROW -
                   (db->settings.show_clock ? db_clock_row() : 0) - 2 * PAD;

        db->overflow = (!tiku_trk_deskbar_mini(db->dock) && (*rows) > room &&
                        room > 0);
        if (!db->overflow) {
            db->scroll = 0;     /* nothing hidden: nothing scrolled past */
        }
        if (db->overflow && (*rows) > room) {
            (*rows) = room;
        }
    }
    db->frame.w = tiku_trk_deskbar_width(db);
    /* The bar starts below the top inset, the global menu bar's strip
     * when that bar is on. */
    db->frame.y = db->top_inset;
    if (tiku_trk_deskbar_vertical(db->dock)) {
        db->frame.x = (db->dock == TIKU_TRK_DB_LEFT_EXPANDED ||
                       db->dock == TIKU_TRK_DB_LEFT_MINI)
                          ? 0 : s->w - db->frame.w;
        db->frame.h = db_leaf_row() + PAD + (*rows) + PAD +
                      (tiku_trk_deskbar_mini(db->dock) ? db_team_row() + 2
                                                       : 0) +
                      db_tray_h(db) + SWITCH_ROW +
                      (db->settings.show_clock ? db_clock_row() : 0) + PAD;
    } else {
        /* Along an edge: the bar spans the screen and is as deep as one
         * row needs.  The application strip lives here: buttons that
         * shrink as more launch and grow back as they quit (DB-033). */
        db->frame.x = 0;
        db->frame.w = s->w;
        db->frame.h = db_leaf_row() + PAD;
        db->frame.y = (db->dock == TIKU_TRK_DB_TOP)
                          ? db->top_inset : s->h - db->frame.h;
        (*rows) = 0;
    }
    if (!tiku_trk_deskbar_shown(db)) {
        /* Hidden: the frame becomes a sliver against the bar's edge
         * (DB-051); the hit test and the drawing both read the frame. */
        if (tiku_trk_deskbar_vertical(db->dock)) {
            if (db->frame.x > 0) {
                db->frame.x = s->w - TIKU_TRK_DB_SLIVER;
            }
            db->frame.w = TIKU_TRK_DB_SLIVER;
        } else {
            if (db->dock == TIKU_TRK_DB_BOTTOM) {
                db->frame.y = s->h - TIKU_TRK_DB_SLIVER;
            }
            db->frame.h = TIKU_TRK_DB_SLIVER;
        }
        tiku_fill(s, db->frame, TIKU_C_PANEL);
        return;
    }
}
