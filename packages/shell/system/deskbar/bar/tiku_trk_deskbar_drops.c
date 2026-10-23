/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_drops.c - drops onto the bar and its open menu, and the
 * clock's fixture and stamp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

tiku_trk_db_drop_t
tiku_trk_deskbar_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                         int *which)
{
    tiku_rect_t f;
    int ry, i;

    if (which != NULL) {
        *which = -1;
    }
    if (db == NULL || !tiku_trk_deskbar_hit(db, x, y)) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    f = tiku_trk_deskbar_frame(db);
    if (!tiku_trk_deskbar_vertical(db->dock)) {
        /* The horizontal strip: the leaf button at the left end, then the
         * application buttons where the last draw put them (DB-033). */
        int right = f.x + f.w - (SWITCH_W + 14) - db_hclock_w(db) -
                    db_trayicons_w(db) - (db->hoverflow ? 12 : 0);
        int slot;

        if (x < f.x + 2 + HLEAF_W) {
            return TIKU_TRK_DB_DROP_LEAF;
        }
        if (db->hbutton_w <= 0 || x < db->hleft) {
            return TIKU_TRK_DB_DROP_BAR;
        }
        slot = (x - db->hleft) / db->hbutton_w;
        i = (db->hoverflow ? db->scroll : 0) + slot;
        if (i < db->team_count &&
            db->hleft + (slot + 1) * db->hbutton_w <= right) {
            if (which != NULL) { *which = db->order[i]; }
            return TIKU_TRK_DB_DROP_TEAM;
        }
        return TIKU_TRK_DB_DROP_BAR;
    }
    if (y - f.y < db_leaf_row()) {
        return TIKU_TRK_DB_DROP_LEAF;
    }
    /* The rows as drawn: from the first one shown, down to the list's
     * bottom (DB-034). */
    ry = db_list_y(db);
    for (i = db_list_first(db); i < db->team_count; i++) {
        int team = db->order[i];
        int team_h = (db->settings.icon_size + 4 > db_team_row())
                         ? db->settings.icon_size + 4 : db_team_row();
        int wins = db->teams[team].expanded ? db->teams[team].win_count : 0;

        if (ry + team_h > db_list_bottom(db)) {
            break;
        }
        if (y >= ry && y < ry + team_h) {
            if (which != NULL) { *which = team; }
            return TIKU_TRK_DB_DROP_TEAM;
        }
        ry += team_h + SEAM;
        /* A window row is not a drop target of its own: a drop there
         * belongs to the application that owns it. */
        for (int j = 0; j < wins; j++) {
            if (ry + db_win_row() > db_list_bottom(db)) {
                break;
            }
            if (y >= ry && y < ry + db_win_row()) {
                if (which != NULL) { *which = team; }
                return TIKU_TRK_DB_DROP_TEAM;
            }
            ry += db_win_row() + SEAM;
        }
    }
    if (tiku_trk_deskbar_vertical(db->dock) && y >= db_tray_top(db)) {
        /* The tray and its clock: a dropped node is a subscription
         * request (DB-038). */
        return TIKU_TRK_DB_DROP_TRAY;
    }
    return TIKU_TRK_DB_DROP_BAR;
}

void
tiku_trk_deskbar_set_dragging(tiku_trk_deskbar_t *db, int dragging)
{
    if (db != NULL) {
        db->dragging = dragging;
        if (!dragging) {
            /* Ending the drag clears the drop highlight. */
            db->drop_team = -1;
        }
    }
}

void
tiku_trk_deskbar_set_drop_team(tiku_trk_deskbar_t *db, int i)
{
    if (db != NULL) {
        db->drop_team = i;
    }
}

int
tiku_trk_deskbar_drop_team(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->drop_team : -1;
}

int
tiku_trk_deskbar_item_enabled(const tiku_trk_deskbar_t *db, int fixed)
{
    if (db == NULL) {
        return 1;
    }
    /* While a drag is in flight a fixed row is reported disabled; folder
     * entries and recents stay enabled as the menu's drop targets
     * (DB-008). */
    return fixed ? !db->dragging : 1;
}

int
db_leaf_enabled(const tiku_trk_deskbar_t *db, const tiku_trk_db_item_t *item)
{
    int fixed = item->kind == TIKU_TRK_DB_ITEM_EXPAND
        ? item->expand == TIKU_TRK_DB_EXPAND_SETTINGS
        : item->command != TIKU_TRK_DB_OPEN_PATH;
    return item->kind != TIKU_TRK_DB_ITEM_DISABLED &&
           item->kind != TIKU_TRK_DB_ITEM_SEP &&
           tiku_trk_deskbar_item_enabled(db, fixed);
}

int
tiku_trk_deskbar_team_type(tiku_trk_deskbar_t *db, int i, const char *type)
{
    if (db == NULL || i < 0 || i >= db->team_count || type == NULL ||
        db->teams[i].ntype >= 8) {
        return -1;
    }
    snprintf(db->teams[i].type[db->teams[i].ntype],
             sizeof db->teams[i].type[0], "%s", type);
    db->teams[i].ntype++;
    return 0;
}

int
tiku_trk_deskbar_team_accepts(const tiku_trk_deskbar_t *db, int i,
                              const char *const *types, int ntypes,
                              int force)
{
    int t, k;

    if (db == NULL || i < 0 || i >= db->team_count) {
        return 0;
    }
    if (force || db->teams[i].takes_anything) {
        return 1;
    }
    if (ntypes <= 0) {
        return 0;                   /* no types: refused */
    }
    /* Every type must be one it opens: a mixed selection it can read
     * only part of is refused. */
    for (t = 0; t < ntypes; t++) {
        int ok = 0;

        for (k = 0; k < db->teams[i].ntype; k++) {
            if (strcmp(db->teams[i].type[k], types[t]) == 0) {
                ok = 1;
                break;
            }
        }
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

int
tiku_trk_deskbar_launchable(const tiku_model_t *m)
{
    if (m == NULL) {
        return 0;
    }
    /* A directory is not launchable (DB-066). */
    return !tiku_model_is_container(m);
}

tiku_trk_db_drop_t
tiku_trk_deskbar_menu_drop_at(const tiku_trk_deskbar_t *db, int x, int y,
                              char *path, size_t max)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rect_t m;
    int rowh, i;

    if (path != NULL && max > 0u) {
        path[0] = '\0';
    }
    if (db == NULL || !db->menu_open) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    rowh = f->height + 6;
    m = db_leaf_menu_rect(db, rowh, db->leaf_count);
    if (x < m.x || x >= m.x + m.w || y < m.y + 2 ||
        y >= m.y + 2 + db->leaf_count * rowh) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    i = (y - m.y - 2) / rowh;
    if (i < 0 || i >= db->leaf_count) {
        return TIKU_TRK_DB_DROP_NONE;
    }
    /* A folder entry, or an open-path row with the folder icon (a mounted
     * store's row), takes the links; any other open-path row is a file,
     * launched with them; every other row is fixed and takes nothing
     * (DB-008). */
    if ((db->leaf[i].kind == TIKU_TRK_DB_ITEM_EXPAND &&
         db->leaf[i].expand == TIKU_TRK_DB_EXPAND_DIR) ||
        (db->leaf[i].kind == TIKU_TRK_DB_ITEM_ROW &&
         db->leaf[i].command == TIKU_TRK_DB_OPEN_PATH &&
         db->leaf[i].path[0] == '/' &&
         strcmp(db->leaf[i].icon, "folder") == 0)) {
        if (path != NULL) {
            snprintf(path, max, "%s", db->leaf[i].path);
        }
        return TIKU_TRK_DB_DROP_MENU_DIR;
    }
    if (db->leaf[i].kind == TIKU_TRK_DB_ITEM_ROW &&
        db->leaf[i].command == TIKU_TRK_DB_OPEN_PATH) {
        if (path != NULL) {
            snprintf(path, max, "%s", db->leaf[i].path);
        }
        return TIKU_TRK_DB_DROP_MENU_FILE;
    }
    return TIKU_TRK_DB_DROP_NONE;
}

void
tiku_trk_deskbar_set_clock(tiku_trk_deskbar_t *db, time_t at)
{
    if (db != NULL) {
        db->fixed_now = at;
    }
}

int
tiku_trk_deskbar_clock_stamp(const tiku_trk_deskbar_t *db, char *out,
                             size_t max)
{
    time_t now;
    struct tm tmv;

    if (db == NULL || out == NULL || max == 0u || !db->settings.show_clock) {
        if (out != NULL && max > 0u) {
            out[0] = '\0';
        }
        return 0;
    }
    now = (db->fixed_now != 0) ? db->fixed_now : time(NULL);
    (void)localtime_r(&now, &tmv);
    /* The fields the rendering can vary with: a repaint is owed only when
     * one of them turns over (DB-044). */
    (void)strftime(out, max, db->settings.show_seconds
                                 ? "%a %e %b %H:%M:%S %Z"
                                 : "%a %e %b %H:%M %Z", &tmv);
    return 1;
}
