/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_desktop_grab.c - screenshots: the desk's own Take Screenshot, and
 * the pictures an application asks for over its session.
 *
 * An application runs in a process of its own and cannot read the desk, so
 * it asks: for the list of windows, for the desk drawn again without its own
 * windows, or for one window drawn alone with transparent edges.  Every
 * picture is compressed on a worker into Desktop/Screenshots; the asker
 * never names a path.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_image.h"
#include "tiku_trail.h"
#include "tiku_trk_desktop_private.h"
#include "tiku_work.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/** @brief A picture on its way to a file, and who to tell. */
typedef struct {
    desktop_t *app;
    int session;                    /* who asked; -1, the desktop itself */
    uint64_t generation;            /* that session's, when it asked     */
    int64_t request;
    uint32_t *px;
    int w, h, alpha, ok;
    long size;
    char path[TIKU_PATH_MAX];
} grab_shot_t;

/**
 * @brief @p title as part of a file name: letters, digits, dots and
 *        underscores kept, every other run of characters one dash, at most
 *        40 characters; "window" when nothing is left.
 */
static void grab_clean(const char *title, char *out, size_t max)
{
    size_t n = 0;

    for (; *title && n + 1 < max && n < 40; title++) {
        char ch = *title;
        int keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '.' || ch == '_';

        if (keep) out[n++] = ch;
        else if (n > 0 && out[n - 1] != '-') out[n++] = '-';
    }
    while (n > 0 && out[n - 1] == '-') n--;
    out[n] = '\0';
    if (!n) snprintf(out, max, "window");
}

/**
 * @brief Where a screenshot of @p what goes: Desktop/Screenshots, made if
 *        need be, "tiku-" @p what and the time, a number after it when a
 *        shot of the same second is there already.
 * @return 0; -1 when the folder cannot be made, the name does not fit, or
 *         every numbered name of that second is taken.
 */
int desktop_shot_path(const char *what, char *out, size_t max)
{
    char desk[TIKU_PATH_MAX], dir[TIKU_PATH_MAX];
    struct tm when;
    struct stat st;
    time_t now = time(NULL);
    int n;

    (void)tiku_desktop_dir(desk, sizeof desk);
    (void)tiku_state_mkparents(desk);
    (void)mkdir(desk, 0755);
    n = snprintf(dir, sizeof dir, "%s/Screenshots", desk);
    if (n < 0 || (size_t)n >= sizeof dir) return -1;  /* too long */
    (void)mkdir(dir, 0755);
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    (void)localtime_r(&now, &when);
    for (int k = 1; k < 100; k++) {
        n = snprintf(out, max, "%s/tiku-%s-%04d%02d%02d-%02d%02d%02d%s%.0d"
                     ".png", dir, what, when.tm_year + 1900, when.tm_mon + 1,
                     when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec,
                     k > 1 ? "-" : "", k > 1 ? k : 0);
        if (n < 0 || (size_t)n >= max) return -1;
        if (lstat(out, &st) != 0) return errno == ENOENT ? 0 : -1;
    }
    return -1;
}

/** @brief Tell session @p session how its asking went: the file made, or
 *         why none was. */
static void grab_tell(desktop_t *app, int session, const grab_shot_t *s,
                      const char *why, int64_t request)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);

    if (!m) return;
    (void)tiku_msg_add_int64(m, "request", request);
    if (why) {
        (void)tiku_msg_add_string(m, "error", why);
    } else {
        (void)tiku_msg_add_string(m, "path", s->path);
        (void)tiku_msg_add_int32(m, "w", s->w);
        (void)tiku_msg_add_int32(m, "h", s->h);
        (void)tiku_msg_add_int64(m, "size", s->size);
    }
    (void)tiku_remote_tell(&app->remote, session, m);
    tiku_msg_free(m);
}

/** @brief On a worker: compress the picture into its file. */
static void grab_run(void *ctx)
{
    grab_shot_t *s = ctx;
    struct stat st;

    s->ok = tiku_png_write(s->path, s->px, s->w, s->h, s->alpha) == 0;
    s->size = s->ok && stat(s->path, &st) == 0 ? (long)st.st_size : 0;
}

/** @brief On the loop: whoever asked is told -- a session that is still
 *         the one that asked, or the desktop's own status line. */
static void grab_done(void *ctx)
{
    grab_shot_t *s = ctx;
    desktop_t *app = s->app;
    const char *leaf = strrchr(s->path, '/');

    if (s->session >= 0) {
        if (s->session < TIKU_REMOTE_SESSIONS &&
            app->remote.session[s->session].used &&
            app->remote.session[s->session].generation == s->generation)
            grab_tell(app, s->session, s, s->ok ? NULL
                      : "The screenshot could not be written.", s->request);
        return;
    }
    snprintf(app->status, sizeof app->status, s->ok ?
             "screenshot saved to Screenshots/%s" :
             "could not save the screenshot", leaf ? leaf + 1 : s->path);
    if (s->ok) tiku_trail("screenshot %s", s->path);
    desktop_damage_all(app);
}

/**
 * @brief Free the picture, after done or when the job is dropped; a file
 *        that was not written is removed.
 */
static void grab_free(void *ctx)
{
    grab_shot_t *s = ctx;

    if (!s->ok) unlink(s->path);     /* the reserved, failed output only */
    free(s->px);
    free(s);
}

/**
 * @brief @p px (w by h, taken over) written as the screenshot @p what, on
 *        a worker; @p session told the outcome once the worker is done,
 *        or -1 for the desktop's own status line.  @return 0 when it was
 *        posted.
 */
static int grab_save(desktop_t *app, int session, const char *what,
                     uint32_t *px, int w, int h, int alpha, int64_t request)
{
    grab_shot_t *s = calloc(1, sizeof *s);
    tiku_work_job_t job;

    if (!s || !px || desktop_shot_path(what, s->path, sizeof s->path) != 0) {
        free(s);
        free(px);
        return -1;
    }
    /* The name is created (O_EXCL) before the job is queued, so two
     * pending captures cannot take one name and no existing file is
     * truncated. */
    int fd = open(s->path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) { free(s); free(px); return -1; }
    if (close(fd) != 0) { unlink(s->path); free(s); free(px); return -1; }
    s->app = app;
    s->session = session;
    s->generation = session >= 0 ? app->remote.session[session].generation
                                 : 0;
    s->px = px;
    s->w = w;
    s->h = h;
    s->alpha = alpha;
    s->request = request;
    memset(&job, 0, sizeof job);
    job.run = grab_run;
    job.done = grab_done;
    job.free = grab_free;
    job.ctx = s;
    snprintf(job.key, sizeof job.key, "grab");
    if (tiku_work_post(&job) != 0) {
        unlink(s->path);
        grab_free(s);
        return -1;
    }
    return 0;
}

/**
 * @brief Save @p px, @p w by @p h and taken over, as the screenshot @p what
 *        on a worker; @p alpha keeps its transparency.  Session @p session
 *        is told the outcome, or with -1 the desktop's status line.
 * @return 0 when the job was posted.
 */
int desktop_shot_save(desktop_t *app, int session, const char *what,
                      uint32_t *px, int w, int h, int alpha)
{
    return grab_save(app, session, what, px, w, h, alpha, 0);
}

/** @brief @p s's native pixels copied out, or NULL out of memory; its
 *         native size into @p w and @p h. */
static uint32_t *grab_copy(const tiku_surface_t *s, int *w, int *h)
{
    int sc = s->scale > 0 ? s->scale : 1;
    size_t n = (size_t)s->w * sc * (size_t)s->h * sc;
    uint32_t *px = malloc(n * sizeof *px);

    if (px) memcpy(px, s->px, n * sizeof *px);
    *w = s->w * sc;
    *h = s->h * sc;
    return px;
}

/** @brief Whether @p w is one of session @p session's windows. */
static int grab_asker(const desktop_t *app, int session,
                      const tiku_window_t *w)
{
    const tiku_remote_session_t *s = &app->remote.session[session];

    for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++)
        if (s->win[j].window == w) return 1;
    return 0;
}

/**
 * @brief The whole desk drawn again without the asker's windows, and
 *        copied; the windows are shown again, and the next frame redraws
 *        the desk before it is presented.
 */
static uint32_t *grab_desk(desktop_t *app, int session, int *w, int *h)
{
    tiku_remote_session_t *s = &app->remote.session[session];
    uint32_t *px;

    for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++)
        if (s->win[j].window && s->win[j].opened)
            s->win[j].window->hidden = 1;
    app->dirty = 1;                     /* the whole frame, not its damage */
    draw_all(app);
    px = grab_copy(app->surf, w, h);
    for (int j = 0; j < TIKU_REMOTE_WINDOWS; j++)
        if (s->win[j].window && s->win[j].opened)
            s->win[j].window->hidden = 0;
    desktop_damage_all(app);
    return px;
}

/** @brief A surface the desk's size and scale, all @p bg. */
static tiku_surface_t *grab_surface(const tiku_surface_t *like, tiku_rgb_t bg)
{
    tiku_surface_t *s = tiku_surface_new(like->w, like->h, bg);

    if (s && like->scale > 1 && tiku_surface_rescale(s, like->scale, bg) != 0) {
        tiku_surface_free(s);
        return NULL;
    }
    return s;
}

/** @brief One pixel from its two drawings, over black @p b and over
 *         white @p l: how much of it covers what is behind, and in what
 *         colour.  @return it, 0xAARRGGBB. */
static uint32_t grab_unmix(uint32_t b, uint32_t l)
{
    int a = 255 - (int)(((l >> 8) & 255u) - ((b >> 8) & 255u));
    uint32_t c = 0;

    if (b == l) return 0xff000000u | b;
    if (a <= 0) return 0;
    if (a > 255) a = 255;
    for (int sh = 0; sh <= 16; sh += 8) {
        int v = (int)((b >> sh) & 255u) * 255 / a;
        c |= (uint32_t)(v > 255 ? 255 : v) << sh;
    }
    return (uint32_t)a << 24 | c;
}

/**
 * @brief Window @p win alone, cut to its frame, its corners and the band
 *        beside its tab transparent: drawn once on black and once on
 *        white, the two telling each pixel's cover and its colour.
 */
static uint32_t *grab_window(desktop_t *app, tiku_window_t *win, int *w,
                             int *h)
{
    tiku_surface_t *dark = grab_surface(app->surf, 0xff000000u);
    tiku_surface_t *lit = grab_surface(app->surf, 0xffffffffu);
    int sc = app->surf->scale > 0 ? app->surf->scale : 1;
    tiku_rect_t f = win->frame;
    uint32_t *px = NULL;

    if (f.x < 0) { f.w += f.x; f.x = 0; }
    if (f.y < 0) { f.h += f.y; f.y = 0; }
    if (f.x + f.w > app->surf->w) f.w = app->surf->w - f.x;
    if (f.y + f.h > app->surf->h) f.h = app->surf->h - f.y;
    if (dark && lit && f.w > 0 && f.h > 0 &&
        (px = malloc((size_t)f.w * sc * (size_t)f.h * sc * sizeof *px))) {
        size_t nw = (size_t)app->surf->w * sc;

        tiku_workspace_draw_window(app->desk, win, dark);
        tiku_workspace_draw_window(app->desk, win, lit);
        *w = f.w * sc;
        *h = f.h * sc;
        for (long i = 0; i < (long)*w * *h; i++) {
            size_t at = (size_t)(f.y * sc + i / *w) * nw + f.x * sc + i % *w;

            px[i] = grab_unmix(dark->px[at] & 0xffffffu,
                               lit->px[at] & 0xffffffu);
        }
    }
    tiku_surface_free(dark);
    tiku_surface_free(lit);
    return px;
}

/**
 * @brief Send session @p session the open windows, front first, without its
 *        own, the minimized and the plain ones: each one's title, and its
 *        generation, the id a picture of it is asked by.
 */
static void grab_list(desktop_t *app, int session)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);

    if (!m) return;
    (void)tiku_msg_add_int32(m, "list", 1);
    for (int i = tiku_workspace_count(app->desk) - 1; i >= 0; i--) {
        tiku_window_t *w = tiku_workspace_at(app->desk, i);

        if (!w || !w->open || w->minimized || w->plain ||
            grab_asker(app, session, w)) continue;
        (void)tiku_msg_add_string(m, "title", w->title);
        (void)tiku_msg_add_int64(m, "id", (int64_t)w->generation);
    }
    (void)tiku_remote_tell(&app->remote, session, m);
    tiku_msg_free(m);
}

/** @brief The open window whose generation is @p id, or NULL. */
static tiku_window_t *grab_find(desktop_t *app, int64_t id)
{
    for (int i = 0; i < tiku_workspace_count(app->desk); i++) {
        tiku_window_t *w = tiku_workspace_at(app->desk, i);

        if (w && w->open && (int64_t)w->generation == id) return w;
    }
    return NULL;
}

/** @brief Take Screenshot: the frame just shown, written on a worker as
 *         tiku-screenshot-..., the status line saying where it went. */
void desktop_shot_desk(desktop_t *app)
{
    int w = 0, h = 0;
    uint32_t *px = grab_copy(app->surf, &w, &h);

    if (desktop_shot_save(app, -1, "screenshot", px, w, h, 0) != 0) {
        snprintf(app->status, sizeof app->status,
                 "could not save the screenshot");
        desktop_damage_all(app);
    }
}

/** @brief Session @p session's TIKU_MSG_GRAB answered: the windows there
 *         are, or a picture of the desk or of one window, told back once
 *         it is written. */
void desktop_grab(desktop_t *app, int session, const tiku_msg_t *msg)
{
    int32_t what = 0;
    int64_t id = 0, request = 0;
    int w = 0, h = 0;
    uint32_t *px = NULL;
    tiku_window_t *win;
    char name[64];

    if (session < 0 || session >= TIKU_REMOTE_SESSIONS ||
        !app->remote.session[session].used) return;
    (void)tiku_msg_find_int32(msg, "what", 0, &what);
    (void)tiku_msg_find_int64(msg, "id", 0, &id);
    (void)tiku_msg_find_int64(msg, "request", 0, &request);
    if (what == TIKU_GRAB_LIST) {
        grab_list(app, session);
        return;
    }
    if (what == TIKU_GRAB_DESK) {
        px = grab_desk(app, session, &w, &h);
        snprintf(name, sizeof name, "desk");
    } else if (what == TIKU_GRAB_WINDOW && (win = grab_find(app, id))) {
        px = grab_window(app, win, &w, &h);
        memcpy(name, "window-", 7);
        grab_clean(win->title, name + 7, sizeof name - 7);
    } else {
        grab_tell(app, session, NULL, "That window has closed.", request);
        return;
    }
    if (!px || grab_save(app, session, name, px, w, h,
                         what == TIKU_GRAB_WINDOW, request) != 0)
        grab_tell(app, session, NULL, "The screenshot could not be taken.",
                  request);
}
