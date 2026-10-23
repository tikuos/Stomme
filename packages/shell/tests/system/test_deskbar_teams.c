#include "tiku_shell_product.h"
/* The Deskbar's applications, a row each: every window listed under the
 * application whose session opened it and this shell's own under
 * TikuTracker; the manifest's icon worn; a second instance sharing its
 * row; titles less the application's name; a row gone with its last
 * window; and what a click on a row reaches -- that application's windows,
 * no one else's.  An application's own tray icons: set, drawn beside the
 * clock (below the collapsed dock's button too), clicked, said on hover,
 * taken away, and gone with the application.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "../../system/desktop/tiku_trk_desktop_teams.c"
#include "../../system/desktop/tiku_trk_desktop_deskbar.c"
#include "../../system/deskbar/bar/tiku_trk_deskbar_private.h"
#include "tiku_app.h"

static int closes, forcequits;

/** @brief A full frame is the flag, as the other system tests keep it. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

/** @brief The shell's window closer, standing in: counted, then gone. */
void close_window(desktop_t *app, tiku_window_t *w)
{
    closes++;
    tiku_workspace_close(app->desk, w);
}

/** @brief The folder windows' lookup, standing in: no window here is a
 *         folder's. */
tiku_trk_folder_window_t *
tiku_trk_folder_lifecycle_find(tiku_trk_folder_lifecycle_t *controller,
                               tiku_window_t *window)
{
    (void)controller;
    (void)window;
    return NULL;
}

/** @brief The Force Quit panel, standing in: counted. */
void desktop_forcequit_open(desktop_t *app)
{
    (void)app;
    forcequits++;
}

/** @brief A window called @p title opened on the desk. */
static tiku_window_t *open_window(desktop_t *app, const char *title, int x)
{
    tiku_window_t *w = tiku_workspace_open(app->desk, title,
        (tiku_rect_t){x, 60, 200, 150}, NULL, NULL, NULL);

    assert(w);
    w->tag = w;                     /* the window, as its own identity */
    return w;
}

/** @brief Window @p w held by session @p i, an application called @p name. */
static void hold(desktop_t *app, int i, const char *name, tiku_window_t *w)
{
    tiku_remote_session_t *s = &app->remote.session[i];
    int j = 0;

    while (s->win[j].window != NULL) j++;
    s->used = 1;
    snprintf(s->name, sizeof s->name, "%s", name);
    s->win[j].window = w;
    s->win[j].opened = 1;
}

/** @brief The Deskbar entry called @p name: its windows, which must be
 *         @p n, and whether it is the active one. */
static const tiku_trk_dbwin_t *team(desktop_t *app, const char *name, int n,
                                    int *active)
{
    const tiku_trk_dbwin_t *wins = NULL;
    int t = tiku_trk_deskbar_find_team(app->deskbar, name);

    assert(t >= 0);
    assert(tiku_trk_deskbar_team_windows(app->deskbar, t, &wins, active) ==
           n);
    return wins;
}

/** @brief A desk with this shell's folder, two instances of TikuDiff and
 *         one of TikuPDF; only TikuDiff's manifest names an icon. */
static desktop_t *setup(tiku_window_t **diff1, tiku_window_t **diff2,
                        tiku_window_t **pdf)
{
    desktop_t *app = calloc(1, sizeof *app);

    assert(app);
    app->surf = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    app->deskbar = tiku_trk_deskbar_new(NULL);
    app->team = tiku_trk_deskbar_add_team(app->deskbar, TIKU_SHELL_TRACKER_NAME);
    snprintf(app->apps.app[0].name, sizeof app->apps.app[0].name, "TikuDiff");
    snprintf(app->apps.app[0].icon, sizeof app->apps.app[0].icon,
             "diff_viewer");
    snprintf(app->apps.app[1].name, sizeof app->apps.app[1].name, "TikuPDF");
    app->apps.count = 2;
    (void)open_window(app, "trk_script", 10);
    *diff1 = open_window(app, "TikuDiff - one.c : two.c", 100);
    *pdf = open_window(app, "TikuPDF - guide.pdf", 200);
    *diff2 = open_window(app, "TikuDiff", 300);
    hold(app, 0, "TikuDiff", *diff1);
    hold(app, 1, "TikuPDF", *pdf);
    hold(app, 2, "TikuDiff", *diff2);
    return app;
}

/** @brief Each window under its application's row, each row its icon;
 *         the row with the focused window active. */
static void rows_test(desktop_t *app)
{
    const tiku_trk_dbwin_t *w;
    int active = 0;

    desktop_publish_teams(app);
    assert(tiku_trk_deskbar_team_count(app->deskbar) == 3);
    w = team(app, TIKU_SHELL_TRACKER_NAME, 1, &active);
    assert(!strcmp(w[0].title, "trk_script") && !active);
    w = team(app, "TikuDiff", 2, &active);
    assert(!strcmp(w[0].title, "TikuDiff") && !strcmp(w[1].title,
           "one.c : two.c") && active);         /* diff2 is in front */
    w = team(app, "TikuPDF", 1, NULL);
    assert(!strcmp(w[0].title, "guide.pdf"));
    assert(!strcmp(tiku_trk_deskbar_team_icon(app->deskbar,
           tiku_trk_deskbar_find_team(app->deskbar, "TikuDiff")),
           "diff_viewer"));
    assert(!strcmp(tiku_trk_deskbar_team_icon(app->deskbar,
           tiku_trk_deskbar_find_team(app->deskbar, "TikuPDF")),
           "application"));
    assert(!strcmp(tiku_trk_deskbar_team_icon(app->deskbar, app->team),
           "folder"));
}

/** @brief What a click on a row reaches: that application's windows. */
static void action_test(desktop_t *app, tiku_window_t *pdf)
{
    tiku_trk_db_pick_t pick;
    int diff = tiku_trk_deskbar_find_team(app->deskbar, "TikuDiff");

    memset(&pick, 0, sizeof pick);
    pick.what = TIKU_TRK_DB_HIDE_ALL;
    pick.team = diff;
    assert(deskbar_pick_team(app, &pick, 0, 0));
    for (int i = 0; i < tiku_workspace_count(app->desk); i++) {
        tiku_window_t *w = tiku_workspace_at(app->desk, i);
        assert(w->minimized == desktop_team_has(app, diff, w));
    }
    pick.what = TIKU_TRK_DB_ACTIVATE_TEAM;
    assert(deskbar_pick_team(app, &pick, 0, 0));
    assert(!tiku_workspace_at(app->desk, 1)->minimized &&
           desktop_team_has(app, diff, tiku_workspace_focused(app->desk)));
    pick.what = TIKU_TRK_DB_QUIT_TEAM;
    pick.team = tiku_trk_deskbar_find_team(app->deskbar, "TikuPDF");
    assert(deskbar_pick_team(app, &pick, 0, 0));
    assert(closes == 1 && forcequits == 0 &&
           tiku_workspace_count(app->desk) == 3);
    app->remote.session[1].win[0].window = NULL;   /* the session let go */
    (void)pdf;
    tiku_trk_deskbar_set_windows(app->deskbar, pick.team, NULL, 0, 0);
    assert(deskbar_pick_team(app, &pick, 0, 0) && forcequits == 1);
    pick.team = app->team;          /* this shell's: no polite quit */
    assert(deskbar_pick_team(app, &pick, 0, 0) && forcequits == 2);
}

/** @brief A row goes with its application's last window; this shell's
 *         row stays with none. */
static void gone_test(desktop_t *app)
{
    tiku_trk_db_pick_t pick;

    desktop_publish_teams(app);
    assert(tiku_trk_deskbar_find_team(app->deskbar, "TikuPDF") < 0 &&
           tiku_trk_deskbar_team_count(app->deskbar) == 2);
    memset(&pick, 0, sizeof pick);
    pick.what = TIKU_TRK_DB_CLOSE_ALL;
    pick.team = tiku_trk_deskbar_find_team(app->deskbar, "TikuDiff");
    assert(deskbar_pick_team(app, &pick, 0, 0) && closes == 3);
    memset(app->remote.session, 0, sizeof app->remote.session);
    desktop_publish_teams(app);
    assert(tiku_trk_deskbar_team_count(app->deskbar) == 1 &&
           app->team == tiku_trk_deskbar_find_team(app->deskbar,
                                                   TIKU_SHELL_TRACKER_NAME));
    (void)team(app, TIKU_SHELL_TRACKER_NAME, 1, NULL);
    pick.what = TIKU_TRK_DB_CLOSE_ALL;
    pick.team = app->team;
    assert(deskbar_pick_team(app, &pick, 0, 0) && closes == 3 &&
           tiku_workspace_count(app->desk) == 0);
    desktop_publish_teams(app);
    (void)team(app, TIKU_SHELL_TRACKER_NAME, 0, NULL);
}

/** @brief Session 0's TIKU_MSG_TRAY: item "delay" with @p icon and @p text. */
static void tray_say(desktop_t *app, const char *icon, const char *text)
{
    tiku_msg_t *m = tiku_app_tray_message("delay", icon, text,
                                          "Grabbing in 5: click to cancel");

    assert(m);
    desktop_tray(app, 0, m);
    tiku_msg_free(m);
}

/** @brief An application's own tray icon: set once and not again for the
 *         same, drawn inside the bar above its clock, clicked, said on
 *         hover, taken away, and swept away with its session. */
static void tray_test(desktop_t *app)
{
    tiku_trk_db_pick_t pick;
    tiku_trk_trayicon_t t;
    tiku_rect_t bar, r;
    int before;

    app->remote.session[0].used = 1;
    app->remote.session[0].generation = 7;
    tiku_trk_deskbar_draw(app->deskbar, app->surf);
    before = tiku_trk_deskbar_frame(app->deskbar).h;
    app->dirty = 0;
    tray_say(app, "grab_timer", "5");
    assert(app->dirty && tiku_trk_deskbar_trayicons(app->deskbar, 0, &t) ==
           1 && t.owner == 7 && !strcmp(t.text, "5"));
    app->dirty = 0;
    tray_say(app, "grab_timer", "5");
    assert(!app->dirty);                /* the same said twice: nothing */
    tiku_trk_deskbar_draw(app->deskbar, app->surf);
    bar = tiku_trk_deskbar_frame(app->deskbar);
    r = tiku_trk_deskbar_trayicon_rect(app->deskbar, 0);
    assert(bar.h == before + ICON_ROW && r.w > 16 && r.x >= bar.x &&
           r.y > bar.y && r.y + r.h < bar.y + bar.h - db_clock_row());
    memset(&pick, 0, sizeof pick);
    assert(tiku_trk_deskbar_click(app->deskbar, r.x + 4, r.y + 8, 0, 1, 1,
                                  &pick));
    assert(pick.what == TIKU_TRK_DB_TRAY_ICON && pick.owner == 7 &&
           !strcmp(pick.path, "delay") &&
           desktop_tray_session(app, pick.owner) == 0 &&
           desktop_tray_session(app, 8) < 0);
    (void)tiku_trk_deskbar_motion(app->deskbar, r.x + 4, r.y + 8);
    assert(!strcmp(tiku_trk_deskbar_tip(app->deskbar),
                   "Grabbing in 5: click to cancel"));
    tray_say(app, "", "");
    assert(!tiku_trk_deskbar_trayicons(app->deskbar, 0, NULL));
    tray_say(app, "grab_timer", "3");
    app->remote.session[0].used = 0;    /* the application has gone */
    desktop_publish_teams(app);
    assert(!tiku_trk_deskbar_trayicons(app->deskbar, 0, NULL));
}

/** @brief Collapsed, the tray sits under the dock's application button
 *         and the clock under the switch, at the bar's foot. */
static void mini_test(desktop_t *app)
{
    tiku_rect_t bar, r;

    app->remote.session[0].used = 1;
    tray_say(app, "grab_timer", "3");
    tiku_trk_deskbar_set_dock(app->deskbar, TIKU_TRK_DB_RIGHT_MINI);
    tiku_trk_deskbar_draw(app->deskbar, app->surf);
    bar = tiku_trk_deskbar_frame(app->deskbar);
    r = tiku_trk_deskbar_trayicon_rect(app->deskbar, 0);
    assert(r.y >= bar.y + db_leaf_row() + 2 * PAD + db_team_row() + 2 &&
           db_tray_top(app->deskbar) + db_tray_h(app->deskbar) +
           SWITCH_ROW + db_clock_row() + PAD == bar.y + bar.h);
    tiku_trk_deskbar_set_dock(app->deskbar, TIKU_TRK_DB_RIGHT_EXPANDED);
}

/** @brief Every group, in order, on one desk. */
int main(void)
{
    tiku_window_t *diff1, *diff2, *pdf;
    desktop_t *app = setup(&diff1, &diff2, &pdf);
    char out[64];

    rows_test(app);
    action_test(app, pdf);
    gone_test(app);
    tray_test(app);
    mini_test(app);
    tiku_trk_deskbar_strip_title("TikuPDF", "TikuPDF - ", out, sizeof out);
    assert(!strcmp(out, "TikuPDF - "));     /* nothing after: kept whole */
    tiku_trk_deskbar_free(app->deskbar);
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    puts("PASS: Deskbar rows per application -- windows sorted, icons "
         "worn, instances shared, rows gone, clicks reach one application; "
         "tray icons set, drawn, clicked, tipped, taken away, swept");
    return 0;
}
