/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_engine.c - the engine itself: making and freeing it, its lock
 * (fs->hand), the moves it notes for tiku_fs_moved_pump() to report, and
 * whether its job has items left or has been cancelled.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief Locks fs->hand, or adds one level if this thread already holds
 *         it. */
void
fop_fs_take(tiku_fs_t *fs)
{
    if (fs->depth > 0 && pthread_equal(fs->owner, pthread_self())) {
        fs->depth++;
        return;
    }
    pthread_mutex_lock(&fs->hand);
    fs->owner = pthread_self();
    fs->depth = 1;
}

/** @brief Takes one level off this thread's hold on fs->hand, and unlocks
 *         it when none is left. */
void
fop_fs_give(tiku_fs_t *fs)
{
    if (--fs->depth == 0) {
        pthread_mutex_unlock(&fs->hand);
    }
}

/** @brief Unlocks fs->hand whatever its depth, while one read or write of
 *         bytes runs.
 *  @return The depth to restore with fop_fs_back(). */
int
fop_fs_rest(tiku_fs_t *fs)
{
    int held = fs->depth;

    fs->depth = 0;
    pthread_mutex_unlock(&fs->hand);
    return held;
}

/** @brief Locks fs->hand again after fop_fs_rest(), at the depth it
 *         returned. */
void
fop_fs_back(tiku_fs_t *fs, int held)
{
    pthread_mutex_lock(&fs->hand);
    fs->owner = pthread_self();
    fs->depth = held;
}

/** @brief Note that an item moved from @p from to @p to, for
 *         tiku_fs_moved_pump() to report; at most 16 wait. */
void
fop_landed(tiku_fs_t *fs, const char *from, const char *to)
{
    if (fs != NULL && fs->moved_fn != NULL && from != NULL && to != NULL &&
        from[0] != '\0' && to[0] != '\0' &&
        fs->moved_n < (int)(sizeof fs->moved / sizeof fs->moved[0])) {
        snprintf(fs->moved[fs->moved_n].from, TIKU_PATH_MAX, "%s", from);
        snprintf(fs->moved[fs->moved_n].to, TIKU_PATH_MAX, "%s", to);
        fs->moved_n++;
    }
}

void
tiku_fs_moved_pump(tiku_fs_t *fs)
{
    if (fs == NULL) {
        return;
    }
    for (;;) {
        char from[TIKU_PATH_MAX], to[TIKU_PATH_MAX];
        tiku_fs_moved_fn fn;
        void *ctx;

        fop_fs_take(fs);
        if (fs->moved_n == 0) {
            fop_fs_give(fs);
            return;
        }
        snprintf(from, sizeof from, "%s", fs->moved[0].from);
        snprintf(to, sizeof to, "%s", fs->moved[0].to);
        fs->moved_n--;
        memmove(&fs->moved[0], &fs->moved[1],
                (size_t)fs->moved_n * sizeof fs->moved[0]);
        fn = fs->moved_fn;
        ctx = fs->moved_ctx;
        fop_fs_give(fs);
        /* The hook runs with fs->hand unlocked: it opens windows. */
        if (fn != NULL) {
            fn(ctx, from, to);
        }
    }
}

/** @brief Install the moved hook. */
void
fop_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx)
{
    if (fs != NULL) {
        fs->moved_fn = fn;
        fs->moved_ctx = ctx;
    }
}

/** @brief Whether the job should stop where it stands. */
int
fop_cancelled(tiku_fs_t *fs)
{
    if (fs->cancelling) {
        return 1;
    }
    if (fs->cancel_fn != NULL && fs->cancel_fn(fs->cancel_ctx)) {
        fs->cancelling = 1;
    }
    return fs->cancelling;
}

/** @brief A zeroed engine over @p backend, or NULL. */
tiku_fs_t *
fop_fs_new(tiku_backend_t *backend)
{
    tiku_fs_t *fs = calloc(1, sizeof *fs);

    if (fs != NULL) {
        fs->backend = backend;
    }
    return fs;
}

/** @brief Free the engine and both undo stacks. */
void
fop_fs_free(tiku_fs_t *fs)
{
    if (fs != NULL) {
        fop_undo_stack_clear(fs->ustack, &fs->ucount);
        fop_undo_stack_clear(fs->rstack, &fs->rcount);
    }
    if (fs != NULL) {
        free(fs);
    }
}

/** @brief Whether an operation has items still to process. */
int
fop_fs_busy(const tiku_fs_t *fs)
{
    return (fs != NULL && fs->prog.op != TIKU_OP_IDLE &&
            fs->qcursor < fs->qcount);
}
