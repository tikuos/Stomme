/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs_private.h - what the tiku_fs files share: the engine's state, the
 * undo records, and the functions one file defines for another, each named
 * with the prefix fop_.  Only the tiku_fs files include it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FS_PRIVATE_H_
#define TIKU_FS_PRIVATE_H_

/* tools/split_file.py reads the two markers below.  It takes every .c in
 * its --dir as the family: run on kits/storage, it rewrites the kit's
 * other files too, so this family is synced in a directory holding only
 * its own files. */

/* split: public kits/storage/tiku_fs.h */

/* split: tag fop */

#include "tiku_fs.h"
#include "tiku_mount.h"
#include "tiku_state.h"
#include "tiku_volume.h"

#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/** @brief The most items one job's queue holds, a folder's children
 *         included. */
#define FS_QUEUE_MAX 512

/** @brief One reversal record: how to put a single item back. */
typedef struct {
    char from[TIKU_PATH_MAX];       /**< where it ended up              */
    char to[TIKU_PATH_MAX];         /**< where it came from; "" for a new
                                       folder or a template's copy      */
    int  created;                   /**< made by the operation: undo
                                       removes it instead of moving it
                                       back                             */
} undo_rec_t;

/**
 * @brief One reversible operation: its kind and the records it produced.
 *
 * Undo and redo take a whole operation, every record of it.
 */
typedef struct {
    tiku_op_t   op;                 /**< what the operation did         */
    int         n;                  /**< records in rec                 */
    undo_rec_t *rec;                /**< n entries, grown as they arrive */
} undo_op_t;

/**
 * @brief How deep the undo and redo stacks go; when one is full, its oldest
 *        operation is dropped.
 */
#define FS_UNDO_MAX 20

/**
 * @brief The engine: the job it runs, the undo history, and the lock under
 *        which a worker steps the job while the loop reads and answers it.
 */
struct tiku_fs {
    tiku_backend_t  *backend;       /**< the backend it works through   */
    char             queue[FS_QUEUE_MAX][TIKU_PATH_MAX]; /**< the items */
    unsigned char    qexpanded[FS_QUEUE_MAX]; /**< per item: 0 before a
                                       folder is expanded; 1 on its second
                                       visit, after its children (FS-049);
                                       2 on that visit when its children
                                       did not all fit in the queue     */
    int              qdone[FS_QUEUE_MAX]; /**< per item: prog.done when
                                       the folder was last expanded     */
    int              qcount;        /**< items in queue                 */
    int              qcursor;       /**< the next item to process       */
    char             dst[TIKU_PATH_MAX]; /**< the destination folder, ""
                                       for a job without one            */
    tiku_conflict_t  policy;        /**< how the next collision is
                                       answered                         */
    tiku_progress_t  prog;          /**< the job's progress             */

    int              pending_ask;   /**< policy is a one-shot Replace,
                                       for the current collision only   */
    undo_op_t        ustack[FS_UNDO_MAX]; /**< what undo can reverse, the
                                       latest last                      */
    int              ucount;        /**< operations in ustack           */
    undo_op_t        rstack[FS_UNDO_MAX]; /**< what redo can repeat, the
                                       latest last                      */
    int              rcount;        /**< operations in rstack           */

    tiku_cancel_fn   cancel_fn;     /**< asked before each item and each
                                       chunk: non-zero cancels the job  */
    void            *cancel_ctx;    /**< cancel_fn's argument           */
    tiku_fs_moved_fn moved_fn;      /**< told of each item that lands
                                       under a new path                 */
    void            *moved_ctx;     /**< moved_fn's argument            */
    int              cancelling;    /**< set by tiku_fs_cancel() or a
                                       non-zero cancel_fn, cleared when
                                       a job starts                     */
    int              link_relative; /**< relative links were asked for:
                                       each is relative when its source
                                       shares the destination's device  */
    char             link_as[TIKU_NAME_MAX]; /**< the one link's name, or
                                       "" for the source's own name     */
    pthread_mutex_t  hand;          /**< the engine lock: a worker stepping
                                       the job and the loop reading or
                                       answering it take turns, and a copy
                                       releases it around each read and
                                       write of bytes                   */
    pthread_t        owner;         /**< the thread that holds the lock */
    int              depth;         /**< how many levels owner holds; 0
                                       while the lock is free           */
    tiku_progress_t  prog_seen;     /**< the copy of prog that
                                       tiku_fs_progress() returns       */
    struct {
        char from[TIKU_PATH_MAX];   /**< where the item was             */
        char to[TIKU_PATH_MAX];     /**< where it is now                */
    } moved[16];                    /**< moves noted for
                                       tiku_fs_moved_pump() to report on
                                       the loop, oldest first: a step may
                                       run on a worker, and the hook
                                       touches windows                  */
    int              moved_n;       /**< moves waiting in moved         */
};

/* Shared across the family: defined in one file, used in another. */

/* tiku_fs_bytes.c */
int fop_copy_across(tiku_fs_t *fs, tiku_backend_t *sb, const char *slocal,
                    tiku_backend_t *db, const char *dlocal, const char *src,
                    char *err, size_t errmax);
int fop_copy_bytes(tiku_fs_t *fs, const char *src, const char *dst, char *err,
                   size_t errmax);
void fop_carry_over(const char *src, const char *dst, int link);
void fop_copy_pose_info(tiku_fs_t *fs, const char *src, const char *dst);

/* tiku_fs_create.c */
int fop_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out, size_t max,
                      char *err, size_t errmax);
int fop_fs_new_from_template(tiku_fs_t *fs, const char *template_path,
                             const char *dst_dir, char *out, size_t max,
                             char *err, size_t errmax);
void fop_fs_new_folder_name(tiku_fs_t *fs, const char *dir, char *out,
                            size_t max);

/* tiku_fs_drop.c */
int fop_on_device(const tiku_fs_t *fs, const char *path);
tiku_backend_t *fop_store_of(const tiku_fs_t *fs, const char *path, char *local,
                             size_t max);
tiku_op_t fop_fs_drop_action(tiku_fs_t *fs, const char *src,
                             const char *dst_dir, int force_copy);

/* tiku_fs_engine.c */
int fop_cancelled(tiku_fs_t *fs);
int fop_fs_busy(const tiku_fs_t *fs);
int fop_fs_rest(tiku_fs_t *fs);
tiku_fs_t *fop_fs_new(tiku_backend_t *backend);
void fop_fs_back(tiku_fs_t *fs, int held);
void fop_fs_free(tiku_fs_t *fs);
void fop_fs_give(tiku_fs_t *fs);
void fop_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx);
void fop_fs_take(tiku_fs_t *fs);
void fop_landed(tiku_fs_t *fs, const char *from, const char *to);

/* tiku_fs_names.c */
const char *fop_leaf_of(const char *path);
int fop_exists(tiku_fs_t *fs, const char *dir, const char *name);
int fop_fs_duplicate_name(tiku_fs_t *fs, const char *dir, const char *name,
                          char *out, size_t max);
int fop_fs_link_name(tiku_fs_t *fs, const char *dir, const char *name,
                     char *out, size_t max);
int fop_fs_rename(tiku_fs_t *fs, const char *path, const char *new_name,
                  char *err, size_t errmax);
int fop_join(char *out, size_t max, const char *dir, const char *name);
void fop_parent_of(const char *path, char *out, size_t max);
int fop_under_trash(const char *path);

/* tiku_fs_queue.c */
int fop_fs_copy(tiku_fs_t *fs, const char *const *paths, int n,
                const char *dst_dir, tiku_conflict_t policy);
int fop_fs_delete(tiku_fs_t *fs, const char *const *paths, int n);
int fop_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n);
int fop_fs_in_trash(tiku_fs_t *fs, const char *path);
int fop_fs_link(tiku_fs_t *fs, const char *const *paths, int n, const char *dst,
                int relative);
int fop_fs_link_as(tiku_fs_t *fs, const char *path, const char *dst,
                   const char *name, int relative);
int fop_fs_move(tiku_fs_t *fs, const char *const *paths, int n,
                const char *dst_dir, tiku_conflict_t policy);
int fop_fs_trash(tiku_fs_t *fs, const char *const *paths, int n);

/* tiku_fs_refusals.c */
int fop_is_dir(const char *path);
tiku_refusal_t fop_fs_check_move(tiku_fs_t *fs, const char *src,
                                 const char *dst_dir, char *msg, size_t max);

/* tiku_fs_step.c */
int fop_fs_step(tiku_fs_t *fs, int budget);
void fop_fs_cancel(tiku_fs_t *fs);
void fop_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how);
void fop_fs_resolve_error(tiku_fs_t *fs, int keep_going);
void fop_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn, void *ctx);

/* tiku_fs_trash.c */
int fop_fs_empty_trash(tiku_fs_t *fs);
int fop_fs_has_trash(const tiku_fs_t *fs);
int fop_fs_original_path(tiku_fs_t *fs, const char *path, char *out,
                         size_t max);
int fop_fs_prepare_trash(tiku_fs_t *fs);
int fop_fs_restore(tiku_fs_t *fs, const char *const *paths, int n, char *err,
                   size_t errmax);
int fop_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out, size_t max);
int fop_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX], int max);
int fop_fs_trash_dirs_of(tiku_fs_t *fs, const struct tiku_volumes *v,
                         char out[][TIKU_PATH_MAX], int max);
int fop_make_path(const char *path);
tiku_delete_q_t fop_fs_delete_question(const tiku_fs_t *fs, const char *path,
                                       int shift_held);
void fop_record_origin(tiku_fs_t *fs, const char *trashed, const char *origin);

/* tiku_fs_tree.c */
int fop_copy_tree(tiku_fs_t *fs, const char *src, const char *dstpath,
                  char *err, size_t errmax);
int fop_move_tree(tiku_fs_t *fs, const char *src, const char *dstpath,
                  char *err, size_t errmax);
int fop_remove_tree(const char *path);

/* tiku_fs_undo.c */
int fop_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max);
int fop_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max);
int fop_fs_last_created(const tiku_fs_t *fs, char out[][TIKU_PATH_MAX],
                        int max);
int fop_fs_redo(tiku_fs_t *fs, char *err, size_t errmax);
int fop_fs_undo(tiku_fs_t *fs, char *err, size_t errmax);
void fop_undo_push(tiku_fs_t *fs, const char *from, const char *to,
                   int created);
void fop_undo_reset(tiku_fs_t *fs, tiku_op_t op);
void fop_undo_stack_clear(undo_op_t *stack, int *count);

/* End of the shared declarations. */

#endif /* TIKU_FS_PRIVATE_H_ */
