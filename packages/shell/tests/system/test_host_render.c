/* Native presentation and ordered input on an isolated X server only.
 * SPDX-License-Identifier: Apache-2.0 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "tiku_host.h"
#include "tiku_trk_splash.h"

/** @brief Monotonic timing for a bounded input wakeup test. */
static int64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}

/**
 * @brief Fullscreen on a server with no manager: the window is the screen.
 *
 * The private server runs no window manager, so this is the fallback
 * path -- the host sizes the window itself -- and the size must come
 * back through the same resize event a manager's answer would.
 */
static void fullscreen_presentation(Display *d, Window w, tiku_host_t *host)
{
    int screen = DefaultScreen(d);
    int want_w = DisplayWidth(d, screen), want_h = DisplayHeight(d, screen);
    int seen_w = 0, seen_h = 0;
    tiku_event_t ev;
    XWindowAttributes attrs;

    tiku_host_set_fullscreen(host, 1);
    for (int64_t until = now_us() + 500000; now_us() < until;) {
        while (tiku_host_poll(host, &ev)) {
            if (ev.type == TIKU_EVENT_RESIZE) { seen_w = ev.width; seen_h = ev.height; }
        }
        if (seen_w == want_w && seen_h == want_h) break;
        tiku_host_wait(host, 2000);
    }
    XSync(d, False);
    assert(XGetWindowAttributes(d, w, &attrs));
    assert(attrs.width == want_w && attrs.height == want_h);
    assert(attrs.x == 0 && attrs.y == 0);
    assert(seen_w == want_w && seen_h == want_h);
    printf("PASS: fullscreen makes the window the screen (%dx%d) and says so as a resize\n",
           want_w, want_h);
}

/** @brief Find the fixture's own window on the private server. */
static Window find_host(Display *d)
{
    Window root, parent, *children = NULL, found = None;
    unsigned n;
    assert(XQueryTree(d, DefaultRootWindow(d), &root, &parent, &children, &n));
    for (unsigned i = 0; i < n; i++) {
        char *name = NULL;
        if (XFetchName(d, children[i], &name) && name) {
            if (!strcmp(name, "Render test host")) found = children[i];
            XFree(name);
        }
    }
    if (children) XFree(children);
    assert(found); return found;
}

/** @brief Drain setup/clipboard protocol traffic with a hard deadline. */
static void settle(tiku_host_t *host)
{
    int64_t until = now_us()+50000;
    tiku_event_t ev;
    while (now_us() < until) {
        while (tiku_host_poll(host, &ev)) {}
        tiku_host_wait(host, 1000);
    }
}

/** @brief Send normalized native motion without moving the real pointer. */
static void motion(Display *d, Window w, int x)
{
    XEvent ev = {0};
    ev.xmotion = (XMotionEvent){.type=MotionNotify, .display=d,
        .window=w, .root=DefaultRootWindow(d), .x=x, .y=40,
        .same_screen=True};
    XSendEvent(d, w, False, PointerMotionMask, &ev);
}

/** @brief Coalescing cannot take motion from beyond a button release. */
static void ordered_input(Display *d, Window w, tiku_host_t *host)
{
    motion(d,w,10); motion(d,w,20);
    XEvent release = {0};
    release.xbutton = (XButtonEvent){.type=ButtonRelease, .display=d,
        .window=w, .root=DefaultRootWindow(d), .x=20, .y=40,
        .button=1, .same_screen=True};
    XSendEvent(d,w,False,ButtonReleaseMask,&release);
    motion(d,w,80); XSync(d,False);
    tiku_event_t ev;
    int count = 0;
    int64_t until = now_us()+500000;
    while (count < 3 && now_us() < until) {
        if (!tiku_host_poll(host,&ev)) { tiku_host_wait(host,1000); continue; }
        if (ev.type != TIKU_EVENT_POINTER_MOVE &&
            ev.type != TIKU_EVENT_POINTER_UP) continue;
        if (count == 0) assert(ev.type == TIKU_EVENT_POINTER_MOVE && ev.x == 20);
        if (count == 1) assert(ev.type == TIKU_EVENT_POINTER_UP && ev.x == 20);
        if (count == 2) assert(ev.type == TIKU_EVENT_POINTER_MOVE && ev.x == 80);
        count++;
    }
    assert(count == 3);
    puts("PASS: motion coalesces without crossing mouse release");
}

/** @brief Idle waits wake on incoming input, and leave that event queued. */
static void wakeup(const char *display, Window w, tiku_host_t *host)
{
    settle(host);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        usleep(30000);
        Display *d = XOpenDisplay(display);
        if (!d) _exit(2);
        motion(d,w,90); XSync(d,False); XCloseDisplay(d); _exit(0);
    }
    int64_t start = now_us();
    tiku_host_wait(host,500000);
    int64_t elapsed = now_us()-start;
    int status;
    assert(waitpid(child,&status,0) == child);
    assert(WIFEXITED(status) && !WEXITSTATUS(status));
    tiku_event_t ev;
    int found = 0;
    while (tiku_host_poll(host,&ev))
        if (ev.type == TIKU_EVENT_POINTER_MOVE && ev.x == 90) found = 1;
    assert(found && elapsed < 450000);
    printf("PASS: input wakes 500 ms wait after %lld us; event preserved\n",
            (long long)elapsed);
}

/** @brief Native pixels and payload reflect sparse updates and Expose repair. */
static void presentation(Display *d, Window w, tiku_host_t *host)
{
    tiku_surface_t *s = tiku_surface_new(320,240,0x123456);
    assert(s && !tiku_host_present(host,s));
    uint64_t frames, bytes, rects, initial;
    tiku_host_present_stats(host,&frames,&initial,&rects);
    assert(initial == 320u*240u*4u && frames == 1 && rects == 1);
    assert(!tiku_host_present(host,s));
    tiku_host_present_stats(host,&frames,&bytes,&rects);
    assert(bytes == initial && frames == 2 && rects == 1);
    s->px[1] = 0x987654;
    assert(!tiku_host_present(host,s));
    tiku_host_present_stats(host,NULL,&bytes,NULL);
    assert(bytes > initial && bytes-initial < initial/8);
    XSync(d,False);
    XImage *im = XGetImage(d,w,0,0,320,240,AllPlanes,ZPixmap); assert(im);
    assert(XGetPixel(im,1,0) == 0x987654 && XGetPixel(im,319,239) == 0x123456);
    XDestroyImage(im);
    XClearArea(d,w,0,0,320,240,True); XSync(d,False);
    /* Startup has not entered its event loop yet. Native damage must
     * restore the complete last frame without waiting for Expose handling. */
    im = XGetImage(d,w,0,0,320,240,AllPlanes,ZPixmap); assert(im);
    assert(XGetPixel(im,1,0) == 0x987654 &&
           XGetPixel(im,319,239) == 0x123456);
    XDestroyImage(im);
    settle(host);
    assert(!tiku_host_present(host,s));
    uint64_t repaired;
    tiku_host_present_stats(host,NULL,&repaired,NULL);
    assert(repaired == bytes+initial);
    XSync(d,False);
    im = XGetImage(d,w,0,0,320,240,AllPlanes,ZPixmap); assert(im);
    assert(XGetPixel(im,1,0) == 0x987654); XDestroyImage(im);
    tiku_surface_free(s);
    puts("PASS: sparse X11 upload, unchanged-frame skip and Expose repair");
}

/** @brief Boot stages stay complete while application event polling is paused. */
static void splash_frames(Display *d, Window w, tiku_host_t *host)
{
    tiku_surface_t *s = tiku_surface_new(320,240,0xffffff);
    tiku_trk_splash_t splash;
    const char *stages[] = {"storage", "fonts", "applications"};
    assert(s);
    tiku_trk_splash_begin(&splash,1,1000000);
    for (int stage = 0; stage < 3; stage++) {
        int64_t now = 1000000 + stage * TIKU_TRK_SPLASH_STEP_US;
        tiku_trk_splash_say(&splash,stages[stage],now);
        tiku_trk_splash_draw(&splash,s,(tiku_rect_t){0,0,320,240},now);
        assert(!tiku_host_present(host,s));
        XSync(d,False);
        /* Simulate a window manager exposing the screen during boot.
         * No host_poll/settle until ALL startup stages are checked. */
        XClearArea(d,w,0,0,320,240,True); XSync(d,False);
        XImage *im = XGetImage(d,w,0,0,320,240,AllPlanes,ZPixmap);
        assert(im);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++)
                assert(XGetPixel(im,x,y) == (s->px[y*320+x] & 0xffffffu));
        XDestroyImage(im);
    }
    tiku_surface_free(s);
    puts("PASS: complete splash logo, black background and text survive boot damage");
}

/** @brief Resizing replaces the retained frame without exposing unpainted data. */
static void resized_presentation(Display *d, Window w, tiku_host_t *host)
{
    XResizeWindow(d,w,400,260); XSync(d,False); settle(host);
    tiku_surface_t *s = tiku_surface_new(400,260,0x543210);
    assert(s && !tiku_host_present(host,s));
    XSync(d,False);
    XClearArea(d,w,0,0,400,260,True); XSync(d,False);
    XImage *im = XGetImage(d,w,0,0,400,260,AllPlanes,ZPixmap);
    assert(im && XGetPixel(im,399,259) == 0x543210);
    XDestroyImage(im); tiku_surface_free(s);
    puts("PASS: resized window retains the complete new frame");
}

int main(void)
{
    const char *display = getenv("TIKU_CLIPBOARD_TEST_DISPLAY");
    if (!display || !*display) {
        puts("SKIP: run make test-host-render for an isolated native display");
        return 0;
    }
    setenv("DISPLAY",display,1); setenv("TIKU_SCALE","1",1);
    Display *d = XOpenDisplay(display); assert(d);
    tiku_host_t *host = tiku_host_open(320,240,"Render test host");
    assert(host); settle(host);
    Window w = find_host(d);
    XWindowAttributes attrs;
    assert(XGetWindowAttributes(d,w,&attrs));
    assert(attrs.map_state == IsUnmapped);
    puts("PASS: native window stays hidden until its first complete frame");
    presentation(d,w,host);
    ordered_input(d,w,host);
    wakeup(display,w,host);
    splash_frames(d,w,host);
    resized_presentation(d,w,host);
    fullscreen_presentation(d,w,host);
    tiku_host_close(host); XCloseDisplay(d);
    return 0;
}
