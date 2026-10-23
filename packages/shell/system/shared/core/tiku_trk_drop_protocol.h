/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_drop_protocol.h - a drag from another application: what it
 * offers, what a drop of it does, and the clipping file a drop of text or
 * a bitmap becomes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DROP_PROTOCOL_H_
#define TIKU_TRK_DROP_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The actions a foreign drag may offer, as bits. */
#define TIKU_TRK_DROP_ACTION_COPY  0x1u
#define TIKU_TRK_DROP_ACTION_MOVE  0x2u
#define TIKU_TRK_DROP_ACTION_LINK  0x4u
#define TIKU_TRK_DROP_ACTION_TRASH 0x8u

/** @brief What a foreign drag offers. */
typedef struct {
    unsigned actions;               /* TIKU_TRK_DROP_ACTION_* bits        */
    int has_refs;                   /* its types carry file references    */
    int has_text;
    int has_bitmap;
    int promise;                    /* the data is made only on the drop  */
    const char *originator;         /* not set by the parser              */
} tiku_trk_foreign_offer_t;

/** @brief What the target tells the drag's source it will do. */
typedef enum {
    TIKU_TRK_DROP_REPLY_NONE = 0,
    TIKU_TRK_DROP_REPLY_COPY_TARGET,
    TIKU_TRK_DROP_REPLY_MOVE_TARGET,
    TIKU_TRK_DROP_REPLY_TRASH_TARGET,
    TIKU_TRK_DROP_REPLY_REJECT
} tiku_trk_drop_reply_t;

/** @brief What a foreign drop does. */
typedef struct {
    tiku_trk_drop_reply_t reply;
    unsigned chosen_action;         /* one TIKU_TRK_DROP_ACTION_* bit     */
    int create_clipping;            /* text, a bitmap or a promise        */
    int accepted;
} tiku_trk_drop_result_t;

/**
 * @brief Read what a foreign drag offers: the actions it names (copy when it
 *        names none), whether its types carry refs, text or a bitmap, and
 *        whether it is a promise.
 *
 * @return 0, or -1 when @p offer is NULL.
 */
int tiku_trk_foreign_offer_parse(tiku_trk_foreign_offer_t *offer,
                                 const char *actions, const char *types,
                                 int promise);
/**
 * @brief What a foreign drop does on its target.
 *
 * A root disk, or anything that is neither a directory nor the Trash,
 * rejects it; the Trash takes a trash action; a directory takes
 * @p requested_action where offered, else copy, else whatever is offered.
 * A promise, text or a bitmap becomes a clipping; an offer with nothing to
 * take is rejected.
 */
tiku_trk_drop_result_t tiku_trk_foreign_drop(const tiku_trk_foreign_offer_t *o,
                                             int target_directory,
                                             int target_trash, int root_disk,
                                             unsigned requested_action);

/**
 * @brief Write a text or bitmap offer to a new clipping file in
 *        @p directory.
 *
 * The file is @p name, or an Untitled one, numbered from 2 when the name is
 * taken.  Any transport can hand it the bytes, and a failed write removes
 * the partial file.  A bitmap over 1 MiB is refused.
 *
 * @param out Receives the path written.
 * @return 0, or -1 when nothing was written.
 */
int tiku_trk_foreign_clip_create(const char *directory, const char *name,
                                 const void *data, size_t size, int bitmap,
                                 char *out, size_t out_size);

#endif
