/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_pick_refusal.c - file requests while the file panel asks its own
 * question; drawn windows; built-in applications; opaque windows.
 *
 * The panel asks where Move to puts a folder's selection.  A hosted
 * application's request and a remote application's request are both
 * refused, and the panel keeps the Move to question.  Once the panel has
 * closed, the hosted application's request opens it.  A window from
 * open_drawn() is painted on a view of the desktop's surface, inside its
 * content and its clip, with no pixels kept for it.  Each built-in is
 * offered under "builtin:" and its id with a row in its category's Deskbar
 * group, and that row's path opens it alone.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../system/desktop/tiku_trk_desktop_embed.c"
#include "../../system/deskbar/bar/tiku_trk_deskbar_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

/** @brief One result line; a failure counts. */
static void
check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

/** @brief Main's file is not linked; nothing here has folders to sync. */
void sync_folder_preferences(desktop_t *app) { (void)app; }
static int builtin_opened, builtin_ticks;
static const char *builtin_path = "unset";
/** @brief A built-in's tick: counted. */
static void tick_builtin(int64_t now) { (void)now; builtin_ticks++; }
static tiku_window_t builtin_window;
/** @brief A built-in's open: counted, with the path it was handed. */
static tiku_window_t *
open_builtin(tiku_workspace_t *workspace, tiku_rect_t area, int *dirty,
             const char *path)
{
    (void)workspace; (void)area; (void)dirty;
    builtin_opened++;
    builtin_path = path;
    return &builtin_window;
}
const tiku_trk_builtin_t *
tiku_trk_builtins(int *count)
{
    static const tiku_trk_builtin_t list[] = {
        {"camera", "Camera", "image/jpeg", "image_viewer", "graphics", open_builtin, tick_builtin},
        {"notes", "Notes", NULL, NULL, NULL, open_builtin, NULL},
    };
    *count = (int)(sizeof list / sizeof list[0]);
    return list;
}
/** @brief The built-ins offered to @p app's launcher and Deskbar, and a
 *         Deskbar row's path starting one. */
static void
builtins(desktop_t *app)
{
    tiku_trk_deskbar_t *db = tiku_trk_deskbar_new(NULL);
    tiku_trk_deskbar_t *saved = app->deskbar;
    int before = app->apps.count;

    if (db == NULL) {
        check(0, "a Deskbar for the built-ins");
        return;
    }
    app->deskbar = db;
    desktop_offer_builtins(app);
    tiku_trk_app_t *camera = &app->apps.app[before], *notes = &app->apps.app[before + 1];
    check(app->apps.count == before + 2 && !strcmp(camera->module, "builtin:camera") &&
          !strcmp(notes->module, "builtin:notes"),
          "each built-in is in the launcher under builtin: and its id");
    check(db->app_count == 2 && !strcmp(db->app[0].label, "Camera") &&
          !strcmp(db->app[0].command, "builtin:camera") &&
          !strcmp(db->app[0].icon, "image_viewer") &&
          db->app[0].group == TIKU_TRK_DB_GROUP_GRAPHICS,
          "a built-in's Deskbar row: its name, module and icon, in its category's group");
    check(db->app[1].group == TIKU_TRK_DB_GROUP_APPS && db->app[1].icon[0] == '\0',
          "one without a category or icon goes to Applications with none");
    check(desktop_launch_builtin_path(app, "builtin:camera") == 1 && builtin_opened == 1 &&
          builtin_path == NULL, "its row's path opens it alone");
    {
        desktop_options_t options;
        desktop_loop_t loop;

        memset(&options, 0, sizeof options);
        memset(&loop, 0, sizeof loop);
        builtin_ticks = 0;
        desktop_turn_end(app, &options, &loop);
        check(builtin_ticks == 1, "a desktop turn ticks each built-in");
    }
    {
        tiku_window_t *w = tiku_workspace_open(app->desk, "Built-in",
            (tiku_rect_t){ 40, 60, 200, 120 }, NULL, NULL, NULL);
        tiku_rect_t d;

        app->dirty = 0;
        app->damage = (tiku_rect_t){ 0, 0, 0, 0 };
        tiku_trk_builtin_damage(w, (tiku_rect_t){ 300, 300, 10, 10 });
        tiku_trk_builtin_damage(NULL, (tiku_rect_t){ 40, 60, 10, 10 });
        check(w != NULL && app->dirty == 0,
              "damage outside a built-in's window, or of none, draws nothing");
        tiku_trk_builtin_damage(w, (tiku_rect_t){ 0, 0, 100, 100 });
        d = app->damage;
        check(app->dirty == 2 && d.x == 40 && d.y == 60 && d.w == 60 && d.h == 40,
              "a built-in's damage is its part inside the window");
        tiku_trk_builtin_damage(w, (tiku_rect_t){ 150, 150, 20, 20 });
        d = app->damage;
        check(app->dirty == 2 && d.x == 40 && d.y == 60 && d.w == 130 && d.h == 110,
              "more damage joins it");
        app->dirty = 1;
        tiku_trk_builtin_damage(w, (tiku_rect_t){ 40, 60, 10, 10 });
        check(app->dirty == 1, "and a full frame stays full");
        app->dirty = 0;
        if (w != NULL) {
            tiku_workspace_close(app->desk, w);
        }
    }
    check(desktop_launch_builtin_path(app, "builtin:missing") == 0 &&
          desktop_launch_builtin_path(app, "/apps/editor.so") == 0 &&
          desktop_launch_builtin_path(app, NULL) == 0 && builtin_opened == 1,
          "a path that names no built-in is left to the caller");
    app->deskbar = saved;
}

/* What the drawn window's painter was handed, and the services it got. */
static struct {
    int                        calls, w, h;
    long                       stride;
    const tiku_app_services_t *services;
    uint32_t                   id;
} painted;

#define RED   TIKU_RGB(200, 0, 0)
#define GREEN TIKU_RGB(0, 200, 0)
#define GROUND TIKU_RGB(1, 2, 3)

/** @brief Paint the window red, its first pixel green, and note the
 *         surface. */
static void
paint_red(void *app, uint32_t id, tiku_surface_t *s)
{
    (void)app;
    (void)id;
    painted.calls++;
    painted.w = s->w;
    painted.h = s->h;
    painted.stride = tiku_surface_stride(s);
    tiku_fill(s, (tiku_rect_t){ 0, 0, s->w, s->h }, RED);
    tiku_pixel(s, 0, 0, GREEN);
}

/** @brief Open one window the desktop draws. */
static int
drawn_start(void **state, const tiku_app_services_t *services)
{
    *state = NULL;
    painted.services = services;
    painted.id = services->open_drawn(services->ctx, "Drawn", 120, 80,
                                      paint_red, NULL);
    return painted.id != 0 ? 0 : -1;
}

static const tiku_app_descriptor_t drawn_app = {
    .id = "test.drawn", .name = "Drawn", .start = drawn_start
};

/* Content draws counted per window, for what an opaque window hides. */
static int lower_draws, upper_draws;

static void
draw_lower(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *c)
{
    (void)w; (void)c;
    lower_draws++;
    tiku_fill(s, r, GREEN);
}

static void
draw_upper(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r, void *c)
{
    (void)w; (void)c;
    upper_draws++;
    tiku_fill(s, r, RED);
}

/** @brief Fill the whole desktop with GROUND, its clip reset. */
static void
ground(desktop_t *app)
{
    tiku_clip_reset(app->surf);
    tiku_fill(app->surf, (tiku_rect_t){ 0, 0, app->surf->w, app->surf->h },
              GROUND);
}

/** @brief Whether the panel is up asking Move to for @p fl. */
static int
asks_move_to(desktop_t *app, folder_t *fl)
{
    return tiku_trk_panel_window_handle(&app->file_panel) != NULL &&
           app->file_panel.target == fl &&
           app->file_panel.command == TIKU_TRK_CMD_MOVE_TO;
}

int
main(void)
{
    desktop_t *app = calloc(1, sizeof *app);
    struct desktop_embed slot;
    folder_t *fl;
    tiku_msg_t *msg;

    if (app == NULL) {
        return 2;
    }
    app->surf = tiku_surface_new(DESK_W, DESK_H, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    app->backend = tiku_mount_open();
    (void)tiku_mount_add(app->backend, "/", tiku_backend_posix_open(), 1);
    app->store = tiku_store_memory_open();
    tiku_trk_panel_window_init(&app->file_panel, app->desk, app->backend,
        app->store, NULL, 18, DESK_W, DESK_H, &app->dirty, NULL, NULL);
    memset(&slot, 0, sizeof slot);
    slot.app = app;
    fl = &app->folders[0];
    snprintf(fl->path, sizeof fl->path, "/tmp");
    app->folder_count = 1;

    printf("An application's request while Move to asks (D-056)\n");
    fpanel_open(app, fl, TIKU_TRK_CMD_MOVE_TO);
    check(asks_move_to(app, fl), "the panel asks where Move to goes");
    check(embed_svc_pick(&slot, 7, TIKU_APP_PICK_OPEN, "/tmp", NULL) == 0,
          "a hosted application's request is refused");
    check(asks_move_to(app, fl) && app->pending_pick.kind == 0,
          "and the panel still asks Move to");

    app->remote.session[0].used = 1;
    msg = tiku_msg_new(0);
    if (msg != NULL) {
        (void)tiku_msg_add_int32(msg, "window", 3);
        (void)tiku_msg_add_int32(msg, "mode", TIKU_APP_PICK_OPEN);
        (void)tiku_msg_add_string(msg, "start", "/tmp");
        desktop_remote_pick(app, 0, msg);
        tiku_msg_free(msg);
    }
    check(asks_move_to(app, fl) && app->pending_pick.kind == 0,
          "a remote application's request leaves Move to asking");

    tiku_trk_panel_window_close(&app->file_panel);
    check(embed_svc_pick(&slot, 7, TIKU_APP_PICK_OPEN, "/tmp", NULL) == 1 &&
          app->file_panel.target == &app->pending_pick,
          "with the panel closed, a hosted application's request opens it");
    tiku_trk_panel_window_close(&app->file_panel);

    printf("A window the desktop draws for a hosted application\n");
    check(embed_start(app, &drawn_app), "the application starts with it");
    {
        struct desktop_embed *es = &app->embeds[0];
        struct embed_win *ew = embed_win_of_id(es, painted.id);
        tiku_window_t *w = ew != NULL ? ew->window : NULL;
        tiku_rect_t c;

        check(w != NULL && ew->paint == paint_red, "the window takes its painter");
        if (w == NULL) {
            return 1;
        }
        c = tiku_window_content(w);
        ground(app);
        embed_window_draw(w, app->surf, c, ew);
        check(painted.calls == 1 && painted.w == c.w && painted.h == c.h &&
              painted.stride == tiku_surface_stride(app->surf),
              "the painter gets a view of the content on the desktop's rows");
        check(ew->frame == NULL, "and the desktop keeps no pixels for it");
        check(tiku_peek(app->surf, c.x, c.y) == GREEN &&
              tiku_peek(app->surf, c.x + c.w - 1, c.y + c.h - 1) == RED &&
              tiku_peek(app->surf, c.x - 1, c.y) == GROUND &&
              tiku_peek(app->surf, c.x + c.w, c.y) == GROUND &&
              tiku_peek(app->surf, c.x, c.y + c.h) == GROUND,
              "its pixels land in the content and nowhere else");
        ground(app);
        tiku_clip_set(app->surf, (tiku_rect_t){ c.x + 10, c.y + 10, 20, 20 });
        embed_window_draw(w, app->surf, c, ew);
        check(tiku_peek(app->surf, c.x + 15, c.y + 15) == RED &&
              tiku_peek(app->surf, c.x, c.y) == GROUND &&
              tiku_peek(app->surf, c.x + 40, c.y + 15) == GROUND,
              "a clipped draw paints the clip alone");
        /* After a frame was drawn: nothing to draw, then the window. */
        app->dirty = 0;
        app->damage = (tiku_rect_t){ 0, 0, 0, 0 };
        painted.services->redraw(painted.services->ctx, painted.id);
        check(app->dirty == 2 && app->damage.x <= w->frame.x &&
              app->damage.y <= w->frame.y &&
              app->damage.x + app->damage.w >= w->frame.x + w->frame.w &&
              app->damage.y + app->damage.h >= w->frame.y + w->frame.h,
              "a redraw damages the window's frame alone");
        /* Half off the right edge: painted whole, the visible part shown. */
        w->frame.x = app->surf->w - w->frame.w / 2;
        c = tiku_window_content(w);
        ground(app);
        embed_window_draw(w, app->surf, c, ew);
        check(painted.calls == 3 && painted.w == c.w && painted.h == c.h &&
              tiku_peek(app->surf, c.x, c.y) == GREEN &&
              tiku_peek(app->surf, app->surf->w - 1, c.y + 5) == RED &&
              tiku_peek(app->surf, c.x - 1, c.y + 5) == GROUND,
              "a window partly off the screen is painted whole and shown");
        embed_drop_window(ew, 0);
        check(ew->paint == NULL && ew->window == NULL,
              "a closed window forgets its painter");
    }
    builtins(app);

    printf("A window that paints all of its content\n");
    {
        tiku_window_t *lower = tiku_workspace_open(app->desk, "Lower",
            (tiku_rect_t){ 20, 40, 300, 200 }, draw_lower, NULL, NULL);
        tiku_window_t *upper = tiku_workspace_open(app->desk, "Upper",
            (tiku_rect_t){ 40, 60, 200, 150 }, draw_upper, NULL, NULL);
        tiku_rect_t c = upper != NULL ? tiku_window_content(upper)
                                      : (tiku_rect_t){ 0, 0, 0, 0 };
        tiku_rect_t inside = { c.x + 10, c.y + 10, 50, 40 };

        tiku_workspace_cache_mode(app->desk, TIKU_CACHE_OFF);
        ground(app);
        tiku_clip_set(app->surf, inside);
        lower_draws = upper_draws = 0;
        tiku_workspace_draw(app->desk);
        check(lower != NULL && upper != NULL && lower_draws == 1 && upper_draws == 1,
              "a clip inside a window draws what lies under it");
        tiku_window_set_opaque(upper, 1);
        ground(app);
        tiku_clip_set(app->surf, inside);
        lower_draws = upper_draws = 0;
        tiku_workspace_draw(app->desk);
        check(lower_draws == 0 && upper_draws == 1 &&
              tiku_peek(app->surf, inside.x, inside.y) == RED,
              "unless the window is opaque: then it alone draws");
        tiku_clip_set(app->surf, (tiku_rect_t){ c.x - 5, c.y + 10, 50, 40 });
        lower_draws = upper_draws = 0;
        tiku_workspace_draw(app->desk);
        check(lower_draws == 1 && upper_draws == 1,
              "a clip reaching past its content draws what lies under it");
        tiku_workspace_set_now(app->desk, 1000);
        upper->opened_at = 1000;
        tiku_clip_set(app->surf, inside);
        lower_draws = upper_draws = 0;
        tiku_workspace_draw(app->desk);
        check(lower_draws == 1, "as does a window still rising into place");
        tiku_workspace_set_now(app->desk, 0);
        tiku_clip_reset(app->surf);
        tiku_workspace_close(app->desk, upper);
        tiku_workspace_close(app->desk, lower);
    }

    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
