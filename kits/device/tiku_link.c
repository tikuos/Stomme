/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_link.c - the link contract's calls, each a dispatch to the backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_link.h"

int
tiku_link_send(tiku_link_t *l, const void *msg, size_t len)
{
    if (l == NULL || l->ops == NULL || l->ops->send == NULL) {
        return -1;
    }
    return l->ops->send(l, msg, len);
}

int
tiku_link_ready(tiku_link_t *l)
{
    if (l == NULL || l->ops == NULL) {
        return 0;
    }
    return (l->ops->ready == NULL) ? 1 : l->ops->ready(l);
}

void
tiku_link_pump(tiku_link_t *l)
{
    if (l != NULL && l->ops != NULL && l->ops->pump != NULL) {
        l->ops->pump(l);
    }
}

int
tiku_link_lost(tiku_link_t *l)
{
    if (l == NULL || l->ops == NULL || l->ops->lost == NULL) {
        return 0;
    }
    return l->ops->lost(l);
}

size_t
tiku_link_mtu(tiku_link_t *l)
{
    if (l == NULL || l->ops == NULL || l->ops->mtu == NULL) {
        return 0u;
    }
    return l->ops->mtu(l);
}

void
tiku_link_on_message(tiku_link_t *l, tiku_link_msg_fn fn, void *ctx)
{
    if (l != NULL) {
        l->recv = fn;
        l->recv_ctx = ctx;
    }
}

void
tiku_link_deliver(tiku_link_t *l, const unsigned char *msg, size_t len)
{
    if (l != NULL && l->recv != NULL) {
        l->recv(msg, len, l->recv_ctx);
    }
}

void
tiku_link_close(tiku_link_t *l)
{
    if (l != NULL && l->ops != NULL && l->ops->close != NULL) {
        l->ops->close(l);
    }
}
