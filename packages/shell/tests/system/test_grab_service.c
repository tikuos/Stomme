/* The desktop's grab service without a desktop: the windows listed front
 * first and the asker's left out; the desk drawn again without the
 * asker's windows, saved, and drawn as it was; a covered window drawn
 * alone on transparency; a window that has gone, refused; Take
 * Screenshot through the same writer.  SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include <stdatomic.h>
#include <dirent.h>
#include "../../system/desktop/tiku_trk_desktop_grab.c"

#define RED  0xffd02020u
#define BLUE 0xff2040c0u

static tiku_window_t *asker;
static int draws, hidden_when_drawn;
static tiku_msg_t *reply;

/** @brief The painter, standing in: the windows drawn onto the frame,
 *         and whether the asker's was left out while they were. */
void draw_all(desktop_t *app)
{
    draws++;
    hidden_when_drawn = asker->hidden;
    tiku_workspace_draw(app->desk);
}

/** @brief A full frame is the flag, as the other system tests keep it. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

/** @brief What session 0 was told, kept for the test to read. */
int tiku_remote_tell(tiku_remote_listener_t *l, int session,
                     const tiku_msg_t *m)
{
    (void)l;
    assert(session == 0);
    tiku_msg_free(reply);
    reply = tiku_msg_copy(m);
    return 1;
}

/** @brief A window's content, one colour: its context. */
static void fill(tiku_window_t *w, tiku_surface_t *s, tiku_rect_t r,
                 void *ctx)
{
    (void)w;
    tiku_fill(s, r, *(const tiku_rgb_t *)ctx);
}

/** @brief Ask what @p what, of window @p id, and wait for the answer. */
static const tiku_msg_t *ask(desktop_t *app, int what, int64_t id)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRAB);

    tiku_msg_free(reply);
    reply = NULL;
    assert(m && tiku_msg_add_int32(m, "what", what) &&
           tiku_msg_add_int64(m, "request", 1234) &&
           tiku_msg_add_int64(m, "id", id));
    desktop_grab(app, 0, m);
    tiku_msg_free(m);
    for (int i = 0; i < 5000 && !reply; i++) {
        (void)tiku_work_collect();
        usleep(1000);
    }
    assert(reply && tiku_msg_what(reply) == TIKU_MSG_GRABBED);
    if (what != TIKU_GRAB_LIST) {
        int64_t request = 0;
        assert(tiku_msg_find_int64(reply, "request", 0, &request) &&
               request == 1234);
    }
    return reply;
}

/** @brief The picture an answer names, read back. */
static void picture(const tiku_msg_t *m, tiku_image_t *im)
{
    const char *path = tiku_msg_find_string(m, "path", 0);
    int64_t size = 0;

    assert(path && strstr(path, "/Screenshots/tiku-"));
    assert(tiku_msg_find_int64(m, "size", 0, &size) && size > 0);
    assert(tiku_image_load(path, im) == TIKU_IMG_OK);
}

/** @brief A desk of three windows, the asker's at the back, and a session
 *         that opened it; the other two into @p under and @p over. */
static desktop_t *setup(tiku_window_t **under, tiku_window_t **over)
{
    static const tiku_rgb_t red = RED, blue = BLUE, back = 0xff808080u;
    desktop_t *app = calloc(1, sizeof *app);

    assert(app);
    app->surf = tiku_surface_new(300, 200, TIKU_C_BACKDROP);
    app->desk = tiku_workspace_new(app->surf);
    asker = tiku_workspace_open(app->desk, "TikuGrab",
        (tiku_rect_t){10, 10, 120, 80}, fill, (void *)&back, NULL);
    *under = tiku_workspace_open(app->desk, "Notes: a list",
        (tiku_rect_t){60, 40, 150, 110}, fill, (void *)&red, NULL);
    *over = tiku_workspace_open(app->desk, "Tracker",
        (tiku_rect_t){140, 90, 150, 100}, fill, (void *)&blue, NULL);
    app->remote.session[0].used = 1;
    app->remote.session[0].generation = 7;
    app->remote.session[0].win[0].window = asker;
    app->remote.session[0].win[0].opened = 1;
    return app;
}

/** @brief The list: front first, each window's generation its id, the
 *         asker's own left out. */
static void list_test(desktop_t *app, const tiku_window_t *over)
{
    const char *title;
    int64_t id;

    ask(app, TIKU_GRAB_LIST, 0);
    assert(tiku_msg_find_int32(reply, "list", 0, &(int32_t){0}));
    assert((title = tiku_msg_find_string(reply, "title", 0)) &&
           !strcmp(title, "Tracker") && tiku_msg_find_int64(reply, "id", 0, &id)
           && id == (int64_t)over->generation);
    assert((title = tiku_msg_find_string(reply, "title", 1)) &&
           !strcmp(title, "Notes: a list"));
    assert(!tiku_msg_find_string(reply, "title", 2));  /* not the asker's */
}

/** @brief The desk drawn again without the asker, saved, and to be drawn
 *         as it was. */
static void desk_test(desktop_t *app)
{
    tiku_image_t im = { 0, 0, NULL };

    picture(ask(app, TIKU_GRAB_DESK, 0), &im);
    assert(draws == 1 && hidden_when_drawn == 1 && !asker->hidden);
    assert(im.w == 300 && im.h == 200 &&
           (im.px[20 * 300 + 20] & 0xffffffu) ==
           (TIKU_C_BACKDROP & 0xffffffu));   /* the asker is not in it */
    assert(app->dirty);                      /* and the desk drawn again */
    tiku_image_free(&im);
}

/** @brief A covered window alone: its own colour where the other covers
 *         it, and the band beside its tab transparent. */
static void window_test(desktop_t *app, const tiku_window_t *under)
{
    tiku_image_t im = { 0, 0, NULL };
    int cx = 145 - 60, cy = 100 - 40;       /* where the other covers it */

    picture(ask(app, TIKU_GRAB_WINDOW, (int64_t)under->generation), &im);
    assert(im.w == 150 && im.h == 110);
    assert(im.px[(size_t)cy * im.w + cx] == RED);
    assert((im.px[3 * im.w + im.w - 3] >> 24) == 0);  /* beside the tab */
    tiku_image_free(&im);
}

/** @brief A window gone, refused; Take Screenshot through the same
 *         writer, the status line saying where it went. */
static void refusal_test(desktop_t *app)
{
    ask(app, TIKU_GRAB_WINDOW, 999999);
    assert(!strcmp(tiku_msg_find_string(reply, "error", 0),
                   "That window has closed."));
    app->status[0] = '\0';
    desktop_shot_desk(app);
    for (int i = 0; i < 5000 && !app->status[0]; i++) {
        (void)tiku_work_collect();
        usleep(1000);
    }
    assert(strstr(app->status,
                  "screenshot saved to Screenshots/tiku-screenshot-"));
}

static atomic_int release_queue;
static void hold_queue(void *ctx)
{
    (void)ctx;
    for (int i = 0; i < 5000 && !atomic_load(&release_queue); i++) usleep(1000);
    assert(atomic_load(&release_queue));
}

static void queued_names_test(desktop_t *app)
{
    /* Hold the writer queue so neither capture can create its file yet. */
    tiku_work_job_t blocker = {.run = hold_queue, .key = "grab"};
    assert(tiku_work_post(&blocker) == 0);
    uint32_t *a = malloc(sizeof *a), *b = malloc(sizeof *b);
    assert(a && b); *a = RED; *b = BLUE;
    assert(desktop_shot_save(app, -1, "queued", a, 1, 1, 0) == 0);
    assert(desktop_shot_save(app, -1, "queued", b, 1, 1, 0) == 0);
    atomic_store(&release_queue, 1);
    for (int i = 0; i < 5000 && tiku_work_busy(); i++) {
        tiku_work_collect(); usleep(1000);
    }
    assert(!tiku_work_busy());
    char path[TIKU_PATH_MAX];
    snprintf(path, sizeof path, "%s/Screenshots", getenv("TIKU_DESKTOP"));
    DIR *d = opendir(path); struct dirent *e; int count = 0, red = 0, blue = 0;
    assert(d);
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "tiku-queued-", 12)) continue;
        snprintf(path, sizeof path, "%s/Screenshots/%s",
                 getenv("TIKU_DESKTOP"), e->d_name);
        tiku_image_t im = {0};
        assert(tiku_image_load(path, &im) == TIKU_IMG_OK &&
               im.w == 1 && im.h == 1);
        red += im.px[0] == RED; blue += im.px[0] == BLUE; count++;
        tiku_image_free(&im);
    }
    closedir(d); assert(count == 2 && red == 1 && blue == 1);
}

/** @brief Every group, in a scratch Desktop of its own. */
int main(void)
{
    char dir[] = "/tmp/tikugrab-XXXXXX", cmd[128];
    tiku_window_t *under, *over;
    desktop_t *app;

    assert(mkdtemp(dir) && !setenv("TIKU_DESKTOP", dir, 1));
    app = setup(&under, &over);
    list_test(app, over);
    desk_test(app);
    window_test(app, under);
    refusal_test(app);
    queued_names_test(app);
    tiku_msg_free(reply);
    tiku_workspace_free(app->desk);
    tiku_surface_free(app->surf);
    free(app);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)system(cmd);
    puts("PASS: grab service lists, leaves the asker out, draws a window "
         "alone on transparency, refuses a window gone, saves the desk");
    return 0;
}
