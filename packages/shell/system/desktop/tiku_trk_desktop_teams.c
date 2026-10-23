/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_teams.c - the Deskbar's applications, a row each, and
 * their tray icons.
 *
 * Every window on the desk is the desktop's own or one an application's
 * session opened.  Each such application gets a row, while the Deskbar has
 * room for one, with the icon its manifest names and its windows under it;
 * the windows of one with no row are listed under TikuTracker's.  Two
 * instances of one application share the row, which goes with the last of
 * their windows.  A click on a row reaches that application only.  A tray
 * icon is set by its session, goes with it, and a click on it is sent to
 * it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trk_desktop_private.h"
#include <string.h>
#include <strings.h>

#define TRACKER_TEAM TIKU_SHELL_TRACKER_NAME

/** @brief The windows one row will be given, gathered in one pass. */
typedef struct {
    char             name[TIKU_TRK_DB_LABEL_MAX];
    tiku_trk_dbwin_t win[TIKU_TRK_DB_WINS_MAX];
    int              count;
} team_list_t;

/**
 * @brief The application window @p w belongs to, by the name its session
 *        gave: the row it is listed under.  A window no session holds is
 *        the desktop's own (folders, panels, the About box): TikuTracker.
 */
const char *
desktop_window_team(desktop_t *app, tiku_window_t *w)
{
    tiku_remote_session_t *s = tiku_remote_owner(&app->remote, w);

    return (s != NULL && s->name[0] != '\0') ? s->name : TRACKER_TEAM;
}

/** @brief The icon the installed application called @p name asks for in
 *         its manifest, or NULL when there is none to be had. */
static const char *
team_icon(desktop_t *app, const char *name)
{
    for (int i = 0; i < app->apps.count; i++) {
        const tiku_trk_app_t *a = &app->apps.app[i];

        if (strcasecmp(a->name, name) == 0 && a->icon[0] != '\0') {
            return a->icon;
        }
    }
    return NULL;
}

/**
 * @brief The list for @p name among the @p n gathered so far, added when
 *        it is new and there is room.
 * @return its index, or -1.
 */
static int
team_slot(team_list_t *lists, int *n, const char *name)
{
    for (int i = 0; i < *n; i++) {
        if (strcasecmp(lists[i].name, name) == 0) {
            return i;
        }
    }
    if (*n >= TIKU_TRK_DB_TEAMS_MAX) {
        return -1;
    }
    snprintf(lists[*n].name, sizeof lists[*n].name, "%s", name);
    lists[*n].count = 0;
    return (*n)++;
}

/** @brief Whether @p name is among the @p n lists gathered this pass. */
static int
team_listed(const team_list_t *lists, int n, const char *name)
{
    for (int i = 0; i < n; i++) {
        if (strcasecmp(lists[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Make the Deskbar's application rows match the lists: a row whose
 *        application has no window left goes, and a row is added, with its
 *        manifest's icon, for one that has just opened its first.
 */
static void
team_sync(desktop_t *app, const team_list_t *lists, int n)
{
    for (int i = app->napp_teams - 1; i >= 0; i--) {
        if (team_listed(lists, n, app->app_teams[i])) {
            continue;
        }
        tiku_trk_deskbar_remove_team(app->deskbar,
            tiku_trk_deskbar_find_team(app->deskbar, app->app_teams[i]));
        memmove(app->app_teams[i], app->app_teams[i + 1],
                (size_t)(app->napp_teams - i - 1) * sizeof app->app_teams[0]);
        app->napp_teams--;
    }
    for (int i = 0; i < n; i++) {
        int k, team;

        if (strcmp(lists[i].name, TRACKER_TEAM) == 0) {
            continue;
        }
        for (k = 0; k < app->napp_teams &&
                    strcasecmp(app->app_teams[k], lists[i].name); k++) {}
        if (k < app->napp_teams || app->napp_teams >= TIKU_TRK_DB_TEAMS_MAX) {
            continue;
        }
        team = tiku_trk_deskbar_add_team(app->deskbar, lists[i].name);
        if (team < 0) {
            continue;
        }
        tiku_trk_deskbar_set_team_icon(app->deskbar, team,
                                       team_icon(app, lists[i].name));
        snprintf(app->app_teams[app->napp_teams++], sizeof app->app_teams[0],
                 "%.47s", lists[i].name);
    }
    /* Rows may have moved: find the desktop's own row again by name. */
    app->team = tiku_trk_deskbar_find_team(app->deskbar, TRACKER_TEAM);
}

/** @brief Remove the tray icons of sessions that have ended. */
static void
tray_sweep(desktop_t *app)
{
    uint64_t live[TIKU_REMOTE_SESSIONS];
    int n = 0;

    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (app->remote.session[i].used) {
            live[n++] = app->remote.session[i].generation;
        }
    }
    if (tiku_trk_deskbar_trayicon_keep(app->deskbar, live, n)) {
        desktop_damage_all(app);
    }
}

/**
 * @brief Publish each window on the desk under its application's row, its
 *        title less the application's name; the row holding the focused
 *        window is the active one.
 * @note Called on every frame.
 */
void
desktop_publish_teams(desktop_t *app)
{
    static team_list_t lists[TIKU_TRK_DB_TEAMS_MAX];
    tiku_window_t *focused = tiku_workspace_focused(app->desk);
    int n = 0, active = -1, count = tiku_workspace_count(app->desk);

    (void)team_slot(lists, &n, TRACKER_TEAM);   /* always there, first */
    for (int i = 0; i < count; i++) {
        tiku_window_t *w = tiku_workspace_at(app->desk, i);
        int slot = w != NULL && w->open
                   ? team_slot(lists, &n, desktop_window_team(app, w)) : -1;
        tiku_trk_dbwin_t *d;

        if (slot < 0 || lists[slot].count >= TIKU_TRK_DB_WINS_MAX) {
            continue;
        }
        d = &lists[slot].win[lists[slot].count++];
        memset(d, 0, sizeof *d);
        tiku_trk_deskbar_strip_title(lists[slot].name, w->title, d->title,
                                     sizeof d->title);
        d->state = w->minimized ? TIKU_TRK_DBW_MINIMIZED : TIKU_TRK_DBW_SHOWN;
        d->current = w == focused;
        d->tag = w->tag;
        if (w == focused) {
            active = slot;
        }
    }
    team_sync(app, lists, n);
    tray_sweep(app);
    for (int i = n - 1; i > 0; i--) {   /* no row: under TikuTracker's */
        if (tiku_trk_deskbar_find_team(app->deskbar, lists[i].name) >= 0) {
            continue;
        }
        for (int k = 0; k < lists[i].count &&
                        lists[0].count < TIKU_TRK_DB_WINS_MAX; k++) {
            lists[0].win[lists[0].count++] = lists[i].win[k];
        }
        active = active == i ? 0 : active;
        lists[i].count = 0;
    }
    for (int i = 0; i < n; i++) {
        tiku_trk_deskbar_set_windows(app->deskbar,
            tiku_trk_deskbar_find_team(app->deskbar, lists[i].name),
            lists[i].win, lists[i].count, i == active);
    }
}

/** @brief Whether window @p w is listed under Deskbar entry @p team. */
int
desktop_team_has(desktop_t *app, int team, tiku_window_t *w)
{
    const char *name = tiku_trk_deskbar_team_app(app->deskbar, team);

    return w != NULL && name != NULL &&
           strcasecmp(desktop_window_team(app, w), name) == 0;
}

/** @brief Whether Deskbar entry @p team is an application's row, not the
 *         desktop's own. */
int
desktop_team_is_app(desktop_t *app, int team)
{
    const char *name = tiku_trk_deskbar_team_app(app->deskbar, team);

    return name != NULL && strcmp(name, TRACKER_TEAM) != 0;
}

/** @brief Session @p session's TIKU_MSG_TRAY: its tray item set, or taken
 *         away when it names no icon and no text. */
void
desktop_tray(desktop_t *app, int session, const tiku_msg_t *msg)
{
    const tiku_remote_session_t *s;

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used) {
        return;
    }
    s = &app->remote.session[session];
    if (tiku_trk_deskbar_trayicon_set(app->deskbar, s->generation,
            tiku_msg_find_string(msg, "id", 0),
            tiku_msg_find_string(msg, "icon", 0),
            tiku_msg_find_string(msg, "text", 0),
            tiku_msg_find_string(msg, "tip", 0)) > 0) {
        desktop_damage_all(app);
    }
}

/**
 * @brief The live session whose generation is @p owner: the session that
 *        set a tray item, while it lasts.
 * @return its index, or -1.
 */
int
desktop_tray_session(desktop_t *app, uint64_t owner)
{
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (app->remote.session[i].used &&
            app->remote.session[i].generation == owner) {
            return i;
        }
    }
    return -1;
}

/** @brief A click on tray item @p id told to @p owner, the session that set
 *         it, as TIKU_MSG_TRAYHIT; nothing when that session has gone. */
void
desktop_tray_hit(desktop_t *app, uint64_t owner, const char *id)
{
    int i = desktop_tray_session(app, owner);
    tiku_msg_t *m = i >= 0 ? tiku_msg_new(TIKU_MSG_TRAYHIT) : NULL;

    if (m != NULL && tiku_msg_add_string(m, "id", id)) {
        (void)tiku_remote_tell(&app->remote, i, m);
    }
    tiku_msg_free(m);
}
