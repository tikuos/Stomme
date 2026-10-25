/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_client.c - the runner that serves one application descriptor in
 * its own process: the services over the window session, and the loop
 * that hands the desktop's messages to the descriptor.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

#include "tiku_client.h"
#include "tiku_app_resources.h"
/** @brief Optional resource model linked into a standalone application. */
extern const tiku_app_resource_export_t tiku_app_resources_v1
    __attribute__((weak));

#include "tiku_clipboard.h"
#include "tiku_dl.h"
#include "tiku_remote.h"
#include "tiku_report.h"
#include "tiku_trail.h"
#include "tiku_watch.h"

/*
 * The stamp that marks a TikuOS application's binary: this string, in a
 * section of its own.  A folder listing (tiku_backend_posix.c) recognises
 * an application by that section's name in the binary's headers, without
 * reading the string.  A program that links this file carries it.
 */
#ifdef __APPLE__
/* Mach-O names a section by its segment too, and the stamp reader
 * knows both spellings. */
__attribute__((used, section("__TIKUOS,__tikuos")))
#else
__attribute__((used, section(".tikuos")))
#endif
static const char tiku_app_stamp_client[] = "TikuOS-application-1";

/** @brief The runner's state, the services' context. */
typedef struct {
    tiku_remote_client_t remote;
    tiku_app_resources_t *resources;
    /* Windows the application holds; the run ends when the last one
     * closes. */
    int                       windows;
    /* The desk's clipboard, mirrored: the shell sends every change as a
     * TELL, so a paste reads from here and never waits on the wire.  Held
     * at its size (NULL when empty); the stamp moves whenever it does. */
    char                     *clip;
    unsigned                  clip_stamp;
    /* The shell's board list, a "name<TAB>port<TAB>mcu" row a line,
     * mirrored like the clipboard.  boards_heard is 0 until the shell
     * first sends it, a turn after start(); before that an empty list
     * does not mean there are no boards. */
    char                      boards[512];
    int                       boards_heard;
    /* The desktop's current device, mirrored the same way. */
    char                      current[64];
} client_ctx_t;

/** @brief Send window @p id's title, content size and size controls. */
static int svc_configure(void *ctx, uint32_t id, const char *title,
                         int w, int h, int resize, int zoom)
{
    client_ctx_t *c = ctx;
    tiku_app_resources_window(c->resources,id,title,w,h,0);
    return tiku_remote_configure(&c->remote, id, title, w, h, resize, zoom);
}

/** @brief Say @p message to the desktop. */
static int svc_message(void *ctx, const tiku_msg_t *message)
{
    client_ctx_t *c = ctx;
    return tiku_remote_say(&c->remote, message);
}

/** @brief Ask the desktop for a window, and count it as held. */
static uint32_t
svc_open(void *ctx, const char *title, int w, int h)
{
    client_ctx_t *c = ctx;
    uint32_t id = tiku_remote_open(&c->remote, title, w, h);
    tiku_trail("window open id %u size %dx%d", id, w, h);

    if (id != 0u) {
        tiku_app_resources_window(c->resources,id,title,w,h,0);
        c->windows++;
    }
    return id;
}

/** @brief Send @p px as window @p id's next frame.  @return 0, or -1. */
static int
svc_frame(void *ctx, uint32_t id, const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;

    tiku_app_resources_controls(c->resources,id,NULL);
    int result = tiku_remote_frame(&c->remote, id, px, w, h);
    if (result < 0) tiku_trail("frame failed id %u size %dx%d", id, w, h);
    return result;
}

/**
 * @brief Send window @p id as its display list when the list can stand
 *        for it, else as pixels.
 *
 * The list goes when the desktop has claimed command streams, and icon
 * and picture commands too if the list holds any; when the list missed
 * nothing; and when it is under half the frame's size.  Otherwise the
 * pixels go, with a text description of the list, if any, for readers.
 */
static int
svc_present(void *ctx, uint32_t id, const struct tiku_dl *dl,
            const uint32_t *px, int w, int h)
{
    client_ctx_t *c = ctx;
    size_t frame = 4u * (size_t)w * (size_t)h;

    /*
     * A list goes only to a desktop that claimed what it holds: an end
     * steps over a command it cannot play (tiku_dl.h), and the window
     * would show holes.  Zero features means no WELCOME yet, and the
     * pixels go.
     */
    if (dl != NULL &&
        (tiku_remote_peer_features(&c->remote) &
             TIKU_FEAT_COMMAND_STREAM) != 0u &&
        (tiku_dl_icons(dl) == 0 ||
         (tiku_remote_peer_features(&c->remote) &
              TIKU_FEAT_ICON_STREAM) != 0u) &&
        (tiku_dl_pictures(dl) == 0 ||
         (tiku_remote_peer_features(&c->remote) &
              TIKU_FEAT_PICTURE_STREAM) != 0u) &&
        tiku_dl_misses(dl) == 0 &&
        tiku_dl_flat_size(dl) * 2u < frame &&
        tiku_remote_draw(&c->remote, id, dl, w, h)) {
        tiku_app_resources_controls(c->resources,id,dl);
        tiku_remote_describe(&c->remote, id, NULL);
        return 1;
    }
    int result = svc_frame(ctx, id, px, w, h);
    tiku_app_resources_controls(c->resources,id,dl);
    char description[4096];
    if (dl) tiku_dl_say(dl, description, sizeof description);
    else description[0] = 0;
    tiku_remote_describe(&c->remote, id, description);
    return result == 0;
}

/** @brief Send window @p id's menu set. */
static int
svc_menus(void *ctx, uint32_t id, const tiku_menuset_t *set)
{
    client_ctx_t *c = ctx;

    tiku_app_resources_menus(c->resources,id,set);
    return tiku_remote_menus(&c->remote, id, set);
}

/** @brief Ask the desktop to bring window @p id forward. */
static int
svc_raise(void *ctx, uint32_t id)
{
    client_ctx_t *c = ctx;

    tiku_remote_raise(&c->remote, id);
    return 0;
}

/** @brief Tell the desktop window @p id's place role. */
static int
svc_place(void *ctx, uint32_t id, int role)
{
    client_ctx_t *c = ctx;

    tiku_remote_place(&c->remote, id, role);
    return 0;
}

/** @brief Tell the desktop the person may resize window @p id. */
static int
svc_sizeable(void *ctx, uint32_t id)
{
    client_ctx_t *c = ctx;

    tiku_remote_sizeable(&c->remote, id);
    return 0;
}

/** @brief Give window @p id back, and stop counting it as held. */
static void
svc_close(void *ctx, uint32_t id)
{
    client_ctx_t *c = ctx;

    /* Only this window goes; the pump ends the run when the last one
     * has gone. */
    tiku_app_resources_window(c->resources,id,NULL,0,0,1);
    tiku_remote_close_window(&c->remote, id);
    tiku_trail("window close requested id %u", id);
    if (c->windows > 0) {
        c->windows--;
    }
}

/** @brief Microseconds on the monotonic clock. */
static int64_t
now_us(void)
{
    struct timespec ts;

    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/**
 * @brief Ask the shell to let the person pick a file.
 *
 * Sent as a TIKU_MSG_PICK over SAY; the answer comes back as a
 * TIKU_MSG_PICKED over TELL.
 */
static int
svc_pick(void *ctx, uint32_t id, int mode, const char *start,
         const char *name)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_PICK);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_int32(m, "window", (int32_t)id);
    (void)tiku_msg_add_int32(m, "mode", (int32_t)mode);
    if (start != NULL) {
        (void)tiku_msg_add_string(m, "start", start);
    }
    if (name != NULL) {
        (void)tiku_msg_add_string(m, "name", name);
    }
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

/** @brief Set the mirror to @p text, cut at TIKU_CLIPBOARD_LIMIT between
 *         characters as the desk's store cuts it, and move its stamp.
 *         Without memory for it the mirror is left empty. */
static void
clip_hold(client_ctx_t *c, const char *text)
{
    size_t n = (text != NULL) ? strlen(text) : 0u;
    char *copy;

    if (n > TIKU_CLIPBOARD_LIMIT) {
        n = TIKU_CLIPBOARD_LIMIT;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    }
    copy = n != 0u ? malloc(n + 1u) : NULL;
    if (copy != NULL) {
        memcpy(copy, text, n);
        copy[n] = '\0';
    }
    free(c->clip);
    c->clip = copy;
    c->clip_stamp++;
}

/** @brief Send exact text to the shell, updating the mirror after acceptance.
 *  @note Allocation, size or transport refusal preserves the old mirror. */
static int
svc_clip_set(void *ctx, const char *text)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m;
    char *copy;
    size_t length;
    int sent;

    if (!text) text = "";
    length = strlen(text);
    if (length > TIKU_CLIPBOARD_LIMIT) return 0;
    copy = malloc(length + 1);
    if (!copy) return 0;
    memcpy(copy, text, length + 1);
    m = tiku_msg_new(TIKU_MSG_CLIP);
    if (!m || !tiku_msg_add_string(m, "text", copy)) {
        tiku_msg_free(m);
        free(copy);
        return 0;
    }
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    if (sent) {
        free(c->clip);
        c->clip = copy;
        c->clip_stamp++;
    } else free(copy);
    return sent;
}

/** @brief The mirrored clipboard, never NULL, and its stamp. */
static const char *
svc_clip_text(void *ctx, unsigned *stamp)
{
    client_ctx_t *c = ctx;

    if (stamp != NULL) {
        *stamp = c->clip_stamp;
    }
    return (c->clip != NULL) ? c->clip : "";
}

/** @brief The @p which'th field of @p row, up to a tab or its end. */
static size_t
field_of(const char *row, int which, char *out, size_t max)
{
    const char *p = row;
    int i;

    out[0] = '\0';
    for (i = 0; i < which; i++) {
        const char *tab = strchr(p, '\t');

        if (tab == NULL || *p == '\n') {
            return 0u;
        }
        p = tab + 1;
    }
    {
        size_t n = strcspn(p, "\t\n");

        if (n >= max) {
            n = max - 1u;
        }
        memcpy(out, p, n);
        out[n] = '\0';
        return n;
    }
}

/** @brief The port of @p board, from the mirrored board list. */
static int
svc_board_port(void *ctx, const char *board, char *out, size_t max)
{
    client_ctx_t *c = ctx;
    const char *row = c->boards;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    while (*row != '\0') {
        char name[64];

        (void)field_of(row, 0, name, sizeof name);
        if (board != NULL && strcmp(name, board) == 0) {
            return (int)field_of(row, 1, out, max);
        }
        row = strchr(row, '\n');
        if (row == NULL) {
            break;
        }
        row++;
    }
    return 0;
}

/** @brief The processor of @p board, from the mirrored board list. */
static int
svc_board_mcu(void *ctx, const char *board, char *out, size_t max)
{
    client_ctx_t *c = ctx;
    const char *row = c->boards;

    if (out == NULL || max == 0u) {
        return 0;
    }
    out[0] = '\0';
    while (*row != '\0') {
        char name[64];

        (void)field_of(row, 0, name, sizeof name);
        if (board != NULL && strcmp(name, board) == 0) {
            return (int)field_of(row, 2, out, max);
        }
        row = strchr(row, '\n');
        if (row == NULL) {
            break;
        }
        row++;
    }
    return 0;
}

/** @brief Ask the shell for something about one board, by name. */
static int
say_board(client_ctx_t *c, uint32_t what, const char *board)
{
    tiku_msg_t *m = tiku_msg_new(what);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "board", (board != NULL) ? board : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

/** @brief Ask the shell to let @p board go. */
static int
svc_board_release(void *ctx, const char *board)
{
    return say_board(ctx, TIKU_MSG_BOARDFREE, board);
}

/** @brief Ask the shell to take @p board back. */
static int
svc_board_reclaim(void *ctx, const char *board)
{
    return say_board(ctx, TIKU_MSG_BOARDBACK, board);
}

/** @brief Ask the shell to look for boards on this machine's ports. */
static int
svc_board_find(void *ctx)
{
    return say_board(ctx, TIKU_MSG_BOARDLOOK, "");
}

/** @brief Open a desktop namespace path without interpreting it locally. */
static int svc_open_path(void *ctx, const char *path)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_OPENPATH);
    if (!m || !path) { tiku_msg_free(m); return 0; }
    tiku_msg_add_string(m, "path", path);
    int sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m); return sent;
}

/** @brief Name this application's page of the desktop's help folder. */
static int svc_help_page(void *ctx, const char *page)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_HELPPAGE);
    if (!m || !page) { tiku_msg_free(m); return 0; }
    tiku_msg_add_string(m, "page", page);
    int sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m); return sent;
}

/** @brief Request a contextual topic from the desktop's reader. */
static int svc_help_open(void *ctx, const char *page)
{
    client_ctx_t *c = ctx;
    if (!page || strlen(page) >= 256) return 0;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_HELPOPEN); if (!m) return 0;
    tiku_msg_add_string(m, "page", page);
    int sent = tiku_remote_say(&c->remote, m); tiku_msg_free(m); return sent;
}

/** @brief Send a report of at most 15000 bytes to the desktop's reader. */
static int svc_help_html(void *ctx, const char *title, const char *html)
{
    client_ctx_t *c = ctx;
    if (!html || strlen(html) > 15000 || (title && strlen(title) > 127))
        return 0;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_HELPHTML); if (!m) return 0;
    if (!tiku_msg_add_string(m, "title", title ? title : "Report") ||
        !tiku_msg_add_string(m, "html", html)) { tiku_msg_free(m); return 0; }
    int sent = tiku_remote_say(&c->remote, m); tiku_msg_free(m); return sent;
}

/** @brief The desktop's current device, mirrored; empty for this machine. */
static int
svc_current_device(void *ctx, char *out, size_t max)
{
    client_ctx_t *c = ctx;

    if (out == NULL || max == 0u) {
        return 0;
    }
    snprintf(out, max, "%s", c->current);
    return (int)strlen(out);
}

/** @brief The mirrored board names, one a line; -1 until the shell first
 *         sends the list. */
static int
svc_board_names(void *ctx, char *out, size_t max)
{
    client_ctx_t *c = ctx;

    if (out == NULL || max == 0u) {
        return 0;
    }
        {
        /* The rows carry a name, a port and a processor; only the names
         * are copied. */
        const char *row = c->boards;
        size_t n = 0u;

        out[0] = '\0';
        while (*row != '\0' && n + 1u < max) {
            char name[64];
            size_t k = field_of(row, 0, name, sizeof name);

            if (k > 0u && n + k + 1u < max) {
                memcpy(out + n, name, k);
                n += k;
                out[n++] = '\n';
                out[n] = '\0';
            }
            row = strchr(row, '\n');
            if (row == NULL) {
                break;
            }
            row++;
        }
        return c->boards_heard ? (int)n : -1;
    }
    return (int)strlen(out);
}

/** @brief Ask the shell to run @p program on @p board. */
static int
svc_board_run(void *ctx, const char *board, const char *program)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDRUN);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "board", (board != NULL) ? board : "");
    (void)tiku_msg_add_string(m, "program",
                              (program != NULL) ? program : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

/** @brief Answer the running program's INPUT with @p text. */
static int
svc_board_input(void *ctx, const char *text)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDIN);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "text", (text != NULL) ? text : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

/** @brief Ask the shell to keep @p program on @p board as @p name. */
static int
svc_board_save(void *ctx, const char *board, const char *name,
               const char *program)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDSAVE);
    int sent;

    if (m == NULL) {
        return 0;
    }
    (void)tiku_msg_add_string(m, "board", (board != NULL) ? board : "");
    (void)tiku_msg_add_string(m, "name", (name != NULL) ? name : "");
    (void)tiku_msg_add_string(m, "program",
                              (program != NULL) ? program : "");
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

/** @brief Ask the shell to break the running program. */
static int
svc_board_stop(void *ctx)
{
    client_ctx_t *c = ctx;
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_BOARDSTOP);
    int sent;

    if (m == NULL) {
        return 0;
    }
    sent = tiku_remote_say(&c->remote, m);
    tiku_msg_free(m);
    return sent;
}

/** @brief Log an event's kind and window, never document text, clipboard
 *         content or typed keys. */
static void trace_event(uint32_t id, const tiku_event_t *e)
{
    if (e->type == TIKU_EVENT_RESIZE)
        tiku_trail("resize window %u to %dx%d", id, e->width, e->height);
    else if (e->type != TIKU_EVENT_POINTER_MOVE && e->type != TIKU_EVENT_EXPOSE)
        tiku_trail("event %d window %u", (int)e->type, id);
}

/**
 * @brief Start @p app with the runner's services, then serve the desktop's
 *        messages and tick() until it is done.
 * @return what tiku_client_run() returns.
 */
static int
pump(const tiku_app_descriptor_t *app, client_ctx_t *ctx, const char *document,
     const tiku_msg_t *message)
{
    tiku_app_services_t services;
    void *state = NULL;
    int result = 0;

    memset(&services, 0, sizeof services);
    services.startup_document = document;
    services.startup_message = message;
    services.ctx = ctx;
    services.open = svc_open;
    services.frame = svc_frame;
    services.present = svc_present;
    services.menus = svc_menus;
    services.close = svc_close;
    services.pick = svc_pick;
    services.place = svc_place;
    services.raise_window = svc_raise;
    services.sizeable = svc_sizeable;
    services.configure = svc_configure;
    services.send_message = svc_message;
    services.clip_set = svc_clip_set;
    services.clip_text = svc_clip_text;
    services.board_names = svc_board_names;
    services.board_run = svc_board_run;
    services.board_input = svc_board_input;
    services.board_stop = svc_board_stop;
        services.board_save = svc_board_save;
        services.board_port = svc_board_port;
    services.board_mcu = svc_board_mcu;
    services.board_release = svc_board_release;
    services.open_path = svc_open_path;
    services.help_page = svc_help_page;
    services.help_open = svc_help_open;
    services.help_html = svc_help_html;
        services.board_reclaim = svc_board_reclaim;
    services.board_find = svc_board_find;
    services.current_device = svc_current_device;
    const char *help = getenv("TIKU_APP_HELP");
    if (!message && help && *help) svc_help_page(ctx, help);
    tiku_watch_install(0);
    tiku_watch_beat();
    tiku_trail("application start: %s (%s)", app->name ? app->name : "app",
               app->id ? app->id : "unknown");
    const tiku_app_resource_export_t *resource_model =
        tiku_app_resource_bindings(app);
    if (!resource_model && &tiku_app_resources_v1 &&
        tiku_app_resources_v1.version == 1 &&
        tiku_app_resources_v1.size == sizeof tiku_app_resources_v1)
        resource_model = &tiku_app_resources_v1;
    ctx->resources=tiku_app_resources_new(&services,app,resource_model);
    if (app->start != NULL && app->start(&state, &services) != 0) {
        tiku_app_resources_free(ctx->resources);ctx->resources=NULL;
        tiku_report_now("application initialization failed");
        tiku_remote_disconnect(&ctx->remote);
        return 1;
    }
    tiku_app_resources_model(ctx->resources,state);
    tiku_trail("application started: %d windows", ctx->windows);
    for (;;) {
        int done = 0;
        int dead = 0;
        tiku_watch_beat();

        /* Drain the queue before waiting: a drag is many messages. */
        for (;;) {
            tiku_event_t event;
            uint32_t id = 0;
            int command = 0;
            int type = tiku_remote_read(&ctx->remote, &id, &event,
                                             &command);

            if (type == 0) {
                break;
            }
            if (type < 0) {
                tiku_trail("desktop connection lost with %d windows open",
                           ctx->windows);
                if (ctx->windows > 0) {
                    tiku_report_now("unexpected desktop connection loss");
                    result = 1;
                }
                dead = 1;
                break;
            }
            if (type == TIKU_RMSG_SHUTDOWN) {
                tiku_trail("desktop requested shutdown");
                done = 1; break;
            }
            if (type == TIKU_RMSG_CLOSED) {
                tiku_app_resources_window(ctx->resources,id,NULL,0,0,1);
                tiku_trail("desktop closed window %u", id);
                /* The desktop closed a window; the last one ends the run. */
                if (app->closed != NULL) {
                    app->closed(state, id);
                }
                if (ctx->windows > 0) {
                    ctx->windows--;
                }
                if ((ctx->windows <= 0 &&
                     !tiku_app_resources_hold_exit(ctx->resources))) {
                    dead = 1;
                    break;
                }
                continue;
            }
            if (type == TIKU_RMSG_EVENT) trace_event(id, &event);
            if (type == TIKU_RMSG_PICK)
                tiku_trail("menu command %d window %u", command, id);
            if (type == TIKU_RMSG_EVENT &&
                tiku_app_deliver_event(app, state, id, &event)) {
                done = 1;
            }
            if (type == TIKU_RMSG_PICK && app->pick != NULL &&
                app->pick(state, id, command)) {
                done = 1;
            }
            if (type == TIKU_RMSG_TELL) {
                /* The shell's message: the board list, a program's output,
                 * the clipboard or a pick's answer; message() hears each. */
                tiku_msg_t *heard = tiku_remote_heard(&ctx->remote);

                if (heard != NULL) {
                    if (tiku_msg_what(heard) == TIKU_MSG_BOARDS) {
                        const char *n = tiku_msg_find_string(heard,
                                                             "names", 0);
                        const char *cur = tiku_msg_find_string(heard,
                                                               "current", 0);
                        char was[64];

                        snprintf(ctx->boards, sizeof ctx->boards, "%s",
                                 (n != NULL) ? n : "");
                        ctx->boards_heard = 1;
                        snprintf(was, sizeof was, "%s", ctx->current);
                        snprintf(ctx->current, sizeof ctx->current, "%s",
                                 (cur != NULL) ? cur : "");
                        /* A change of device is told through
                         * board_said(TIKU_APP_BOARD_CURRENT). */
                        if (strcmp(was, ctx->current) != 0 &&
                            app->board_said != NULL) {
                            app->board_said(state, TIKU_APP_BOARD_CURRENT,
                                            ctx->current);
                        }
                    }
                    if (tiku_msg_what(heard) == TIKU_MSG_BOARDOUT &&
                        app->board_said != NULL) {
                        int32_t what = 0;
                        const char *text = tiku_msg_find_string(heard,
                                                                "text", 0);

                        (void)tiku_msg_find_int32(heard, "what", 0, &what);
                        app->board_said(state, (int)what,
                                        (text != NULL) ? text : "");
                    }
                    if (tiku_msg_what(heard) == TIKU_MSG_CLIP) {
                        const char *text =
                            tiku_msg_find_string(heard, "text", 0);

                        clip_hold(ctx, text);
                    }
                    if (tiku_msg_what(heard) == TIKU_MSG_PICKED &&
                        app->picked != NULL) {
                        const char *path =
                            tiku_msg_find_string(heard, "path", 0);
                        int32_t win = 0;

                        (void)tiku_msg_find_int32(heard, "window", 0,
                                                  &win);
                        if (path != NULL) {
                            app->picked(state, (uint32_t)win, path);
                        }
                    }
                    if (!tiku_app_resources_message(ctx->resources, heard) &&
                        app->message)
                        app->message(state, heard);
                    tiku_msg_free(heard);
                }
            }
            if (done || (ctx->windows <= 0 &&
                         !tiku_app_resources_hold_exit(ctx->resources))) {
                done = 1;
                break;
            }
        }
        if (!done && !dead && app->tick != NULL) {
            app->tick(state, now_us());
        }
        tiku_app_resources_poll(ctx->resources,now_us());
        if (done || dead ||
            (ctx->windows <= 0 &&
             !tiku_app_resources_hold_exit(ctx->resources)) ||
            tiku_app_resources_finished(ctx->resources)) {
            break;
        }
        {
            /* Wait for the desktop's next message, at most
             * TIKU_CLIENT_TICK_MS, then tick() again. */
            struct pollfd fds;

            fds.fd = ctx->remote.fd;
            fds.events = POLLIN;
            fds.revents = 0;
            (void)poll(&fds, 1, TIKU_CLIENT_TICK_MS);
        }
    }
    tiku_app_resources_free(ctx->resources);ctx->resources=NULL;
    if (app->stop != NULL) {
        tiku_trail("application stopping, exit %d", result);
        app->stop(state);
    }
    tiku_remote_disconnect(&ctx->remote);
    free(ctx->clip);
    ctx->clip = NULL;
    return result;
}

int
tiku_client_run(const tiku_app_descriptor_t *app)
{
    return tiku_client_run_document(app, NULL);
}

int
tiku_client_run_document(const tiku_app_descriptor_t *app, const char *path)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    tiku_report_install(app->name, NULL);
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect(&ctx.remote,
                                 (app->name != NULL) ? app->name : "app",
                                 8000) != 0) {
        tiku_report_now("cannot connect to TikuDesktop");
        return 1;
    }
    return pump(app, &ctx, path, NULL);
}

int tiku_client_run_message(const tiku_app_descriptor_t *app,
                            const tiku_msg_t *message)
{
    client_ctx_t ctx;
    if (!app || !message) return 1;
    tiku_report_install(app->name, NULL);
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect(&ctx.remote, app->name ? app->name : "app", 8000)) {
        tiku_report_now("cannot connect to TikuDesktop");
        return 1;
    }
    return pump(app, &ctx, NULL, message);
}

int
tiku_client_run_fd(const tiku_app_descriptor_t *app, int fd)
{
    client_ctx_t ctx;

    if (app == NULL) {
        return 1;
    }
    tiku_report_install(app->name, NULL);
    memset(&ctx, 0, sizeof ctx);
    if (tiku_remote_connect_fd(&ctx.remote,
            (app->name != NULL) ? app->name : "app", fd) != 0) {
        tiku_report_now("cannot attach to desktop transport");
        return 1;
    }
    return pump(app, &ctx, NULL, NULL);
}
