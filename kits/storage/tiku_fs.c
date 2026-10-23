/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs.c - file operations, run in steps: the engine's entry points.
 *
 * An operation is a queue plus a cursor: tiku_fs_step() does a little and
 * returns, and a collision or an error stops the queue until it is answered.
 * Each tiku_fs_x() here calls fop_fs_x(), which does the work, with the
 * engine lock (fs->hand) held, except tiku_fs_new() and tiku_fs_free(),
 * which create and destroy that lock; tiku_fs_progress() copies the
 * progress under it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

tiku_fs_t *
tiku_fs_new(tiku_backend_t *backend)
{
    tiku_fs_t *fs = fop_fs_new(backend);

    if (fs != NULL) {
        (void)pthread_mutex_init(&fs->hand, NULL);
    }
    return fs;
}

void
tiku_fs_free(tiku_fs_t *fs)
{
    if (fs != NULL) {
        (void)pthread_mutex_destroy(&fs->hand);
        fop_fs_free(fs);
    }
}

const tiku_progress_t *
tiku_fs_progress(const tiku_fs_t *fs)
{
    tiku_fs_t *w = (tiku_fs_t *)fs;

    if (w == NULL) {
        return NULL;
    }
    /* A copy, taken with the engine lock held: the caller reads it while
     * a worker goes on changing the original. */
    fop_fs_take(w);
    w->prog_seen = w->prog;
    fop_fs_give(w);
    return &w->prog_seen;
}

void
tiku_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx)
{
    if (fs == NULL) {
        return;
    }
    fop_fs_take((tiku_fs_t *)fs);
    fop_fs_set_moved(fs, fn, ctx);
    fop_fs_give((tiku_fs_t *)fs);
}

tiku_op_t
tiku_fs_drop_action(tiku_fs_t *fs, const char *src, const char *dst_dir, int force_copy)
{
    tiku_op_t r;

    if (fs == NULL) {
        return (tiku_op_t)0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_drop_action(fs, src, dst_dir, force_copy);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_duplicate_name(tiku_fs_t *fs, const char *dir, const char *name, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_duplicate_name(fs, dir, name, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_link_name(tiku_fs_t *fs, const char *dir, const char *name, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_link_name(fs, dir, name, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_copy(tiku_fs_t *fs, const char *const *paths, int n, const char *dst_dir, tiku_conflict_t policy)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_copy(fs, paths, n, dst_dir, policy);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_move(tiku_fs_t *fs, const char *const *paths, int n, const char *dst_dir, tiku_conflict_t policy)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_move(fs, paths, n, dst_dir, policy);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_duplicate(fs, paths, n);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash(tiku_fs_t *fs, const char *const *paths, int n)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_trash(fs, paths, n);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

tiku_refusal_t
tiku_fs_check_move(tiku_fs_t *fs, const char *src, const char *dst_dir, char *msg, size_t max)
{
    tiku_refusal_t r;

    if (fs == NULL) {
        return (tiku_refusal_t)0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_check_move(fs, src, dst_dir, msg, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_delete(tiku_fs_t *fs, const char *const *paths, int n)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_delete(fs, paths, n);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX], int max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_trash_dirs(fs, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_prepare_trash(tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_prepare_trash(fs);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash_dirs_of(tiku_fs_t *fs, const struct tiku_volumes *vs, char out[][TIKU_PATH_MAX], int max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_trash_dirs_of(fs, vs, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_trash_dir(fs, path, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_has_trash(const tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_has_trash(fs);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_restore(tiku_fs_t *fs, const char *const *paths, int n, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_restore(fs, paths, n, err, errmax);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_original_path(tiku_fs_t *fs, const char *path, char *out, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_original_path(fs, path, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_empty_trash(tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_empty_trash(fs);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

tiku_delete_q_t
tiku_fs_delete_question(const tiku_fs_t *fs, const char *path, int shift_held)
{
    tiku_delete_q_t r;

    if (fs == NULL) {
        return (tiku_delete_q_t)0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_delete_question(fs, path, shift_held);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_rename(tiku_fs_t *fs, const char *path, const char *new_name, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_rename(fs, path, new_name, err, errmax);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_step(tiku_fs_t *fs, int budget)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_step(fs, budget);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

void
tiku_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how)
{
    if (fs == NULL) {
        return;
    }
    fop_fs_take((tiku_fs_t *)fs);
    fop_fs_resolve(fs, how);
    fop_fs_give((tiku_fs_t *)fs);
}

void
tiku_fs_cancel(tiku_fs_t *fs)
{
    if (fs == NULL) {
        return;
    }
    fop_fs_take((tiku_fs_t *)fs);
    fop_fs_cancel(fs);
    fop_fs_give((tiku_fs_t *)fs);
}

void
tiku_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn, void *ctx)
{
    if (fs == NULL) {
        return;
    }
    fop_fs_take((tiku_fs_t *)fs);
    fop_fs_set_cancel_check(fs, fn, ctx);
    fop_fs_give((tiku_fs_t *)fs);
}

void
tiku_fs_resolve_error(tiku_fs_t *fs, int keep_going)
{
    if (fs == NULL) {
        return;
    }
    fop_fs_take((tiku_fs_t *)fs);
    fop_fs_resolve_error(fs, keep_going);
    fop_fs_give((tiku_fs_t *)fs);
}

int
tiku_fs_busy(const tiku_fs_t *fs)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_busy(fs);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_in_trash(tiku_fs_t *fs, const char *path)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_in_trash(fs, path);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_link(tiku_fs_t *fs, const char *const *paths, int n, const char *dst, int relative)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_link(fs, paths, n, dst, relative);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_link_as(tiku_fs_t *fs, const char *path, const char *dst,
                const char *name, int relative)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_link_as(fs, path, dst, name, relative);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

void
tiku_fs_new_folder_name(tiku_fs_t *fs, const char *dir, char *out, size_t max)
{
    if (fs == NULL) {
        return;
    }
    fop_fs_take((tiku_fs_t *)fs);
    fop_fs_new_folder_name(fs, dir, out, max);
    fop_fs_give((tiku_fs_t *)fs);
}

int
tiku_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out, size_t max, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_new_folder(fs, dir, out, max, err, errmax);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_new_from_template(tiku_fs_t *fs, const char *template_path, const char *dst_dir, char *out, size_t max, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return -1;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_new_from_template(fs, template_path, dst_dir, out, max, err,
                                 errmax);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_can_undo(fs, what, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_undo(tiku_fs_t *fs, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_undo(fs, err, errmax);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_can_redo(fs, what, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_last_created(const tiku_fs_t *fs, char out[][TIKU_PATH_MAX], int max)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_last_created(fs, out, max);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}

int
tiku_fs_redo(tiku_fs_t *fs, char *err, size_t errmax)
{
    int r;

    if (fs == NULL) {
        return 0;
    }
    fop_fs_take((tiku_fs_t *)fs);
    r = fop_fs_redo(fs, err, errmax);
    fop_fs_give((tiku_fs_t *)fs);
    return r;
}
