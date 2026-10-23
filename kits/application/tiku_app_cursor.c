/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_app_cursor.c - the pointer shape a window's content asks for.
 *
 * The request is a message the desktop answers (TIKU_MSG_CURSOR in
 * tiku_remote.h).  It is a file of its own: an application that never
 * asks for a cursor links no message code for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_app.h"
#include "tiku_msg.h"
#include "tiku_remote.h"

struct tiku_msg *
tiku_app_cursor_message(uint32_t window, int shape)
{
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_CURSOR);

    if (m == NULL) {
        return NULL;
    }
    if (!tiku_msg_add_int32(m, "window", (int32_t)window) ||
        !tiku_msg_add_int32(m, "shape", shape)) {
        tiku_msg_free(m);
        return NULL;
    }
    return m;
}

int tiku_app_cursor_set(const tiku_app_services_t *services, uint32_t window,
                    int shape)
{
    tiku_msg_t *m;
    int sent;

    if (services == NULL || services->send_message == NULL) {
        return 0;
    }
    m = tiku_app_cursor_message(window, shape);
    sent = m != NULL && services->send_message(services->ctx, m);
    tiku_msg_free(m);
    return sent;
}

int tiku_app_text_rect(const tiku_app_services_t *services, uint32_t window,
    int x,
                   int y, int width, int height)
{
    if (!services || !services->send_message || width < 0 || height < 0)
        return 0;
    tiku_msg_t *message = tiku_msg_new(TIKU_MSG_TEXT_CARET);
    if (!message)
        return 0;
    int sent = tiku_msg_add_int32(message, "window", window) &&
               tiku_msg_add_int32(message, "x", x) &&
               tiku_msg_add_int32(message, "y", y) &&
               tiku_msg_add_int32(message, "width", width) &&
               tiku_msg_add_int32(message, "height", height) &&
               services->send_message(services->ctx, message);
    tiku_msg_free(message);
    return sent;
}
