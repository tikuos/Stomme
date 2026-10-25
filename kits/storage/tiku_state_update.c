/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_state_update.c - copied record merges and guarded sidecar replacement.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_state_update.h"
#include "tiku_state_private.h"
#include "tiku_atomic_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define UPDATE_BYTES (4u * 1024u * 1024u)
#define UPDATE_RECORDS 4096u

/** @brief A complete regular-file capture, including absence. */
typedef struct { unsigned char *data; size_t size; int exists; } update_file_t;
struct tiku_store_update {
    tiku_store_t *source, *before, *baseline, *model, *saved;
    char path[512];
    int persistent, started, finished;
    atomic_int cancelled;
    update_file_t file;
    tiku_store_update_result_t result;
};

/** @brief Find an exact record in copied cache data. */
static state_rec_t *find(const tiku_store_t *store, const char *node, const char *attr)
{
    state_rec_t *record;
    for (record = store ? store->recs : NULL; record; record = record->next)
        if (!strcmp(record->node, node) && !strcmp(record->attr, attr)) return record;
    return NULL;
}

/** @brief Compare record presence and complete value bytes. */
static int same(const state_rec_t *a, const state_rec_t *b)
{
    return (!a || !b) ? a == b : a->len == b->len && (!a->len || !memcmp(a->val, b->val, a->len));
}

/** @brief Count and bound serialized record storage before copying it. */
static int bounded(const tiku_store_t *store)
{
    size_t bytes = strlen(SIDECAR_MAGIC), count = 0; state_rec_t *r;
    for (r = store->recs; r; r = r->next) {
        bytes += 12 + strlen(r->node) + strlen(r->attr) + r->len;
        if (++count > UPDATE_RECORDS || bytes > UPDATE_BYTES) return 0;
    }
    return 1;
}

tiku_store_t *tiku_state_private_copy(const tiku_store_t *store)
{
    tiku_store_t *copy; state_rec_t *r;
    if (!store || !bounded(store)) return NULL;
    copy = tiku_store_memory_open(); if (!copy) return NULL;
    for (r = store->recs; r; r = r->next) {
        if (tiku_state_write(copy, r->node, r->attr, r->val, r->len)) { tiku_store_free(copy); return NULL; }
    }
    copy->dirty = 0; return copy;
}

/** @brief Remove one copied record, preserving other attributes of its node. */
static void remove_record(tiku_store_t *store, const char *node, const char *attr)
{
    state_rec_t **link = &store->recs;
    while (*link) {
        state_rec_t *r = *link;
        if (!strcmp(r->node, node) && !strcmp(r->attr, attr)) { *link = r->next; free(r->val); free(r); return; }
        link = &r->next;
    }
}

/** @brief Merge local differences into remote records; optionally retain later local edits. */
static tiku_store_t *merge(const tiku_store_t *base, const tiku_store_t *local,
    const tiku_store_t *remote, int prefer_local, int *conflict)
{
    tiku_store_t *out = tiku_state_private_copy(remote); state_rec_t *r;
    if (!out) return NULL;
    for (r = base->recs; r; r = r->next) {
        const state_rec_t *next = find(local, r->node, r->attr), *disk = find(remote, r->node, r->attr);
        if (same(r, next)) continue;
        if (!same(disk, r) && !same(disk, next)) { *conflict = 1; if (!prefer_local) goto fail; }
        if (!next) remove_record(out, r->node, r->attr);
        else if (tiku_state_write(out, r->node, r->attr, next->val, next->len)) goto fail;
    }
    for (r = local->recs; r; r = r->next) {
        const state_rec_t *disk;
        if (find(base, r->node, r->attr)) continue;
        disk = find(remote, r->node, r->attr);
        if (disk && !same(disk, r)) { *conflict = 1; if (!prefer_local) goto fail; }
        if (tiku_state_write(out, r->node, r->attr, r->val, r->len)) goto fail;
    }
    if (!bounded(out)) goto fail;
    return out;
fail:
    tiku_store_free(out); return NULL;
}

/** @brief Whether two cached stores contain the same records regardless of list order. */
static int stores_same(const tiku_store_t *a, const tiku_store_t *b)
{
    state_rec_t *r;
    for (r = a->recs; r; r = r->next) if (!same(r, find(b, r->node, r->attr))) return 0;
    for (r = b->recs; r; r = r->next) if (!find(a, r->node, r->attr)) return 0;
    return 1;
}

/** @brief Release captured file bytes. */
static void file_clear(update_file_t *file)
{
    free(file->data); memset(file, 0, sizeof *file);
}

/** @brief Capture bounded regular-file bytes without waiting on special files. */
static int file_read(const char *path, update_file_t *file)
{
    struct stat st; int fd; size_t used = 0; ssize_t n;
    memset(file, 0, sizeof *file);
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > UPDATE_BYTES) { close(fd); return -1; }
    file->data = malloc(UPDATE_BYTES + 1); if (!file->data) { close(fd); return -1; }
    while (used <= UPDATE_BYTES) {
        n = read(fd, file->data + used, UPDATE_BYTES + 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); file_clear(file); return -1; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd);
    if (used > UPDATE_BYTES) { file_clear(file); return -1; }
    file->exists = 1; file->size = used; return 0;
}

/** @brief Decode an entire sidecar, rejecting truncated, duplicate and invalid records. */
static tiku_store_t *decode(const update_file_t *file)
{
    size_t offset = strlen(SIDECAR_MAGIC), count = 0;
    tiku_store_t *store = tiku_store_memory_open();
    if (!store) return NULL;
    if (!file->exists) return store;
    if (file->size < offset || memcmp(file->data, SIDECAR_MAGIC, offset)) goto fail;
    while (offset < file->size) {
        uint32_t lengths[3]; char node[STATE_NODE_MAX], attr[STATE_ATTR_MAX];
        size_t total;
        if (++count > UPDATE_RECORDS || file->size - offset < sizeof lengths) goto fail;
        memcpy(lengths, file->data + offset, sizeof lengths); offset += sizeof lengths;
        if (lengths[0] >= sizeof node || lengths[1] >= sizeof attr || lengths[2] > STATE_VAL_MAX) goto fail;
        total = (size_t)lengths[0] + lengths[1] + lengths[2]; if (total > file->size - offset) goto fail;
        if (memchr(file->data + offset, 0, (size_t)lengths[0] + lengths[1])) goto fail;
        memcpy(node, file->data + offset, lengths[0]); node[lengths[0]] = 0; offset += lengths[0];
        memcpy(attr, file->data + offset, lengths[1]); attr[lengths[1]] = 0; offset += lengths[1];
        if (find(store, node, attr) || tiku_state_write(store, node, attr, file->data + offset, lengths[2])) goto fail;
        offset += lengths[2];
    }
    store->dirty = 0; return store;
fail:
    tiku_store_free(store); return NULL;
}

/** @brief Write the existing sidecar layout from a worker-owned merged store. */
static int produce(FILE *stream, void *context)
{
    tiku_store_update_t *update = context; state_rec_t *r;
    if (fwrite(SIDECAR_MAGIC, 1, strlen(SIDECAR_MAGIC), stream) != strlen(SIDECAR_MAGIC)) return -1;
    for (r = update->saved->recs; r; r = r->next) {
        uint32_t lengths[] = {(uint32_t)strlen(r->node), (uint32_t)strlen(r->attr), (uint32_t)r->len};
        if (fwrite(lengths, 1, sizeof lengths, stream) != sizeof lengths ||
            fwrite(r->node, 1, lengths[0], stream) != lengths[0] || fwrite(r->attr, 1, lengths[1], stream) != lengths[1] ||
            fwrite(r->val, 1, r->len, stream) != r->len) return -1;
    }
    return 0;
}

/** @brief Refuse a cancelled update or changed complete sidecar capture. */
static int unchanged(void *context)
{
    tiku_store_update_t *update = context; update_file_t current; int bad;
    if (atomic_load(&update->cancelled) || file_read(update->path, &current)) return -1;
    bad = current.exists != update->file.exists || current.size != update->file.size ||
        (current.size && memcmp(current.data, update->file.data, current.size));
    file_clear(&current); return bad ? -1 : 0;
}

tiku_store_update_t *tiku_store_update_begin(tiku_store_t *source)
{
    tiku_store_update_t *update;
    if (!source || source->kind == STORE_XATTR) { errno = ENOTSUP; return NULL; }
    if (!source->writable) { errno = EACCES; return NULL; }
    if (source->update) { errno = EBUSY; return NULL; }
    update = calloc(1, sizeof *update); if (!update) return NULL;
    atomic_init(&update->cancelled, 0);
    update->before = tiku_state_private_copy(source); update->model = tiku_state_private_copy(source);
    update->baseline = source->persisted ? tiku_state_private_copy(source->persisted) : tiku_store_memory_open();
    if (!update->before || !update->model || !update->baseline) { tiku_store_update_free(update); errno = ENOMEM; return NULL; }
    update->source = source; update->persistent = source->kind == STORE_SIDECAR;
    strcpy(update->path, source->path); source->update = update; return update;
}

tiku_store_t *tiku_store_update_model(tiku_store_update_t *update)
{
    return update && !update->started ? update->model : NULL;
}

void tiku_store_update_run(tiku_store_update_t *update)
{
    tiku_store_t *remote; int conflict = 0; tiku_atomic_result_t result;
    if (!update || update->started) return;
    update->started = 1; update->result = TIKU_STORE_UPDATE_FAILED;
    if (atomic_load(&update->cancelled)) { update->result = TIKU_STORE_UPDATE_CANCELLED; return; }
    if (!bounded(update->model)) return;
    if (!update->persistent) {
        update->saved = tiku_state_private_copy(update->model);
        update->result = update->saved ? TIKU_STORE_UPDATE_OK : TIKU_STORE_UPDATE_FAILED; return;
    }
    if (file_read(update->path, &update->file)) return;
    remote = decode(&update->file); if (!remote) return;
    update->saved = merge(update->baseline, update->model, remote, 0, &conflict);
    if (update->saved && !update->model->dirty && stores_same(update->saved, remote)) {
        tiku_store_free(remote); update->result = TIKU_STORE_UPDATE_OK; return;
    }
    tiku_store_free(remote);
    if (!update->saved) { update->result = conflict ? TIKU_STORE_UPDATE_CONFLICT : TIKU_STORE_UPDATE_FAILED; return; }
    result = tiku_atomic_file_guarded(update->path, produce, unchanged, update);
    update->result = result == TIKU_ATOMIC_OK ? TIKU_STORE_UPDATE_OK : result == TIKU_ATOMIC_UNCERTAIN ? TIKU_STORE_UPDATE_UNCERTAIN :
        result == TIKU_ATOMIC_CONFLICT ? (atomic_load(&update->cancelled) ? TIKU_STORE_UPDATE_CANCELLED : TIKU_STORE_UPDATE_CONFLICT) : TIKU_STORE_UPDATE_FAILED;
}

tiku_store_update_result_t tiku_store_update_finish(tiku_store_update_t *update)
{
    tiku_store_t *merged, old; int conflict = 0;
    if (!update || !update->started || update->finished || !update->source) return TIKU_STORE_UPDATE_FAILED;
    if (update->result != TIKU_STORE_UPDATE_OK && update->result != TIKU_STORE_UPDATE_UNCERTAIN) {
        update->source->update = NULL; update->source = NULL; update->finished = 1; return update->result;
    }
    merged = merge(update->before, update->source, update->saved, 1, &conflict);
    if (!merged) return TIKU_STORE_UPDATE_NO_MEMORY;
    old = *update->source;
    update->source->recs = merged->recs; merged->recs = NULL;
    update->source->persisted = update->saved; update->saved = NULL;
    update->source->dirty = !stores_same(update->source, update->source->persisted);
    update->source->needs_reload = 0;
    update->source->update = NULL;
    merged->recs = old.recs; tiku_store_free(merged); tiku_store_free(old.persisted);
    update->source = NULL; update->finished = 1;
    if (conflict) update->result = TIKU_STORE_UPDATE_UNCERTAIN;
    return update->result;
}

void tiku_store_update_cancel(tiku_store_update_t *update)
{
    if (update) atomic_store(&update->cancelled, 1);
}

void tiku_store_update_detach(tiku_store_update_t *update)
{
    if (!update) return;
    tiku_store_update_cancel(update);
    if (update->source) { update->source->update = NULL; update->source->needs_reload = 1; update->source = NULL; }
}

void tiku_store_update_free(tiku_store_update_t *update)
{
    if (!update) return;
    if (!update->started && update->source) {
        update->source->update = NULL; update->source = NULL;
    }
    tiku_store_update_detach(update);
    tiku_store_free(update->before); tiku_store_free(update->baseline); tiku_store_free(update->model); tiku_store_free(update->saved);
    file_clear(&update->file); free(update);
}

int tiku_store_persistent(const tiku_store_t *store)
{
    return store && store->kind == STORE_SIDECAR && store->writable;
}

int tiku_store_persisted_read(tiku_store_t *store, const char *node, const char *attr,
    void *buffer, size_t max)
{
    const tiku_store_t *saved;
    state_rec_t *record;
    size_t size;
    if (!store || !node || !attr || !buffer) { errno = EINVAL; return -1; }
    if (store->kind == STORE_XATTR) { errno = ENOTSUP; return -1; }
    saved = store->kind == STORE_SIDECAR ? store->persisted : store;
    if (!saved) { errno = EAGAIN; return -1; }
    record = find(saved, node, attr);
    if (!record) { errno = ENOENT; return -1; }
    size = record->len < max ? record->len : max;
    if (size) memcpy(buffer, record->val, size);
    return (int)size;
}
