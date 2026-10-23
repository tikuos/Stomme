#include "tiku_shell_product.h"
/* Standalone Tracker's image handoff, without touching the user's desktop.
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <sys/socket.h>
#define tiku_remote_connect image_test_connect
#include "../../system/tracker/tiku_trk_app_image.c"
#undef tiku_remote_connect

static int client_fd = -1;

/** @brief A socket pair substitutes only connection establishment. */
int image_test_connect(tiku_remote_client_t *client, const char *name, int wait)
{
    (void)wait;
    return tiku_remote_connect_fd(client, name, client_fd);
}

/** @brief Isolate each request from real sockets, sessions and files. */
static void listener_start(tiku_remote_listener_t *listener)
{
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    memset(listener, 0, sizeof *listener); listener->fd = -1;
    for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) listener->session[i].fd = -1;
    assert(tiku_remote_adopt(listener, pair[0]) >= 0);
    client_fd = pair[1];
}

/** @brief Serve a real typed request and acknowledge the exact literal path. */
static void serve(tiku_remote_listener_t *listener, const char *path, int document)
{
    int seen = 0;
    for (int n = 0; n < 3000 && tiku_work_busy(); n++) {
        tiku_remote_poll(listener);
        for (int i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
            tiku_msg_t *m = tiku_remote_said(listener, i);
            if (!m) continue;
            assert(tiku_msg_what(m) == (document ? TIKU_MSG_DOCUMENTOPEN :
                                                 TIKU_MSG_IMAGEOPEN));
            assert(!strcmp(tiku_msg_find_string(m, "path", 0), path));
            tiku_msg_free(m); seen++;
            m = tiku_msg_new(document ? TIKU_MSG_DOCUMENTOPENED :
                                       TIKU_MSG_IMAGEOPENED);
            tiku_msg_add_int32(m, "accepted", 1);
            tiku_remote_tell(listener, i, m); tiku_msg_free(m);
        }
        tiku_work_collect(); usleep(1000);
    }
    assert(seen == 1 && !tiku_work_busy());
    tiku_remote_shutdown(listener);
    client_fd = -1;
}

/** @brief Requests are copied, acknowledged, and safe after the browser closes. */
int main(void)
{
    app_t *app = calloc(1, sizeof *app); assert(app);
    app->backend = tiku_backend_posix_open(); assert(app->backend);
    tiku_model_t model = {0};
    snprintf(model.type, sizeof model.type, "image/png");
    snprintf(model.path, sizeof model.path, "/tmp/sensor camera;literal.PNG");
    tiku_remote_listener_t *listener = calloc(1, sizeof *listener);
    assert(listener); listener_start(listener);
    assert(browser_open_image(app, &model));
    assert(app->image_job && browser_open_image(app, &model));
    serve(listener, model.path, 0);
    assert(!app->image_job && strstr(app->status, "Opening image on"));
    const char *types[] = {"text/plain", "text/markdown", "text/html",
                           "text/x-basic", "application/x-tiku-project"};
    for (int i = 0; i < 5; i++) {
        snprintf(model.type, sizeof model.type, "%s", types[i]);
        listener_start(listener);
        assert(browser_open_image(app, &model));
        serve(listener, model.path, 1);
        assert(!app->image_job && strstr(app->status, "Opening document on"));
    }
    listener_start(listener);
    assert(browser_open_image(app, &model));
    browser_image_detach(app);
    tiku_backend_close(app->backend); free(app);
    serve(listener, model.path, 1); free(listener);
    puts("PASS: Tracker image handoff, acknowledgements and safe close");
    return 0;
}
