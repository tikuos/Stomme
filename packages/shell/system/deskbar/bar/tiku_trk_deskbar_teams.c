/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_teams.c - the running applications ("teams"): their
 * order, their instances, their icons and their windows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_deskbar_private.h"

/**
 * @brief Whether team @p a sorts before @p b: Tracker first when asked,
 *        then by name when asked, else in arrival order.
 */
static int
team_before(const tiku_trk_deskbar_t *db, int a, int b)
{
    if (db->settings.tracker_first) {
        int at = strcmp(db->teams[a].name, TIKU_SHELL_TRACKER_NAME) == 0;
        int bt = strcmp(db->teams[b].name, TIKU_SHELL_TRACKER_NAME) == 0;

        if (at != bt) { return at; }
    }
    if (db->settings.sort_apps) {
        return strcmp(db->teams[a].name, db->teams[b].name) < 0;
    }
    return a < b;
}

/** @brief Put the team order under team_before(), by insertion sort. */
static void
sort_teams(tiku_trk_deskbar_t *db)
{
    int i, j;

    for (i = 1; i < db->team_count; i++) {
        int key = db->order[i];

        for (j = i - 1; j >= 0 && team_before(db, key, db->order[j]); j--) {
            db->order[j + 1] = db->order[j];
        }
        db->order[j + 1] = key;
    }
}

void
tiku_trk_deskbar_apply_settings(tiku_trk_deskbar_t *db,
                                const tiku_trk_deskbar_settings_t *settings)
{
    if (db == NULL || settings == NULL) { return; }
    db->settings = *settings;
    tiku_trk_deskbar_settings_normalize(&db->settings);
    db->autohide = db->settings.auto_hide;
    db->autoraise = db->settings.auto_raise;
    if (!db->settings.show_expander) {
        /* No expander: every row collapses, since nothing could close a
         * row left open (DB-023). */
        int i;

        for (i = 0; i < db->team_count; i++) {
            db->teams[i].expanded = 0;
        }
    }
    /* The whole interface reads its metrics from these faces, so the
     * next paint uses the new size and family everywhere. */
    (void)tiku_font_set_family(db->settings.font_family);
    (void)tiku_font_set_size(db->settings.font_size);
    sort_teams(db);
}

tiku_trk_db_dock_t
tiku_trk_deskbar_dock(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->dock : TIKU_TRK_DB_RIGHT_EXPANDED;
}

int
tiku_trk_deskbar_team_count(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->team_count : 0;
}

const char *
tiku_trk_deskbar_team_name(const tiku_trk_deskbar_t *db, int i)
{
    if (db == NULL || i < 0 || i >= db->team_count) {
        return NULL;
    }
    return db->teams[db->order[i]].name;
}

int
tiku_trk_deskbar_add_team(tiku_trk_deskbar_t *db, const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    /* One entry per application however many instances it has, matched
     * without regard to case (DB-015). */
    for (i = 0; i < db->team_count; i++) {
        if (strcasecmp(db->teams[i].name, name) == 0) {
            return i;
        }
    }
    if (db->team_count >= TIKU_TRK_DB_TEAMS_MAX) {
        return -1;
    }
    i = db->team_count++;
    db->order[i] = i;
    memset(&db->teams[i], 0, sizeof db->teams[i]);
    snprintf(db->teams[i].name, sizeof db->teams[i].name, "%s", name);
    /* Tracker's entry takes files of any type. */
    db->teams[i].takes_anything = (strcmp(name, TIKU_SHELL_TRACKER_NAME) == 0);
    /* With no expander a row opened here could not be folded (DB-023). */
    db->teams[i].expanded = db->settings.show_expander &&
                            db->settings.expand_new_apps;
    /* Added without a pid: the shell's own seed, never pruned away. */
    db->teams[i].shell = 1;
    sort_teams(db);
    return i;
}

int
tiku_trk_deskbar_add_team_pid(tiku_trk_deskbar_t *db, const char *name,
                              long pid, const char *command, int *changed)
{
    int i, k, was = -1;

    if (db == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < db->team_count; i++) {
        if (strcasecmp(db->teams[i].name, name) == 0) {
            was = i;
            break;
        }
    }
    if (was < 0) {
        was = tiku_trk_deskbar_add_team(db, name);
        if (was < 0) {
            return -1;
        }
        db->teams[was].shell = 0;
        if (changed != NULL) { *changed = 1; }
    }
    for (k = 0; k < db->teams[was].pid_count; k++) {
        if (db->teams[was].pids[k] == pid) {
            return was;                    /* already counted */
        }
    }
    if (db->teams[was].pid_count <
        (int)(sizeof db->teams[was].pids / sizeof db->teams[was].pids[0])) {
        db->teams[was].pids[db->teams[was].pid_count++] = pid;
        if (changed != NULL) { *changed = 1; }
    }
    if (db->teams[was].command[0] == '\0' && command != NULL) {
        snprintf(db->teams[was].command, sizeof db->teams[was].command,
                 "%s", command);
    }
    return was;
}

void
tiku_trk_deskbar_remove_team(tiku_trk_deskbar_t *db, int team)
{
    int i;

    if (db == NULL || team < 0 || team >= db->team_count) {
        return;
    }
    for (i = team; i < db->team_count - 1; i++) {
        db->teams[i] = db->teams[i + 1];
    }
    db->team_count--;
    /* The display order indexes rows that have moved, so it is rebuilt
     * and sorted again. */
    for (i = 0; i < db->team_count; i++) {
        db->order[i] = i;
    }
    db->win_menu_team = -1;
    sort_teams(db);
}

int
tiku_trk_deskbar_find_team(const tiku_trk_deskbar_t *db, const char *name)
{
    int i;

    if (db == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < db->team_count; i++) {
        if (strcasecmp(db->teams[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

const char *
tiku_trk_deskbar_team_app(const tiku_trk_deskbar_t *db, int team)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return NULL;
    }
    return db->teams[team].name;
}

int
tiku_trk_deskbar_team_windows(const tiku_trk_deskbar_t *db, int team,
                              const tiku_trk_dbwin_t **wins, int *active)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return 0;
    }
    if (wins != NULL) {
        *wins = db->teams[team].wins;
    }
    if (active != NULL) {
        *active = db->teams[team].active;
    }
    return db->teams[team].win_count;
}

void
tiku_trk_deskbar_set_team_icon(tiku_trk_deskbar_t *db, int team,
                               const char *icon)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return;
    }
    snprintf(db->teams[team].icon, sizeof db->teams[team].icon, "%s",
             icon != NULL ? icon : "");
}

const char *
tiku_trk_deskbar_team_icon(const tiku_trk_deskbar_t *db, int team)
{
    const tiku_trk_team_t *t;

    if (db == NULL || team < 0 || team >= db->team_count) {
        return "application";
    }
    t = &db->teams[team];
    if (t->icon[0] != '\0') {
        return t->icon;
    }
    /* Tracker's entry is drawn with the folder icon, and any other entry
     * without an icon of its own with the generic application icon. */
    return strcmp(t->name, TIKU_SHELL_TRACKER_NAME) == 0
           ? "folder" : "application";
}

int
tiku_trk_deskbar_prune_teams(tiku_trk_deskbar_t *db, const long *live, int n)
{
    int i, changed = 0;

    if (db == NULL) {
        return 0;
    }
    for (i = db->team_count - 1; i >= 0; i--) {
        tiku_trk_team_t *t = &db->teams[i];
        int k, kept = 0;

        if (t->shell) {
            continue;
        }
        for (k = 0; k < t->pid_count; k++) {
            int j, alive = 0;

            for (j = 0; j < n; j++) {
                if (live != NULL && live[j] == t->pids[k]) {
                    alive = 1;
                    break;
                }
            }
            if (alive) {
                t->pids[kept++] = t->pids[k];
            } else {
                changed = 1;
            }
        }
        t->pid_count = kept;
        if (kept == 0) {
            /* The last instance went, and the entry goes with it
             * (DB-015). */
            tiku_trk_deskbar_remove_team(db, i);
            changed = 1;
        }
    }
    return changed;
}

int
tiku_trk_deskbar_apps_menu_open(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->apps_menu_open : 0;
}

int
tiku_trk_deskbar_hbutton_width(const tiku_trk_deskbar_t *db)
{
    return (db != NULL) ? db->hbutton_w : 0;
}

int
tiku_trk_deskbar_team_pids(const tiku_trk_deskbar_t *db, int team, long *out,
                           int max)
{
    int k, n = 0;

    if (db == NULL || team < 0 || team >= db->team_count || out == NULL) {
        return 0;
    }
    for (k = 0; k < db->teams[team].pid_count && n < max; k++) {
        out[n++] = db->teams[team].pids[k];
    }
    return n;
}

const char *
tiku_trk_deskbar_team_command(const tiku_trk_deskbar_t *db, int team)
{
    if (db == NULL || team < 0 || team >= db->team_count) {
        return "";
    }
    return db->teams[team].command;
}

void
tiku_trk_deskbar_strip_title(const char *app, const char *title, char *out,
                             size_t max)
{
    size_t al, tl;
    const char *p = title;

    if (app == NULL || title == NULL || out == NULL) {
        return;
    }
    al = strlen(app);
    /* "<app>: rest" -> "rest", and "<app> - rest", the way the desk's own
     * applications title a window, -> "rest" */
    if (strncmp(p, app, al) == 0 && p[al] == ':') {
        p += al + 1;
        while (*p == ' ') { p++; }
    } else if (strncmp(p, app, al) == 0 && strncmp(p + al, " - ", 3) == 0 &&
               p[al + 3] != '\0') {
        p += al + 3;
    }
    snprintf(out, max, "%s", p);
    /* "rest - <app>" -> "rest" */
    tl = strlen(out);
    if (tl > al + 3 && strcmp(out + tl - al, app) == 0 &&
        out[tl - al - 1] == ' ' && out[tl - al - 2] == '-') {
        out[tl - al - 3] = '\0';
    }
}

/** @brief Natural order, so "Doc 2" sorts before "Doc 10" (DB-031). */
static int
natural_less(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            long x = 0, y = 0;
            while (*a >= '0' && *a <= '9') { x = x * 10 + (*a++ - '0'); }
            while (*b >= '0' && *b <= '9') { y = y * 10 + (*b++ - '0'); }
            if (x != y) { return (x < y); }
            continue;
        }
        if (*a != *b) { return ((unsigned char)*a < (unsigned char)*b); }
        a++;
        b++;
    }
    return (*a == '\0' && *b != '\0');
}

void
tiku_trk_deskbar_set_windows(tiku_trk_deskbar_t *db, int team,
                             const tiku_trk_dbwin_t *wins, int n, int active)
{
    tiku_trk_team_t *t;
    int i, j;

    if (db == NULL || team < 0 || team >= db->team_count) {
        return;
    }
    t = &db->teams[team];
    if (n > TIKU_TRK_DB_WINS_MAX) {
        n = TIKU_TRK_DB_WINS_MAX;
    }
    for (i = 0; i < n; i++) {
        t->wins[i] = wins[i];
    }
    t->win_count = n;
    t->active = active;
    /* Kept in natural title order, so the list does not reshuffle as
     * windows are raised. */
    for (i = 1; i < n; i++) {
        tiku_trk_dbwin_t key = t->wins[i];
        for (j = i - 1; j >= 0 && natural_less(key.title, t->wins[j].title);
             j--) {
            t->wins[j + 1] = t->wins[j];
        }
        t->wins[j + 1] = key;
    }
}
