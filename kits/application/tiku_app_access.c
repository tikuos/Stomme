/*
 * KintsugiDesktop v0.01 Application Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_app_access.c - logical semantic publication through the host service.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include "tiku_app.h"
#include "tiku_remote.h"
#include <stdlib.h>
int
tiku_app_access(const tiku_app_services_t *services, uint32_t window,
                const tiku_access_tree_t *tree)
{
    if (!services || !services->send_message || !window)
        return 0;
    void *bytes = NULL;
    size_t size = 0;
    if (tree && tiku_access_encode(tree, &bytes, &size))
        return 0;
    tiku_msg_t *message = tiku_msg_new(TIKU_MSG_ACCESS_TREE);
    int sent = message && tiku_msg_add_int32(message, "window", window) &&
               (!tree || tiku_msg_add_blob(message, "tree", bytes, size)) &&
               services->send_message(services->ctx, message);
    tiku_msg_free(message);
    free(bytes);
    return sent;
}
