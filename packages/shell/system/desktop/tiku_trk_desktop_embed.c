/*
 * Desktop shell and file manager.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_embed.c - hosting an application in the desktop's own
 * process: the services it is given (windows, menus, placement, the file
 * panel, the clipboard, boards and help) and the windows held for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_shell_product.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"

/**
 * @brief The 75 ms periodic task: pump the board run, then tick every
 *        running hosted application.
 * @return 0, so the task stays scheduled.
 */
int
tick_embeds(void *ctx)
{
    desktop_t *app = ctx;
    int e;
#if TIKU_SHELL_BOARD_SERVICES


    /* The desktop owns the board run; the asking application is sent
     * what it prints. */
    desktop_board_pump(app);
#endif


    for (e = 0; e < TIKU_TRK_EMBED_MAX; e++) {
        if (app->embeds[e].running &&
            app->embeds[e].descriptor->tick != NULL) {
            app->embeds[e].descriptor->tick(app->embeds[e].state,
                                            tiku_trk_now());
        }
    }
    return 0;
}

/** @brief Draw a hosted application's window: its latest frame, on the
 *         panel colour. */
static void
embed_window_draw(tiku_window_t *window, tiku_surface_t *surface,
                  tiku_rect_t content, void *context)
{
    struct embed_win *ew = context;

    (void)window;
    tiku_fill(surface, content, TIKU_C_PANEL);
    if (ew == NULL || ew->frame == NULL) {
        return;
    }
    tiku_surface_t frame = {.px = ew->frame, .w = ew->fw,
                            .h = ew->fh, .scale = 1};
    tiku_blit(surface, content.x, content.y, &frame);
}

/** @brief The window @p w of a hosted application, or NULL. */
struct embed_win *
embed_win_of_window(desktop_t *app, const tiku_window_t *w)
{
    int i, j;

    if (w == NULL) {
        return NULL;
    }
    for (i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        if (!app->embeds[i].running) {
            continue;
        }
        for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
            if (app->embeds[i].win[j].window == w) {
                return &app->embeds[i].win[j];
            }
        }
    }
    return NULL;
}

/** @brief The window this application knows as @p id, or NULL. */
static struct embed_win *
embed_win_of_id(struct desktop_embed *slot, uint32_t id)
{
    int j;

    for (j = 0; slot != NULL && j < TIKU_TRK_EMBED_WINDOWS; j++) {
        if (slot->win[j].window != NULL && slot->win[j].id == id) {
            return &slot->win[j];
        }
    }
    return NULL;
}

/** @brief How many windows this application still holds. */
static int
embed_window_count(const struct desktop_embed *slot)
{
    int j, n = 0;

    for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
        if (slot->win[j].window != NULL) {
            n++;
        }
    }
    return n;
}

/**
 * @brief Open a window for the embedded application, @p w by @p h,
 *        titled @p title.  @return its id; 0 when the slot has no free
 *        window or the workspace opens none.
 */
static uint32_t
embed_svc_open(void *ctx, const char *title, int w, int h)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;
    struct embed_win *ew = NULL;
    tiku_rect_t frame;
    int j;

    for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
        if (slot->win[j].window == NULL) {
            ew = &slot->win[j];
            break;
        }
    }
    if (ew == NULL) {
        return 0;               /* no free window in the slot */
    }
    frame.w = w + 2 * TIKU_WINDOW_BORDER;
    frame.h = h + tiku_window_tab_h() + 2 * TIKU_WINDOW_BORDER;
    frame.x = (app->surf->w - frame.w) / 2;
    frame.y = (app->surf->h - frame.h) / 3;
    /* Each application's windows start offset by its slot, and are laid
     * side by side in a grid, so none covers another's tab. */
    frame.x += (int)(slot - app->embeds) * 24;
    frame.y += (int)(slot - app->embeds) * 20;
    {
        int across = frame.w + 8;
        int down = frame.h + 8;
        int cols = (across > 0) ? (app->surf->w / across) : 1;

        if (cols < 1) {
            cols = 1;
        }
        frame.x += (j % cols) * across;
        frame.y += (j / cols) * down;
        /* Kept on the screen; when it does not fit, the top-left corner
         * stays on and the bottom is cut. */
        if (frame.y + frame.h > app->surf->h) {
            frame.y = app->surf->h - frame.h;
        }
        if (frame.x + frame.w > app->surf->w) {
            frame.x = app->surf->w - frame.w;
        }
        if (frame.x < 0) {
            frame.x = 0;
        }
        if (frame.y < 0) {
            frame.y = 0;
        }
        /* Never under the global bar, so the tab stays reachable; the
         * bottom may run off the screen. */
        if (app->gbar_on == 1 && frame.y < app->menus.bar.height) {
            frame.y = app->menus.bar.height;
        }
    }
    ew->slot = slot;
    ew->id = slot->next_id++;
    /* The workspace tag is this window's own entry, so
     * tiku_workspace_find() by tag, as the Deskbar's window rows use it,
     * returns this window and not another of the application's. */
    ew->window = tiku_workspace_open(app->desk, title, frame,
                                          embed_window_draw, ew, ew);
    if (ew->window == NULL) {
        return 0;
    }
    tiku_window_set_size_controls(ew->window, 0, 0);
    desktop_damage_all(app);
    return ew->id;
}

/**
 * @brief Take a frame of pixels for window @p id, kept as a copy of the
 *        application's own.  @return 0, -1 for no such window or no memory.
 */
static int
embed_svc_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);
    uint32_t *copy;

    if (ew == NULL || px == NULL || w < 1 || h < 1) {
        return -1;
    }
    copy = realloc(ew->frame, 4u * (size_t)w * (size_t)h);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, px, 4u * (size_t)w * (size_t)h);
    ew->frame = copy;
    ew->fw = w;
    ew->fh = h;
    desktop_damage_all(slot->app);
    return 0;
}

/**
 * @brief A menu row picked in an embedded window, handed to the
 *        application with the id of the window that published it.
 */
static void
embed_menu_pick(tiku_window_t *window, int command, void *context)
{
    struct embed_win *ew = context;
    struct desktop_embed *slot = (ew != NULL) ? ew->slot : NULL;

    (void)window;
    if (slot != NULL && slot->running && slot->descriptor->pick != NULL &&
        slot->descriptor->pick(slot->state, ew->id, command)) {
        embed_stop(slot);
    }
}

/** @brief Publish @p set as window @p id's menus; a pick goes to
 *         embed_menu_pick(). */
static int
embed_svc_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew != NULL) {
        tiku_window_publish_menus(ew->window, set, embed_menu_pick, ew);
    }
    return 0;
}

/**
 * @brief Place window @p id by its role: an announcement or a panel goes
 *        centred in the upper third, at a fixed size.  The application
 *        names the role; the desktop chooses the place.
 * @return 0, or -1 for another role or no such window.
 */
static int
embed_svc_place(void *ctx, uint32_t id, int role)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;
    struct embed_win *ew = embed_win_of_id(slot, id);
    int plain = (role == TIKU_APP_PLACE_ANNOUNCE);
#if TIKU_SHELL_NOTE_TABS


    if (ew == NULL || (role != TIKU_APP_PLACE_ANNOUNCE &&
                       role != TIKU_APP_PLACE_PANEL &&
                       role != TIKU_APP_PLACE_NOTE)) {
#else


    if (ew == NULL || (role != TIKU_APP_PLACE_ANNOUNCE &&
                       role != TIKU_APP_PLACE_PANEL)) {
#endif

        return -1;
#if TIKU_SHELL_NOTE_TABS

    }
    tiku_window_set_tab_auto_hide(ew->window, role == TIKU_APP_PLACE_NOTE);
    if (role == TIKU_APP_PLACE_NOTE) {
        desktop_damage_all(app);
        return 0;
#endif

    }
    if (plain) {
        /* An announcement has no tab: its frame shrinks to the content
         * and a hairline.  A panel keeps its tab and its frame. */
        ew->window->frame.w -= 2 * TIKU_WINDOW_BORDER - 2;
        ew->window->frame.h -= tiku_window_tab_h() +
                               2 * TIKU_WINDOW_BORDER - 2;
    }
    ew->window->frame.x = (app->surf->w - ew->window->frame.w) / 2;
    ew->window->frame.y = (app->surf->h - ew->window->frame.h) / 3;
    tiku_window_set_size_controls(ew->window, 0, 0);
    /* An announcement has no tab and no close box; a panel keeps
     * both. */
    tiku_window_set_plain(ew->window, plain);
    desktop_damage_all(app);
    return 0;
}

/**
 * @brief Resize window @p id to content @p w by @p h; its corner stays
 *        put unless the window would run off the screen.
 * @return 0, or -1 for no such window or a bad size.
 */
static int
embed_svc_resize(void *ctx, uint32_t id, int w, int h)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew == NULL || w <= 0 || h <= 0) {
        return -1;
    }
    ew->window->frame.w = w + 2 * TIKU_WINDOW_BORDER;
    ew->window->frame.h = h + tiku_window_tab_h() +
                          2 * TIKU_WINDOW_BORDER;
    /* Clamped to the screen as at opening: the top-left corner stays on
     * when the window does not fit. */
    if (ew->window->frame.y + ew->window->frame.h > app->surf->h) {
        ew->window->frame.y = app->surf->h - ew->window->frame.h;
    }
    if (ew->window->frame.x + ew->window->frame.w > app->surf->w) {
        ew->window->frame.x = app->surf->w - ew->window->frame.w;
    }
    if (ew->window->frame.y < 0) {
        ew->window->frame.y = 0;
    }
    if (ew->window->frame.x < 0) {
        ew->window->frame.x = 0;
    }
    desktop_damage_all(app);
    return 0;
}

/**
 * @brief An application asks for the file panel: the desktop shows its
 *        panel and answers later.
 *
 * One question at a time: while the desktop's own question or an
 * application's question is up, another application's is refused, not
 * queued, and may be asked again.  The window whose question is up may ask
 * a newer one, which replaces it.
 *
 * @return 1 when the panel opened, 0 when refused.
 */
static int
embed_svc_pick(void *ctx, uint32_t id, int mode, const char *start,
               const char *name)
{
    struct desktop_embed *slot = ctx;
    desktop_t *app = slot->app;

    if (panel_asks_desktop(app) ||
        (app->pending_pick.kind != 0 &&
         !(app->pending_pick.kind == 1 && app->pending_pick.slot == slot &&
           app->pending_pick.window == id))) {
        return 0;
    }
    app->pending_pick.kind = 1;
    app->pending_pick.slot = slot;
    app->pending_pick.window = id;
    app->pending_pick.local = 0;
    if (mode == TIKU_APP_PICK_SAVE) {
        tiku_trk_panel_window_open_save(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    } else if (mode == TIKU_APP_PICK_LOCAL_FOLDER) {
        /* A folder is the answer, as in the desktop's own folder
         * questions. */
        tiku_trk_panel_window_open(&app->file_panel, &app->pending_pick,
                                   TIKU_TRK_CMD_APP_PICK, start);
    } else {
        /* To open, a non-empty @p name filters the offer: only that file
         * name can be chosen, and every other file is greyed. */
        tiku_trk_panel_window_open_file(&app->file_panel,
            &app->pending_pick, TIKU_TRK_CMD_APP_PICK, start, name);
    }
    desktop_damage_all(app);
    return 1;
}

/**
 * @brief Close one window of an application; the last one closing stops
 *        the application.
 *
 * @param tell nonzero when the desktop closed it and the application is to
 *        be told; zero when the application asked.
 */
void
embed_drop_window(struct embed_win *ew, int tell)
{
    struct desktop_embed *slot = (ew != NULL) ? ew->slot : NULL;
    tiku_window_t *window;

    if (slot == NULL || ew->window == NULL) {
        return;
    }
    window = ew->window;
    ew->window = NULL;          /* before the close, so nothing re-enters */
    free(ew->frame);
    ew->frame = NULL;
    ew->fw = 0;
    ew->fh = 0;
    if (slot->app->embed_press == ew) {
        slot->app->embed_press = NULL;
    }
    tiku_workspace_close(slot->app->desk, window);
    if (tell && slot->running && slot->descriptor->closed != NULL) {
        slot->descriptor->closed(slot->state, ew->id);
    }
    desktop_damage_all(slot->app);
    if (embed_window_count(slot) == 0) {
        embed_stop(slot);
    }
}

/**
 * @brief Let the person resize window @p id from now on.
 * @return 0, or -1 for no such window.
 */
static int
embed_svc_sizeable(void *ctx, uint32_t id)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew == NULL || ew->window == NULL) {
        return -1;
    }
    /* A resizable window gets the zoom box too: to the whole desk and
     * back. */
    tiku_window_set_size_controls(ew->window, 1, 1);
    return 0;
}

/** @brief Set the desk's clipboard to @p text, for an application in this
 *         process.  @return 1 after complete publication, else 0. */
static int
embed_svc_clip_set(void *ctx, const char *text)
{
    (void)ctx;
    if (!text) text = "";
    /* The new stamp makes clip_mirror() send the text to the remote
     * sessions on the next turn, as for a field's copy. */
    return tiku_clipboard_import(text, strlen(text));
}

/** @brief The clipboard's text, the desk's own, and its stamp. */
static const char *
embed_svc_clip_text(void *ctx, unsigned *stamp)
{
    (void)ctx;
    if (stamp != NULL) {
        *stamp = tiku_clipboard_stamp();
    }
    return tiku_clipboard_text();
}

/** @brief Bring one of this application's windows to the front. */
static int
embed_svc_raise(void *ctx, uint32_t id)
{
    struct desktop_embed *slot = ctx;
    struct embed_win *ew = embed_win_of_id(slot, id);

    if (ew == NULL) {
        return -1;
    }
    tiku_workspace_activate(slot->app->desk, ew->window);
    desktop_damage_all(slot->app);
    return 0;
}

/** @brief Close window @p id at the application's own request. */
static void
embed_svc_close(void *ctx, uint32_t id)
{
    struct desktop_embed *slot = ctx;

    /* The application asked, so it is not told. */
    embed_drop_window(embed_win_of_id(slot, id), 0);
}

/**
 * @brief Stop the embedded application in @p slot: its stop hook, then
 *        every window it still holds.  A stopped slot is left alone.
 */
void
embed_stop(struct desktop_embed *slot)
{
    desktop_t *app = slot->app;
    int j;

    if (!slot->running) {
        return;
    }
    slot->running = 0;
    if (slot->descriptor->stop != NULL) {
        slot->descriptor->stop(slot->state);
    }
    slot->state = NULL;
    for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
        struct embed_win *ew = &slot->win[j];

        if (ew->window != NULL) {
            tiku_workspace_close(app->desk, ew->window);
            ew->window = NULL;
        }
        free(ew->frame);
        ew->frame = NULL;
        if (app->embed_press == ew) {
            app->embed_press = NULL;
        }
    }
    desktop_damage_all(app);
}

/**
 * @brief Hand @p ev to the application with the id of window @p ew; an
 *        application with no windowed handler gets its plain one.
 * Stops the application when its handler returns nonzero.
 * @return nonzero when the application says it is done.
 */
int
embed_deliver(struct embed_win *ew, const tiku_event_t *ev)
{
    struct desktop_embed *slot = (ew != NULL) ? ew->slot : NULL;

    if (slot == NULL || !slot->running) {
        return 0;
    }
    int done=tiku_app_deliver_event(slot->descriptor, slot->state,ew->id,ev);
    if(done)embed_stop(slot);
    return done;
}
#if TIKU_SHELL_BOARD_SERVICES


/*---------------------------------------------------------------------------*/
/* BOARD, PATH AND HELP SERVICES, AND STARTING AN APPLICATION                */
/*---------------------------------------------------------------------------*/

/** @brief The boards the desktop has mounted, for a hosted application. */
static int
embed_svc_board_names(void *ctx, char *out, size_t max)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_names(slot->app, out, max);
}

/** @brief The current device (empty for this machine), for an app that
 *         defaults its work to it. */
static int
embed_svc_current_device(void *ctx, char *out, size_t max)
{
    struct desktop_embed *slot = ctx;

    if (out == NULL || max == 0u) {
        return 0;
    }
    snprintf(out, max, "%s", slot->app->current_device);
    return (int)strlen(out);
}

/** @brief What a board says about itself, for an app that shows it. */
static int
embed_svc_device_stats(void *ctx, const char *board, char *out, size_t max)
{
    struct desktop_embed *slot = ctx;

    return desktop_device_stats(slot->app, board, out, max);
}

/** @brief Run a program on a board, telling this slot what it prints. */
static int
embed_svc_board_run(void *ctx, const char *board, const char *program)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_run(slot->app, slot, board, program);
}

/** @brief Answer the running program's INPUT. */
static int
embed_svc_board_input(void *ctx, const char *text)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_input(slot->app, text);
}

/** @brief Save a program on a board, telling this slot when it is
 *         saved. */
static int
embed_svc_board_save(void *ctx, const char *board, const char *name,
                     const char *program)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_save(slot->app, slot, board, name, program);
}

/** @brief The port a board is on. */
static int
embed_svc_board_port(void *ctx, const char *board, char *out, size_t max)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_port(slot->app, board, out, max);
}

/** @brief The processor a board reports. */
static int
embed_svc_board_mcu(void *ctx, const char *board, char *out, size_t max)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_mcu(slot->app, board, out, max);
}

/** @brief Look for boards on this machine's ports; the slot is told when
 *         the look ends. */
static int
embed_svc_board_find(void *ctx)
{
    struct desktop_embed *slot = ctx;

    desktop_board_look_begin(slot->app, -1, slot);
    return 1;
}
#endif


/**
 * @brief Open @p path for an application: a folder in a folder window,
 *        anything else through open_one(), and, when nothing opens it, the
 *        folder that holds it.
 * @return 1 when something opened.
 */
int
desktop_app_open_path(desktop_t *app, const char *path)
{
    tiku_model_t model;

    if (app == NULL || path == NULL ||
        app->backend == NULL ||
        app->backend->ops->stat(app->backend, path,
                                      &model) != 0) {
        return 0;
    }
    tiku_trail("open %s for an application", path);
    /* A folder opens in a folder window of its own. */
    if (tiku_model_is_container(&model)) {
        return open_folder(app, path) ? 1 : 0;
    }
    if (open_one(app, &model)) {
        return 1;
    }
    /* No application opens it (a fresh machine has no registry): open
     * the folder holding it. */
    {
        char folder[TIKU_PATH_MAX];
        char *slash;

        snprintf(folder, sizeof folder, "%s", path);
        slash = strrchr(folder, '/');
        if (slash == NULL || slash == folder) {
            return 0;
        }
        *slash = '\0';
        return open_folder(app, folder) != NULL;
    }
}

/** @brief Open @p path for the hosted application, as
 *         desktop_app_open_path() does. */
static int embed_svc_open_path(void *ctx, const char *path)
{
    struct desktop_embed *slot = ctx;
    return slot && desktop_app_open_path(slot->app, path);
}

/**
 * @brief Remember the help page this application named for itself.
 * @return whether the desktop has a help folder.
 */
static int embed_svc_help_page(void *ctx, const char *page)
{
    struct desktop_embed *slot = ctx;

    if (slot == NULL) return 0;
    snprintf(slot->help_page, sizeof slot->help_page, "%s",
             page != NULL ? page : "");
    return desktop_help_ready(slot->app);
}

/** @brief Open help page @p page for the application; a name of 256 bytes
 *         or more is refused. */
static int embed_svc_help_open(void *ctx, const char *page)
{
    struct desktop_embed *slot = ctx;
    if (!slot || !page || strlen(page) >= 256) return 0;
    return desktop_help_open(slot->app, page);
}

/**
 * @brief Show generated HTML under @p title in the reader, which owns it
 *        from then on; HTML over 15000 bytes or a title over 127 is
 *        refused.
 */
static int embed_svc_help_html(void *ctx, const char *title, const char *html)
{
    struct desktop_embed *slot = ctx;
    if (!slot || !html || strlen(html) > 15000 ||
        (title && strlen(title) > 127)) return 0;
    return desktop_help_html(slot->app, title, html);
}
#if TIKU_SHELL_BOARD_SERVICES


/** @brief Release a board: unmount it and close its port and its window
 *         channel, and remember it for a reclaim. */
static int
embed_svc_board_release(void *ctx, const char *board)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_release(slot->app, board);
}

/** @brief Ask for a released board back. */
static int
embed_svc_board_reclaim(void *ctx, const char *board)
{
    struct desktop_embed *slot = ctx;

    /* The slot is told RECLAIMED or FAILED once every port has answered;
     * this returns only whether the asking was taken. */
    return desktop_board_reclaim(slot->app, board, -1, slot);
}

/** @brief Break the running program. */
static int
embed_svc_board_stop(void *ctx)
{
    struct desktop_embed *slot = ctx;

    return desktop_board_stop(slot->app);
}
#endif


/**
 * @brief Host @p descriptor in this process, over workspace services.
 *
 * @return nonzero when it started and took a slot.
 */
int
embed_start(desktop_t *app, const tiku_app_descriptor_t *descriptor)
{
    struct desktop_embed *slot = NULL;
    int i;

    for (i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        if (!app->embeds[i].running) {
            slot = &app->embeds[i];
            break;
        }
    }
    if (slot == NULL || descriptor == NULL || descriptor->start == NULL) {
        return 0;
    }
    memset(slot, 0, sizeof *slot);
    slot->app = app;
    slot->descriptor = descriptor;
    slot->next_id = 1;
    /* The services table lives in the slot: an application keeps the
     * pointer for its lifetime, and each table's context is its own
     * slot. */
    slot->services.ctx = slot;
    slot->services.open = embed_svc_open;
    slot->services.frame = embed_svc_frame;
    slot->services.menus = embed_svc_menus;
    slot->services.close = embed_svc_close;
    slot->services.place = embed_svc_place;
    slot->services.resize = embed_svc_resize;
    slot->services.pick = embed_svc_pick;
    slot->services.raise_window = embed_svc_raise;
    slot->services.sizeable = embed_svc_sizeable;
    slot->services.clip_set = embed_svc_clip_set;
    slot->services.clip_text = embed_svc_clip_text;
#if TIKU_SHELL_BOARD_SERVICES

    slot->services.board_names = embed_svc_board_names;
    slot->services.current_device = embed_svc_current_device;
    slot->services.device_stats = embed_svc_device_stats;
    slot->services.board_run = embed_svc_board_run;
    slot->services.board_input = embed_svc_board_input;
        slot->services.board_stop = embed_svc_board_stop;
        slot->services.board_save = embed_svc_board_save;
        slot->services.board_port = embed_svc_board_port;
    slot->services.board_mcu = embed_svc_board_mcu;
        slot->services.board_release = embed_svc_board_release;
#endif

    slot->services.open_path = embed_svc_open_path;
#if TIKU_SHELL_BOARD_SERVICES

        slot->services.board_reclaim = embed_svc_board_reclaim;
    slot->services.board_find = embed_svc_board_find;
#endif

    slot->services.help_page = embed_svc_help_page;
    slot->services.help_open = embed_svc_help_open;
    slot->services.help_html = embed_svc_help_html;
    if (descriptor->start(&slot->state, &slot->services) != 0) {
        return 0;
    }
    slot->running = 1;
    return 1;
}

/**
 * @brief Start @p descriptor, or, when an application with its id runs,
 *        bring that application's window forward.
 * @return nonzero when it runs.
 */
int
embed_open_or_raise(desktop_t *app, const tiku_app_descriptor_t *descriptor)
{
    int i;

    if (descriptor == NULL || descriptor->id == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_TRK_EMBED_MAX; i++) {
        struct desktop_embed *slot = &app->embeds[i];

        if (slot->running && slot->descriptor->id != NULL &&
            strcmp(slot->descriptor->id, descriptor->id) == 0) {
            int j;

            /* The window in the lowest slot the application holds. */
            for (j = 0; j < TIKU_TRK_EMBED_WINDOWS; j++) {
                if (slot->win[j].window != NULL) {
                    tiku_workspace_activate(app->desk,
                                                 slot->win[j].window);
                    break;
                }
            }
            desktop_damage_all(app);
            return 1;
        }
    }
    return embed_start(app, descriptor);
}
