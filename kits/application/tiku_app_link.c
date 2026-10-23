/*
 * KintsugiDesktop v0.01 Application Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_app.h"
#include "tiku_remote.h"
#include "tiku_uri.h"
#include <time.h>
int64_t
tiku_remote_gesture_clock(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}
void
tiku_remote_gesture(tiku_remote_window_t *window, int64_t now)
{
    if (window && window->window && window->focused && now > 0)
        window->link_gesture = now;
}
int
tiku_remote_link(tiku_remote_session_t *session, const tiku_msg_t *message,
                 int64_t now, int (*dispatch)(void *, const char *), void *ctx)
{
    int32_t id;
    const char *target = tiku_msg_find_string(message, "target", 0);
    if (!session || !dispatch || !tiku_uri_external(target) ||
        !tiku_msg_find_int32(message, "window", 0, &id))
        return 0;
    for (unsigned i = 0; i < TIKU_REMOTE_WINDOWS; i++) {
        tiku_remote_window_t *window = session->win + i;
        if (window->win_id != (uint32_t)id || window->session != session ||
            !window->window || !window->focused)
            continue;
        int64_t gesture = window->link_gesture;
        window->link_gesture = 0;
        if (!gesture || now < gesture || now - gesture > 2000000)
            return 0;
        return dispatch(ctx, target);
    }
    return 0;
}
int
tiku_app_open_link(const tiku_app_services_t *services, uint32_t window,
                   const char *target, int64_t request)
{
    if (!services || !services->send_message || !window || request <= 0 ||
        !tiku_uri_external(target))
        return 0;
    tiku_msg_t *message = tiku_msg_new(TIKU_MSG_LINK_OPEN);
    int sent = message && tiku_msg_add_int32(message, "window", window) &&
               tiku_msg_add_string(message, "target", target) &&
               tiku_msg_add_int64(message, "request", request) &&
               services->send_message(services->ctx, message);
    tiku_msg_free(message);
    return sent;
}
