/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_tray.c - an application's own icon beside the Deskbar's clock.
 *
 * The request is a message the desktop answers (TIKU_MSG_TRAY in
 * tiku_remote.h).  It is a file of its own: an application that never
 * asks for a tray item links no message code for it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_app.h"
#include "tiku_msg.h"
#include "tiku_remote.h"

struct tiku_msg *
tiku_app_tray_message(const char *id, const char *icon, const char *text,
                      const char *tip)
{
    tiku_msg_t *m;

    if (id == NULL || id[0] == '\0' || (m = tiku_msg_new(TIKU_MSG_TRAY))
        == NULL) {
        return NULL;
    }
    if (!tiku_msg_add_string(m, "id", id) ||
        !tiku_msg_add_string(m, "icon", icon != NULL ? icon : "") ||
        !tiku_msg_add_string(m, "text", text != NULL ? text : "") ||
        !tiku_msg_add_string(m, "tip", tip != NULL ? tip : "")) {
        tiku_msg_free(m);
        return NULL;
    }
    return m;
}

int
tiku_app_tray_set(const tiku_app_services_t *services, const char *id,
                  const char *icon, const char *text, const char *tip)
{
    tiku_msg_t *m;
    int sent;

    if (services == NULL || services->send_message == NULL) {
        return 0;
    }
    m = tiku_app_tray_message(id, icon, text, tip);
    sent = m != NULL && services->send_message(services->ctx, m);
    tiku_msg_free(m);
    return sent;
}
