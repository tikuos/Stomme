/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_step.c - a job run one item at a time: each item trashed,
 * deleted, linked, duplicated, copied or moved; the collision or error the
 * job stops on, and the answer that resumes it; and cancelling.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/**
 * @brief Write the path of @p target as seen from inside @p dir.
 *
 * Built by walking the two absolute paths while they agree, then one ".."
 * per component of @p dir that is left, then the rest of the target.
 *
 * @return 0 on success.
 */
static int
relative_path(const char *dir, const char *target, char *out, size_t max)
{
    size_t i = 0, cut = 0;
    size_t n = 0;

    if (dir == NULL || target == NULL || dir[0] != '/' || target[0] != '/') {
        return -1;
    }
    while (dir[i] != '\0' && target[i] != '\0' && dir[i] == target[i]) {
        if (dir[i] == '/') {
            cut = i;                /* last slash they still share */
        }
        i++;
    }
    if (dir[i] == '\0' && (target[i] == '/' || target[i] == '\0')) {
        cut = i;
    }
    if (target[i] == '\0' && (dir[i] == '/' || dir[i] == '\0')) {
        cut = i;
    }
    out[0] = '\0';
    for (i = cut; dir[i] != '\0'; i++) {
        if (dir[i] == '/' && dir[i + 1] != '\0') {
            if (n + 3u >= max) {
                return -1;
            }
            memcpy(out + n, "../", 3u);
            n += 3u;
        }
    }
    {
        const char *rest = target + cut;

        while (*rest == '/') {
            rest++;
        }
        if (n + strlen(rest) + 1u > max) {
            return -1;
        }
        memcpy(out + n, rest, strlen(rest) + 1u);
    }
    return (out[0] != '\0') ? 0 : -1;
}

/**
 * @brief Stop on a failure and put "would you like to continue?" up.
 *
 * The item is not counted yet: tiku_fs_resolve_error() counts it when the
 * question is answered.  @p can_continue is 0 for a delete failure, whose
 * question offers no Continue.
 *
 * @return 0, so the caller can `return fail_ask(...)` from a step.
 */
static int
fail_ask(tiku_fs_t *fs, const char *msg, int can_continue)
{
    snprintf(fs->prog.error, sizeof fs->prog.error, "%s", msg);
    fs->prog.error_ask = 1;
    fs->prog.error_can_continue = can_continue;
    return 0;
}

/** @brief Note that the job stopped part-way and drop the rest. */
static int
give_up(tiku_fs_t *fs)
{
    fs->prog.cancelled = 1;
    fs->qcursor = fs->qcount;
    return 1;
}

/**
 * @brief Whether the folder at the cursor is expanded now: on its first
 *        visit, or on a later one when its children did not all fit and the
 *        round since its last expansion deleted something.
 */
static int
expand_due(const tiku_fs_t *fs)
{
    int c = fs->qcursor;

    return fs->qexpanded[c] == 0 ||
           (fs->qexpanded[c] == 2 && fs->prog.done > fs->qdone[c]);
}

/**
 * @brief Delete the item at the cursor: a directory's children are queued
 *        ahead of it and the directory is visited again after them.
 * @return 1 to go on with the queue, 0 when the job stopped on a
 *         question: a device node or a failed removal.
 */
static int
step_delete(tiku_fs_t *fs)
{
    const char *src = fs->queue[fs->qcursor];

    if (fop_on_device(fs, src)) {
        /*
         * The backend ops remove nothing, so a device node cannot be
         * trashed or deleted; unlink() on its namespace path would
         * only report ENOENT.  The refusal names the limit and can be
         * continued past, so the rest of a queue that spans a board
         * and the disk still goes.
         */
        char msg[240];

        snprintf(msg, sizeof msg,
                 "\"%.60s\" is on a device: the connection can read "
                 "and write its nodes, but not remove them.",
                 fop_leaf_of(src));
        return fail_ask(fs, msg, 1);
    } else if (fop_is_dir(src) && expand_due(fs)) {
        /* A folder goes children first: each child is queued as an
         * item of its own in the processed slots before the folder,
         * and the folder stays in its slot for a second visit after
         * them, so a child that cannot go is reported by name and its
         * siblings still go (FS-049).  Children that do not fit wait
         * for the next round, which the second visit starts. */
        DIR *d;
        int slot, more = 0;

        if (fs->qcount < FS_QUEUE_MAX) {
            /* The items still to run move to the end of the queue,
             * leaving every slot before them free. */
            int live = fs->qcount - fs->qcursor;
            int at = FS_QUEUE_MAX - live;

            memmove(fs->queue[at], fs->queue[fs->qcursor],
                    (size_t)live * sizeof fs->queue[0]);
            memmove(&fs->qexpanded[at], &fs->qexpanded[fs->qcursor],
                    (size_t)live);
            memmove(&fs->qdone[at], &fs->qdone[fs->qcursor],
                    (size_t)live * sizeof fs->qdone[0]);
            fs->qcursor = at;
            fs->qcount = FS_QUEUE_MAX;
            src = fs->queue[at];
        }
        slot = fs->qcursor;
        d = opendir(src);
        if (d != NULL) {
            struct dirent *e;

            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 ||
                    strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                if (slot == 0) {
                    more = 1;
                    break;
                }
                if (fop_join(fs->queue[slot - 1], TIKU_PATH_MAX,
                         src, e->d_name) == 0) {
                    slot--;
                    fs->qexpanded[slot] = 0;
                }
            }
            (void)closedir(d);
        }
        if (slot < fs->qcursor) {
            /* The children and the folder's second visit. */
            fs->prog.total += fs->qcursor - slot + 1;
            fs->qexpanded[fs->qcursor] = more ? 2 : 1;
            fs->qdone[fs->qcursor] = fs->prog.done;
            fs->qcursor = slot;
            return 1;
        }
        /* Nothing inside (or no room to expand): fall through to the
         * straight attempt below by treating it as visited. */
        goto delete_attempt;
    } else {
delete_attempt:
        if (remove(src) != 0) {
            if (errno == ENOTEMPTY || errno == EEXIST) {
                /* While a descendant is still queued, the folder goes
                 * to the back of the queue: it can only be empty after
                 * them. */
                size_t plen = strlen(src);
                int k;

                for (k = fs->qcursor + 1; k < fs->qcount; k++) {
                    if (strncmp(fs->queue[k], src, plen) == 0 &&
                        fs->queue[k][plen] == '/') {
                        break;
                    }
                }
                if (k < fs->qcount && fs->qcount < FS_QUEUE_MAX) {
                    fs->qexpanded[fs->qcount] = 1;
                    snprintf(fs->queue[fs->qcount], TIKU_PATH_MAX,
                             "%s", src);
                    fs->qcount++;
                    fs->prog.total = fs->qcount;
                    fs->qcursor++;
                    return 1;
                }
            }
            /* The dialog names the leaf, which is what was
             * selected. */
            char msg[240];

            snprintf(msg, sizeof msg,
                     "Error deleting \"%.60s\": %.150s.",
                     fop_leaf_of(src), strerror(errno));
            return fail_ask(fs, msg, 0);
        }
        fs->prog.done++;
    }
    fs->qcursor++;
    return 1;
}

/**
 * @brief Link the item at the cursor into fs->dst, relative when
 *        fs->link_relative and both ends share a device.
 * @return 1 when the item is done, 0 when the job stopped on a question:
 *         the link could not be made.
 */
static int
step_link(tiku_fs_t *fs)
{
    const char *src = fs->queue[fs->qcursor];
    char name[TIKU_NAME_MAX], dstpath[TIKU_PATH_MAX];

    char target[TIKU_PATH_MAX];
    struct stat ss, ds;

    /* "<name> link" only when the plain name is taken, so linking into
     * another folder keeps the original name (FS-009). */
    if (tiku_fs_link_name(fs, fs->dst,
                          fs->link_as[0] != '\0' ? fs->link_as
                                                  : fop_leaf_of(src),
                          name, sizeof name) != 0 ||
        fop_join(dstpath, sizeof dstpath, fs->dst, name) != 0) {
        fs->prog.failed++;
        fs->qcursor++;
        return 1;
    }
    snprintf(target, sizeof target, "%s", src);
    if (fs->link_relative && stat(src, &ss) == 0 &&
        stat(fs->dst, &ds) == 0 && ss.st_dev == ds.st_dev) {
        char rel[TIKU_PATH_MAX];

        /* A relative link is made only when source and destination
         * share a device and a relative path can be built; otherwise
         * the link is absolute (FS-010). */
        if (relative_path(fs->dst, src, rel, sizeof rel) == 0) {
            snprintf(target, sizeof target, "%s", rel);
        }
    }
    if (symlink(target, dstpath) != 0) {
        char msg[240];

        /* EPERM or ENOSYS means the disk takes no links, and the
         * message says so (FS-011). */
        snprintf(msg, sizeof msg, "%s",
                 (errno == EPERM || errno == ENOSYS)
                     ? "The target disk does not support creating links."
                     : "Error creating link.");
        return fail_ask(fs, msg, 1);
    }
    fop_undo_push(fs, dstpath, src, 1);
    fs->prog.done++;
    fs->qcursor++;
    return 1;
}

/**
 * @brief Process one queued item: trash, delete and link go to
 *        step_delete() and step_link(); copy, move and duplicate are
 *        done here.
 * @return 1 when the item is done or the job ends, 0 when the job stopped
 *         on a question: a collision or an error.
 */
static int
step_one(tiku_fs_t *fs)
{
    const char *src = fs->queue[fs->qcursor];
    char dstdir[TIKU_PATH_MAX], dstpath[TIKU_PATH_MAX];
    char name[TIKU_NAME_MAX];
    char err[256];

    if (fop_cancelled(fs)) {
        return give_up(fs);
    }
    snprintf(fs->prog.current, sizeof fs->prog.current, "%s", src);
    err[0] = '\0';

    if (fs->prog.op == TIKU_OP_TRASH && tiku_fs_has_trash(fs) &&
        !fop_under_trash(src)) {
        /* The item is renamed into the Trash, and where it came from is
         * recorded for a restore.  An item already in the Trash falls
         * through to deletion. */
        char trash[TIKU_PATH_MAX], want[TIKU_NAME_MAX];

        if (tiku_fs_trash_dir(fs, src, trash, sizeof trash) != 0) {
            /* No Trash on this item's filesystem: the item is deleted
             * below, and the question put to the user was a straight
             * delete (FS-044). */
            trash[0] = '\0';
        }
        if (trash[0] != '\0') {
        /* A name already taken in the Trash gets the same " copy" treatment
         * a duplicate does, so trashing two files of one name keeps both. */
        if (fop_exists(fs, trash, fop_leaf_of(src))) {
            (void)tiku_fs_duplicate_name(fs, trash, fop_leaf_of(src), want,
                                             sizeof want);
        } else {
            snprintf(want, sizeof want, "%s", fop_leaf_of(src));
        }
        if (fop_join(dstpath, sizeof dstpath, trash, want) != 0 ||
            rename(src, dstpath) != 0) {
            snprintf(fs->prog.error, sizeof fs->prog.error,
                     "cannot move '%.80s' to the Trash (%.60s)",
                     fop_leaf_of(src), strerror(errno));
            fs->prog.failed++;
        } else {
            /* Recorded after the move, under the name it now has. */
            fop_record_origin(fs, dstpath, src);
            /* Undo moves it back, as for any move. */
            fop_undo_push(fs, dstpath, src, 0);
            fop_landed(fs, src, dstpath);
            fs->prog.done++;
        }
        fs->qcursor++;
        return 1;
        }
    }
    if (fs->prog.op == TIKU_OP_TRASH || fs->prog.op == TIKU_OP_DELETE)
        return step_delete(fs);

    if (fs->prog.op == TIKU_OP_LINK) return step_link(fs);
    if (fs->prog.op == TIKU_OP_DUPLICATE) {
        /* Each item duplicates into its own parent, even when the
         * selection spans folders (FS-008). */
        fop_parent_of(src, dstdir, sizeof dstdir);
        if (tiku_fs_duplicate_name(fs, dstdir, fop_leaf_of(src), name,
                                       sizeof name) != 0) {
            fs->prog.failed++;
            fs->qcursor++;
            return 1;
        }
    } else {
        snprintf(dstdir, sizeof dstdir, "%s", fs->dst);
        snprintf(name, sizeof name, "%s", fop_leaf_of(src));
        {   /* An item already in the destination is skipped, not copied
             * onto itself (FS-005). */
            char srcdir[TIKU_PATH_MAX];
            fop_parent_of(src, srcdir, sizeof srcdir);
            if (strcmp(srcdir, dstdir) == 0) {
                fs->prog.skipped++;
                fs->qcursor++;
                return 1;
            }
        }
    }
    if (fop_join(dstpath, sizeof dstpath, dstdir, name) != 0) {
        fs->prog.failed++;
        fs->qcursor++;
        return 1;
    }

    /* Collisions: ask, unless the caller already chose a policy. */
    if (fs->prog.op != TIKU_OP_DUPLICATE && fop_exists(fs, dstdir, name)) {
        switch (fs->policy) {
        case TIKU_CONFLICT_SKIP:
            /* A policy the caller set stands for the whole job; a one-off
             * answer comes through tiku_fs_resolve(). */
            fs->prog.skipped++;
            fs->qcursor++;
            return 1;
        case TIKU_CONFLICT_RENAME:
            if (tiku_fs_duplicate_name(fs, dstdir, name, name,
                                           sizeof name) != 0 ||
                fop_join(dstpath, sizeof dstpath, dstdir, name) != 0) {
                fs->prog.failed++;
                fs->qcursor++;
                return 1;
            }
            break;
        case TIKU_CONFLICT_REPLACE:
            if (fs->pending_ask) {
                /* A one-shot "Replace": used up by this item. */
                fs->pending_ask = 0;
                fs->policy = TIKU_CONFLICT_ASK;
            }
            /* The old file is removed first, not written over: a file
             * that cannot be removed is not written into (FS-021).  A
             * directory is merged into, not removed. */
            if (!fop_is_dir(dstpath) && remove(dstpath) != 0) {
                char msg[240];

                snprintf(msg, sizeof msg,
                         "There was a problem trying to replace \"%.60s\". "
                         "The item might be open or busy.", name);
                return fail_ask(fs, msg, 1);
            }
            break;
        default:
            fs->prog.conflict = 1;
            snprintf(fs->prog.conflict_path, sizeof fs->prog.conflict_path,
                     "%s", dstpath);
            /* What the question offers: a folder collision always gets the
             * four-way form, and so does any collision while more than one
             * is pending; a lone file-versus-file collision gets the short
             * form (FS-018, FS-019). */
            fs->prog.conflict_isdir = fop_is_dir(dstpath);
            fs->prog.conflict_many = (fs->prog.collisions > 1) ||
                                     fs->prog.conflict_isdir;
            return 0;               /* stop and wait for an answer */
        }
    }

    if (fs->prog.op == TIKU_OP_MOVE) {
        /* Through the tree walker, so a folder moves too, merging into a
         * same-named folder at the destination (FS-029). */
        int rc = fop_move_tree(fs, src, dstpath, err, sizeof err);

        if (rc == -2) {
            return give_up(fs);
        }
        if (rc != 0) {
            char msg[240];

            snprintf(msg, sizeof msg, "Error moving file \"%.60s\": %.150s.",
                     fop_leaf_of(src), err[0] ? err : "move failed");
            return fail_ask(fs, msg, 1);
        }
        fop_undo_push(fs, dstpath, src, 0);
        fop_landed(fs, src, dstpath);
        fs->prog.done++;
    } else {
        /* The same for a copy: a folder already there is merged into,
         * and its children with no counterpart in the source stay
         * (FS-020). */
        int rc = fop_copy_tree(fs, src, dstpath, err, sizeof err);

        if (rc == -2) {
            /* Cancelled mid-file.  What the copy managed stays, and is
             * recorded, so an undo can still take it away (FS-034). */
            fop_undo_push(fs, dstpath, src, 1);
            return give_up(fs);
        }
        if (rc != 0) {
            char msg[240];

            snprintf(msg, sizeof msg,
                     "Error copying file \"%.60s\": %.150s.", fop_leaf_of(src),
                     err[0] ? err : "copy failed");
            return fail_ask(fs, msg, 1);
        }
        {
            char srcdir[TIKU_PATH_MAX];

            fop_parent_of(src, srcdir, sizeof srcdir);
            if (strcmp(srcdir, dstdir) != 0) {
                fop_copy_pose_info(fs, src, dstpath);
            }
        }
        fop_undo_push(fs, dstpath, src, 1);
        fs->prog.done++;
    }
    fs->qcursor++;
    return 1;
}

/** @brief Process up to @p budget items; nothing while a question is open.
 *  @return Items processed. */
int
fop_fs_step(tiku_fs_t *fs, int budget)
{
    int n = 0;

    if (fs == NULL || fs->prog.conflict || fs->prog.error_ask) {
        return 0;
    }
    while (n < budget && fs->qcursor < fs->qcount) {
        if (!step_one(fs)) {
            break;                  /* stopped on a question */
        }
        n++;
    }
    if (fs->qcursor >= fs->qcount && !fs->prog.conflict) {
        fs->prog.op = (fs->prog.total > 0) ? fs->prog.op : TIKU_OP_IDLE;
    }
    return n;
}

/** @brief Answer the collision the job stopped on; see tiku_fs_resolve(). */
void
fop_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how)
{
    if (fs == NULL || !fs->prog.conflict) {
        return;
    }
    fs->prog.conflict = 0;
    /* The two "all" answers become the job's standing policy; Skip and
     * Replace answer the collision in hand, and the next one asks again
     * (FS-019). */
    switch (how) {
    case TIKU_CONFLICT_SKIP_ALL:
        fs->policy = TIKU_CONFLICT_SKIP;
        break;
    case TIKU_CONFLICT_REPLACE_ALL:
        fs->policy = TIKU_CONFLICT_REPLACE;
        break;
    case TIKU_CONFLICT_SKIP:
        fs->prog.skipped++;
        fs->qcursor++;
        fs->policy = TIKU_CONFLICT_ASK;
        break;
    case TIKU_CONFLICT_REPLACE:
        /* Applied to this item, then back to asking. */
        fs->policy = TIKU_CONFLICT_REPLACE;
        fs->pending_ask = 1;
        break;
    default:
        fs->policy = how;
        break;
    }
}

/** @brief Stop the job; what is done stays done. */
void
fop_fs_cancel(tiku_fs_t *fs)
{
    if (fs != NULL) {
        /* The flag reaches a copy inside a file at its next chunk, and the
         * copy removes that file (FS-033). */
        fs->cancelling = 1;
        fs->qcursor = fs->qcount;
        fs->prog.conflict = 0;
        fs->prog.error_ask = 0;
        fs->prog.cancelled = 1;
    }
}

/** @brief Install the check asked before every chunk. */
void
fop_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn,
                             void *ctx)
{
    if (fs != NULL) {
        fs->cancel_fn = fn;
        fs->cancel_ctx = ctx;
    }
}

/** @brief Answer the error the job stopped on; see
 *         tiku_fs_resolve_error(). */
void
fop_fs_resolve_error(tiku_fs_t *fs, int keep_going)
{
    if (fs == NULL || !fs->prog.error_ask) {
        return;
    }
    fs->prog.error_ask = 0;
    fs->prog.failed++;
    fs->qcursor++;
    if (fs->prog.op == TIKU_OP_DELETE) {
        /* A deletion goes on past a failure whatever the answer, so a
         * child that will not go costs only that child (FS-049); only a
         * cancel stops it. */
        return;
    }
    if (!keep_going || !fs->prog.error_can_continue) {
        (void)give_up(fs);
    }
}
