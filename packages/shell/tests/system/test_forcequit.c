/* Force Quit safety, native controls, and stopped-peer backpressure. */
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "../../system/desktop/tiku_trk_desktop_forcequit.c"
/** @brief The painter is not linked: a full frame is the flag, as before. */
void desktop_damage_all(desktop_t *app) { app->dirty = 1; }

static long expected_pid;
void tiku_trk_launch_expect_stop(pid_t pid) { expected_pid = pid; }
void close_window(desktop_t *app, tiku_window_t *w)
{ tiku_workspace_close(app->desk, w); }

/** Exercise actual press/release handling, not just action functions. */
static void click(struct desktop_forcequit *p, tiku_rect_t r)
{
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                      .x = r.x + 6, .y = r.y + 6};
    tiku_window_send(p->window, &e);
    e.type = TIKU_EVENT_POINTER_UP;
    tiku_window_send(p->window, &e);
}

/** Connect from the child: socketpair-before-fork authenticates the parent. */
static pid_t peer(int listen_fd, struct sockaddr_un *address, int *accepted)
{
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        alarm(15);
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0 || connect(fd, (struct sockaddr *)address, sizeof *address))
            _exit(2);
        /* never read or respond: a frozen application. */
        for (;;) pause();
    }
    *accepted = accept(listen_fd, NULL, NULL);
    assert(*accepted >= 0);
    return pid;
}

/** A full socket cannot block input, and pending messages stay framed. */
static void backpressure(void)
{
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    tiku_remote_listener_t *l = calloc(1, sizeof *l); assert(l);
    l->fd = -1;
    assert(!tiku_remote_adopt(l, pair[0]));
    assert(tiku_remote_process_handle(&l->session[0], NULL) < 0);
    int flags = fcntl(pair[1], F_GETFL, 0);
    assert(!fcntl(pair[1], F_SETFL, flags | O_NONBLOCK));
    tiku_window_t window = {.open = 1};
    l->session[0].win[0].window = &window;
    l->session[0].win[0].session = &l->session[0];
    l->session[0].win[0].opened = 1;
    l->session[0].win[0].win_id = 17;
    tiku_event_t e = {.type = TIKU_EVENT_POINTER_MOVE, .x = 123};
    for (int i = 0; i < 20000; i++) tiku_remote_event(l, &window, &e);
    assert(l->session[0].out_size > 0);
    assert(l->session[0].out_size <= 512u * 1024u);
    /* Draining in arbitrary chunks still yields complete protocol frames:
     * what the socket holds (a session's is roomy) and the queue behind. */
    unsigned char *bytes = malloc(4u * 1024u * 1024u); assert(bytes);
    size_t total = 0;
    for (int i = 0; i < 5000 && total + 8192 <= 4u * 1024u * 1024u; i++) {
        ssize_t n = read(pair[1], bytes + total, 8192);
        if (n > 0) total += (size_t)n;
        tiku_remote_poll(l);
        if (n <= 0 && !l->session[0].out_size) break;  /* all through */
    }
    size_t frame = 8 + 4 + sizeof e;
    assert(!l->session[0].out_size && total && !(total % frame));
    for (size_t i = 0; i < total; i += frame) {
        uint32_t type, n, id;
        memcpy(&type, bytes + i, 4); memcpy(&n, bytes + i + 4, 4);
        memcpy(&id, bytes + i + 8, 4);
        assert(type == TIKU_RMSG_EVENT && n == 4 + sizeof e && id == 17);
    }
    close(pair[1]); tiku_remote_poll(l); free(bytes); free(l);
}

int main(void)
{
    alarm(20);
    backpressure();
    assert(tiku_remote_process_handle(NULL, NULL) < 0);
    assert(tiku_remote_process_kill(-1) < 0);
    desktop_t *app = calloc(1, sizeof *app); assert(app);
    app->surf = tiku_surface_new(1024, 768, TIKU_C_PANEL);
    app->desk = tiku_workspace_new(app->surf); app->remote.fd = -1;
    char dir[] = "/tmp/tiku-forcequit-XXXXXX"; assert(mkdtemp(dir));
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s/peer", dir);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0); assert(listener >= 0);
    assert(!bind(listener, (struct sockaddr *)&addr, sizeof addr));
    assert(!listen(listener, 2));
    int fd[2]; pid_t child[2];
    for (int i = 0; i < 2; i++) {
        child[i] = peer(listener, &addr, &fd[i]);
        assert(!tiku_remote_adopt(&app->remote, fd[i]));
        snprintf(app->remote.session[i].name, 32, "Frozen app %d", i + 1);
    }
    close(listener); unlink(addr.sun_path); rmdir(dir);
    tiku_event_t key = {.type = TIKU_EVENT_KEY_DOWN, .key = 'k',
                        .modifiers = TIKU_MOD_CMD | TIKU_MOD_OPTION};
    assert(desktop_forcequit_input(app, &key));
    struct desktop_forcequit *p = app->forcequit;
    assert(p && p->count == 2 && p->selected == -1);
    assert(p->rows[0].handle >= 0 && p->rows[0].pid == child[0]);
    assert(fcntl(p->rows[0].handle, F_GETFD) & FD_CLOEXEC);
    fq_layout_t l = fq_layout(tiku_window_content(p->window));
    click(p, fq_row_rect(l.list, 0));
    click(p, l.kill); assert(p->confirm && !expected_pid);
    key.key = TIKU_KEY_RETURN; key.modifiers = 0;
    tiku_window_send(p->window, &key);
    assert(!p->confirm && !kill(child[0], 0) && !kill(child[1], 0));
    click(p, l.kill); click(p, l.done);
    assert(!p->confirm && !expected_pid);
    int old_handle = dup(p->rows[0].handle); assert(old_handle >= 0);
    click(p, l.kill); click(p, l.kill);
    assert(expected_pid == child[0] && !p->confirm);
    int status; assert(waitpid(child[0], &status, 0) == child[0]);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    assert(!kill(child[1], 0));
    assert(tiku_remote_process_kill(old_handle) < 0 && errno == ESRCH);
    close(old_handle);
    tiku_remote_poll(&app->remote);
    click(p, l.refresh); assert(p->count == 1 && p->selected == -1);
    tiku_window_t *builtin = tiku_workspace_open(app->desk, "Folder",
        (tiku_rect_t){20, 20, 160, 120}, NULL, NULL, NULL);
    click(p, l.pick); assert(p->picking);
    tiku_event_t pick = {.type = TIKU_EVENT_POINTER_DOWN, .button = 1,
                         .x = 40, .y = 60};
    assert(desktop_forcequit_input(app, &pick));
    assert(p->builtin == builtin && p->selected == -1 && !p->picking);
    pick.type = TIKU_EVENT_POINTER_UP;
    assert(desktop_forcequit_input(app, &pick));
    click(p, l.kill); assert(builtin->open && !p->confirm);
    click(p, l.close); assert(!builtin->open && !kill(child[1], 0));
    click(p, l.pick); key.key = TIKU_KEY_ESCAPE;
    assert(desktop_forcequit_input(app, &key) && !p->picking);
    int released = p->rows[0].handle;
    tiku_workspace_close(app->desk, p->window);
    assert(!desktop_forcequit_handle(app));
    assert(fcntl(released, F_GETFD) < 0 && errno == EBADF);
    desktop_forcequit_open(app);
    assert(p->window && p->count == 1 && p->selected == -1);
    released = p->rows[0].handle;
    desktop_forcequit_destroy(app);
    assert(fcntl(released, F_GETFD) < 0 && errno == EBADF);
    assert(!kill(child[1], SIGTERM)); assert(waitpid(child[1], NULL, 0) > 0);
    tiku_remote_poll(&app->remote);
    tiku_workspace_free(app->desk); tiku_surface_free(app->surf); free(app);
    puts("Force Quit: identity, cancellation, isolation, picker and queue pass.");
    return 0;
}
