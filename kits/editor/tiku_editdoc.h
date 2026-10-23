/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc.h - a UTF-8 text document with transactions and undo.
 *
 * Byte positions, half-open ranges, lines broken at LF, CR and CRLF.  A
 * change is one transaction of replacements that applies whole or leaves
 * text, history, anchors and view selections as they were.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EDITDOC_H_
#define TIKU_EDITDOC_H_

#include <stddef.h>
#include <stdint.h>

/** @brief A byte offset into the document; -1 is no position. */
typedef int64_t tiku_edit_pos_t;

/** @brief The bytes from @c start up to, not including, @c end. */
typedef struct {
    tiku_edit_pos_t start;      /* first byte                          */
    tiku_edit_pos_t end;        /* one past the last byte               */
} tiku_edit_range_t;

/** @brief One of a view's selections: the end that stays and the end the
 *         caret is at; equal ends are a caret alone. */
typedef struct {
    tiku_edit_pos_t anchor;     /* the fixed end                        */
    tiku_edit_pos_t caret;      /* the moving end                       */
} tiku_edit_selection_t;

/** @brief Why a call did nothing; every failure leaves the document as
 *         it was. */
typedef enum {
    TIKU_EDIT_OK = 0,
    TIKU_EDIT_INVALID,          /* NULL, or a handle that is not live   */
    TIKU_EDIT_INVALID_TEXT,     /* malformed UTF-8, or a NUL byte       */
    TIKU_EDIT_INVALID_RANGE,    /* outside, reversed, inside a
                                   character, out of order or
                                   overlapping                          */
    TIKU_EDIT_READ_ONLY,        /* the document refuses changes         */
    TIKU_EDIT_STALE,            /* the expected generation is not
                                   the document's, or the execution
                                   line was set for other text          */
    TIKU_EDIT_LIMIT,            /* a configured size, or the arithmetic */
    TIKU_EDIT_NO_MEMORY,        /* an allocation failed                 */
    TIKU_EDIT_BUSY,             /* asked during a notification or read  */
    TIKU_EDIT_NOTHING,          /* no step to undo or redo, or no
                                   execution line                       */
    TIKU_EDIT_SHORT,            /* the buffer is too small; nothing
                                   was written                          */
    TIKU_EDIT_BAD_PATTERN,      /* invalid regular expression           */
    TIKU_EDIT_BAD_REPLACEMENT   /* invalid capture reference or escape  */
} tiku_edit_status_t;

/** @brief The document's limits; zero takes the default. */
typedef struct {
    uint64_t max_bytes;         /* longest document                     */
    size_t   history_bytes;     /* undo target, per document            */
} tiku_editdoc_limits_t;

/** @brief The longest document when the limits leave max_bytes 0. */
#define TIKU_EDITDOC_MAX_BYTES_DEFAULT ((uint64_t)1 << 30)
/** @brief The undo target when the limits leave history_bytes 0. */
#define TIKU_EDITDOC_HISTORY_DEFAULT   ((size_t)32 << 20)

/** @brief A view, whose selections the document keeps.  This handle,
 *         tiku_edit_anchor_t and tiku_edit_listener_t stay valid until
 *         their object goes; slot 0 is none, and the three kinds are not
 *         interchangeable. */
typedef struct {
    uint32_t slot;              /* index + 1; 0 is none                 */
    uint32_t generation;        /* the slot's life it belongs to        */
} tiku_edit_view_t;

/** @brief A position that follows the text through edits. */
typedef struct {
    uint32_t slot;              /* index + 1; 0 is none                 */
    uint32_t generation;        /* the slot's life it belongs to        */
} tiku_edit_anchor_t;

/** @brief A registered change listener. */
typedef struct {
    uint32_t slot;              /* index + 1; 0 is none                 */
    uint32_t generation;        /* the slot's life it belongs to        */
} tiku_edit_listener_t;

/** @brief Which way an anchor goes when text is inserted exactly at it:
 *         LEFT stays before the new text, RIGHT moves past it. */
typedef enum {
    TIKU_EDIT_LEFT = 0,
    TIKU_EDIT_RIGHT = 1
} tiku_edit_affinity_t;

/** @brief One replacement, in the coordinates before its transaction. */
typedef struct {
    tiku_edit_range_t range;    /* the bytes replaced                   */
    const char *text;           /* what replaces them; NULL when
                                   length is 0                          */
    size_t length;              /* bytes in text                        */
} tiku_edit_replace_t;

/** @brief Typing that may join the previous typing step for undo. */
#define TIKU_EDIT_TYPING 0x1u

/** @brief A transaction: replacements in ascending order that do not
 *         overlap, applied as one undo step. */
typedef struct {
    const tiku_edit_replace_t *edits;   /* count replacements           */
    size_t count;                       /* 0 changes no text            */
    tiku_edit_view_t view;              /* the view it came from        */
    int select;                         /* set the view's selection     */
    tiku_edit_pos_t anchor;             /* selection after, in the
                                           coordinates after            */
    tiku_edit_pos_t caret;              /* the selection's moving end   */
    unsigned flags;                     /* TIKU_EDIT_TYPING             */
    uint64_t generation;                /* the document's generation
                                           expected; 0 accepts any      */
    const tiku_edit_selection_t *selections; /* with select, the view's
                                           selections after, in the
                                           coordinates after; anchor and
                                           caret are then unused        */
    size_t nselections;                 /* 0: anchor and caret alone    */
    size_t main;                        /* index in selections of the
                                           main one                     */
} tiku_edit_transaction_t;

/** @brief What a change was. */
typedef enum {
    TIKU_EDIT_EDITED = 0,
    TIKU_EDIT_UNDONE,
    TIKU_EDIT_REDONE,
    TIKU_EDIT_LOADED
} tiku_edit_cause_t;

/** @brief A committed change as listeners receive it; its spans are read
 *         with tiku_edit_change_span() while the notification runs. */
typedef struct {
    tiku_edit_cause_t cause;            /* edit, undo, redo or load     */
    uint64_t generation_before;         /* generation before the change */
    uint64_t generation_after;          /* generation after the change  */
    int64_t lines_before;               /* line count before the change */
    int64_t lines_after;                /* line count after the change  */
    size_t count;                       /* spans, ascending             */
    tiku_edit_view_t view;              /* the view it came from, if
                                           attached; none for a load    */
    const void *detail;                 /* internal: the spans          */
    int inverse;                        /* internal: spans are reversed */
} tiku_edit_change_t;

/** @brief A document, freed when its last reference is released. */
typedef struct tiku_editdoc tiku_editdoc_t;

/** @brief Called after a change has been committed.  The document refuses
 *         changes while listeners run. */
typedef void (*tiku_edit_listen_fn)(void *ctx, tiku_editdoc_t *doc,
                                    const tiku_edit_change_t *change);

/** @brief Called with the document's bytes in order, in one or two runs;
 *         nonzero stops.  The bytes are valid only during the call. */
typedef int (*tiku_edit_bytes_fn)(void *ctx, const char *bytes, size_t n);

/** @brief An owned copy of the text, for work away from the UI thread. */
typedef struct {
    char *text;                 /* length bytes and a NUL               */
    size_t length;              /* bytes of text                        */
    uint64_t generation;        /* the generation it copies             */
    uint64_t state;             /* the history state it copies          */
} tiku_edit_snapshot_t;

/** @brief A new empty document with one reference; @p limits may be
 *         NULL. */
tiku_edit_status_t tiku_editdoc_create(const tiku_editdoc_limits_t *limits,
                                       tiku_editdoc_t **doc);
/** @brief Take a reference. */
void tiku_editdoc_retain(tiku_editdoc_t *doc);
/** @brief Drop a reference; the last frees the document, its text and
 *         history, and everything registered on it. */
void tiku_editdoc_release(tiku_editdoc_t *doc);

/** @brief Replace all the text, clear the history and count the result
 *         as saved.  Anchors and selections map as for one replacement. */
tiku_edit_status_t tiku_editdoc_load(tiku_editdoc_t *doc, const char *text,
                                     size_t length);

/** @brief Bytes held. */
tiku_edit_pos_t tiku_editdoc_length(const tiku_editdoc_t *doc);
/** @brief The byte at @p pos, or -1 outside the text. */
int tiku_editdoc_byte_at(const tiku_editdoc_t *doc, tiku_edit_pos_t pos);
/** @brief Copy @p range and a NUL into @p out.  For a valid range
 *         @p length is set to its size; with TIKU_EDIT_SHORT nothing is
 *         written. */
tiku_edit_status_t tiku_editdoc_read(const tiku_editdoc_t *doc,
                                     tiku_edit_range_t range, char *out,
                                     size_t cap, size_t *length);
/** @brief Hand @p range to @p fn without copying it. */
tiku_edit_status_t tiku_editdoc_each(tiku_editdoc_t *doc,
                                     tiku_edit_range_t range,
                                     tiku_edit_bytes_fn fn, void *ctx);

/** @brief Lines held, at least 1; a final line break starts an empty
 *         last line. */
int64_t tiku_editdoc_lines(const tiku_editdoc_t *doc);
/** @brief Where @p line starts; line == lines() gives the length, and -1
 *         is returned for anything else outside. */
tiku_edit_pos_t tiku_editdoc_line_start(const tiku_editdoc_t *doc,
                                        int64_t line);
/** @brief Where @p line's text ends, before its line break; -1 outside. */
tiku_edit_pos_t tiku_editdoc_line_end(const tiku_editdoc_t *doc,
                                      int64_t line);
/** @brief The line holding @p pos, the length included; -1 outside. */
int64_t tiku_editdoc_line_of(const tiku_editdoc_t *doc, tiku_edit_pos_t pos);

/** @brief Whether @p pos is in 0..length and not inside a character. */
int tiku_editdoc_is_boundary(const tiku_editdoc_t *doc, tiku_edit_pos_t pos);
/** @brief Whether @p pos may hold a caret: a boundary not between the CR
 *         and LF of one line break. */
int tiku_editdoc_is_caret(const tiku_editdoc_t *doc, tiku_edit_pos_t pos);
/** @brief The next caret position: one character, a CRLF as one;
 *         clamped to the length. */
tiku_edit_pos_t tiku_editdoc_next(const tiku_editdoc_t *doc,
                                  tiku_edit_pos_t pos);
/** @brief The previous caret position, clamped to 0 and the length. */
tiku_edit_pos_t tiku_editdoc_prev(const tiku_editdoc_t *doc,
                                  tiku_edit_pos_t pos);

/** @brief The document's generation, raised by every committed change,
 *         undo, redo and load. */
uint64_t tiku_editdoc_generation(const tiku_editdoc_t *doc);
/** @brief The history state of the text now; equal states hold equal
 *         text, a state is never reused, and 0 is never one. */
uint64_t tiku_editdoc_state(const tiku_editdoc_t *doc);
/** @brief Record that the text of @p state is what the file holds; 0
 *         records that no state does, and tiku_editdoc_modified() then
 *         returns 1 whatever the text. */
void tiku_editdoc_saved(tiku_editdoc_t *doc, uint64_t state);
/** @brief Whether the text now differs from the state last saved. */
int tiku_editdoc_modified(const tiku_editdoc_t *doc);
/** @brief Refuse or allow every change: edits, undo, redo and load. */
void tiku_editdoc_set_read_only(tiku_editdoc_t *doc, int read_only);
/** @brief Whether the document refuses every change. */
int tiku_editdoc_read_only(const tiku_editdoc_t *doc);

/** @brief Apply one transaction whole, or nothing. */
tiku_edit_status_t tiku_editdoc_apply(tiku_editdoc_t *doc,
                                      const tiku_edit_transaction_t *t);
/** @brief Step the history back one transaction, giving the view it came
 *         from, while that view is attached, the selections it had
 *         before. */
tiku_edit_status_t tiku_editdoc_undo(tiku_editdoc_t *doc);
/** @brief Step the history forward one transaction, giving the view it
 *         came from, while that view is attached, the selections it had
 *         after. */
tiku_edit_status_t tiku_editdoc_redo(tiku_editdoc_t *doc);
/** @brief Whether there is a transaction to undo. */
int tiku_editdoc_can_undo(const tiku_editdoc_t *doc);
/** @brief Whether there is a transaction to redo. */
int tiku_editdoc_can_redo(const tiku_editdoc_t *doc);
/** @brief End the current typing step; the next typing starts another. */
void tiku_editdoc_seal(tiku_editdoc_t *doc);

/** @brief An anchor at a character boundary @p pos. */
tiku_edit_status_t tiku_editdoc_anchor_create(tiku_editdoc_t *doc,
                                              tiku_edit_pos_t pos,
                                              tiku_edit_affinity_t affinity,
                                              tiku_edit_anchor_t *anchor);
/** @brief Where an anchor is now. */
tiku_edit_status_t tiku_editdoc_anchor_pos(const tiku_editdoc_t *doc,
                                           tiku_edit_anchor_t anchor,
                                           tiku_edit_pos_t *pos);
/** @brief Put an anchor at another character boundary. */
tiku_edit_status_t tiku_editdoc_anchor_move(tiku_editdoc_t *doc,
                                            tiku_edit_anchor_t anchor,
                                            tiku_edit_pos_t pos);
/** @brief Free an anchor; its handle stops working. */
void tiku_editdoc_anchor_destroy(tiku_editdoc_t *doc,
                                 tiku_edit_anchor_t anchor);

/** @brief Attach a view with a caret at 0; the document keeps the view's
 *         selections and moves them through edits, undo and redo. */
tiku_edit_status_t tiku_editdoc_view_attach(tiku_editdoc_t *doc,
                                            tiku_edit_view_t *view);
/** @brief Forget a view; history keeps its steps. */
void tiku_editdoc_view_detach(tiku_editdoc_t *doc, tiku_edit_view_t view);
/** @brief A view's main selection now. */
tiku_edit_status_t tiku_editdoc_view_selection(const tiku_editdoc_t *doc,
                                               tiku_edit_view_t view,
                                               tiku_edit_pos_t *anchor,
                                               tiku_edit_pos_t *caret);
/** @brief Set a selection at caret positions, the view's only one;
 *         setting the selection of the view that is typing ends its typing
 *         step. */
tiku_edit_status_t tiku_editdoc_view_select(tiku_editdoc_t *doc,
                                            tiku_edit_view_t view,
                                            tiku_edit_pos_t anchor,
                                            tiku_edit_pos_t caret);
/**
 * @brief Set a view's selections, @p count of them at caret positions;
 *        the one at @p main is the selection the one-selection calls use.
 *
 * Kept in order by their starts.  Two that overlap, or a caret touching
 * another selection, become one, the main one if either was; two
 * selections that only touch stay two.  Every edit keeps these rules.
 */
tiku_edit_status_t tiku_editdoc_view_select_many(
    tiku_editdoc_t *doc, tiku_edit_view_t view,
    const tiku_edit_selection_t *selections, size_t count, size_t main);
/** @brief A view's selections in order: how many, up to @p cap of them in
 *         @p out, and the main one's place in @p main; 0 when the view is
 *         not attached. */
size_t tiku_editdoc_view_selections(const tiku_editdoc_t *doc,
                                    tiku_edit_view_t view,
                                    tiku_edit_selection_t *out, size_t cap,
                                    size_t *main);

/** @brief Call @p fn after every committed change. */
tiku_edit_status_t tiku_editdoc_listen(tiku_editdoc_t *doc,
                                       tiku_edit_listen_fn fn, void *ctx,
                                       tiku_edit_listener_t *listener);
/** @brief Stop a listener; safe from inside a notification. */
void tiku_editdoc_unlisten(tiku_editdoc_t *doc,
                           tiku_edit_listener_t listener);
/** @brief The @p i-th span of a change: what it replaced in the text
 *         before and what holds its result in the text after. */
void tiku_edit_change_span(const tiku_edit_change_t *change, size_t i,
                           tiku_edit_range_t *before,
                           tiku_edit_range_t *after);

/** @brief Copy the whole text with its generation and history state. */
tiku_edit_status_t tiku_editdoc_snapshot(const tiku_editdoc_t *doc,
                                         tiku_edit_snapshot_t *snapshot);
/** @brief Free a snapshot's text. */
void tiku_edit_snapshot_free(tiku_edit_snapshot_t *snapshot);

#endif /* TIKU_EDITDOC_H_ */
