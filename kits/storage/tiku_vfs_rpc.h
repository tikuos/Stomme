/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_rpc.h - a board's namespace reached through the desktop that
 * holds its connection.
 *
 * An application has no line to the board: its proxy backend sends a
 * TIKU_MSG_VFS_REQUEST to the desktop, which answers from the board's own
 * backend with a TIKU_MSG_VFS_REPLY (tiku_vfs_serve()).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VFS_RPC_H
#define TIKU_VFS_RPC_H
#include "tiku_model.h"
#include "tiku_msg.h"

/** @brief The message codes of a request and of its reply. */
#define TIKU_MSG_VFS_REQUEST 0x76667371u
#define TIKU_MSG_VFS_REPLY   0x76667372u
/** @brief A value carried is shorter than this many bytes. */
#define TIKU_VFS_RPC_BYTES 4096
/** @brief The most rows a listing carries. */
#define TIKU_VFS_RPC_ROWS 129

/**
 * @brief A backend for @p board whose operations are requests to the
 *        desktop.
 *
 * @note One worker thread calls its operations, each blocking until the
 *       reply comes or 15 seconds pass; tiku_vfs_proxy_pump() and
 *       tiku_vfs_proxy_reply() run on the app's UI thread.  Close it only
 *       once that worker has finished.
 * @return The backend, or NULL for an empty name or one with a '/'.
 */
tiku_backend_t *tiku_vfs_proxy_new(const char *board);

/** @brief Hand the waiting request, if any, to @p send, once; a send that
 *         fails cancels the proxy. */
void tiku_vfs_proxy_pump(tiku_backend_t *b,
    int (*send)(void *, const tiku_msg_t *), void *ctx);

/** @brief Deliver the desktop's reply to the waiting request; a reply to
 *         any other request is dropped. */
void tiku_vfs_proxy_reply(tiku_backend_t *b, const tiku_msg_t *reply);

/** @brief Wake the worker and fail every request from now on. */
void tiku_vfs_proxy_cancel(tiku_backend_t *b);

/**
 * @brief Answer one request from @p backend: stat, list, read, or a write,
 *        which needs the device's key and the value the requester last
 *        read.
 *
 * @note Call it on the worker that owns the device, so it is serialised
 *       with the device's other traffic.  The caller owns the reply and
 *       delivers it only to the session that asked.
 * @return The reply, or NULL when it cannot be built.
 */
tiku_msg_t *tiku_vfs_serve(tiku_backend_t *backend, const tiku_msg_t *request);
#endif
