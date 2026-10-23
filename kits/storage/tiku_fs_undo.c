/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_undo.c - undo and redo: the records each operation leaves, the
 * two stacks that keep the last FS_UNDO_MAX operations, an operation
 * reversed or done again, and the name each kind of operation is shown by.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_fs_private.h"

/** @brief Release one operation's records. */
static void
undo_op_free(undo_op_t *o)
{
    free(o->rec);
    o->rec = NULL;
    o->n = 0;
    o->op = TIKU_OP_IDLE;
}

/** @brief Empty a stack. */
void
fop_undo_stack_clear(undo_op_t *stack, int *count)
{
    while (*count > 0) {
        undo_op_free(&stack[--(*count)]);
    }
}

/**
 * @brief Push @p o; when the stack is full, the oldest operation is
 *        dropped.
 */
static undo_op_t *
undo_stack_push(undo_op_t *stack, int *count, const undo_op_t *o)
{
    if (*count >= FS_UNDO_MAX) {
        int i;

        undo_op_free(&stack[0]);
        for (i = 1; i < FS_UNDO_MAX; i++) {
            stack[i - 1] = stack[i];
        }
        stack[FS_UNDO_MAX - 1].rec = NULL;
        (*count)--;
    }
    stack[*count] = *o;
    return &stack[(*count)++];
}

/**
 * @brief Begin recording a new operation of kind @p op, and empty the redo
 *        stack.
 */
void
fop_undo_reset(tiku_fs_t *fs, tiku_op_t op)
{
    undo_op_t fresh;

    memset(&fresh, 0, sizeof fresh);
    fresh.op = op;
    fop_undo_stack_clear(fs->rstack, &fs->rcount);
    (void)undo_stack_push(fs->ustack, &fs->ucount, &fresh);
}

/** @brief Add one record to the operation being recorded: the item is now
 *         at @p from, it came from @p to, and @p created marks a thing the
 *         operation made. */
void
fop_undo_push(tiku_fs_t *fs, const char *from, const char *to, int created)
{
    undo_op_t  *o;
    undo_rec_t *grown, *r;

    if (fs->ucount == 0) {
        return;
    }
    o = &fs->ustack[fs->ucount - 1];
    grown = realloc(o->rec, (size_t)(o->n + 1) * sizeof *o->rec);
    if (grown == NULL) {
        return;                     /* the operation stands; only its
                                     * reversal is lost                  */
    }
    o->rec = grown;
    r = &o->rec[o->n++];
    memset(r, 0, sizeof *r);
    snprintf(r->from, sizeof r->from, "%s", from);
    snprintf(r->to, sizeof r->to, "%s", to ? to : "");
    r->created = created;
}

/** @brief Whether the last operation can be undone; @p what takes its
 *         name. */
int
fop_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max)
{
    const undo_op_t *o;

    if (fs == NULL || fs->ucount == 0) {
        return 0;
    }
    o = &fs->ustack[fs->ucount - 1];
    if (o->n == 0) {
        return 0;
    }
    /* A delete cannot be undone, so it is not offered. */
    if (o->op == TIKU_OP_DELETE) {
        return 0;
    }
    if (what != NULL) {
        snprintf(what, max, "%s", tiku_fs_op_name(o->op));
    }
    return 1;
}

/** @brief The paths the last operation put in place; see
 *         tiku_fs_last_created(). */
int
fop_fs_last_created(const tiku_fs_t *fs,
                         char out[][TIKU_PATH_MAX], int max)
{
    const undo_op_t *o;
    int i, n = 0;

    if (fs == NULL || out == NULL || max <= 0 || fs->ucount <= 0) {
        return 0;
    }
    o = &fs->ustack[fs->ucount - 1];
    for (i = 0; i < o->n && n < max; i++) {
        /* A path the operation put in place: made outright, or the
         * destination of a copy or a move, whose 'to' names its origin. */
        if (o->rec[i].created || o->rec[i].to[0] != '\0') {
            snprintf(out[n], TIKU_PATH_MAX, "%s", o->rec[i].from);
            n++;
        }
    }
    return n;
}

/** @brief Whether the last undo can be redone; @p what takes its name. */
int
fop_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max)
{
    const undo_op_t *o;

    if (fs == NULL || fs->rcount == 0) {
        return 0;
    }
    o = &fs->rstack[fs->rcount - 1];
    if (o->n == 0) {
        return 0;
    }
    if (what != NULL) {
        snprintf(what, max, "%s", tiku_fs_op_name(o->op));
    }
    return 1;
}

const char *
tiku_fs_op_name(tiku_op_t op)
{
    static const char *names[] = {
        "", "Copy", "Move", "Duplicate", "Move to Trash", "Delete",
        "Rename", "Create Link", "New Folder"
    };

    if ((size_t)op >= sizeof names / sizeof names[0]) {
        return "";
    }
    return names[op];
}

/**
 * @brief Put one record back the way it was.
 *
 * @return 1 when something was undone.
 */
static int
undo_one(tiku_fs_t *fs, const undo_rec_t *r, char *err, size_t errmax)
{
    if (r->created) {
        /* Whatever the operation made goes, tree and all: a copied folder
         * holds what the copy made inside it. */
        return (fop_remove_tree(r->from) == 0);
    }
    if (rename(r->from, r->to) == 0) {
        fop_landed(fs, r->from, r->to);
        return 1;
    }
    if (err != NULL) {
        snprintf(err, errmax, "cannot undo '%s' (%s)", r->from,
                 strerror(errno));
    }
    return 0;
}

/**
 * @brief Do one record over again, in the direction the operation went.
 *
 * A record the operation made is copied again from where it came from,
 * except a new folder's (@p op), which is made afresh; any other record is
 * renamed again.
 */
static int
redo_one(tiku_fs_t *fs, tiku_op_t op, const undo_rec_t *r, char *err,
         size_t errmax)
{
    if (op == TIKU_OP_NEW_FOLDER) {
        return (fop_make_path(r->from) == 0);
    }
    if (r->created) {
        if (r->to[0] == '\0') {
            return 0;               /* nothing recorded to repeat from   */
        }
        return (fop_copy_tree(fs, r->to, r->from, err, errmax) == 0);
    }
    if (rename(r->to, r->from) == 0) {
        fop_landed(fs, r->to, r->from);
        return 1;
    }
    if (err != NULL) {
        snprintf(err, errmax, "cannot redo '%s' (%s)", r->to,
                 strerror(errno));
    }
    return 0;
}

/** @brief Reverse the last operation, its records in reverse order.
 *  @return Records reversed, or -1 when there is nothing to undo. */
int
fop_fs_undo(tiku_fs_t *fs, char *err, size_t errmax)
{
    undo_op_t *o;
    int i, n = 0;

    if (!tiku_fs_can_undo(fs, NULL, 0)) {
        return -1;
    }
    o = &fs->ustack[fs->ucount - 1];
    /* Reverse order, so a sequence that renamed around a collision unwinds
     * without colliding with itself. */
    for (i = o->n - 1; i >= 0; i--) {
        n += undo_one(fs, &o->rec[i], err, errmax);
    }
    /* The operation goes to the redo stack whatever its records did: a
     * partly reversed operation can still be redone. */
    (void)undo_stack_push(fs->rstack, &fs->rcount, o);
    memset(o, 0, sizeof *o);
    fs->ucount--;
    return n;
}

/** @brief Do the last undone operation again.
 *  @return Records redone, or -1 when there is nothing to redo. */
int
fop_fs_redo(tiku_fs_t *fs, char *err, size_t errmax)
{
    undo_op_t *o;
    int i, n = 0;

    if (!tiku_fs_can_redo(fs, NULL, 0)) {
        return -1;
    }
    o = &fs->rstack[fs->rcount - 1];
    for (i = 0; i < o->n; i++) {
        n += redo_one(fs, o->op, &o->rec[i], err, errmax);
    }
    (void)undo_stack_push(fs->ustack, &fs->ucount, o);
    memset(o, 0, sizeof *o);
    fs->rcount--;
    return n;
}
