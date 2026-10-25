/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_remote.c - the window session, both ends: the desktop's listener
 * and its sessions, and the application's client.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <signal.h>

#include "tiku_sys.h"
#include "tiku_clipboard.h"
#include "tiku_remote.h"
#include "tiku_text_input.h"
#include "tiku_event.h"
#include "tiku_trail.h"

/** @brief One frame's ceiling, from the dimension cap. */
#define REMOTE_MAX_PAYLOAD \
    (12u + 4u * TIKU_REMOTE_MAX_DIM * TIKU_REMOTE_MAX_DIM)

/**
 * @brief What the desktop may send down in one message.
 *
 * The largest message down is the clipboard, mirrored whole to every
 * session: the bound is its limit plus room for the message around it.
 * The client allocates a message's whole length before reading it.
 */
#define REMOTE_MAX_DOWN (TIKU_CLIPBOARD_LIMIT + 64u * 1024u)

/**
 * @brief What a session's output may hold of ordinary messages.
 *
 * A stopped application fills it, and further messages to it are
 * refused and dropped.  A message larger than half of it, a clipboard,
 * is counted apart: one is taken when no other is waiting, and ordinary
 * messages queue behind it to this bound, so a stopped application holds
 * this and one clipboard at most.
 */
#define REMOTE_QUEUE_MAX (512u * 1024u)

int tiku_remote_process_handle(const tiku_remote_session_t *session, long *pid)
{
    if (session == NULL || !session->used || session->fd < 0) {
        errno = ENOTCONN;
        return -1;
    }
    return tiku_sys_process_pin(session->fd, pid);
}

int tiku_remote_process_kill(int handle)
{
    return tiku_sys_process_kill(handle);
}

int tiku_remote_path(char *out, size_t max)
{
    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        return -1;
    }
    if ((size_t)snprintf(out, max, "%s/.config/tracker/desk.sock",
                         home) >= max) {
        return -1;
    }
    return 0;
}

/** @brief Give a session's socket 1 MB buffers each way, so a clipboard
 *         crosses in a few turns.  An fd that is not a socket (a serial
 *         line) is left as it is. */
static void
roomy(int fd)
{
    int size = 1024 * 1024;

    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
}

/** @brief Make @p fd nonblocking, and ignore SIGPIPE: a write to a peer
 *         that has gone then fails with EPIPE.  @return 0, or -1. */
static int
set_nonblock(int fd)
{
    (void)signal(SIGPIPE, SIG_IGN);

    int flags = fcntl(fd, F_GETFL, 0);

    return (flags >= 0) ? fcntl(fd, F_SETFL, flags | O_NONBLOCK) : -1;
}

/*
 * How long a peer that has stopped reading is waited for, and the step
 * the wait is taken in.  The descriptors are nonblocking: without this
 * ceiling a peer that never reads would keep a send retrying EAGAIN for
 * ever.
 */
#define REMOTE_STALL_MS 2000
#define REMOTE_STEP_MS    50

/**
 * @brief Wait for @p fd to be ready for @p what, a step at a time.
 *
 * @param waited  Milliseconds spent so far, added to; reset by the
 *                caller whenever something did move.
 * @return 0 when it is worth trying again, -1 when the peer has had
 *         long enough.
 */
static int
wait_ready(int fd, short what, int *waited)
{
    struct pollfd one;

    if (*waited >= REMOTE_STALL_MS) {
        return -1;
    }
    one.fd = fd;
    one.events = what;
    one.revents = 0;
    if (poll(&one, 1u, REMOTE_STEP_MS) < 0 && errno != EINTR) {
        return -1;
    }
    *waited += REMOTE_STEP_MS;
    return 0;
}

/**
 * @brief Send all @p n bytes, or close the connection: a peer stalled for
 *        REMOTE_STALL_MS gets its descriptor closed and set to -1.
 *
 * A partly sent message would make the far end read what follows as a
 * length.  Every send guards on `fd < 0` and does nothing, and the read
 * side reports the disconnect.
 */
static void
send_all(int *fd, const void *data, size_t n)
{
    const unsigned char *p = data;
    int waited = 0;

    while (n > 0u) {
        ssize_t wrote = write(*fd, p, n);

        if (wrote > 0) {
            p += (size_t)wrote;
            n -= (size_t)wrote;
            waited = 0;         /* it is moving: the wait starts again */
            continue;
        }
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            wait_ready(*fd, POLLOUT, &waited) == 0) {
            continue;
        }
        break;
    }
    if (n > 0u) {
        tiku_trail("transport write failed: errno %d, %zu bytes pending",
                   errno, n);
        (void)close(*fd);
        *fd = -1;
    }
}

/** @brief Write one message: its type and length, then @p n bytes of
 *         @p payload. */
static void
send_msg(int *fd, uint32_t type, const void *payload, uint32_t n)
{
    uint32_t head[2];

    head[0] = type;
    head[1] = n;
    send_all(fd, head, sizeof head);
    if (n > 0u && *fd >= 0) {
        send_all(fd, payload, n);
    }
}

/* LISTENER                                                                  */

static void window_unmap(tiku_remote_window_t *win);

/** @brief Count @p w bytes of @p s's output as written: those that were
 *         the large message's come off what it has left. */
static void large_written(tiku_remote_session_t *s, size_t w)
{
    if (!s->out_large) return;
    if (w <= s->large_at) { s->large_at -= w; return; }
    w -= s->large_at; s->large_at = 0;
    s->out_large = w >= s->out_large ? 0 : s->out_large - w;
}

/** @brief Flush a bounded amount without waiting for a frozen peer. */
static void session_flush(tiku_remote_session_t *s)
{
    size_t budget = 1024u * 1024u;
    while (s->out_sent < s->out_size && budget) {
        size_t n = s->out_size - s->out_sent;
        if (n > budget) n = budget;
        ssize_t sent = write(s->fd, s->out + s->out_sent, n);
        if (sent <= 0) {
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                            errno == EINTR)) return;
            (void)shutdown(s->fd, SHUT_RDWR);
            return;
        }
        s->out_sent += (size_t)sent; budget -= (size_t)sent;
        large_written(s, (size_t)sent);
    }
    if (s->out_sent == s->out_size) {
        free(s->out); s->out = NULL;
        s->out_sent = s->out_size = 0;
        s->out_large = s->large_at = 0;
    }
}

/**
 * @brief Queue one message for @p s, then write what the socket takes now.
 *
 * A message is queued whole or refused, never cut.  When the queue is
 * full the session is kept, so a stopped application can still be force
 * quit.
 * @return 1 when queued, 0 when refused.
 */
static int session_send(tiku_remote_session_t *s, uint32_t type,
                         const void *payload, uint32_t n)
{
    session_flush(s);
    size_t pending = s->out_size - s->out_sent, small = pending - s->out_large;
    size_t ceiling = REMOTE_QUEUE_MAX;
    if (type == TIKU_RMSG_EVENT && n == 4 + sizeof(tiku_event_t)) {
        tiku_event_t event;
        memcpy(&event, (const unsigned char *)payload + 4, sizeof event);
        if (event.type == TIKU_EVENT_TEXT_INPUT ||
            event.type == TIKU_EVENT_PREEDIT)
            ceiling = (size_t)2 << 20;
    }
    int large = 8u + n > REMOTE_QUEUE_MAX / 2;
    if (n > REMOTE_MAX_DOWN ||
        (large ? s->out_large != 0 : small + 8u + n > ceiling))
        return 0;
    if (pending && s->out_sent) memmove(s->out, s->out + s->out_sent, pending);
    s->out_sent = 0; s->out_size = pending;
    unsigned char *out = realloc(s->out, pending + 8u + n);
    if (!out) return 0;
    s->out = out;
    memcpy(out + pending, &type, 4);
    memcpy(out + pending + 4, &n, 4);
    if (n) memcpy(out + pending + 8, payload, n);
    s->out_size += 8u + n;
    if (large) { s->large_at = pending; s->out_large = 8u + n; }
    session_flush(s);
    return 1;
}

/** @brief The row this session knows as @p id, or NULL. */
static tiku_remote_window_t *
session_window(tiku_remote_session_t *s, uint32_t id)
{
    int j;

    for (j = 0; s != NULL && j < TIKU_REMOTE_WINDOWS; j++) {
        if (s->win[j].opened && s->win[j].win_id == id) {
            return &s->win[j];
        }
    }
    return NULL;
}

/** @brief A free row of @p s, or NULL when every row is in use. */
static tiku_remote_window_t *
session_window_free(tiku_remote_session_t *s)
{
    int j;

    for (j = 0; s != NULL && j < TIKU_REMOTE_WINDOWS; j++) {
        if (!s->win[j].opened) {
            return &s->win[j];
        }
    }
    return NULL;
}

/** @brief Give back every window's surface, frame and list, close the
 *         connection, and free what the session holds. */
static void
session_free(tiku_remote_session_t *s)
{
    int j;

    for (j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
        window_unmap(&s->win[j]);
        free(s->win[j].frame);
        s->win[j].frame = NULL;
        tiku_dl_free(s->win[j].list);
        s->win[j].list = NULL;
    }
    if (s->fd >= 0) {
        (void)close(s->fd);
    }
    free(s->buf);
    free(s->out);
    while (s->said_count > 0) {
        tiku_msg_free(s->said[s->said_head]);
        s->said_head = (s->said_head + 1) % TIKU_REMOTE_SAID;
        s->said_count--;
    }
    memset(s, 0, sizeof *s);
    s->fd = -1;
}

int tiku_remote_listen(tiku_remote_listener_t *listener)
{
    struct sockaddr_un addr;
    char dir[108];
    char *slash;
    int i;

    memset(listener, 0, sizeof *listener);
    listener->fd = -1;
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        listener->session[i].fd = -1;
    }
    if (tiku_remote_path(listener->path,
                              sizeof listener->path) != 0) {
        return -1;
    }
    /* Make every directory on the way: a fresh home has no .config
     * either. */
    snprintf(dir, sizeof dir, "%s", listener->path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        char *step;

        *slash = '\0';
        for (step = strchr(dir + 1, '/'); step != NULL;
             step = strchr(step + 1, '/')) {
            *step = '\0';
            (void)mkdir(dir, 0700);
            *step = '/';
        }
        (void)mkdir(dir, 0700);
    }
    (void)unlink(listener->path);
    listener->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener->fd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", listener->path);
    if (bind(listener->fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(listener->fd, 4) != 0) {
        (void)close(listener->fd);
        listener->fd = -1;
        return -1;
    }
    (void)set_nonblock(listener->fd);
    return 0;
}

void tiku_remote_shutdown(tiku_remote_listener_t *listener)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (listener->session[i].used) {
            /* Best effort and nonblocking, behind anything already queued. */
            session_send(&listener->session[i], TIKU_RMSG_SHUTDOWN, NULL, 0);
            session_free(&listener->session[i]);
        }
    }
    if (listener->fd >= 0) {
        (void)close(listener->fd);
        (void)unlink(listener->path);
        listener->fd = -1;
    }
}

/** @brief Give back a shared surface, if this window had one. */
static void
window_unmap(tiku_remote_window_t *win)
{
    tiku_access_release(win->access);
    win->access = NULL;
    win->access_fresh = 1;
    if (win->shared != NULL) {
        (void)munmap(win->shared, win->shared_bytes);
        win->shared = NULL;
        win->shared_bytes = 0u;
        if (win->session != NULL && win->session->owner != NULL) {
            win->session->owner->mapped--;
        }
    }
}

/** @brief The window id every addressed message carries first. */
static uint32_t
id_of(const unsigned char *p)
{
    uint32_t id;

    memcpy(&id, p, 4);
    return id;
}

/** @brief A HELLO: take the client's name and features and answer with a
 *         WELCOME carrying this end's.
 *  @return 0; -1 for another protocol version, to drop the session. */
static int
session_hello(tiku_remote_session_t *s)
{
    const unsigned char *p = s->buf;
    int changed = 0;
    switch (s->cur_type) {
    case TIKU_RMSG_HELLO:
        if (s->want >= 4u + 32u) {
            uint32_t version;

            memcpy(&version, p, 4);
            if (version != TIKU_REMOTE_VERSION) {
                return -1;      /* another version: drop the session */
            }
            memcpy(s->name, p + 4, 32);
            s->name[31] = '\0';
            /* Longer means the peer said what else it can do; a hello of
             * 36 bytes claims nothing more. */
            if (s->want >= 4u + 32u + 4u) {
                memcpy(&s->features, p + 36, 4);
            }
            {
                /* Answer with this end's features: a client sends lists,
                 * icons and pictures only to a desktop that claimed them. */
                unsigned char reply[8];
                uint32_t version = TIKU_REMOTE_VERSION;
                uint32_t mine =
                    TIKU_FEAT_COMMAND_STREAM | TIKU_FEAT_ICON_STREAM |
                    TIKU_FEAT_PICTURE_STREAM | TIKU_FEAT_SHARED_SURFACE |
                    TIKU_FEAT_FRAME_SNAPSHOT | TIKU_FEAT_TEXT_V2;

                memcpy(reply, &version, 4);
                memcpy(reply + 4, &mine, 4);
                session_send(s, TIKU_RMSG_WELCOME, reply, sizeof reply);
            }
        }
        break;
    default: break;
    }
    return changed;
}

/** @brief SURFACE maps transfer storage; READY copies one complete frame.
 *         Negotiated snapshots release the buffer after copying its pixels.
 *  @return nonzero when a window changed, -1 to close a session on invalid
 *          publication or unavailable snapshot storage. */
static int
session_surface(tiku_remote_session_t *s)
{
    const unsigned char *p = s->buf;
    tiku_remote_window_t *win;
    int changed = 0;
    switch (s->cur_type) {
    case TIKU_RMSG_SURFACE:
        if (s->want >= 12u + TIKU_REMOTE_SHM_NAME) {
            char name[TIKU_REMOTE_SHM_NAME];
            int32_t w, h;
            uint32_t id;

            memcpy(&id, p, 4);
            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            memcpy(name, p + 12, sizeof name);
            name[sizeof name - 1] = '\0';
            if (w < 1 || h < 1 || w > TIKU_REMOTE_MAX_DIM ||
                h > TIKU_REMOTE_MAX_DIM) {
                return -1;
            }
            win = session_window(s, id);
            if (win == NULL) {
                break;          /* a surface for no window it holds */
            }
            window_unmap(win);
            {
                size_t bytes = 4u * (size_t)w * (size_t)h *
                               TIKU_REMOTE_BUFFERS;
                int fd = shm_open(name, O_RDWR, 0600);

                if (fd >= 0) {
                    /*
                     * The size is checked against the object: a SURFACE
                     * claiming more than the object holds would map
                     * short, and a read past its end would fault.  Such a
                     * surface is not mapped.
                     */
                    struct stat sb;
                    void *at = MAP_FAILED;

                    if (fstat(fd, &sb) == 0 && sb.st_size >= 0 &&
                        (size_t)sb.st_size >= bytes) {
                        at = mmap(NULL, bytes, PROT_READ, MAP_SHARED,
                                  fd, 0);
                    }
                    (void)close(fd);
                    /* Unlinked, mapped or not: a mapping keeps the object
                     * alive, and a name left behind outlives the window. */
                    (void)shm_unlink(name);
                    if (at != MAP_FAILED) {
                        win->shared = at;
                        win->shared_bytes = bytes;
                        if (s->owner != NULL) {
                            s->owner->mapped++;
                        }
                        win->sw = w;
                        win->sh = h;
                    }
                }
            }
            changed = 1;
        }
        break;
    case TIKU_RMSG_READY:
        if (s->want < 8u) return -1;
        if (s->features & TIKU_FEAT_FRAME_SNAPSHOT) {
            uint64_t token;

            if (s->want != 16u) return -1;
            memcpy(&token, p + 8, 8);
            if (!token) return -1;
        }
        win = session_window(s, id_of(p));
        if (win != NULL && win->shared != NULL) {
            uint32_t index;
            size_t bytes = 4u * (size_t)win->sw * (size_t)win->sh;
            uint32_t *pixels;

            memcpy(&index, p + 4, 4);
            if (index >= TIKU_REMOTE_BUFFERS) return -1;
            pixels = realloc(win->frame, bytes);
            if (!pixels) return -1;
            memcpy(pixels, (const unsigned char *)win->shared + index * bytes,
                   bytes);
            win->frame = pixels;
            win->shown = (int)index;
            win->fw = win->sw;
            win->fh = win->sh;
            win->list_fresh = 0;
            win->description[0] = 0;
            tiku_dl_free(win->list); win->list = NULL;
            changed = 1;
        } else if (win != NULL) {
            return -1;
        }
        if (s->features & TIKU_FEAT_FRAME_SNAPSHOT)
            session_send(s, TIKU_RMSG_RELEASE, p, 16u);
        break;
    default: break;
    }
    return changed;
}

/** @brief A FRAME copies pixels into its window; a DRAW gives it a
 *         display list.
 *  @return nonzero when a window changed. */
static int
session_paint(tiku_remote_session_t *s)
{
    const unsigned char *p = s->buf;
    tiku_remote_window_t *win;
    int changed = 0;
    switch (s->cur_type) {
    case TIKU_RMSG_FRAME:
        if (s->want >= 12u) {
            int32_t w, h;

            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            win = session_window(s, id_of(p));
            if (win != NULL && w >= 1 && h >= 1 &&
                w <= TIKU_REMOTE_MAX_DIM &&
                h <= TIKU_REMOTE_MAX_DIM &&
                s->want >= 12u + 4u * (size_t)w * (size_t)h) {
                uint32_t *px = realloc(win->frame,
                                       4u * (size_t)w * (size_t)h);

                if (px != NULL) {
                    memcpy(px, p + 12, 4u * (size_t)w * (size_t)h);
                    win->frame = px;
                    win->fw = w;
                    win->fh = h;
                    win->list_fresh = 1;   /* the copy is the window */
                    win->description[0] = 0;
                    tiku_dl_free(win->list); win->list = NULL;
                    changed = 1;
                }
            }
        }
        break;
    case TIKU_RMSG_DRAW:
        if (s->want >= 12u) {
            int32_t w, h;

            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            win = session_window(s, id_of(p));
            if (win != NULL && w >= 1 && h >= 1 &&
                w <= TIKU_REMOTE_MAX_DIM && h <= TIKU_REMOTE_MAX_DIM) {
                tiku_dl_t *dl = tiku_dl_unflatten(p + 12, s->want - 12u,
                                                  NULL);

                if (dl != NULL) {
                    uint32_t *px = realloc(win->frame,
                                           4u * (size_t)w * (size_t)h);

                    if (px != NULL) {
                        /*
                         * Played into the buffer a copied frame lands
                         * in, so what reads the window cannot tell which
                         * way it arrived.
                         */
                        tiku_surface_t face;

                        memset(px, 0, 4u * (size_t)w * (size_t)h);
                        memset(&face, 0, sizeof face);
                        face.px = px;
                        face.w = w;
                        face.h = h;
                        face.scale = 1;
                        face.clip.w = w;
                        face.clip.h = h;
                        (void)tiku_dl_play(dl, &face);
                        win->frame = px;
                        win->fw = w;
                        win->fh = h;
                        win->list_fresh = 1;
                        win->description[0] = 0;
                        changed = 1;
                        tiku_dl_free(win->list);
                        win->list = dl;
                        dl = NULL;
                    }
                    tiku_dl_free(dl);
                }
            }
        }
        break;
    default: break;
    }
    return changed;
}

/** @brief Apply one complete message to its session: HELLO through
 *         session_hello(), SURFACE and READY through session_surface(),
 *         FRAME and DRAW through session_paint(), the rest here.
 *  @return nonzero when a window changed, -1 to drop the session. */
static int
session_message(tiku_remote_session_t *s)
{
    const unsigned char *p = s->buf;
    tiku_remote_window_t *win;
    int changed = 0;

    switch (s->cur_type) {
    case TIKU_RMSG_HELLO:
        return session_hello(s);
    case TIKU_RMSG_SURFACE:
    case TIKU_RMSG_READY:
        return session_surface(s);
    case TIKU_RMSG_OPEN:
        if (s->want >= 12u + TIKU_REMOTE_TITLE) {
            int32_t w, h;

            memcpy(&w, p + 4, 4);
            memcpy(&h, p + 8, 4);
            if (w < 1 || h < 1 || w > TIKU_REMOTE_MAX_DIM ||
                h > TIKU_REMOTE_MAX_DIM) {
                return -1;
            }
            /* An OPEN naming a window this session already holds is
             * that window again, not a second one: the id is the
             * application's own word for it. */
            win = session_window(s, id_of(p));
            if (win == NULL) {
                win = session_window_free(s);
            }
            if (win == NULL) {
                break;          /* every row is in use */
            }
            if (!win->opened) {
                if (s->next_window_generation == UINT64_MAX)
                    return -1;
                win->generation = ++s->next_window_generation;
            }
            win->session = s;
            win->win_id = id_of(p);
            memcpy(win->title, p + 12, TIKU_REMOTE_TITLE);
            win->title[TIKU_REMOTE_TITLE - 1] = '\0';
            win->open_w = w;
            win->open_h = h;
            win->opened = 1;
            changed = 1;
        }
        break;
    case TIKU_RMSG_FRAME:
    case TIKU_RMSG_DRAW:
        return session_paint(s);
    case TIKU_RMSG_RAISE:
        /* Noted here; the shell does the raising. */
        win = (s->want >= 4u) ? session_window(s, id_of(p)) : NULL;
        if (win != NULL) {
            win->wants_front = 1;
            changed = 1;
        }
        break;
    case TIKU_RMSG_PLACE:
        /* What kind of window this is: noted for the shell, which owns
         * where such a window goes and what it wears. */
        win = (s->want >= 8u) ? session_window(s, id_of(p)) : NULL;
        if (win != NULL) {
            int32_t role = 0;

            memcpy(&role, p + 4, 4);
            win->wants_place = (int)role;
            changed = 1;
        }
        break;
    case TIKU_RMSG_SIZEABLE:
        win = (s->want >= 4u) ? session_window(s, id_of(p)) : NULL;
        if (win != NULL) {
            win->wants_size = 1;
            changed = 1;
        }
        break;
    case TIKU_RMSG_DESCRIBE:
        win = s->want >= 5 && s->want <= 4100 ?
              session_window(s, id_of(p)) : NULL;
        if (win) {
            size_t n = s->want - 4;
            memcpy(win->description, p + 4, n);
            win->description[n - 1] = 0;
            changed = 1;
        }
        break;
    case TIKU_RMSG_CONFIGURE:
        win = s->want == 20u + TIKU_REMOTE_TITLE ?
              session_window(s, id_of(p)) : NULL;
        if (win != NULL) {
            int32_t v[4];
            memcpy(v, p + 4, sizeof v);
            if (v[0] >= 0 && v[1] >= 0 &&
                v[0] <= TIKU_REMOTE_MAX_DIM && v[1] <= TIKU_REMOTE_MAX_DIM &&
                (!v[0] == !v[1]) &&
                (v[2] == 0 || v[2] == 1) && (v[3] == 0 || v[3] == 1)) {
                win->config_w = v[0]; win->config_h = v[1];
                win->resizable = v[2]; win->zoomable = v[3];
                memcpy(win->title, p + 20, TIKU_REMOTE_TITLE);
                win->title[TIKU_REMOTE_TITLE - 1] = 0;
                win->configured = changed = 1;
            }
        }
        break;
    case TIKU_RMSG_MENUS:
        win = (s->want >= 4u + sizeof(tiku_menuset_t))
                  ? session_window(s, id_of(p)) : NULL;
        if (win != NULL) {
            memcpy(&win->menus, p + 4, sizeof win->menus);
            win->has_menus = 1;
            win->menus_fresh = 1;
            changed = 1;
        }
        break;
    case TIKU_RMSG_MENUS_TEXT:
        win = (s->want >= 4u) ? session_window(s, id_of(p)) : NULL;
        if (win != NULL) {
            char text[2048];
            size_t n = s->want - 4u;

            if (n >= sizeof text) {
                n = sizeof text - 1u;
            }
            memcpy(text, p + 4, n);
            text[n] = '\0';
            if (tiku_remote_menus_from_text(text, &win->menus) > 0) {
                win->has_menus = 1;
                win->menus_fresh = 1;
                changed = 1;
            }
        }
        break;
    case TIKU_RMSG_SAY: {
        tiku_msg_t *m = tiku_msg_unflatten(p, s->want, NULL);

        if (m == NULL) {
            /* A payload that is not a message is dropped; the session
             * stays open. */
            break;
        }
        if (s->said_count >= TIKU_REMOTE_SAID) {
            tiku_msg_free(m);
            s->said_lost++;     /* dropped, and counted */
            break;
        }
        s->said[(s->said_head + s->said_count) % TIKU_REMOTE_SAID] = m;
        s->said_count++;
        changed = 1;
        break;
    }
    case TIKU_RMSG_CLOSE:
        /* Naming a window gives back that window; a CLOSE naming none
         * is the application leaving (tiku_remote_disconnect()). */
        if (s->want >= 4u) {
            win = session_window(s, id_of(p));
            if (win != NULL) {
                window_unmap(win);
                free(win->frame);
                win->frame = NULL;
                tiku_dl_free(win->list);
                win->list = NULL;
                win->opened = 0;
                win->has_menus = 0;
                win->menus_fresh = 0;
                /* The window itself is the desktop's to close.  It moves
                 * to reap: an OPEN in this same batch may take the row,
                 * and a pointer left there would read as a window
                 * already made. */
                if (win->window != NULL && win->reap == NULL) {
                    win->reap = win->window;
                    win->window = NULL;
                }
                changed = 1;
            }
            break;
        }
        return -1;              /* the client is done: drop the session */
    default:
        break;
    }
    return changed;
}

int tiku_remote_sessions(const tiku_remote_listener_t *listener)
{
    int i, n = 0;

    if (listener == NULL) {
        return 0;
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (listener->session[i].fd >= 0) {
            n++;
        }
    }
    return n;
}

int tiku_remote_wait(tiku_remote_listener_t *listener, int timeout_us)
{
    struct pollfd watch[TIKU_REMOTE_SESSIONS + 1];
    nfds_t n = 0;
    int i, ready;

    if (listener == NULL || timeout_us < 0) {
        return 0;
    }
    if (listener->fd >= 0) {
        watch[n].fd = listener->fd;
        watch[n].events = POLLIN;
        watch[n].revents = 0;
        n++;
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (listener->session[i].fd >= 0) {
            watch[n].fd = listener->session[i].fd;
            watch[n].events = POLLIN;
            watch[n].revents = 0;
            n++;
        }
    }
    if (n == 0) {
        return 0;
    }
    /* Rounded up to whole milliseconds, so a timeout under 1 ms still
     * waits. */
    ready = poll(watch, n, (timeout_us + 999) / 1000);
    return ready > 0;
}

/** @brief Read whatever is ready.
 *  @return nonzero when a window changed, or -1 when the session died. */
static int
session_read(tiku_remote_session_t *s)
{
    int changed = 0;

    for (;;) {
        if (s->want == 0u) {
            uint32_t head[2];

            while (s->hgot < sizeof s->hbuf) {
                ssize_t got = read(s->fd, s->hbuf + s->hgot,
                                   sizeof s->hbuf - s->hgot);

                if (got == 0) {
                    return -1;
                }
                if (got < 0) {
                    return (errno == EAGAIN || errno == EWOULDBLOCK ||
                            errno == EINTR) ? changed : -1;
                }
                s->hgot += (size_t)got;
            }
            memcpy(head, s->hbuf, sizeof head);
            s->hgot = 0u;
            if (head[1] > REMOTE_MAX_PAYLOAD) {
                return -1;      /* past REMOTE_MAX_PAYLOAD: drop it */
            }
            s->cur_type = head[0];
            s->want = head[1];
            s->got = 0u;
            free(s->buf);
            s->buf = (s->want > 0u) ? malloc(s->want) : NULL;
            if (s->want > 0u && s->buf == NULL) {
                return -1;
            }
            if (s->want == 0u) {
                int r = session_message(s);

                if (r < 0) {
                    return -1;
                }
                changed |= r;
                continue;
            }
        }
        while (s->got < s->want) {
            ssize_t got = read(s->fd, s->buf + s->got, s->want - s->got);

            if (got == 0) {
                return -1;
            }
            if (got < 0) {
                return (errno == EAGAIN || errno == EWOULDBLOCK ||
                        errno == EINTR) ? changed : -1;
            }
            s->got += (size_t)got;
        }
        {
            int r = session_message(s);

            if (r < 0) {
                return -1;
            }
            changed |= r;
        }
        free(s->buf);
        s->buf = NULL;
        s->want = 0u;
        s->got = 0u;
    }
}

int tiku_remote_adopt(tiku_remote_listener_t *listener, int fd)
{
    int i;

    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (!listener->session[i].used) {
            memset(&listener->session[i], 0, sizeof listener->session[i]);
            listener->session[i].fd = fd;
            listener->session[i].used = 1;
            listener->session[i].generation = ++listener->next_generation;
            listener->session[i].owner = listener;
            (void)set_nonblock(fd);
            roomy(fd);
            return 0;
        }
    }
    return -1;
}

int tiku_remote_poll(tiku_remote_listener_t *listener)
{
    int changed = 0;
    int i;

    while (listener->fd >= 0) {
        int fd = accept(listener->fd, NULL, NULL);

        if (fd < 0) {
            break;
        }
        for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
            if (!listener->session[i].used) {
                memset(&listener->session[i], 0,
                       sizeof listener->session[i]);
                listener->session[i].fd = fd;
                listener->session[i].used = 1;
                listener->session[i].generation = ++listener->next_generation;
                listener->session[i].owner = listener;
                (void)set_nonblock(fd);
                roomy(fd);
                fd = -1;
                break;
            }
        }
        if (fd >= 0) {
            (void)close(fd);    /* every session is in use */
        }
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        tiku_remote_session_t *s = &listener->session[i];
        int r;

        if (!s->used) {
            continue;
        }
        session_flush(s);
        r = session_read(s);
        if (r < 0) {
            session_free(s);
            changed = 1;
        } else {
            changed |= r;
        }
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        int j;

        if (!listener->session[i].used) {
            continue;
        }
        for (j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            const tiku_remote_window_t *win =
                &listener->session[i].win[j];

            changed |= win->opened && win->window == NULL;
            changed |= win->menus_fresh;
            changed |= win->wants_front;
            changed |= win->reap != NULL;
            changed |= win->wants_size;
        }
    }
    return changed;
}

const uint32_t *
tiku_remote_pixels(const tiku_remote_window_t *win)
{
    if (win == NULL) {
        return NULL;
    }
    return win->frame;
}

int tiku_remote_mapped(const tiku_remote_listener_t *listener)
{
    return (listener != NULL) ? listener->mapped : 0;
}

tiku_remote_session_t *
tiku_remote_owner(tiku_remote_listener_t *listener,
                       struct tiku_window *window)
{
    int i;

    if (window == NULL) {
        return NULL;
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        int j;

        if (!listener->session[i].used) {
            continue;
        }
        for (j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            if (listener->session[i].win[j].window == window) {
                return &listener->session[i];
            }
        }
    }
    return NULL;
}

tiku_remote_window_t *
tiku_remote_window_of(tiku_remote_listener_t *listener,
                           const struct tiku_window *window)
{
    int i, j;

    if (listener == NULL || window == NULL) {
        return NULL;
    }
    for (i = 0; i < TIKU_REMOTE_SESSIONS; i++) {
        if (!listener->session[i].used) {
            continue;
        }
        for (j = 0; j < TIKU_REMOTE_WINDOWS; j++) {
            if (listener->session[i].win[j].window == window) {
                return &listener->session[i].win[j];
            }
        }
    }
    return NULL;
}

void tiku_remote_event(tiku_remote_listener_t *listener,
                       struct tiku_window *window,
                       const tiku_event_t *event)
{
    tiku_remote_window_t *win = tiku_remote_window_of(listener,
                                                                window);
    unsigned char payload[4 + sizeof *event];

    if (win == NULL || win->session == NULL) {
        return;
    }
    if (!(win->session->features & TIKU_FEAT_TEXT_V2)) {
        if (event->type == TIKU_EVENT_PREEDIT)
            return;
        if (event->type == TIKU_EVENT_TEXT_INPUT && event->dy >= 1024) {
            if (!(event->button & TIKU_TEXT_BEGIN))
                return;
            tiku_event_t rejected = {.type = TIKU_EVENT_TEXT_INPUT,
                                     .button = TIKU_TEXT_REJECT};
            memcpy(payload, &win->win_id, 4);
            memcpy(payload + 4, &rejected, sizeof rejected);
            session_send(win->session, TIKU_RMSG_EVENT, payload,
                         sizeof payload);
            return;
        }
    }
    if (event->type == TIKU_EVENT_KEY_DOWN ||
        event->type == TIKU_EVENT_POINTER_DOWN)
        tiku_remote_gesture(win, tiku_remote_gesture_clock());
    memcpy(payload, &win->win_id, 4);
    memcpy(payload + 4, event, sizeof *event);
    if (!(win->session->features & TIKU_FEAT_TEXT_V2) &&
        event->type == TIKU_EVENT_TEXT_INPUT) {
        tiku_event_t legacy = *event;
        legacy.repeat = 0;
        memcpy(payload + 4, &legacy, sizeof legacy);
    }
    session_send(win->session, TIKU_RMSG_EVENT, payload,
             sizeof payload);
}

/** @brief Queue a native close or quit shortcut without granting input authority. */
static int request_shortcut(tiku_remote_listener_t *listener,struct tiku_window *window,unsigned key)
{
    tiku_remote_window_t *win = tiku_remote_window_of(listener, window);
    tiku_event_t event = {.type=TIKU_EVENT_KEY_DOWN,.key=key,.modifiers=TIKU_MOD_CMD};
    unsigned char payload[4 + sizeof event];
    if (!win || !win->session || !win->session->used || win->session->fd < 0 || !win->opened) return 0;
    memcpy(payload,&win->win_id,4); memcpy(payload+4,&event,sizeof event);
    return session_send(win->session,TIKU_RMSG_EVENT,payload,sizeof payload);
}

int tiku_remote_request_close(tiku_remote_listener_t *listener,struct tiku_window *window)
{
    return request_shortcut(listener,window,'w');
}

int tiku_remote_request_quit(tiku_remote_listener_t *listener,struct tiku_window *window)
{
    return request_shortcut(listener,window,'q');
}

void tiku_remote_pick(tiku_remote_listener_t *listener,
                      struct tiku_window *window, int command)
{
    tiku_remote_window_t *win = tiku_remote_window_of(listener,
                                                                window);
    unsigned char payload[8];
    int32_t c = command;

    if (win == NULL || win->session == NULL) {
        return;
    }
    memcpy(payload, &win->win_id, 4);
    memcpy(payload + 4, &c, 4);
    tiku_remote_gesture(win, tiku_remote_gesture_clock());
    session_send(win->session, TIKU_RMSG_PICK, payload,
             sizeof payload);
}

void tiku_remote_window_closed(tiku_remote_listener_t *listener,
                               struct tiku_window *window)
{
    tiku_remote_window_t *win = tiku_remote_window_of(listener,
                                                                window);

    if (win == NULL || win->session == NULL) {
        return;
    }
    session_send(win->session, TIKU_RMSG_CLOSED, &win->win_id, 4);
    /* The row is given back, not the session: an application whose
     * second window was closed still has its first. */
    window_unmap(win);
    free(win->frame);
    win->frame = NULL;
    tiku_dl_free(win->list);
    win->list = NULL;
    win->window = NULL;
    win->opened = 0;
    win->has_menus = 0;
    win->menus_fresh = 0;
}

/** @brief The session at @p index, when there is a live one. */
static tiku_remote_session_t *
session_at(tiku_remote_listener_t *listener, int index)
{
    if (listener == NULL || index < 0 || index >= TIKU_REMOTE_SESSIONS) {
        return NULL;
    }
    return listener->session[index].used ? &listener->session[index] : NULL;
}

tiku_msg_t *
tiku_remote_said(tiku_remote_listener_t *listener, int index)
{
    tiku_remote_session_t *s = session_at(listener, index);
    tiku_msg_t *m;

    if (s == NULL || s->said_count == 0) {
        return NULL;
    }
    m = s->said[s->said_head];
    s->said_head = (s->said_head + 1) % TIKU_REMOTE_SAID;
    s->said_count--;
    return m;
}

int tiku_remote_lost(const tiku_remote_listener_t *listener, int index)
{
    if (listener == NULL || index < 0 ||
        index >= TIKU_REMOTE_SESSIONS || !listener->session[index].used) {
        return 0;
    }
    return listener->session[index].said_lost;
}

/**
 * @brief Flatten @p m and send it as @p type: queued to @p session when
 *        one is given, else written down @p fd.
 *
 * The size is asked for first and the buffer taken to fit, so a message
 * is never half-written onto a wire the other end is reading in frames.
 * @return 0 when it could not be flattened, the fd is closed or the
 *         session refused it; else 1.
 */
static int
send_flat(int *fd, uint32_t type, const tiku_msg_t *m,
          tiku_remote_session_t *session)
{
    size_t n = tiku_msg_flat_size(m);
    unsigned char *flat;

    if (*fd < 0 || n == 0u || n > REMOTE_MAX_PAYLOAD) {
        return 0;
    }
    flat = malloc(n);
    if (flat == NULL) {
        return 0;
    }
    if (!tiku_msg_flatten(m, flat, n, NULL)) {
        free(flat);
        return 0;
    }
    int sent = 1;
    if (session) sent = session_send(session, type, flat, (uint32_t)n);
    else {
        send_msg(fd, type, flat, (uint32_t)n);
        sent = *fd >= 0;
    }
    free(flat);
    return sent;
}

int tiku_remote_tell(tiku_remote_listener_t *listener, int index,
                      const tiku_msg_t *m)
{
    tiku_remote_session_t *s = session_at(listener, index);

    return (s != NULL) ? send_flat(&s->fd, TIKU_RMSG_TELL, m, s) : 0;
}

/* CLIENT                                                                    */

int tiku_remote_connect(tiku_remote_client_t *client,
                         const char *name, int wait_ms)
{
    struct sockaddr_un addr;
    char path[108];
    int waited = 0;

    memset(client, 0, sizeof *client);
    client->fd = -1;
    client->next_id = 1;
    if (tiku_remote_path(path, sizeof path) != 0) {
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
    for (;;) {
        client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client->fd < 0) {
            return -1;
        }
        if (connect(client->fd, (struct sockaddr *)&addr,
                    sizeof addr) == 0) {
            break;
        }
        (void)close(client->fd);
        client->fd = -1;
        if (waited >= wait_ms) {
            return -1;
        }
        usleep(100000);
        waited += 100;
    }
    return tiku_remote_connect_fd(client, name, client->fd);
}

int tiku_remote_connect_fd(tiku_remote_client_t *client,
                            const char *name, int fd)
{
    unsigned char payload[4 + 32 + 4];
    uint32_t version = TIKU_REMOTE_VERSION;
    uint32_t features = TIKU_FEAT_TEXT_V2;
    struct sockaddr_storage address;
    socklen_t len = sizeof address;

    if (fd < 0) {
        return -1;
    }
    /*
     * Cleared first: the client holds the message it last heard, and a
     * pointer nobody set would be freed at disconnect.
     */
    memset(client, 0, sizeof *client);
    client->fd = fd;
    client->next_id = 1;
    if (getsockname(fd, (struct sockaddr *)&address, &len) == 0 &&
        address.ss_family == AF_UNIX) {
        features |= TIKU_FEAT_SHARED_SURFACE | TIKU_FEAT_FRAME_SNAPSHOT;
    }
    client->features = features;
    memset(payload, 0, sizeof payload);
    memcpy(payload, &version, 4);
    snprintf((char *)payload + 4, 32, "%s", (name != NULL) ? name : "app");
    memcpy(payload + 36, &features, 4);
    send_msg(&client->fd, TIKU_RMSG_HELLO, payload, sizeof payload);
    (void)set_nonblock(client->fd);
    roomy(client->fd);
    return 0;
}

unsigned
tiku_remote_peer_features(const tiku_remote_client_t *client)
{
    return (client != NULL) ? client->peer_features : 0u;
}

int tiku_remote_draw(tiku_remote_client_t *client, uint32_t id,
                      const tiku_dl_t *dl, int w, int h)
{
    size_t n;
    unsigned char *payload;
    int ok;

    if (client == NULL || client->fd < 0 || dl == NULL ||
        w < 1 || h < 1 || w > TIKU_REMOTE_MAX_DIM ||
        h > TIKU_REMOTE_MAX_DIM) {
        return 0;
    }
    n = tiku_dl_flat_size(dl);
    if (12u + n > REMOTE_MAX_PAYLOAD) {
        return 0;
    }
    payload = malloc(12u + n);
    if (payload == NULL) {
        return 0;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &w, 4);
    memcpy(payload + 8, &h, 4);
    ok = (n == 0u) || tiku_dl_flatten(dl, payload + 12, n, NULL);
    if (ok) {
        send_msg(&client->fd, TIKU_RMSG_DRAW, payload,
                 (uint32_t)(12u + n));
        ok = client->fd >= 0;
    }
    free(payload);
    return ok;
}

int tiku_remote_say(tiku_remote_client_t *client,
                     const tiku_msg_t *m)
{
    return client ? send_flat(&client->fd, TIKU_RMSG_SAY, m, NULL) : 0;
}

tiku_msg_t *
tiku_remote_heard(tiku_remote_client_t *client)
{
    tiku_msg_t *m;

    if (client == NULL) {
        return NULL;
    }
    m = client->heard;
    client->heard = NULL;
    return m;
}

void tiku_remote_disconnect(tiku_remote_client_t *client)
{
    tiku_msg_free(client->heard);
    client->heard = NULL;
    if (client->shared != NULL) {
        (void)munmap(client->shared, client->shared_bytes);
        client->shared = NULL;
        client->shared_bytes = 0u;
    }
    if (client->fd >= 0) {
        send_msg(&client->fd, TIKU_RMSG_CLOSE, NULL, 0);
        (void)close(client->fd);
        client->fd = -1;
    }
}

uint32_t tiku_remote_open(tiku_remote_client_t *client, const char *title,
                      int w, int h)
{
    unsigned char payload[12 + TIKU_REMOTE_TITLE];
    uint32_t id = client->next_id++;
    int32_t ww = w, hh = h;

    if (client->fd < 0 || w < 1 || h < 1 ||
        w > TIKU_REMOTE_MAX_DIM || h > TIKU_REMOTE_MAX_DIM) {
        return 0;
    }
    memset(payload, 0, sizeof payload);
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    snprintf((char *)payload + 12, TIKU_REMOTE_TITLE, "%s",
             (title != NULL) ? title : "");
    send_msg(&client->fd, TIKU_RMSG_OPEN, payload, sizeof payload);
    return client->fd >= 0 ? id : 0;
}

void tiku_remote_close_window(tiku_remote_client_t *client, uint32_t id)
{
    if (client == NULL || client->fd < 0) {
        return;
    }
    send_msg(&client->fd, TIKU_RMSG_CLOSE, &id, 4);
}

void tiku_remote_raise(tiku_remote_client_t *client, uint32_t id)
{
    if (client == NULL || client->fd < 0) {
        return;
    }
    send_msg(&client->fd, TIKU_RMSG_RAISE, &id, 4);
}

void tiku_remote_place(tiku_remote_client_t *client, uint32_t id, int role)
{
    unsigned char payload[8];
    int32_t r = role;

    memcpy(payload, &id, 4);
    memcpy(payload + 4, &r, 4);
    send_msg(&client->fd, TIKU_RMSG_PLACE, payload, sizeof payload);
}

void tiku_remote_sizeable(tiku_remote_client_t *client, uint32_t id)
{
    if (client == NULL || client->fd < 0) {
        return;
    }
    send_msg(&client->fd, TIKU_RMSG_SIZEABLE, &id, 4);
}

int tiku_remote_configure(tiku_remote_client_t *client, uint32_t id,
    const char *title, int w, int h, int resize, int zoom)
{
    unsigned char p[20 + TIKU_REMOTE_TITLE] = {0};
    int32_t v[4] = {w, h, !!resize, !!zoom};
    if (!client || client->fd < 0 || !id || w < 0 || h < 0 ||
        w > TIKU_REMOTE_MAX_DIM || h > TIKU_REMOTE_MAX_DIM || (!w != !h))
        return 0;
    memcpy(p, &id, 4); memcpy(p + 4, v, sizeof v);
    snprintf((char *)p + 20, TIKU_REMOTE_TITLE, "%s", title ? title : "");
    send_msg(&client->fd, TIKU_RMSG_CONFIGURE, p, sizeof p);
    return client->fd >= 0;
}

void tiku_remote_describe(tiku_remote_client_t *client, uint32_t id,
                          const char *text)
{
    unsigned char p[4100];
    if (!client || client->fd < 0 || !id) return;
    size_t n = text ? strnlen(text, 4095) : 0;
    memcpy(p, &id, 4);
    if (n) memcpy(p + 4, text, n);
    p[4 + n] = 0;
    send_msg(&client->fd, TIKU_RMSG_DESCRIBE, p, (uint32_t)(5 + n));
}

/**
 * @brief Make a surface both processes can see, and tell the desktop.
 *
 * @return nonzero when the surface is there to paint into.
 */
static int
client_share(tiku_remote_client_t *client, uint32_t id, int w, int h)
{
    unsigned char payload[12 + TIKU_REMOTE_SHM_NAME];
    size_t bytes = 4u * (size_t)w * (size_t)h * TIKU_REMOTE_BUFFERS;
    int32_t ww = w, hh = h;
    void *at;
    int fd;

    if (client->shared != NULL && client->shared_id == id &&
        client->shared_w == w &&
        client->shared_h == h) {
        return 1;
    }
    if (client->shared != NULL) {
        (void)munmap(client->shared, client->shared_bytes);
        client->shared = NULL;
    }
    snprintf(client->shm_name, sizeof client->shm_name,
             "/tiku-desk-%ld-%u-%u", (long)getpid(), (unsigned)id,
             (unsigned)++client->shared_serial);
    /* Each queued SURFACE owns its name.  A reused name would let a slow
     * desktop map the replacement for the earlier message, then unlink it
     * before the replacement's message is processed. */
    fd = shm_open(client->shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return 0;
    }
    if (ftruncate(fd, (off_t)bytes) != 0) {
        (void)close(fd);
        (void)shm_unlink(client->shm_name);
        return 0;
    }
    {
        /* With no room in the shared store the pixels are copied
         * instead: a mapping without its pages faults on the first
         * write. */
        int reserved = tiku_sys_reserve(fd, (long long)bytes);

        if (reserved) {
            tiku_trail("shared surface %dx%d unavailable: error %d",
                       w, h, reserved);
            (void)close(fd);
            (void)shm_unlink(client->shm_name);
            return 0;
        }
    }
    at = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    (void)close(fd);
    if (at == MAP_FAILED) {
        (void)shm_unlink(client->shm_name);
        return 0;
    }
    client->shared = at;
    client->shared_id = id;
    client->shared_bytes = bytes;
    client->shared_w = w;
    client->shared_h = h;
    client->next_buffer = 0;
    memset(client->busy, 0, sizeof client->busy);
    memset(payload, 0, sizeof payload);
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    snprintf((char *)payload + 12, TIKU_REMOTE_SHM_NAME, "%s",
             client->shm_name);
    send_msg(&client->fd, TIKU_RMSG_SURFACE, payload, sizeof payload);
    /* The desktop unlinks the name once it has mapped it; this end keeps
     * only the mapping. */
    return client->fd >= 0;
}

int tiku_remote_frame(tiku_remote_client_t *client, uint32_t id,
                       const uint32_t *px, int w, int h)
{
    size_t n = 12u + 4u * (size_t)w * (size_t)h;
    unsigned char *payload;
    int32_t ww = w, hh = h;

    if (client == NULL || client->fd < 0 || id == 0u ||
        px == NULL || w < 1 || h < 1 ||
        w > TIKU_REMOTE_MAX_DIM || h > TIKU_REMOTE_MAX_DIM) {
        return -1;
    }
    if ((client->features & client->peer_features &
         TIKU_FEAT_FRAME_SNAPSHOT) != 0u &&
        client->frame_serial != UINT64_MAX &&
        client_share(client, id, w, h)) {
        size_t one = 4u * (size_t)w * (size_t)h;
        unsigned char ready[16];
        uint32_t index = (uint32_t)client->next_buffer;

        for (unsigned tried = 0; tried < TIKU_REMOTE_BUFFERS; tried++) {
            if (!client->busy[index]) {
                uint64_t token = ++client->frame_serial;

                memcpy((unsigned char *)client->shared + index * one, px, one);
                memcpy(ready, &id, 4);
                memcpy(ready + 4, &index, 4);
                memcpy(ready + 8, &token, 8);
                client->busy[index] = token;
                send_msg(&client->fd, TIKU_RMSG_READY, ready, sizeof ready);
                client->next_buffer = (index + 1) % TIKU_REMOTE_BUFFERS;
                return client->fd >= 0 ? 0 : -1;
            }
            index = (index + 1) % TIKU_REMOTE_BUFFERS;
        }
    }
    if (client->fd < 0) return -1;
    payload = malloc(n);
    if (payload == NULL) {
        return -1;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, &ww, 4);
    memcpy(payload + 8, &hh, 4);
    memcpy(payload + 12, px, 4u * (size_t)w * (size_t)h);
    send_msg(&client->fd, TIKU_RMSG_FRAME, payload, (uint32_t)n);
    free(payload);
    return client->fd >= 0 ? 0 : -1;
}

int tiku_remote_menus_from_text(const char *text, tiku_menuset_t *set)
{
    const char *p = text;

    if (text == NULL || set == NULL) {
        return 0;
    }
    memset(set, 0, sizeof *set);
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[160], word[16];
        tiku_menu_list_t *menu =
            set->nmenu > 0 ? &set->menu[set->nmenu - 1] : NULL;
        int n;

        if (len >= sizeof line) {
            len = sizeof line - 1u;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        while (len > 0u && (line[len - 1u] == '\r' || line[len - 1u] == ' ')) {
            line[--len] = '\0';
        }
        p += (eol ? (size_t)(eol - p) + 1u : strlen(p));
        if (sscanf(line, "%15s%n", word, &n) < 1) {
            continue;
        }
        if (strcmp(word, "menu") == 0) {
            if (set->nmenu < TIKU_MENUSET_MENUS) {
                menu = &set->menu[set->nmenu++];
                memset(menu, 0, sizeof *menu);
                snprintf(menu->title, sizeof menu->title, "%s",
                         line + n + (line[n] == ' '));
            }
        } else if (menu != NULL && menu->nitem < TIKU_MENUSET_ITEMS &&
                   strcmp(word, "sep") == 0) {
            menu->item[menu->nitem++].separator = 1;
        } else if (menu != NULL && menu->nitem < TIKU_MENUSET_ITEMS &&
                   strcmp(word, "item") == 0) {
            tiku_menu_entry_t *e = &menu->item[menu->nitem];
            char sc[8];
            int m2;

            if (sscanf(line + n, "%d %7s%n", &e->command, sc, &m2) < 2) {
                continue;
            }
            e->sc = (sc[0] != '-' && sc[1] == '\0') ? sc[0] : 0;
            e->mods = e->sc ? TIKU_MOD_CMD : 0u;
            e->enabled = 1;
            snprintf(e->label, sizeof e->label, "%s",
                     line + n + m2 + (line[n + m2] == ' '));
            menu->nitem++;
        }
    }
    return set->nmenu;
}

int tiku_remote_menus(tiku_remote_client_t *client, uint32_t id,
                       const tiku_menuset_t *menus)
{
    size_t n = 4u + sizeof *menus;
    unsigned char *payload;

    if (client->fd < 0 || menus == NULL) {
        return -1;
    }
    payload = malloc(n);
    if (payload == NULL) {
        return -1;
    }
    memcpy(payload, &id, 4);
    memcpy(payload + 4, menus, sizeof *menus);
    send_msg(&client->fd, TIKU_RMSG_MENUS, payload, (uint32_t)n);
    free(payload);
    return 0;
}

int tiku_remote_read(tiku_remote_client_t *client, uint32_t *id,
                      tiku_event_t *event, int *command)
{
    uint32_t head[2];
    ssize_t got;
    int waited;

    if (client->fd < 0) {
        return -1;
    }
    /* The header accumulates across calls: a serial line has no peek. */
    while (client->hgot < sizeof client->hbuf) {
        got = read(client->fd, client->hbuf + client->hgot,
                   sizeof client->hbuf - client->hgot);
        if (got == 0) {
            return -1;
        }
        if (got < 0) {
            return (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) ? 0 : -1;
        }
        client->hgot += (size_t)got;
    }
    memcpy(head, client->hbuf, sizeof head);
    {
        size_t n = head[1];
        unsigned char *buf;
        size_t at = 0;

        if (head[1] > REMOTE_MAX_DOWN) {
            return -1;
        }
        buf = malloc((n > 0u) ? n : 1u);
        if (buf == NULL) {
            return -1;
        }
        /* Mid-message the header is taken, so the body is read to its end
         * here, waiting at most REMOTE_STALL_MS for a peer gone quiet. */
        waited = 0;
        while (at < n) {
            got = read(client->fd, buf + at, n - at);
            if (got == 0) {
                free(buf);
                return -1;
            }
            if (got < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if ((errno == EAGAIN || errno == EWOULDBLOCK) &&
                    wait_ready(client->fd, POLLIN, &waited) == 0) {
                    continue;
                }
                free(buf);
                return -1;
            }
            at += (size_t)got;
            waited = 0;
        }
        client->hgot = 0u;
        if (id != NULL && head[1] >= 4u) {
            memcpy(id, buf, 4);
        }
        if (head[0] == TIKU_RMSG_EVENT && event != NULL &&
            head[1] >= 4u + sizeof *event) {
            memcpy(event, buf + 4, sizeof *event);
        }
        if (head[0] == TIKU_RMSG_PICK && command != NULL &&
            head[1] >= 8u) {
            int32_t c;

            memcpy(&c, buf + 4, 4);
            *command = c;
        }
        if (head[0] == TIKU_RMSG_WELCOME && head[1] >= 8u) {
            uint32_t v, f;

            memcpy(&v, buf, 4);
            memcpy(&f, buf + 4, 4);
            client->peer_version = (int)v;
            client->peer_features = f;
        }
        if (head[0] == TIKU_RMSG_RELEASE && head[1] == 16u) {
            uint32_t window, index;
            uint64_t token;

            memcpy(&window, buf, 4);
            memcpy(&index, buf + 4, 4);
            memcpy(&token, buf + 8, 8);
            if (window == client->shared_id && index < TIKU_REMOTE_BUFFERS &&
                token && client->busy[index] == token)
                client->busy[index] = 0;
        }
        if (head[0] == TIKU_RMSG_TELL) {
            /* One at a time: a message not taken by now is dropped. */
            tiku_msg_free(client->heard);
            client->heard =
                tiku_msg_unflatten(buf, n, NULL);
        }
        free(buf);
    }
    return (int)head[0];
}

int
tiku_remote_session_tell(tiku_remote_session_t *session,
                         const tiku_msg_t *message)
{
    return session && session->used
               ? send_flat(&session->fd, TIKU_RMSG_TELL, message, session)
               : 0;
}
