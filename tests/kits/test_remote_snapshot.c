/*
 * KintsugiDesktop v0.01 application transport tests.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * test_remote_snapshot.c - immutable displayed frames during repaint bursts.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <unistd.h>

#include "tiku_remote.h"
#include "tiku_text_input.h"

#ifdef __linux__
static void *refuse_resize;
/** @brief Linker's unwrapped allocator for the refusal hook. */
void *__real_realloc(void *pointer, size_t bytes);

/** @brief Refuse resizing the selected frame without exhausting host memory. */
void *
__wrap_realloc(void *pointer, size_t bytes)
{
    return pointer && pointer == refuse_resize ? NULL :
        __real_realloc(pointer, bytes);
}
#endif

/** @brief Receive pending desktop messages without dispatching input. */
static void
receive(tiku_remote_client_t *client)
{
    int type;

    while ((type = tiku_remote_read(client, NULL, NULL, NULL)) > 0) { }
    assert(type == 0);
}

/** @brief Send a complete small protocol message through a socket pair. */
static void
message(int fd, uint32_t type, const void *payload, uint32_t bytes)
{
    uint32_t header[2] = {type, bytes};

    assert(write(fd, header, sizeof header) == sizeof header);
    if (bytes) assert(write(fd, payload, bytes) == bytes);
}

/** @brief Construct one acknowledgment, including malformed and stale tokens. */
static void
release(int fd, uint32_t id, uint32_t index, uint64_t token, uint32_t bytes)
{
    unsigned char payload[16];

    memcpy(payload, &id, 4);
    memcpy(payload + 4, &index, 4);
    memcpy(payload + 8, &token, 8);
    assert(bytes <= sizeof payload);
    message(fd, TIKU_RMSG_RELEASE, payload, bytes);
}

/** @brief Connect a local client and consume feature negotiation. */
static tiku_remote_listener_t *
connect_pair(tiku_remote_client_t *client, int pair[2], uint32_t *id)
{
    tiku_remote_listener_t *listener = calloc(1, sizeof *listener);

    assert(listener);
    listener->fd = -1;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(tiku_remote_adopt(listener, pair[0]) == 0);
    assert(tiku_remote_connect_fd(client, "snapshot", pair[1]) == 0);
    *id = tiku_remote_open(client, "Snapshot", 8, 8);
    assert(*id);
    tiku_remote_poll(listener);
    receive(client);
    return listener;
}

/** @brief Close the pair and require that the desktop retains no mappings. */
static void
disconnect_pair(tiku_remote_listener_t *listener, tiku_remote_client_t *client)
{
    tiku_remote_disconnect(client);
    tiku_remote_poll(listener);
    assert(tiku_remote_mapped(listener) == 0);
    tiku_remote_shutdown(listener);
    free(listener);
}

/** @brief Repaints cannot alter the displayed pixels before publication. */
static void
burst(void)
{
    tiku_remote_listener_t *listener;
    tiku_remote_client_t client;
    tiku_remote_window_t *window;
    uint32_t pixels[64], expected[64], queued[128], id;
    int pair[2];

    listener = connect_pair(&client, pair, &id);
    window = &listener->session[0].win[0];
    for (size_t i = 0; i < 64; i++) pixels[i] = expected[i] = 0xff102030u + i;
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    tiku_remote_poll(listener);
    receive(&client);
    assert(window->shared);
    assert(!memcmp(tiku_remote_pixels(window), expected, sizeof expected));
    for (unsigned frame = 1; frame <= 12; frame++) {
        for (size_t i = 0; i < 64; i++) pixels[i] = 0xff102030u + i + frame;
        assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
        assert(!memcmp(tiku_remote_pixels(window), expected, sizeof expected));
        if (frame == 2) memcpy(queued, client.shared, sizeof queued);
        if (frame >= 2) {
            assert(client.busy[0] && client.busy[1]);
            assert(!memcmp(queued, client.shared, sizeof queued));
        }
    }
    tiku_remote_poll(listener);
    assert(!memcmp(tiku_remote_pixels(window), pixels, sizeof pixels));
    receive(&client);
    assert(!client.busy[0] && !client.busy[1]);
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    assert(client.busy[0] || client.busy[1]);
    tiku_remote_poll(listener);
    disconnect_pair(listener, &client);
}

/** @brief Late and malformed releases cannot release another publication. */
static void
acknowledgments(void)
{
    tiku_remote_listener_t *listener;
    tiku_remote_client_t client;
    uint32_t pixels[64] = {0xffabcdef}, id, other;
    uint64_t old, current, newer;
    int pair[2];

    listener = connect_pair(&client, pair, &id);
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    old = client.busy[0];
    assert(old);
    tiku_remote_poll(listener);
    receive(&client);
    assert(!client.busy[0]);
    client.next_buffer = 0;
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    current = client.busy[0];
    assert(current > old);
    release(pair[0], id, 0, old, 16);
    release(pair[0], id, 0, 0, 16);
    release(pair[0], id, 0, current, 8);
    release(pair[0], id, 0, current, 15);
    release(pair[0], id + 1, 0, current, 16);
    release(pair[0], id, TIKU_REMOTE_BUFFERS, current, 16);
    receive(&client);
    assert(client.busy[0] == current);
    assert(tiku_remote_frame(&client, id, pixels, 4, 8) == 0);
    newer = client.busy[0];
    assert(newer > current);
    release(pair[0], id, 0, current, 16);
    receive(&client);
    assert(client.busy[0] == newer);
    other = tiku_remote_open(&client, "Other", 8, 8);
    assert(other && other != id);
    assert(tiku_remote_frame(&client, other, pixels, 8, 8) == 0);
    assert(client.busy[0] > newer);
    current = client.busy[0];
    release(pair[0], id, 0, current, 16);
    release(pair[0], other, 0, newer, 16);
    receive(&client);
    assert(client.busy[0] == current);
    tiku_remote_poll(listener);
    receive(&client);
    assert(!client.busy[0] && !client.busy[1]);
    assert(listener->session[0].win[0].fw == 4);
    assert(listener->session[0].win[1].fw == 8);
    disconnect_pair(listener, &client);
}

/** @brief No greeting or an older desktop uses copied frames, without mapping. */
static void
compatibility(void)
{
    tiku_remote_listener_t *listener;
    tiku_remote_client_t client;
    uint32_t pixels[64] = {0xff123456}, id;
    int pair[2];
    unsigned peers[] = {0, TIKU_FEAT_SHARED_SURFACE,
        TIKU_FEAT_COMMAND_STREAM | TIKU_FEAT_SHARED_SURFACE};

    listener = connect_pair(&client, pair, &id);
    for (size_t i = 0; i < sizeof peers / sizeof *peers; i++) {
        client.peer_features = peers[i];
        assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
        assert(!client.shared);
        tiku_remote_poll(listener);
        assert(!listener->session[0].win[0].shared);
        assert(!memcmp(tiku_remote_pixels(&listener->session[0].win[0]),
                       pixels, sizeof pixels));
    }
    client.peer_features = TIKU_FEAT_FRAME_SNAPSHOT;
    client.frame_serial = UINT64_MAX;
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    assert(!client.shared && client.frame_serial == UINT64_MAX);
    tiku_remote_poll(listener);
    disconnect_pair(listener, &client);
}

/** @brief Network sockets carry pixels without shared-memory capabilities. */
static void
network(void)
{
    tiku_remote_listener_t *listener = calloc(1, sizeof *listener);
    tiku_remote_client_t client;
    struct sockaddr_in address = {.sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t length = sizeof address;
    uint32_t pixels[64] = {0xff123456}, id;
    int server, peer, fd;

    assert(listener);
    listener->fd = -1;
    server = socket(AF_INET, SOCK_STREAM, 0);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0 && fd >= 0);
    assert(bind(server, (struct sockaddr *)&address, length) == 0);
    assert(listen(server, 1) == 0);
    assert(getsockname(server, (struct sockaddr *)&address, &length) == 0);
    assert(connect(fd, (struct sockaddr *)&address, length) == 0);
    peer = accept(server, NULL, NULL);
    assert(peer >= 0);
    close(server);
    assert(tiku_remote_adopt(listener, peer) == 0);
    assert(tiku_remote_connect_fd(&client, "network", fd) == 0);
    assert(!(client.features & (TIKU_FEAT_SHARED_SURFACE |
                                 TIKU_FEAT_FRAME_SNAPSHOT)));
    client.peer_features = TIKU_FEAT_FRAME_SNAPSHOT;
    id = tiku_remote_open(&client, "Network", 8, 8);
    assert(id);
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    assert(!client.shared);
    for (unsigned i = 0; i < 200 && !listener->session[0].win[0].frame; i++) {
        struct pollfd ready = {.fd = peer, .events = POLLIN};

        assert(poll(&ready, 1, 10) >= 0);
        tiku_remote_poll(listener);
    }
    assert(listener->session[0].win[0].frame);
    assert(!memcmp(tiku_remote_pixels(&listener->session[0].win[0]),
                   pixels, sizeof pixels));
    disconnect_pair(listener, &client);
}

/** @brief Legacy eight-byte READY messages still publish desktop snapshots. */
static void
legacy(void)
{
    tiku_remote_listener_t *listener;
    tiku_remote_client_t client;
    unsigned char hello[40] = {0};
    uint32_t version = TIKU_REMOTE_VERSION, features = TIKU_FEAT_SHARED_SURFACE;
    uint32_t pixels[64] = {0xff56789a}, ready[2], id;
    int pair[2];

    listener = connect_pair(&client, pair, &id);
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    tiku_remote_poll(listener);
    receive(&client);
    memcpy(hello, &version, 4);
    memcpy(hello + 4, "Legacy", 7);
    memcpy(hello + 36, &features, 4);
    message(pair[1], TIKU_RMSG_HELLO, hello, sizeof hello);
    pixels[0]++;
    memcpy(client.shared, pixels, sizeof pixels);
    ready[0] = id;
    ready[1] = 0;
    message(pair[1], TIKU_RMSG_READY, ready, sizeof ready);
    tiku_remote_poll(listener);
    assert(listener->session[0].used);
    assert(!memcmp(tiku_remote_pixels(&listener->session[0].win[0]),
                   pixels, sizeof pixels));
    memset(client.shared, 0, sizeof pixels);
    assert(!memcmp(tiku_remote_pixels(&listener->session[0].win[0]),
                   pixels, sizeof pixels));
    disconnect_pair(listener, &client);
}

/** @brief A mapped replacement cannot blank the last published frame. */
static void
publication(void)
{
    tiku_remote_listener_t *listener;
    tiku_remote_client_t client;
    tiku_remote_window_t *window;
    unsigned char payload[12 + TIKU_REMOTE_SHM_NAME] = {0};
    uint32_t pixels[64] = {0xff123456}, id, dimensions[3];
    int pair[2], fd;

    listener = connect_pair(&client, pair, &id);
    window = &listener->session[0].win[0];
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    tiku_remote_poll(listener);
    receive(&client);
    assert(window->shared && window->frame);
    dimensions[0] = id;
    dimensions[1] = dimensions[2] = 4;
    memcpy(payload, dimensions, sizeof dimensions);
    snprintf((char *)payload + 12, TIKU_REMOTE_SHM_NAME,
             "/tiku-snapshot-test-%ld", (long)getpid());
    fd = shm_open((char *)payload + 12, O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(fd >= 0 && ftruncate(fd, 4 * 4 * 4 * TIKU_REMOTE_BUFFERS) == 0);
    close(fd);
    message(pair[1], TIKU_RMSG_SURFACE, payload, sizeof payload);
    tiku_remote_poll(listener);
    assert(window->shared && window->sw == 4 && window->sh == 4);
    assert(window->fw == 8 && window->fh == 8);
    assert(!memcmp(tiku_remote_pixels(window), pixels, sizeof pixels));
    /* A missing mapping leaves the displayed snapshot intact. */
    snprintf((char *)payload + 12, TIKU_REMOTE_SHM_NAME,
             "/tiku-snapshot-missing-%ld", (long)getpid());
    message(pair[1], TIKU_RMSG_SURFACE, payload, sizeof payload);
    tiku_remote_poll(listener);
    assert(!window->shared && window->fw == 8 && window->fh == 8);
    assert(!memcmp(tiku_remote_pixels(window), pixels, sizeof pixels));
    disconnect_pair(listener, &client);
}

/** @brief Invalid shared publication indices, tokens and sizes close sessions. */
static void
malformed(void)
{
    for (unsigned kind = 0; kind < 4; kind++) {
        tiku_remote_listener_t *listener;
        tiku_remote_client_t client;
        uint32_t pixels[64] = {0xffabcdef}, id, index;
        uint64_t token;
        unsigned char payload[16];
        int pair[2];

        listener = connect_pair(&client, pair, &id);
        assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
        tiku_remote_poll(listener);
        receive(&client);
        index = kind == 0 ? TIKU_REMOTE_BUFFERS : 0;
        token = kind == 1 ? 0 : 1;
        memcpy(payload, &id, 4);
        memcpy(payload + 4, &index, 4);
        memcpy(payload + 8, &token, 8);
        message(pair[1], TIKU_RMSG_READY, payload,
                kind == 2 ? 8 : kind == 3 ? 15 : 16);
        tiku_remote_poll(listener);
        assert(!listener->session[0].used);
        disconnect_pair(listener, &client);
    }
}

/** @brief Snapshot allocation refusal closes the session and frees mappings. */
static void
allocation_failure(void)
{
#ifdef __linux__
    tiku_remote_listener_t *listener;
    tiku_remote_client_t client;
    uint32_t pixels[64] = {0xffabcdef}, id;
    int pair[2];

    listener = connect_pair(&client, pair, &id);
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    tiku_remote_poll(listener);
    receive(&client);
    refuse_resize = listener->session[0].win[0].frame;
    assert(refuse_resize);
    assert(tiku_remote_frame(&client, id, pixels, 8, 8) == 0);
    tiku_remote_poll(listener);
    refuse_resize = NULL;
    assert(!listener->session[0].used);
    disconnect_pair(listener, &client);
#endif
}

/** @brief Carry a maximum composition atomically and retain old-peer refusal.
 */
static void
composition_transport(void)
{
    tiku_remote_client_t client;
    int pair[2];
    uint32_t id;
    tiku_remote_listener_t *listener = connect_pair(&client, pair, &id);
    assert(client.peer_features & TIKU_FEAT_TEXT_V2);
    tiku_remote_window_t *window = &listener->session[0].win[0];
    tiku_window_t dummy = {0};
    window->window = &dummy;
    tiku_text_input_v2_t *send = calloc(1, sizeof *send),
                         *receive = calloc(1, sizeof *receive);
    char *text = malloc(TIKU_TEXT_INPUT_V2_MAX);
    assert(send && receive && text);
    for (size_t i = 0; i < 65532; i += 4)
        memcpy(text + i, "\xf0\x9f\x98\x80", 4);
    text[65532] = 0;
    assert(tiku_text_input_set_v2(send, text, 65532));
    tiku_event_t event;
    while (tiku_text_input_next_v2(send, &event))
        tiku_remote_event(listener, &dummy, &event);
    int result = TIKU_TEXT_PENDING, type;
    size_t steps = 0;
    while (result != TIKU_TEXT_COMPLETE) {
        tiku_remote_poll(listener);
        while ((type = tiku_remote_read(&client, NULL, &event, NULL)) > 0) {
            assert(type == TIKU_RMSG_EVENT);
            result = tiku_text_input_event_v2(receive, &event);
            assert(result != TIKU_TEXT_REJECTED);
        }
        assert(type == 0 && ++steps < 100000);
    }
    assert(receive->length == 65532 && !memcmp(receive->text, text, 65533));
    listener->session[0].features &= ~TIKU_FEAT_TEXT_V2;
    assert(tiku_text_input_set_v2(send, text, 65532));
    while (tiku_text_input_next_v2(send, &event))
        tiku_remote_event(listener, &dummy, &event);
    tiku_remote_poll(listener);
    assert(tiku_remote_read(&client, NULL, &event, NULL) == TIKU_RMSG_EVENT);
    assert(event.type == TIKU_EVENT_TEXT_INPUT &&
           event.button == TIKU_TEXT_REJECT);
    tiku_text_input_t legacy = {0};
    assert(tiku_text_input_event(&legacy, &event) == TIKU_TEXT_REJECTED);
    assert(tiku_remote_read(&client, NULL, &event, NULL) == 0);
    event =
        (tiku_event_t){.type = TIKU_EVENT_PREEDIT, .button = TIKU_TEXT_REJECT};
    tiku_remote_event(listener, &dummy, &event);
    assert(tiku_remote_read(&client, NULL, &event, NULL) == 0);
    window->window = NULL;
    free(send);
    free(receive);
    free(text);
    disconnect_pair(listener, &client);
}

/** @brief Run shared-frame ownership regressions on local test connections. */
int main(void)
{
    composition_transport();
    burst();
    acknowledgments();
    compatibility();
    network();
    legacy();
    publication();
    malformed();
    allocation_failure();
    puts("remote snapshots: ownership, release tokens, compatibility and bounds hold");
    return 0;
}
