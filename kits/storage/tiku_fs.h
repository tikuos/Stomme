/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fs.h - file operations.
 *
 * Copy, move, duplicate, rename, trash, delete, links, new folders, undo and
 * redo.  The same-volume test decides move versus copy, Duplicate makes up a
 * fresh name, and an operation runs in steps that report progress.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FS_H_
#define TIKU_FS_H_

#include <stddef.h>
#include <stdint.h>
#include "tiku_model.h"

struct tiku_volumes;

/** @brief What an operation is doing, for the progress window. */
typedef enum {
    TIKU_OP_IDLE = 0,
    TIKU_OP_COPY,
    TIKU_OP_MOVE,
    TIKU_OP_DUPLICATE,
    TIKU_OP_TRASH,
    TIKU_OP_DELETE,
    TIKU_OP_RENAME,
    TIKU_OP_LINK,
    TIKU_OP_NEW_FOLDER,
} tiku_op_t;

/** @brief How a name collision was resolved. */
typedef enum {
    TIKU_CONFLICT_ASK = 0,   /* stop and report; the UI decides       */
    TIKU_CONFLICT_REPLACE,
    TIKU_CONFLICT_SKIP,
    TIKU_CONFLICT_RENAME,    /* invent a fresh name, as Duplicate does */
    /* The two sticky answers.  Replace and Skip answer the collision in
     * hand, and the next one asks again; the "all" answers stand for every
     * later collision of the job (FS-019). */
    TIKU_CONFLICT_REPLACE_ALL,
    TIKU_CONFLICT_SKIP_ALL,
} tiku_conflict_t;

/** @brief Live state of a running operation. */
typedef struct {
    tiku_op_t op;
    int           total;          /* items to process                     */
    int           done;
    int           skipped;
    int           failed;
    char          current[TIKU_PATH_MAX];
    char          error[256];     /* the backend's own words, verbatim    */
    int           conflict;       /* 1 while stopped on a collision       */
    char          conflict_path[TIKU_PATH_MAX];
    /* What the question offers.  A folder collision, or any collision
     * while several are pending, gets the four-way form; a lone
     * file-versus-file collision gets Cancel and Replace (FS-018, FS-019). */
    int           conflict_many;  /* four buttons rather than two          */
    int           conflict_isdir;
    int           collisions;     /* counted before the job starts         */
    /* Set when the job stopped on an error, not a collision: the error
     * question offers to continue or to stop (FS-035). */
    int           error_ask;
    int           error_can_continue;  /* 0 for a delete failure          */
    int           cancelled;      /* the job was stopped part-way          */
    /* Bytes written so far, from which the speed is computed. */
    int64_t       bytes;
    /* Where the job is putting things -- the "To: <dir>" half of what is
     * happening (FS-069).  Empty for jobs with no destination. */
    char          to[TIKU_PATH_MAX];
} tiku_progress_t;

/**
 * @brief Asked before every chunk of a file as well as every item;
 *        non-zero abandons the job, part-way through a large file if need
 *        be.
 */
typedef int (*tiku_cancel_fn)(void *ctx);

typedef struct tiku_fs tiku_fs_t;

/**
 * @brief Told whenever an item lands under a new path (FS-060).
 *
 * Every move, rename, trashing, restore and the undo of one reports here
 * with both paths, including a name a collision changed on the way.
 */
typedef void (*tiku_fs_moved_fn)(void *ctx, const char *from,
                                     const char *to);

/** @brief Listen for items landing somewhere else.  One listener. */
void tiku_fs_set_moved(tiku_fs_t *fs, tiku_fs_moved_fn fn, void *ctx);

/**
 * @brief Tell the moved hook what moved since the last call.
 *
 * @note Call it from the loop: a step may run on a worker, and the hook
 *       touches windows.
 */
void tiku_fs_moved_pump(tiku_fs_t *fs);

/** @brief Create the operation engine for one backend. */
tiku_fs_t *tiku_fs_new(tiku_backend_t *backend);

/** @brief Free the engine and its undo and redo history. */
void tiku_fs_free(tiku_fs_t *fs);

/**
 * @brief Decide what a drop does: move within a volume, copy across one,
 *        so dragging to another disk keeps the original.
 *
 * @param force_copy  Non-zero when the user asked for a copy explicitly.
 * @return TIKU_OP_MOVE or TIKU_OP_COPY.
 */
tiku_op_t tiku_fs_drop_action(tiku_fs_t *fs, const char *src,
                                      const char *dst_dir, int force_copy);

/**
 * @brief The name Duplicate would give a copy of @p path.
 *
 * "<name> copy", then "copy 2", "copy 3"; duplicating something already
 * called "<name> copy" gives "copy 2", never "copy copy" (FS-006).
 */
int tiku_fs_duplicate_name(tiku_fs_t *fs, const char *dir,
                               const char *name, char *out, size_t max);

/**
 * @brief The name Create Link would use: the plain name unless it is taken,
 *        then "<name> link", "<name> link 2" (FS-009).
 */
int tiku_fs_link_name(tiku_fs_t *fs, const char *dir,
                          const char *name, char *out, size_t max);

/** @brief Start copying @p paths into @p dst_dir.  @return 0 if accepted. */
int tiku_fs_copy(tiku_fs_t *fs, const char *const *paths, int n,
                     const char *dst_dir, tiku_conflict_t policy);

/** @brief Start moving.  Items already in @p dst_dir are skipped (FS-005). */
int tiku_fs_move(tiku_fs_t *fs, const char *const *paths, int n,
                     const char *dst_dir, tiku_conflict_t policy);

/** @brief Duplicate in place; each item lands in its own parent (FS-008). */
int tiku_fs_duplicate(tiku_fs_t *fs, const char *const *paths, int n);

/**
 * @brief Move to Trash, or delete where there is no Trash.
 *
 * The Trash, anything in it and anything that holds it are dropped from the
 * selection first, without a message.
 */
int tiku_fs_trash(tiku_fs_t *fs, const char *const *paths, int n);

/**
 * @brief Why an operation on an item was refused (FS-023..FS-025).
 *
 * Refusals, not questions: none has an "anyway" answer, as each would
 * destroy something the user did not name.
 */
typedef enum {
    TIKU_REFUSE_NONE = 0,
    TIKU_REFUSE_INTO_SELF,   /* a folder into itself or its own child */
    TIKU_REFUSE_BY_CHILD,    /* replacing a folder with its own child */
    TIKU_REFUSE_KIND,        /* a file over a folder, or the reverse   */
    TIKU_REFUSE_TRASH,       /* the Trash cannot be moved or copied    */
    TIKU_REFUSE_VOLUME       /* a whole volume is not an item to move  */
} tiku_refusal_t;

/**
 * @brief Whether moving or copying @p src into @p dst_dir is allowed.
 *
 * @param msg Receives the sentence to show; the wording is the reason.
 * @return TIKU_REFUSE_NONE when the operation may proceed.
 */
tiku_refusal_t tiku_fs_check_move(tiku_fs_t *fs, const char *src,
                                          const char *dst_dir, char *msg,
                                          size_t max);

/**
 * @brief Delete @p paths outright, with no Trash and no undo.
 *
 * Unlike a trashing, a deletion does not drop the Trash's contents from the
 * selection, so emptying the Trash goes through here.
 *
 * @return 0 if accepted.
 */
int tiku_fs_delete(tiku_fs_t *fs, const char *const *paths, int n);

/*---------------------------------------------------------------------------*/
/* The Trash (FS-038, FS-039, FS-040, FS-044, FS-046, FS-047, FS-048)        */
/*---------------------------------------------------------------------------*/

/** @brief Where trashed items go, relative to the store's root. */
#define TIKU_TRASH_NAME ".Trash"

/**
 * @brief Every Trash there is, one per writable mounted volume, each made
 *        when absent (PVN-027, FS-081).
 *
 * A Trash is per filesystem, so trashing is a rename: "the Trash" is the
 * union of these.
 *
 * @return how many were written.
 */
int tiku_fs_trash_dirs(tiku_fs_t *fs, char out[][TIKU_PATH_MAX],
                           int max);

/** @brief Make every writable mounted volume's Trash, at startup. */
int tiku_fs_prepare_trash(tiku_fs_t *fs);

/**
 * @brief The same over a volume table the caller supplies: read-only
 *        volumes skipped, each Trash listed once.
 */
int tiku_fs_trash_dirs_of(tiku_fs_t *fs,
                              const struct tiku_volumes *vs,
                              char out[][TIKU_PATH_MAX], int max);

/**
 * @brief The Trash folder for @p path, made when absent.
 *
 * Whether a store has a Trash is answered by making one, and the
 * trash-or-delete decision depends on that answer.  It is the home folder's
 * when home is on @p path's filesystem, else one at that filesystem's root.
 * A NULL @p path stands for the home folder.
 *
 * @return 0 on success, -1 on a device or when no Trash can be made.
 */
int tiku_fs_trash_dir(tiku_fs_t *fs, const char *path, char *out,
                          size_t max);

/**
 * @brief Whether this store can hold a Trash at all (FS-044).
 *
 * A device namespace cannot: its nodes are not files.
 *
 * @note When it cannot, the caller offers deletion outright under its own
 *       name, not as Move to Trash.
 */
int tiku_fs_has_trash(const tiku_fs_t *fs);

/**
 * @brief Put a trashed item back where it came from (FS-040).
 *
 * Missing parent folders are made again.  An item already at the original
 * path is not overwritten: that restore is refused.
 *
 * @param err Receives the reason for the last refusal.
 * @return Number restored.
 */
int tiku_fs_restore(tiku_fs_t *fs, const char *const *paths, int n,
                        char *err, size_t errmax);

/** @brief Where a trashed item came from, into @p out.
 *  @return 1, or 0 when nothing was recorded. */
int tiku_fs_original_path(tiku_fs_t *fs, const char *path, char *out,
                              size_t max);

/** @brief Empty the Trash.  @return Number removed. */
int tiku_fs_empty_trash(tiku_fs_t *fs);

/**
 * @brief What the Delete key asks (FS-046, FS-047).
 *
 * Three-way -- Cancel, Move to Trash, Delete -- except inside the Trash, on
 * a filesystem with no Trash, or with Shift held, where the item is deleted
 * outright.
 */
typedef enum {
    TIKU_DELETE_ASK = 0,     /* the three-way question                */
    TIKU_DELETE_STRAIGHT     /* no question: delete outright          */
} tiku_delete_q_t;

/** @brief The Delete key's question for @p path; see tiku_delete_q_t. */
tiku_delete_q_t tiku_fs_delete_question(const tiku_fs_t *fs,
                                                const char *path,
                                                int shift_held);

/** @brief Rename one entry.  @return 0, or -1 with @p err filled. */
int tiku_fs_rename(tiku_fs_t *fs, const char *path,
                       const char *new_name, char *err, size_t errmax);

/**
 * @brief Whether a name is acceptable before anything is attempted.
 *
 * Empty, "." / "..", and names containing '/' are refused; the message is
 * what the UI shows.
 */
int tiku_fs_name_ok(const char *name, char *err, size_t errmax);

/**
 * @brief The characters a name may not contain, as one string: "/"
 *        (MA-046).
 *
 * The name editor refuses these at the keystroke.
 */
const char *tiku_fs_name_deny(void);

/** @brief Advance the current operation by at most @p budget items.
 *  @return items processed; it stops early on a question. */
int tiku_fs_step(tiku_fs_t *fs, int budget);

/** @brief Snapshot of what is happening, for the progress window. */
const tiku_progress_t *tiku_fs_progress(const tiku_fs_t *fs);

/** @brief Answer a conflict the engine stopped on and continue. */
void tiku_fs_resolve(tiku_fs_t *fs, tiku_conflict_t how);

/** @brief Abandon the running operation; what is done stays done. */
void tiku_fs_cancel(tiku_fs_t *fs);

/**
 * @brief Install the mid-copy cancel check.
 *
 * @p fn is polled between chunks; returning non-zero stops the job where
 * it stands.
 */
void tiku_fs_set_cancel_check(tiku_fs_t *fs, tiku_cancel_fn fn,
                                  void *ctx);

/**
 * @brief Answer the "would you like to continue?" the error put up.
 *
 * @p keep_going non-zero counts the item as failed and moves on; zero
 * abandons the rest of the job.
 */
void tiku_fs_resolve_error(tiku_fs_t *fs, int keep_going);

/** @brief Whether an operation has items still to process. */
int tiku_fs_busy(const tiku_fs_t *fs);

/*---------------------------------------------------------------------------*/
/* Space, links, new items and undo                                          */
/*---------------------------------------------------------------------------*/

/** @brief Bytes free on @p path's filesystem, 0 when it will not say. */
uint64_t tiku_fs_free_bytes(const char *path);

/**
 * @brief What @p paths would cost on a destination, blocks included.
 *
 * Recurses; a directory costs one block of its own and a file costs its
 * size plus a block, the block clamped to [1024, 8192].
 */
uint64_t tiku_fs_items_size(const char *const *paths, int n);

/**
 * @brief Whether @p total plus 4 KB of headroom still fits.
 *
 * @return 1 when the copy may start; a @p free_bytes of 0 means unknown,
 *         and fits.
 */
int tiku_fs_fits(uint64_t total, uint64_t free_bytes);

/** @brief How much ceremony a move of @p path needs (FS-027). */
typedef enum {
    TIKU_CONFIRM_NONE = 0,
    TIKU_CONFIRM_ASK,        /* config or settings: a plain question */
    TIKU_CONFIRM_OVERRIDE    /* system or home: Shift must be held   */
} tiku_confirm_t;

/**
 * @brief Whether moving @p path should stop and ask first.
 *
 * System directories are matched by containment and the home folder
 * exactly, so a file inside home moves without a question; anything under
 * the configuration folder asks.
 */
tiku_confirm_t tiku_fs_confirm_move(const char *path);

/** @brief Whether @p path is the Trash or something inside it (FS-014). */
int tiku_fs_in_trash(tiku_fs_t *fs, const char *path);

/**
 * @brief Create symbolic links to @p paths in @p dst.
 *
 * @param relative Non-zero asks for a link relative to @p dst; the link is
 *                 absolute when source and destination are on different
 *                 devices (FS-010).
 */
int tiku_fs_link(tiku_fs_t *fs, const char *const *paths, int n,
                     const char *dst, int relative);

/**
 * @brief Create one symbolic link to @p path in @p dst, called @p name
 *        rather than the target's own leaf; "<name> link" when taken.
 */
int tiku_fs_link_as(tiku_fs_t *fs, const char *path, const char *dst,
                    const char *name, int relative);

/**
 * @brief The name a new folder would get in @p dir, collisions avoided.
 *
 * "New folder", then "New folder 2" .. "New folder 9", then "New folder10".
 */
void tiku_fs_new_folder_name(tiku_fs_t *fs, const char *dir,
                                 char *out, size_t max);

/**
 * @brief Create a new folder in @p dir; its path lands in @p out.
 *
 * Undoable: undo removes it again, redo makes it afresh.
 *
 * @return 0 on success.
 */
int tiku_fs_new_folder(tiku_fs_t *fs, const char *dir, char *out,
                           size_t max, char *err, size_t errmax);

/**
 * @brief Create a new item by copying a stored document template.
 *
 * The destination name is "New <template name>", numbered when taken; no
 * existing item is replaced.  Files, folders and symbolic links are copied
 * with the same tree, permission and time rules as an ordinary copy.
 */
int tiku_fs_new_from_template(tiku_fs_t *fs, const char *template_path,
                                  const char *dst_dir, char *out, size_t max,
                                  char *err, size_t errmax);

/**
 * @brief Whether the last operation can be undone, and its description.
 *
 * A move and a rename are reversed; a copy is undone by removing what it
 * created; a delete cannot be undone and answers 0.
 */
int tiku_fs_can_undo(const tiku_fs_t *fs, char *what, size_t max);

/** @brief Reverse the last operation.  @return items reversed, or -1. */
int tiku_fs_undo(tiku_fs_t *fs, char *err, size_t errmax);

/**
 * @brief Whether the last undo can be put back, and its description.
 *
 * Any new operation empties the redo history.
 */
int tiku_fs_can_redo(const tiku_fs_t *fs, char *what, size_t max);

/**
 * @brief The paths the last operation put in place: what a duplicate, a
 *        copy or a move made, read from the records undo keeps (AW-033).
 *
 * @return Count written.
 */
int tiku_fs_last_created(const tiku_fs_t *fs,
                             char out[][TIKU_PATH_MAX], int max);

/** @brief Replay the operation the last undo reversed. */
int tiku_fs_redo(tiku_fs_t *fs, char *err, size_t errmax);

/** @brief The menu word for @p op ("Copy", "Move to Trash", ...). */
const char *tiku_fs_op_name(tiku_op_t op);

/**
 * @brief One path as one piece of text: a node's value, or a
 *        container's children by name, joined with '|'.
 *
 * The path is answered by whatever backend serves it: through a
 * namespace, /devices/board1/sys/temp is a board's sensor and
 * /home/me/note.txt a local file.  A value is cut to what fits and
 * NUL-terminated.
 *
 * @return 1 with @p out filled, 0 when nothing serves @p path.
 */
int tiku_backend_text(tiku_backend_t *b, const char *path,
                      char *out, size_t max);

#endif /* TIKU_FS_H_ */
