/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * test_remote_packets.c - compare a stream peer and a whole-message peer.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_remote.h"
#include "tiku_dl.h"
#include "tiku_event.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static tiku_remote_listener_t stream, local;
static tiku_remote_session_t *peer;
static int wire;
static unsigned char returned[96];
static size_t returned_size;

static int reply(void *ctx, uint32_t type, const void *p, uint32_t n)
{
    (void)ctx;
    assert(n <= sizeof returned - 8);
    memcpy(returned, &type, 4);
    memcpy(returned + 4, &n, 4);
    memcpy(returned + 8, p, n);
    returned_size = n + 8;
    return 1;
}

static void both(uint32_t type, const void *data, uint32_t n)
{
    unsigned char bytes[512];
    assert(n <= sizeof bytes - 8);
    memcpy(bytes, &type, 4);
    memcpy(bytes + 4, &n, 4);
    memcpy(bytes + 8, data, n);
    assert(write(wire, bytes, 8 + n) == (ssize_t)(8 + n));
    assert(tiku_remote_poll(&stream) > 0);
    assert(tiku_remote_receive(peer, bytes, 8 + n) > 0);
}

int main(void)
{
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    stream.fd = local.fd = -1;
    wire = pair[1];
    assert(tiku_remote_adopt(&stream, pair[0]) == 0);
    peer = tiku_remote_attach(&local, reply, NULL);
    assert(peer);
    uint32_t fields[3] = {7, 64, 32};
    unsigned char body[256] = {0};
    memcpy(body, fields, 12);
    memcpy(body + 12, "Board", 6);
    both(TIKU_RMSG_OPEN, body, 12 + TIKU_REMOTE_TITLE);
    tiku_remote_window_t *a = &stream.session[0].win[0], *b = &peer->win[0];
    assert(a->opened && b->opened && !strcmp(a->title, b->title));
    tiku_dl_t *dl = tiku_dl_new();
    assert(dl);
    tiku_dl_button(dl, (tiku_rect_t){4, 4, 55, 24}, "Go", 0);
    size_t used;
    assert(tiku_dl_flatten(dl, body + 12, sizeof body - 12, &used));
    both(TIKU_RMSG_DRAW, body, (uint32_t)used + 12);
    assert(a->fw == 64 && b->fw == 64 && a->fh == b->fh && b->fh == 32);
    assert(!memcmp(a->frame, b->frame, 64 * 32 * sizeof *a->frame));
    tiku_dl_free(dl);

    static unsigned char tag;
    a->window = b->window = (struct tiku_window *)(void *)&tag;
    tiku_event_t press = {.type = TIKU_EVENT_POINTER_DOWN, .x = 17, .y = 12};
    tiku_remote_event(&stream, a->window, &press);
    tiku_remote_event(&local, b->window, &press);
    unsigned char stream_reply[96];
    assert(returned_size == 12 + sizeof press);
    assert(recv(wire, stream_reply, returned_size, MSG_WAITALL) == (ssize_t)returned_size);
    assert(!memcmp(stream_reply, returned, returned_size));
    both(TIKU_RMSG_CLOSE, fields, 4);
    assert(!a->opened && !b->opened && !a->frame && !b->frame);
    uint64_t generation = peer->generation;
    tiku_remote_detach(peer);
    assert(tiku_remote_receive(peer, body, 12) < 0);
    peer = tiku_remote_attach(&local, reply, NULL);
    assert(peer && peer->generation > generation && !peer->win[0].opened);
    assert(tiku_remote_receive(peer, body, 7) < 0);
    tiku_remote_detach(peer);
    tiku_remote_shutdown(&stream);
    close(wire);
    puts("PASS open, draw (2048 pixels), input (whole record), close and reattach");
}
