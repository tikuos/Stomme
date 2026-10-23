/* Real Editor process over the desktop transport, including large resizes.
 * SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <sys/socket.h>
#include "../../apps/editor/tiku_editor_private.h"
#include "tiku_remote.h"

/** Wait for a complete frame of the requested size without hiding disconnects. */
static int await_size(tiku_remote_listener_t *desk, int w, int h)
{
    for (int i = 0; i < 1000; i++) {
        tiku_remote_poll(desk);
        tiku_remote_session_t *s = &desk->session[0];
        if (!s->used) return 0;
        if (s->win[0].fw == w && s->win[0].fh == h &&
            tiku_remote_pixels(&s->win[0])) return 1;
        usleep(2000);
    }
    return 0;
}

/** An isolated client runs exactly the application shipped to the desktop. */
int main(void)
{
    int pair[2], status = 0, ok = 1;
    char reports[] = "/tmp/tiku-editor-resize-XXXXXX";
    assert(mkdtemp(reports));
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        close(pair[0]);
        setenv("TIKU_TRK_REPORTS", reports, 1);
        _exit(tiku_client_run_fd(&tiku_edit_app, pair[1]));
    }
    close(pair[1]);
    tiku_remote_listener_t desk = {0}; desk.fd = -1;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) desk.session[i].fd = -1;
    assert(!tiku_remote_adopt(&desk, pair[0]));
    ok = await_size(&desk, 560, 400);
    tiku_window_t fake = {0}; desk.session[0].win[0].window = &fake;
    const int sizes[][2] = {{1024, 768}, {1280, 800}, {1920, 1080},
        {480, 260}, {2560, 1440}, {3840, 2160}, {780, 530}};
    for (unsigned i = 0; ok && i < sizeof sizes / sizeof sizes[0]; i++) {
        tiku_event_t e = {.type = TIKU_EVENT_RESIZE,
            .width = sizes[i][0], .height = sizes[i][1]};
        tiku_remote_event(&desk, &fake, &e);
        ok = await_size(&desk, e.width, e.height);
        printf("[%s] Editor resize %d x %d keeps the session alive\n",
               ok ? "PASS" : "FAIL", e.width, e.height);
    }
    if (ok) {
        tiku_event_t e = {.type = TIKU_EVENT_KEY_DOWN,
            .key = 'w', .modifiers = TIKU_MOD_CMD};
        tiku_remote_event(&desk, &fake, &e);
    }
    for (int i = 0; i < 1000; i++) {
        tiku_remote_poll(&desk);
        if (waitpid(child, &status, WNOHANG) == child) { child = 0; break; }
        usleep(2000);
    }
    if (child) { kill(child, SIGKILL); waitpid(child, &status, 0); ok = 0; }
    tiku_remote_shutdown(&desk);
    return ok && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}
