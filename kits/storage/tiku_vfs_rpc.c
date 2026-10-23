/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_rpc.c - the namespace proxy an application uses, and the
 * desktop's side that serves it (tiku_vfs_serve.inl).
 *
 * Messages are versioned and bounded: a value is under TIKU_VFS_RPC_BYTES
 * and a listing at most TIKU_VFS_RPC_ROWS rows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_vfs_rpc.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief A proxy: the one request in flight, its reply, and the value
 *         last read, which a write is checked against. */
typedef struct {
    tiku_backend_t base;
    pthread_mutex_t lock;
    pthread_cond_t ready;
    tiku_msg_t *request, *reply;
    int cancelled, sent;
    int32_t sequence;
    char board[64], key[40], last_path[256];
    unsigned char last_value[TIKU_VFS_RPC_BYTES];
    size_t last_size;
} proxy_t;

/** @brief Whether @p path is canonical: absolute, under 256 bytes, with no
 *         control character and no empty, "." or ".." component. */
static int valid_path(const char *path)
{
    if (!path || *path != '/' || strlen(path) >= 256) return 0;
    for (const char *p = path; *p; p++) {
        if ((unsigned char)*p < 32 || (unsigned char)*p == 127) return 0;
        if (*p == '/' && (p[1] == '/' ||
            (p[1] == '.' && (!p[2] || p[2] == '/' ||
            (p[2] == '.' && (!p[3] || p[3] == '/')))))) return 0;
    }
    return 1;
}

/**
 * @brief Post one request and wait up to 15 seconds for its reply; the
 *        device's key is learnt from the first reply.
 * @return The reply, or NULL on a bad path, a cancel, a timeout or a
 *         request already in flight.
 */
static tiku_msg_t *exchange(proxy_t *p, const char *op, const char *path,
                             const void *data, size_t size)
{
    if (!valid_path(path)) return NULL;
    pthread_mutex_lock(&p->lock);
    if (p->cancelled || p->request) { pthread_mutex_unlock(&p->lock); return NULL; }
    tiku_msg_t *m = tiku_msg_new(TIKU_MSG_VFS_REQUEST);
    if (p->sequence == INT32_MAX) p->sequence = 0;
    int ok = m && tiku_msg_add_int32(m, "version", 1) &&
        tiku_msg_add_int32(m, "sequence", ++p->sequence) &&
        tiku_msg_add_string(m, "board", p->board) &&
        tiku_msg_add_string(m, "key", p->key) &&
        tiku_msg_add_string(m, "op", op) && tiku_msg_add_string(m, "path", path);
    if (ok && data) ok = tiku_msg_add_blob(m, "data", data, size) &&
        tiku_msg_add_blob(m, "expected", p->last_value, p->last_size);
    if (!ok) { tiku_msg_free(m); pthread_mutex_unlock(&p->lock); return NULL; }
    p->request = m; p->sent = 0;
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 15;
    while (!p->reply && !p->cancelled)
        if (pthread_cond_timedwait(&p->ready, &p->lock, &until) == ETIMEDOUT) break;
    tiku_msg_t *reply = p->reply;
    p->reply = NULL; p->request = NULL;
    if (reply) {
        const char *key = tiku_msg_find_string(reply, "key", 0);
        if (key && !p->key[0]) snprintf(p->key, sizeof p->key, "%s", key);
    }
    pthread_mutex_unlock(&p->lock);
    tiku_msg_free(m);
    return reply;
}

/** @brief Whether a reply says ok; a reply with no status is a failure. */
static int succeeded(const tiku_msg_t *m)
{
    int32_t ok = 0;
    return m && tiku_msg_find_int32(m, "ok", 0, &ok) && ok == 1;
}

/** @brief Row @p index of a reply as a Model: the name, kind and permission
 *         bits a wire model carries. */
static int model_get(const tiku_msg_t *m, int index, tiku_model_t *out)
{
    const char *name = tiku_msg_find_string(m, "name", index);
    int32_t kind = 0, perm = 0;
    if (!name || strlen(name) >= sizeof out->name ||
        !tiku_msg_find_int32(m, "kind", index, &kind) ||
        !tiku_msg_find_int32(m, "perm", index, &perm)) return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", name);
    out->kind = kind; out->facts.perm = (unsigned)perm;
    return 1;
}

/** @brief Stat @p path through the desktop.  0, or -1. */
static int proxy_stat(tiku_backend_t *b, const char *path, tiku_model_t *out)
{
    tiku_msg_t *m = exchange(b->impl, "stat", path, NULL, 0);
    int ok = succeeded(m) && model_get(m, 0, out);
    if (ok) { out->backend = b; snprintf(out->path, sizeof out->path, "%s", path); }
    tiku_msg_free(m); return ok ? 0 : -1;
}

/** @brief List @p path through the desktop.  Entries, or -1. */
static int proxy_list(tiku_backend_t *b, const char *path, tiku_entry_fn fn, void *ctx)
{
    tiku_msg_t *m = exchange(b->impl, "list", path, NULL, 0);
    int32_t count = 0;
    if (!succeeded(m) || !tiku_msg_find_int32(m, "count", 0, &count) ||
        count < 0 || count > TIKU_VFS_RPC_ROWS) { tiku_msg_free(m); return -1; }
    for (int i = 0; i < count; i++) {
        tiku_model_t out;
        if (!model_get(m, i, &out)) { count = -1; break; }
        out.backend = b;
        if (fn && fn(&out, ctx)) break;
    }
    tiku_msg_free(m); return count;
}

/** @brief Read @p path through the desktop, and keep the value for a later
 *         write's check.  Bytes, or -1. */
static int proxy_read(tiku_backend_t *b, const char *path, void *out, size_t max)
{
    proxy_t *p = b->impl;
    tiku_msg_t *m = exchange(p, "read", path, NULL, 0);
    size_t size = 0;
    const void *data = m ? tiku_msg_find_blob(m, "data", 0, &size) : NULL;
    int ok = succeeded(m) && data && size <= max && size < TIKU_VFS_RPC_BYTES;
    if (ok) {
        memcpy(out, data, size); memcpy(p->last_value, data, size);
        p->last_size = size;
        snprintf(p->last_path, sizeof p->last_path, "%s", path);
    }
    tiku_msg_free(m); return ok ? (int)size : -1;
}

/**
 * @brief Write @p path through the desktop: at most 512 bytes, to the path
 *        this proxy last read, and taken only when the device still holds
 *        the value read.
 * @return 0, or -1 with @p error filled.
 */
static int proxy_write(tiku_backend_t *b, const char *path, const void *data,
                       size_t size, char *error, size_t max)
{
    proxy_t *p = b->impl;
    tiku_msg_t *m = NULL;
    if (size <= 512 && p->key[0] && !strcmp(p->last_path, path))
        m = exchange(p, "write", path, data, size);
    int ok = succeeded(m);
    const char *why = m ? tiku_msg_find_string(m, "error", 0) : NULL;
    if (error && max) snprintf(error, max, "%s", ok ? "" : why ? why :
        "Device unavailable or value not read. Refresh before saving.");
    p->last_path[0] = 0;
    tiku_msg_free(m); return ok ? 0 : -1;
}

void tiku_vfs_proxy_cancel(tiku_backend_t *b)
{
    if (!b) return;
    proxy_t *p = b->impl;
    pthread_mutex_lock(&p->lock); p->cancelled = 1;
    pthread_cond_broadcast(&p->ready); pthread_mutex_unlock(&p->lock);
}

/** @brief Free the proxy and any reply left waiting. */
static void proxy_close(tiku_backend_t *b)
{
    proxy_t *p = b->impl;
    pthread_mutex_destroy(&p->lock); pthread_cond_destroy(&p->ready);
    tiku_msg_free(p->reply); free(p);
}

static const tiku_backend_ops_t proxy_ops = {
    .name = "desktop-vfs", .stat = proxy_stat, .list = proxy_list,
    .read = proxy_read, .write = proxy_write, .close = proxy_close
};

tiku_backend_t *tiku_vfs_proxy_new(const char *board)
{
    if (!board || !*board || strlen(board) >= 64 || strchr(board, '/')) return NULL;
    proxy_t *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    if (pthread_mutex_init(&p->lock, NULL)) { free(p); return NULL; }
    if (pthread_cond_init(&p->ready, NULL)) {
        pthread_mutex_destroy(&p->lock); free(p); return NULL;
    }
    p->base.ops = &proxy_ops; p->base.impl = p;
    snprintf(p->board, sizeof p->board, "%s", board);
    return &p->base;
}

void tiku_vfs_proxy_pump(tiku_backend_t *b,
    int (*send)(void *, const tiku_msg_t *), void *ctx)
{
    if (!b) return;
    proxy_t *p = b->impl;
    pthread_mutex_lock(&p->lock);
    tiku_msg_t *m = p->request && !p->sent && !p->cancelled ?
                    tiku_msg_copy(p->request) : NULL;
    if (m) p->sent = 1;
    pthread_mutex_unlock(&p->lock);
    if (m && (!send || !send(ctx, m))) tiku_vfs_proxy_cancel(b);
    tiku_msg_free(m);
}

void tiku_vfs_proxy_reply(tiku_backend_t *b, const tiku_msg_t *reply)
{
    if (!b || !reply || tiku_msg_what(reply) != TIKU_MSG_VFS_REPLY) return;
    proxy_t *p = b->impl;
    int32_t sequence = 0;
    tiku_msg_find_int32(reply, "sequence", 0, &sequence);
    pthread_mutex_lock(&p->lock);
    if (p->request && !p->reply && sequence == p->sequence) {
        p->reply = tiku_msg_copy(reply);
        pthread_cond_signal(&p->ready);
    }
    pthread_mutex_unlock(&p->lock);
}

#include "tiku_vfs_serve.inl"
