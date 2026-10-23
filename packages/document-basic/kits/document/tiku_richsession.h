/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richsession.h - owner-thread selection and typing state for one view.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHSESSION_H_
#define TIKU_RICHSESSION_H_

#include "tiku_richcmd.h"

/** @brief Headless editing state retaining a document through two anchors. */
typedef struct tiku_richsession tiku_richsession_t;

/** @brief A session's directional endpoints before and after one change. */
typedef struct {
    uint64_t generation;      /**< current document generation */
    tiku_rich_pos_t previous_anchor, previous_caret;
    tiku_rich_pos_t anchor, caret;
} tiku_rich_selection_change_t;
/** @brief Owner-thread callback; change is borrowed for the duration of call. */
typedef void (*tiku_rich_selection_fn)(void *context,
    tiku_richsession_t *session, const tiku_rich_selection_change_t *change);
/** @brief Replace a session's selection observer, or clear it with NULL.
 *  @note Delivery follows navigation and published edits/undo/redo/imports
 *        when endpoint values change. Registration emits no event. Callbacks
 *        may query state but not mutate the document, sessions or observers.
 *        Context is borrowed until replacement or session destruction.
 *        Registration and delivery allocate nothing; BUSY during callbacks. */
tiku_rich_status_t tiku_richsession_observe_selection(
    tiku_richsession_t *session, tiku_rich_selection_fn callback, void *context);

/** @brief Create a session at the document start; failure clears *out.
 *  @note Sessions use two entries from the document's anchor budget.
 *        All session calls belong to the document's owner thread. */
tiku_rich_status_t tiku_richsession_create(tiku_richdoc_t *doc,
                                           tiku_richsession_t **out);
/** @brief Destroy a session, or return BUSY during document notification.
 *  @note NULL is accepted. Other sessions and snapshots remain valid. */
tiku_rich_status_t tiku_richsession_destroy(tiku_richsession_t *session);
/** @brief Document-local, non-reused session identity; zero for NULL. */
uint64_t tiku_richsession_id(const tiku_richsession_t *session);
/** @brief Copy selection endpoints in anchor/caret order, not sorted order. */
tiku_rich_status_t tiku_richsession_selection(
    const tiku_richsession_t *session, tiku_rich_pos_t *anchor,
    tiku_rich_pos_t *caret);
/** @brief Copy selected plain text, joining paragraphs with LF.
 *  @note NULL buffer with zero capacity queries the byte length excluding
 *        NUL. Too little space returns LIMIT without changing the buffer.
 *        Selection direction is preserved; an empty selection is empty text.
 *        No allocation or layout is required. */
tiku_rich_status_t tiku_richsession_text(const tiku_richsession_t *session,
    char *buffer, size_t capacity, size_t *length);
/** @brief Copy selection endpoints in document order without moving them. */
tiku_rich_status_t tiku_richsession_range(const tiku_richsession_t *session,
    tiku_rich_pos_t *first, tiku_rich_pos_t *last);
/** @brief Set a directional selection atomically and clear pending formats.
 *  @note Positions must be live UTF-8 boundaries; this is not grapheme
 *        navigation. Zero generation accepts the current document. */
tiku_rich_status_t tiku_richsession_select(tiku_richsession_t *session,
    uint64_t generation, tiku_rich_pos_t anchor, tiku_rich_pos_t caret);
/** @brief Set a session-local editing restriction, without editing content. */
tiku_rich_status_t tiku_richsession_set_read_only(
    tiku_richsession_t *session, int read_only);
/** @brief Whether this session or its document refuses content edits. */
int tiku_richsession_read_only(const tiku_richsession_t *session);
/** @brief Query selection formatting, including pending caret formatting.
 *  @note A returned link is borrowed until the next document mutation. */
tiku_rich_status_t tiku_richsession_format(
    const tiku_richsession_t *session, tiku_rich_selection_t *out);
/** @brief Query a command using this session's selection and permissions.
 *  @note Caret character formatting and character styles affect pending
 *        typing attributes. Link commands require selected text. */
tiku_rich_status_t tiku_richsession_query(const tiku_richsession_t *session,
    tiku_richcmd_kind_t kind, tiku_richcmd_state_t *out);
/** @brief Execute a command and publish its selection with the document.
 *  @note Text replacement, deletion, paragraph break and rich paste collapse
 *        the selection after inserted content. Undo/redo restore the
 *        originating session's selection, even after explicit navigation.
 *        Failed commands leave selections and pending attributes unchanged.
 *        Zero generation accepts current content; each edit is one group. */
tiku_rich_status_t tiku_richsession_execute(tiku_richsession_t *session,
    uint64_t generation, const tiku_richcmd_t *command);
/** @brief Insert committed typing, joining compatible adjacent insertions.
 *  @note Replacements and paragraph breaks start separate groups. Navigation,
 *        formatting, other edits, save checkpoints and explicit break calls
 *        end a group. Use execute for paste or one indivisible IME commit. */
tiku_rich_status_t tiku_richsession_type(tiku_richsession_t *session,
    uint64_t generation, const char *text, size_t length);
/** @brief End this session's current typing group without changing content.
 *  @note Call on focus loss, composition start or a host typing timeout. */
tiku_rich_status_t tiku_richsession_break_undo(tiku_richsession_t *session);

#endif /* TIKU_RICHSESSION_H_ */
