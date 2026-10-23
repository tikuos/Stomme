/* TikuGrab driven without a desktop, a fake one answering its messages:
 * the blank window and the list it asks for; a window chosen and grabbed;
 * the picture shown, transparency over a checkerboard; Open, Show and
 * Save As; the delay counted down, beside the Deskbar's clock too, and
 * cancelled by a click there; a refusal said.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <unistd.h>
#include "../../apps/grab/tiku_grabview.c"

static tiku_workspace_t *desk;
static tiku_msg_t *asked;           /* the last request to the desktop */
static tiku_msg_t *tray;            /* the last tray item it set       */
static int requests, saves, trays;
static int refuse_send;
static char opened[TIKU_PATH_MAX];

static void settle(void)
{
    for (int i = 0; i < 8000; i++) {
        (void)tiku_work_collect();
        if (!tiku_work_busy()) return;
        usleep(1000);
    }
    assert(!"worker did not finish");
}

/** @brief The desktop's service, standing in: each request kept. */
static int host_send(void *ctx, const struct tiku_msg *m)
{
    (void)ctx;
    if (tiku_msg_what(m) == TIKU_MSG_TRAY) {    /* kept apart, counted */
        tiku_msg_free(tray);
        tray = tiku_msg_copy(m);
        trays++;
        return 1;
    }
    tiku_msg_free(asked);
    asked = tiku_msg_copy(m);
    requests++;
    return !refuse_send;
}

/** @brief The Save panel, as a counted question. */
static void host_save(void *ctx, const char *start, const char *name)
{
    (void)ctx;
    (void)start;
    assert(name && !strncmp(name, "tiku-window-", 12));
    saves++;
}

/** @brief open_path, remembered. */
static int host_open(void *ctx, const char *path)
{
    (void)ctx;
    snprintf(opened, sizeof opened, "%s", path);
    return 1;
}

/** @brief The countdown's tray text as last set; "" once taken away. */
static const char *tray_text(void)
{
    assert(tray && !strcmp(tiku_msg_find_string(tray, "id", 0), "delay"));
    return tiku_msg_find_string(tray, "text", 0);
}

/** @brief What the last request asked for, TIKU_GRAB_*. */
static int32_t last_what(void)
{
    int32_t what = -1;

    assert(asked && tiku_msg_what(asked) == TIKU_MSG_GRAB);
    (void)tiku_msg_find_int32(asked, "what", 0, &what);
    return what;
}

/** @brief The desktop's list: two windows. */
static void answer_list(tiku_grabview_t *c)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);

    assert(m && tiku_msg_add_int32(m, "list", 1));
    assert(tiku_msg_add_string(m, "title", "Tracker") &&
           tiku_msg_add_int64(m, "id", 5));
    assert(tiku_msg_add_string(m, "title", "Notes") &&
           tiku_msg_add_int64(m, "id", 3));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
}

/** @brief A key pressed in the window. */
static void press(tiku_grabview_t *c, unsigned key, unsigned mods)
{
    tiku_event_t ev = {.type = TIKU_EVENT_KEY_DOWN, .key = key,
                       .modifiers = mods};

    gb_event(c->window, &ev, c);
}

/** @brief The surface's pixel at @p x, @p y, its colour alone. */
static uint32_t at(const tiku_surface_t *s, int x, int y)
{
    return s->px[(size_t)y * s->w + x] & 0xffffffu;
}

/** @brief Before anything is grabbed: the list asked for, the preview
 *         blank, nothing said, nothing to open or save. */
static void empty_tests(tiku_grabview_t *c, const tiku_surface_t *s)
{
    gb_layout_t l = gb_layout(c, tiku_window_content(c->window));

    assert(requests == 1 && last_what() == TIKU_GRAB_LIST);
    tiku_workspace_draw(desk);
    assert(!c->status[0] && at(s, l.view.x + l.view.w / 2,
                              l.view.y + l.view.h / 2) ==
                            (TIKU_C_DOC & 0xffffffu));
    assert(!gb_enabled(c, GB_OPEN) && !gb_enabled(c, GB_SAVE));
}

/** @brief The list answered, a window chosen with the keys and grabbed:
 *         the request names it, and Grab waits for the answer. */
static void grab_tests(tiku_grabview_t *c)
{
    int64_t id = 0;

    answer_list(c);
    assert(c->ntarget == 3 && !strcmp(c->target[2].title, "Notes"));
    press(c, TIKU_KEY_DOWN, 0);
    press(c, TIKU_KEY_DOWN, 0);
    assert(c->sel == 2);
    press(c, 'g', TIKU_MOD_CMD);
    assert(last_what() == TIKU_GRAB_WINDOW && c->waiting &&
           !gb_enabled(c, GB_GRAB));
    assert(tiku_msg_find_int64(asked, "id", 0, &id) && id == 3);
}

/** @brief The desktop's answer, a picture with a transparent corner at
 *         @p shot: shown, the corner over the checkerboard, said. */
static void picture_tests(tiku_grabview_t *c, const tiku_surface_t *s,
                          const char *shot)
{
    gb_layout_t l = gb_layout(c, tiku_window_content(c->window));
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);
    static uint32_t px[40 * 30];
    int x0, y0;

    for (int i = 0; i < 40 * 30; i++)
        px[i] = (i % 40 < 6 && i / 40 < 4) ? 0 : 0xff2080d0u;
    assert(tiku_png_write(shot, px, 40, 30, 1) == 0);
    assert(m && tiku_msg_add_string(m, "path", shot) &&
           tiku_msg_add_int64(m, "request", c->request) &&
           tiku_msg_add_int32(m, "w", 40) && tiku_msg_add_int32(m, "h", 30) &&
           tiku_msg_add_int64(m, "size", 321));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
    settle();
    assert(!c->waiting && c->picture && c->picture->image.w == 40 &&
           strstr(c->status, "Saved as tiku-window-Notes-1.png, 321 B"));
    assert(last_what() == TIKU_GRAB_LIST);  /* the titles asked for again */
    tiku_workspace_draw(desk);
    x0 = l.view.x + 8 + (l.view.w - 16 - 40) / 2;
    y0 = l.view.y + 8 + (l.view.h - 16 - 30) / 2;
    assert(at(s, x0 + 20, y0 + 20) == 0x2080d0u);
    assert(at(s, x0 + 1, y0 + 1) == 0xffffffu ||
           at(s, x0 + 1, y0 + 1) == 0xe4e4e4u);   /* the checkerboard */
}

/** @brief Open, Show and Save As, on the picture at @p shot in @p dir. */
static void output_tests(tiku_grabview_t *c, const char *dir,
                         const char *shot)
{
    char copy[256];
    FILE *f;

    gb_action(c, GB_OPEN);
    assert(!strcmp(opened, shot));
    gb_action(c, GB_SHOW);
    assert(!strcmp(opened, dir));
    gb_action(c, GB_SAVE);
    assert(saves == 1);
    snprintf(copy, sizeof copy, "%s/kept.png", dir);
    tiku_grabview_picked(c, copy);
    settle();
    assert(strstr(c->status, "A copy is in kept.png"));
    f = fopen(copy, "rb");
    assert(f);
    fclose(f);
}

/** @brief Five seconds' delay: nothing asked until they have passed, the
 *         status counting down; then a refusal, said. */
static void delay_tests(tiku_grabview_t *c)
{
    tiku_msg_t *m;

    gb_action(c, GB_DELAY_5);
    c->sel = 0;
    tiku_grabview_tick(c, 10000000);
    requests = 0;
    gb_action(c, GB_GRAB);
    assert(requests == 0 && strstr(c->status, "Grabbing in 5"));
    assert(trays == 1 && !strcmp(tray_text(), "5") &&
           !strcmp(tiku_msg_find_string(tray, "icon", 0), "grab_timer"));
    tiku_grabview_tick(c, 12100000);
    assert(requests == 0 && strstr(c->status, "Grabbing in 3") &&
           !strcmp(tray_text(), "3"));
    tiku_grabview_tick(c, 12200000);
    assert(trays == 2);                 /* the same second: not resent */
    tiku_grabview_tick(c, 15000001);
    assert(requests == 1 && last_what() == TIKU_GRAB_DESK && c->waiting);
    assert(!strcmp(tray_text(), "") &&
           !strcmp(tiku_msg_find_string(tray, "icon", 0), ""));
    m = tiku_msg_new(TIKU_MSG_GRABBED);
    assert(m && tiku_msg_add_string(m, "error", "That window has closed."));
    assert(tiku_msg_add_int64(m, "request", c->request));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
    assert(!c->waiting && !strcmp(c->status, "That window has closed."));
}

static void safety_tests(tiku_grabview_t *c, const char *dir, const char *shot)
{
    struct stat before, after;
    char alias[512];
    assert(stat(shot, &before) == 0);
    assert(gb_copy(shot, shot) == 0 && stat(shot, &after) == 0);
    assert(before.st_size == after.st_size && before.st_ino == after.st_ino);
    snprintf(alias, sizeof alias, "%s/alias.png", dir);
    assert(link(shot, alias) == 0 && gb_copy(shot, alias) == 0);
    assert(stat(shot, &after) == 0 && before.st_size == after.st_size);
    /* invalid source never truncates target */
    assert(gb_copy(dir, shot) == -1);
    assert(stat(shot, &after) == 0 && before.st_size == after.st_size);

    answer_list(c); c->sel = 1; c->delay = 5;
    gb_action(c, GB_GRAB);
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_GRABBED);
    assert(m && tiku_msg_add_int32(m, "list", 1));
    tiku_grabview_message(c, m); tiku_msg_free(m);
    assert(c->sel == 0); /* pending window remains pinned after refresh */
    tiku_grabview_tick(c, c->due);
    int64_t id = 0, old = c->request;
    assert(last_what() == TIKU_GRAB_WINDOW &&
           tiku_msg_find_int64(asked, "id", 0, &id) && id == 5);
    tiku_grabview_tick(c, c->deadline);
    assert(!c->waiting && gb_enabled(c, GB_GRAB) &&
           strstr(c->status, "30 seconds"));
    c->delay = 0; gb_action(c, GB_GRAB);
    m = tiku_msg_new(TIKU_MSG_GRABBED);
    assert(m && tiku_msg_add_int64(m, "request", old) &&
           tiku_msg_add_string(m, "error", "stale reply"));
    tiku_grabview_message(c, m); tiku_msg_free(m);
    assert(c->waiting && !strstr(c->status, "stale"));
    press(c, TIKU_KEY_ESCAPE, 0);
    assert(!c->waiting && gb_enabled(c, GB_GRAB));
    c->delay = 5; gb_action(c, GB_GRAB);
    assert(c->due); press(c, TIKU_KEY_ESCAPE, 0); assert(!c->due);

    gb_layout_t l = gb_layout(c, (tiku_rect_t){0, 0, 180, 300});
    assert(l.list.x + l.list.w <= 180 && l.view.w >= 0 && l.view.h >= 0);
    for (int i = 0; i < c->dialog.count; i++) {
        tiku_rect_t r = c->dialog.control[i].rect;
        assert(r.x >= 0 && r.x + r.w <= 180);
    }
    c->ntarget = GB_TARGETS + 1;
    for (int i = 1; i < c->ntarget; i++) {
        c->target[i].id = i;
        snprintf(c->target[i].title, sizeof c->target[i].title, "Window %d", i);
    }
    for (int i = 0; i < GB_TARGETS; i++) press(c, TIKU_KEY_DOWN, 0);
    assert(c->sel == GB_TARGETS && c->top > 0);
    l = gb_layout(c, tiku_window_content(c->window));
    int old_top = c->top;
    tiku_event_t wheel = {.type = TIKU_EVENT_WHEEL, .x = l.list.x + 4,
                           .y = l.list.y + 4, .dy = 1};
    assert(gb_event(c->window, &wheel, c) && c->top < old_top);
    old_top = c->top;
    tiku_event_t arrow = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                           .x = l.bar.x + 3, .y = l.bar.y + 3};
    assert(gb_event(c->window, &arrow, c) && c->top == old_top - 1);
    arrow.type = TIKU_EVENT_POINTER_UP;
    assert(gb_event(c->window, &arrow, c) && !c->scroll.tracking);
    c->delay = 0; refuse_send = 1;
    gb_action(c, GB_GRAB);
    assert(!c->waiting && strstr(c->status, "could not be asked"));
    refuse_send = 0;
    /* Destroy with a preview outstanding: callbacks must not touch the host. */
    gb_preview(c, 23, 17);
}

/** @brief A countdown cancelled by a click on its tray icon: nothing asked
 *         of the desktop, the icon taken away, the cancel said as
 *         Cancel Capture says it. */
static void cancel_tests(tiku_grabview_t *c)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_TRAYHIT);

    gb_action(c, GB_GRAB);
    assert(c->due && !strcmp(tray_text(), "5"));
    assert(m && tiku_msg_add_string(m, "id", "delay"));
    tiku_grabview_message(c, m);
    tiku_msg_free(m);
    assert(!c->due && strstr(c->status, "Capture cancelled.") &&
           !strcmp(tray_text(), "") && requests == 1);
    tiku_grabview_tick(c, 30000000);
    assert(requests == 1 || last_what() == TIKU_GRAB_LIST);  /* no grab */
}

/** @brief Every group, in order, on one window. */
int main(int argc, char **argv)
{
    int dirty = 0;
    tiku_grabview_host_t host = { host_send, host_save, host_open, NULL };
    char dir[] = "/tmp/tikugrabapp-XXXXXX", shot[256], cmd[300];
    tiku_surface_t *s = tiku_surface_new(1000, 680, TIKU_C_BACKDROP);
    tiku_grabview_t *c;

    assert(mkdtemp(dir));
    desk = tiku_workspace_new(s);
    c = tiku_grabview_new(desk, &dirty, &host);
    assert(c);
    tiku_grabview_open(c);
    empty_tests(c, s);
    grab_tests(c);
    snprintf(shot, sizeof shot, "%s/tiku-window-Notes-1.png", dir);
    picture_tests(c, s, shot);
    output_tests(c, dir, shot);
    if (argc > 1) {
        tiku_workspace_draw(desk); settle(); tiku_workspace_draw(desk);
        assert(tiku_surface_png(s, argv[1]) == 0);
    }
    if (argc > 2) {
        tiku_rect_t old = c->window->frame;
        c->window->frame.w = 210; c->window->frame.h = 430;
        tiku_workspace_draw(desk); settle(); tiku_workspace_draw(desk);
        assert(tiku_surface_png(s, argv[2]) == 0);
        c->window->frame = old;
    }
    delay_tests(c);
    cancel_tests(c);
    safety_tests(c, dir, shot);
    tiku_grabview_destroy(c);
    settle();
    tiku_workspace_free(desk);
    tiku_surface_free(s);
    tiku_msg_free(asked);
    tiku_msg_free(tray);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
    (void)system(cmd);
    puts("TikuGrab: blank, list, grab a window, preview, open, show, save "
         "as, delay in the tray, cancelled there, refusal PASS");
    return 0;
}
