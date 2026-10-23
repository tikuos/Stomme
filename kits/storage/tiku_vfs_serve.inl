/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_vfs_serve.inl - the desktop's side of the namespace proxy,
 * included by tiku_vfs_rpc.c: tiku_vfs_serve() answers one request from
 * the backend that holds the board.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @brief A listing being put into a reply. */
typedef struct { tiku_msg_t *reply; int count, failed; } rpc_listing_t;

/** @brief Add one Model to a reply: its name, kind and permission bits. */
static int model_put(tiku_msg_t *reply, const tiku_model_t *m)
{
    return tiku_msg_add_string(reply, "name", m->name) &&
        tiku_msg_add_int32(reply, "kind", m->kind) &&
        tiku_msg_add_int32(reply, "perm", (int32_t)m->facts.perm);
}

/** @brief Add one listed entry to the reply, at most TIKU_VFS_RPC_ROWS: one
 *         more than an inspector's 128 rows, so it can tell the listing was
 *         cut. */
static int serve_entry(const tiku_model_t *m, void *ctx)
{
    rpc_listing_t *l = ctx;
    if (l->count == TIKU_VFS_RPC_ROWS) return 1;
    if (!model_put(l->reply, m)) { l->failed = 1; return 1; }
    l->count++;
    return 0;
}

/**
 * @brief Write the request's value when the node is a writable leaf that
 *        still holds the value the requester last read; the check and the
 *        write happen in one turn of the device's worker.
 * @return 1 when written.
 */
static int serve_write(tiku_backend_t *b, const tiku_msg_t *request,
                        const char *path, char *error, size_t max)
{
    size_t size = 0, expected_size = 0;
    const void *data = tiku_msg_find_blob(request, "data", 0, &size);
    const void *expected = tiku_msg_find_blob(request, "expected", 0, &expected_size);
    unsigned char current[TIKU_VFS_RPC_BYTES];
    tiku_model_t model;
    if (!data || !expected || !size || size > 512 ||
        expected_size >= sizeof current || memchr(data, 0, size) ||
        !b->ops->stat || !b->ops->read || !b->ops->write ||
        b->ops->stat(b, path, &model) || tiku_model_is_container(&model) ||
        !tiku_model_is_writable(&model)) return 0;
    int n = b->ops->read(b, path, current, sizeof current);
    if (n < 0 || (size_t)n != expected_size || memcmp(current, expected, expected_size)) {
        snprintf(error, max, "Value changed on the device. Refresh before saving.");
        return 0;
    }
    return b->ops->write(b, path, data, size, error, max) == 0;
}

tiku_msg_t *tiku_vfs_serve(tiku_backend_t *b, const tiku_msg_t *request)
{
    tiku_msg_t *reply = tiku_msg_new(TIKU_MSG_VFS_REPLY);
    int32_t sequence = 0, version = 0;
    int ok = 0;
    char error[256] = "Device unavailable, invalid request or unsupported operation.";
    if (!reply) return NULL;
    tiku_msg_find_int32(request, "sequence", 0, &sequence);
    tiku_msg_find_int32(request, "version", 0, &version);
    tiku_msg_add_int32(reply, "sequence", sequence);
    tiku_msg_add_string(reply, "key", b ? b->devid : "");
    const char *path = tiku_msg_find_string(request, "path", 0);
    const char *op = tiku_msg_find_string(request, "op", 0);
    const char *key = tiku_msg_find_string(request, "key", 0);
    if (tiku_msg_what(request) != TIKU_MSG_VFS_REQUEST ||
        tiku_msg_flat_size(request) > 8192 ||
        !b || !b->ops || version != 1 || sequence <= 0 || !valid_path(path) ||
        !op || !key || strlen(key) >= sizeof b->devid ||
        (*key && strcmp(key, b->devid))) goto done;
    if (!strcmp(op, "stat") && b->ops->stat) {
        tiku_model_t model;
        ok = b->ops->stat(b, path, &model) == 0 && model_put(reply, &model);
    } else if (!strcmp(op, "list") && b->ops->list) {
        rpc_listing_t listing = {.reply = reply};
        ok = b->ops->list(b, path, serve_entry, &listing) >= 0 && !listing.failed;
        ok &= tiku_msg_add_int32(reply, "count", listing.count);
    } else if (!strcmp(op, "read") && b->ops->read) {
        unsigned char value[TIKU_VFS_RPC_BYTES];
        int n = b->ops->read(b, path, value, sizeof value);
        ok = n >= 0 && n < (int)sizeof value &&
             tiku_msg_add_blob(reply, "data", value, (size_t)n);
    } else if (!strcmp(op, "write") && *key) {
        ok = serve_write(b, request, path, error, sizeof error);
    }
done:
    if (!tiku_msg_add_int32(reply, "ok", ok) ||
        (!ok && !tiku_msg_add_string(reply, "error", error))) {
        tiku_msg_free(reply); return NULL;
    }
    return reply;
}
