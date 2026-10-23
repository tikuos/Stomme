/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richwatch.h - synchronous notifications of published document state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHWATCH_H_
#define TIKU_RICHWATCH_H_

#include "tiku_richdoc.h"

/** @brief Owner-thread subscription retaining its document. */
typedef struct tiku_richwatch tiku_richwatch_t;
/** @brief Cause of a published content or document-metadata change. */
typedef enum {
    TIKU_RICH_CHANGE_EDIT,
    TIKU_RICH_CHANGE_UNDO,
    TIKU_RICH_CHANGE_REDO,
    TIKU_RICH_CHANGE_IMPORT,
    TIKU_RICH_CHANGE_SAVED,
    TIKU_RICH_CHANGE_PERMISSION,
    TIKU_RICH_CHANGE_HISTORY
} tiku_rich_change_kind_t;
/** @brief Borrowed callback data describing fully published state. */
typedef struct {
    tiku_rich_change_kind_t kind;
    uint64_t generation;
    uint64_t state;             /**< content's stable save-state token */
    uint64_t origin;            /**< originating session ID, or zero */
    int dirty;
} tiku_rich_change_t;
/** @brief Inspect published state on the document's owner thread.
 *  @note Reads and snapshots are allowed. Mutation and subscription changes
 *        return BUSY; document/anchor releases and void mutators do nothing.
 *        Queue those actions. Context must outlive the subscription.
 *        Do not transfer callback-data pointers to another thread. */
typedef void (*tiku_rich_changed_fn)(void *context, tiku_richdoc_t *doc,
                                     const tiku_rich_change_t *change);
/** @brief Subscribe to successful changes; failure clears *out.
 *  @note At most 1024 subscriptions per document, charged to its allocator.
 *        Notifications run newest subscription first, without allocation.
 *        Failed/no-change operations and local session changes emit none. */
tiku_rich_status_t tiku_richwatch_create(tiku_richdoc_t *doc,
    tiku_rich_changed_fn callback, void *context, tiku_richwatch_t **out);
/** @brief Remove and release a subscription, or return BUSY in a callback.
 *  @note NULL is accepted. Success guarantees no further callbacks. */
tiku_rich_status_t tiku_richwatch_destroy(tiku_richwatch_t *watch);

#endif /* TIKU_RICHWATCH_H_ */
