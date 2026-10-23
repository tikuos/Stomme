/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_editdoc_private.h - the document engine's parts, shared between
 * its files and its tests.  Nothing here is API.
 *
 * The gap-buffer and line-index structures follow Scintilla 5.6.7
 * src/SplitVector.h and src/Partitioning.h, Copyright 1998-2007 by Neil
 * Hodgson; see LICENSE.Scintilla and UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#ifndef TIKU_EDITDOC_PRIVATE_H_
#define TIKU_EDITDOC_PRIVATE_H_

#include "tiku_editdoc.h"

#include <stddef.h>
#include <stdint.h>

/* A gap buffer of bytes (tiku_editdoc_buffer.c). */
typedef struct {
    char *body;                 /**< capacity bytes: part1, gap, part2  */
    ptrdiff_t capacity;         /**< bytes allocated                    */
    ptrdiff_t length;           /**< bytes held                         */
    ptrdiff_t part1;            /**< bytes before the gap               */
    ptrdiff_t gap;              /**< capacity - length                  */
    ptrdiff_t grow;             /**< the next growth step               */
} tiku_ed_bytes_t;

/* A gap buffer of 64-bit positions (tiku_editdoc_buffer.c). */
typedef struct {
    int64_t *body;              /**< capacity entries                   */
    ptrdiff_t capacity;         /**< entries allocated                  */
    ptrdiff_t length;           /**< entries held                       */
    ptrdiff_t part1;            /**< entries before the gap             */
    ptrdiff_t gap;              /**< capacity - length                  */
    ptrdiff_t grow;             /**< the next growth step               */
} tiku_ed_starts_t;

/* Line starts with a deferred shift (tiku_editdoc_lines.c). */
typedef struct {
    tiku_ed_starts_t body;      /**< partition starts, then the end     */
    int64_t step_partition;     /**< starts after it lack step_length   */
    int64_t step_length;        /**< the pending shift                  */
} tiku_ed_lines_t;

/* Text and its line index, changed together. */
typedef struct {
    tiku_ed_bytes_t bytes;      /**< the text                           */
    tiku_ed_lines_t lines;      /**< where each line starts             */
} tiku_ed_text_t;

/* One replacement as history keeps it. */
typedef struct {
    int64_t pre;                /**< start before the transaction       */
    int64_t post;               /**< start after the transaction        */
    int64_t removed;            /**< bytes taken out                    */
    int64_t added;              /**< bytes put in                       */
    size_t removed_at;          /**< removed bytes' offset in the payload */
    size_t added_at;            /**< added bytes' offset in the payload */
} tiku_ed_piece_t;

/* The kind of typing step a record is, for joining the next. */
enum {
    TIKU_ED_TYPED_NONE = 0,     /**< not a typing step                  */
    TIKU_ED_TYPED_INSERT,       /**< text put in at every piece         */
    TIKU_ED_TYPED_DELETE        /* one caret step taken out at every
                                   piece                                */
};

/* One transaction in the history. */
typedef struct {
    tiku_ed_piece_t *pieces;    /**< ascending, npieces of them         */
    size_t npieces;             /**< replacements                       */
    char *payload;              /**< removed and added bytes            */
    size_t payload_len;         /**< bytes in payload                   */
    uint64_t state_before;      /**< history state it starts from       */
    uint64_t state_after;       /**< the state it makes                 */
    tiku_edit_view_t view;      /**< the view it came from              */
    int64_t before_anchor;      /**< main selection before: fixed end   */
    int64_t before_caret;       /**< main selection before: moving end  */
    int64_t after_anchor;       /**< main selection after: fixed end    */
    int64_t after_caret;        /**< main selection after: moving end   */
    tiku_edit_selection_t *more; /**< the view's other selections:
                                   nbefore before, then nafter after    */
    size_t nbefore;             /**< other selections before            */
    size_t nafter;              /**< other selections after             */
    int typed;                  /**< TIKU_ED_TYPED_*                    */
    size_t cost;                /**< what it charges the history budget */
} tiku_ed_record_t;

/* The undo history: records[0 .. current) are applied, the rest wait to
 * be redone. */
typedef struct {
    tiku_ed_record_t *records;  /**< oldest first                       */
    size_t count;               /**< records held                       */
    size_t cap;                 /**< records allocated                  */
    size_t current;             /**< records applied                    */
    size_t cost;                /**< sum of the records' costs          */
    size_t target;              /**< the budget pruning keeps to        */
    uint64_t next_state;        /**< the next state identity to hand out */
    uint64_t base_state;        /**< the state before records[0]        */
    uint64_t saved_state;       /**< the state the file holds           */
    int open;                   /**< the last record takes more typing  */
} tiku_ed_history_t;

/* A registered anchor. */
typedef struct {
    int64_t pos;                /**< where it is now                    */
    uint32_t generation;        /**< bumped when the slot is freed      */
    uint8_t live;               /**< in use                             */
    uint8_t right;              /**< moves past text inserted at it     */
} tiku_ed_anchor_t;

/* A registered view's selections: the main one, and the others in order
 * of their starts, kept apart from each other and from it. */
typedef struct {
    int64_t anchor;             /**< the main selection's fixed end     */
    int64_t caret;              /**< the main selection's moving end    */
    tiku_edit_selection_t *more; /**< the other selections, nmore of them */
    size_t nmore;               /**< selections in more                 */
    size_t more_cap;            /**< room in more                       */
    uint64_t selection_serial;  /**< raised by the view_select calls    */
    uint32_t generation;        /**< bumped when the slot is freed      */
    uint8_t live;               /**< attached                           */
} tiku_ed_view_t;

/* A line marker: it sits at its line's start. */
typedef struct {
    int64_t pos;                /**< where its line starts              */
    uint32_t generation;        /**< bumped when the slot is taken or
                                   freed                                */
    uint8_t live;               /**< in use                             */
    uint8_t kind;               /**< 0 to TIKU_EDIT_MARKER_KINDS - 1    */
} tiku_ed_marker_t;

/* An indicator: a range of text with a kind. */
typedef struct {
    int64_t start, end;         /**< the range, start before end        */
    uint32_t generation;        /**< bumped when the slot is taken or
                                   freed                                */
    uint8_t live;               /**< in use                             */
    uint8_t kind;               /**< 0 to TIKU_EDIT_INDICATOR_KINDS - 1 */
} tiku_ed_indicator_t;

/* An annotation: text shown under a line, kept at that line's start. */
typedef struct {
    int64_t pos;                /**< where its line starts              */
    char *text;                 /**< what it says, NUL-terminated       */
    uint8_t live;               /**< in use                             */
    uint8_t kind;               /**< 0 to TIKU_EDIT_INDICATOR_KINDS - 1 */
} tiku_ed_annotation_t;

/* A registered listener. */
typedef struct {
    tiku_edit_listen_fn fn;     /**< what to call                       */
    void *ctx;                  /**< its argument                       */
    uint64_t since;             /**< notification it was added in, or 0 */
    uint32_t generation;        /**< bumped when the slot is freed      */
    uint8_t live;               /**< registered                         */
} tiku_ed_listener_t;

/* The document. */
struct tiku_editdoc {
    unsigned refs;              /**< references held                    */
    tiku_ed_text_t text;        /**< bytes and lines                    */
    uint64_t max_bytes;         /**< the size limit                     */
    uint64_t generation;        /**< raised by every committed change   */
    int read_only;              /**< refuse every change                */
    int busy;                   /**< listeners or a read are running    */
    uint64_t dispatch;          /**< serial of the latest notification  */
    tiku_ed_history_t history;  /**< undo and redo                      */
    tiku_ed_anchor_t *anchors;  /**< nanchors slots                     */
    size_t nanchors;            /**< anchor slots allocated             */
    tiku_ed_view_t *views;      /**< nviews slots                       */
    size_t nviews;              /**< view slots allocated               */
    tiku_ed_listener_t *listeners; /**< nlisteners slots                */
    size_t nlisteners;          /**< listener slots allocated           */
    tiku_ed_marker_t *markers;  /**< nmarkers slots                     */
    size_t nmarkers;            /**< marker slots allocated             */
    tiku_ed_indicator_t *indicators; /**< nindicators slots             */
    size_t nindicators;         /**< indicator slots allocated          */
    tiku_ed_annotation_t *annotations; /**< nannotations slots          */
    size_t nannotations;        /**< annotation slots allocated         */
    uint64_t annotation_serial; /**< raised when an annotation is added
                                   or removed                           */
    int64_t exec_line;          /**< where a runtime stopped, -1 none   */
    uint64_t exec_state;        /**< the history state it was set for   */
    tiku_ed_piece_t loaded;     /**< the one span a load reports        */
};

/* Allocation, counted and hookable for tests (tiku_editdoc_alloc.c). */

/* @p n bytes from the allocator in force; NULL for 0 bytes or no room. */
void *tiku_ed_alloc(size_t n);
/* @p p resized to @p n bytes; NULL for 0 bytes or no room, @p p then
 * left as it was. */
void *tiku_ed_realloc(void *p, size_t n);
/* Free @p p; NULL is ignored. */
void tiku_ed_free(void *p);
/* Open a commit on this thread.  Code inside a commit must not allocate;
 * an allocation before tiku_ed_commit_end() is counted in
 * tiku_ed_test_commit_allocations(). */
void tiku_ed_commit_begin(void);
/* Close the commit tiku_ed_commit_begin() opened. */
void tiku_ed_commit_end(void);

/* An allocator a test puts in place of the C library's. */
typedef struct {
    void *(*alloc_fn)(size_t n);             /**< malloc                  */
    void *(*realloc_fn)(void *p, size_t n);  /**< realloc                 */
    void (*free_fn)(void *p);                /**< free                    */
} tiku_ed_allocator_t;

/* Substitute the allocator for a test; NULL restores the C library's. */
void tiku_ed_test_allocator(const tiku_ed_allocator_t *allocator);
/* Allocations attempted inside a commit since the process started. */
unsigned long tiku_ed_test_commit_allocations(void);

/* UTF-8 (tiku_editdoc_utf8.c). */

/* Whether the @p n bytes at @p s are valid UTF-8 without a NUL. */
int tiku_ed_utf8_valid(const char *s, size_t n);
/* The length of the sequence @p lead starts, or 0 for a byte no sequence
 * starts with. */
int tiku_ed_utf8_length(unsigned char lead);

/* The byte gap buffer (tiku_editdoc_buffer.c). */

/* An empty buffer; nothing is allocated until tiku_ed_bytes_reserve(). */
void tiku_ed_bytes_init(tiku_ed_bytes_t *b);
/* Free the storage and leave the buffer empty, with nothing allocated. */
void tiku_ed_bytes_free(tiku_ed_bytes_t *b);
/* Room for @p extra more bytes; 0 when there is none.  The only call
 * that allocates. */
int tiku_ed_bytes_reserve(tiku_ed_bytes_t *b, ptrdiff_t extra);
/* The byte at @p pos as an unsigned value; 0 outside the bytes held. */
int tiku_ed_bytes_at(const tiku_ed_bytes_t *b, ptrdiff_t pos);
/* Insert @p n bytes at @p pos into room tiku_ed_bytes_reserve() made. */
void tiku_ed_bytes_insert(tiku_ed_bytes_t *b, ptrdiff_t pos, const char *s,
                          ptrdiff_t n);
/* Remove @p n bytes at @p pos; the storage is never freed. */
void tiku_ed_bytes_delete(tiku_ed_bytes_t *b, ptrdiff_t pos, ptrdiff_t n);
/* Copy @p n bytes from @p pos, across the gap. */
void tiku_ed_bytes_copy(const tiku_ed_bytes_t *b, char *out, ptrdiff_t pos,
                        ptrdiff_t n);
/* Hand @p n bytes from @p pos to @p fn in at most two runs; nonzero when
 * @p fn stopped. */
int tiku_ed_bytes_each(const tiku_ed_bytes_t *b, ptrdiff_t pos, ptrdiff_t n,
                       tiku_edit_bytes_fn fn, void *ctx);

/* The gap buffer of 64-bit positions (tiku_editdoc_buffer.c). */

/* An empty buffer; nothing is allocated until tiku_ed_starts_reserve(). */
void tiku_ed_starts_init(tiku_ed_starts_t *v);
/* Free the storage and leave the buffer empty, with nothing allocated. */
void tiku_ed_starts_free(tiku_ed_starts_t *v);
/* Room for @p extra more entries; 0 when there is none.  The only call
 * that allocates. */
int tiku_ed_starts_reserve(tiku_ed_starts_t *v, ptrdiff_t extra);
/* Insert @p value at @p pos into room tiku_ed_starts_reserve() made. */
void tiku_ed_starts_insert(tiku_ed_starts_t *v, ptrdiff_t pos, int64_t value);
/* The entry at @p pos; 0 outside the entries. */
int64_t tiku_ed_starts_at(const tiku_ed_starts_t *v, ptrdiff_t pos);
/* Set the entry at @p pos; outside the entries nothing changes. */
void tiku_ed_starts_set(tiku_ed_starts_t *v, ptrdiff_t pos, int64_t value);
/* Insert @p n entries at @p pos into room tiku_ed_starts_reserve()
 * made. */
void tiku_ed_starts_insert_many(tiku_ed_starts_t *v, ptrdiff_t pos,
                                const int64_t *values, ptrdiff_t n);
/* Remove @p n entries at @p pos; the storage is never freed. */
void tiku_ed_starts_delete(tiku_ed_starts_t *v, ptrdiff_t pos, ptrdiff_t n);
/* Add @p delta to the entries from @p start up to @p end. */
void tiku_ed_starts_add(tiku_ed_starts_t *v, ptrdiff_t start, ptrdiff_t end,
                        int64_t delta);
/* Copy @p n entries from @p pos, across the gap. */
void tiku_ed_starts_copy(const tiku_ed_starts_t *v, int64_t *out,
                         ptrdiff_t pos, ptrdiff_t n);

/* The line index (tiku_editdoc_lines.c): partitions of the text, one per
 * line, the last one ending at the text's length. */

/* One empty partition; allocates, and gives 0 when there is no room. */
int tiku_ed_lines_init(tiku_ed_lines_t *l);
/* Free the storage. */
void tiku_ed_lines_free(tiku_ed_lines_t *l);
/* Back to one empty partition in the storage held; never allocates. */
void tiku_ed_lines_reset(tiku_ed_lines_t *l);
/* Room for @p extra more partitions; 0 when there is none. */
int tiku_ed_lines_reserve(tiku_ed_lines_t *l, ptrdiff_t extra);
/* The number of partitions. */
int64_t tiku_ed_lines_count(const tiku_ed_lines_t *l);
/* Where @p partition starts, the pending shift applied; the count gives
 * the text's length, and anything else outside gives 0. */
int64_t tiku_ed_lines_start(const tiku_ed_lines_t *l, int64_t partition);
/* The partition holding @p pos: the first for a position before 0, the
 * last for one at or past the length. */
int64_t tiku_ed_lines_find(const tiku_ed_lines_t *l, int64_t pos);
/* Insert partition @p partition, starting at @p pos, into room
 * tiku_ed_lines_reserve() made; the one there and those after it move up
 * one. */
void tiku_ed_lines_insert(tiku_ed_lines_t *l, int64_t partition, int64_t pos);
/* Move the starts of the partitions after @p partition by @p delta,
 * through the one pending shift. */
void tiku_ed_lines_shift(tiku_ed_lines_t *l, int64_t partition, int64_t delta);
/* Remove where @p partition starts, joining it to the partition before. */
void tiku_ed_lines_remove(tiku_ed_lines_t *l, int64_t partition);
/* Insert @p n partitions from @p partition on, starting at the positions
 * in @p pos, into room tiku_ed_lines_reserve() made. */
void tiku_ed_lines_insert_many(tiku_ed_lines_t *l, int64_t partition,
                               const int64_t *pos, ptrdiff_t n);
/* Move where @p partition starts. */
void tiku_ed_lines_set_start(tiku_ed_lines_t *l, int64_t partition,
                             int64_t pos);

/* Text with its lines kept current (tiku_editdoc_lines.c). */

/* Empty text of one line; 0 when there is no room. */
int tiku_ed_text_init(tiku_ed_text_t *t);
/* Free the bytes and the line index. */
void tiku_ed_text_free(tiku_ed_text_t *t);
/* Room for @p bytes more text and @p lines more line starts; 0 when
 * there is none.  Insertion and deletion never allocate, and an insertion
 * needs this room. */
int tiku_ed_text_reserve(tiku_ed_text_t *t, int64_t bytes, int64_t lines);
/* Remove @p n bytes at @p pos, keeping the lines current. */
void tiku_ed_text_delete(tiku_ed_text_t *t, int64_t pos, int64_t n);
/* Where @p line's text ends, before its line break; -1 outside. */
int64_t tiku_ed_text_line_end(const tiku_ed_text_t *t, int64_t line);
/* The line starts to reserve for inserting the @p n bytes at @p s. */
int64_t tiku_ed_text_line_cost(const char *s, int64_t n);
/* Insert @p n bytes at @p pos, keeping the lines current. */
void tiku_ed_text_insert(tiku_ed_text_t *t, int64_t pos, const char *s,
                         int64_t n);

/* History (tiku_editdoc_undo.c). */

/* An empty history kept to @p target bytes, its state counted as saved. */
void tiku_ed_history_init(tiku_ed_history_t *h, size_t target);
/* Free every record and the table. */
void tiku_ed_history_free(tiku_ed_history_t *h);
/* Free @p r's pieces, payload and selections, and zero it. */
void tiku_ed_record_free(tiku_ed_record_t *r);
/* The state of the text now: the last applied record's, or the state
 * before the first record. */
uint64_t tiku_ed_history_state(const tiku_ed_history_t *h);
/* Room for one more record after those applied; allocates, and gives 0
 * when there is no room. */
int tiku_ed_history_reserve(tiku_ed_history_t *h);
/* Put @p r, which the history then owns, after the records applied,
 * freeing the records that waited to be redone, and charge its cost;
 * tiku_ed_history_reserve() made the room. */
void tiku_ed_history_push(tiku_ed_history_t *h, tiku_ed_record_t *r);
/* Free the oldest applied records while the history costs more than its
 * target, keeping at least one record. */
void tiku_ed_history_prune(tiku_ed_history_t *h);
/* Free every record and start a new state, counted as saved. */
void tiku_ed_history_clear(tiku_ed_history_t *h);
/* Whether @p r, a typing record, joins the last record: nothing waits to
 * be redone, the last record is open to typing and is not the saved
 * state, and @p r comes from the same view, is the same kind of typing
 * and has as many pieces, each going on where the last record's piece in
 * its place stopped. */
int tiku_ed_history_joins(const tiku_ed_history_t *h,
                          const tiku_ed_record_t *r);
/* The last record and @p r merged into @p m: pieces, payload, and room for
 * the last record's selections before, copied, and @p after selections
 * after; allocates and leaves the last record as it was. */
int tiku_ed_history_merge(const tiku_ed_history_t *h, const tiku_ed_record_t *r,
                          size_t after, tiku_ed_record_t *m);
/* Give the last record @p m's pieces, payload and selections, and a new
 * state after; the old ones come back in @p m for the caller to free.  No
 * allocation. */
void tiku_ed_history_replace_last(tiku_ed_history_t *h, tiku_ed_record_t *m);

/* Anchors, views and listeners (tiku_editdoc_anchors.c). */

/* Move every anchor, selection, line marker, indicator and annotation
 * through one replacement of [s, e) by n bytes. */
void tiku_ed_marks_map(tiku_editdoc_t *doc, int64_t s, int64_t e, int64_t n);
/* After a change: move selection ends out of the middle of a CRLF, merge
 * the selections that have come together, and settle the line markers,
 * indicators and annotations. */
void tiku_ed_marks_snap(tiku_editdoc_t *doc);
/* Free the anchors, views and listeners. */
void tiku_ed_marks_free(tiku_editdoc_t *doc);
/* Tell every listener of @p change but those added while it is told;
 * the document refuses changes until they return. */
void tiku_ed_dispatch(tiku_editdoc_t *doc, const tiku_edit_change_t *change);
/* The live view a handle names, or NULL. */
tiku_ed_view_t *tiku_ed_view_get(const tiku_editdoc_t *doc,
                                 tiku_edit_view_t view);
/* Room in @p v for @p n other selections; 0 when there is none.  Never
 * changes what the view has selected. */
int tiku_ed_view_room(tiku_ed_view_t *v, size_t n);
/* Put @p n selections in order of their starts. */
void tiku_ed_selections_sort(tiku_edit_selection_t *more, size_t n);
/* Merge the @p *n selections in @p more, in order of their starts, with
 * each other and with @p main: two that overlap, or a caret touching
 * another selection, become one, kept in @p main if either was it.  @p *n
 * becomes how many are left in @p more.  No allocation. */
void tiku_ed_selections_merge(tiku_edit_selection_t *main,
                              tiku_edit_selection_t *more, size_t *n);

/* Line markers (tiku_edit_decorations.c). */

/* Move each line marker through a replacement of [s, e) by n bytes: one
 * at s moves past the new text, one after s up to e moves to s. */
void tiku_ed_markers_map(tiku_editdoc_t *doc, int64_t s, int64_t e, int64_t n);
/* Put each line marker back at the start of the line it is on. */
void tiku_ed_markers_settle(tiku_editdoc_t *doc);
/* Remove every line marker, keeping the table. */
void tiku_ed_markers_drop(tiku_editdoc_t *doc);
/* Free the table of line markers. */
void tiku_ed_markers_free(tiku_editdoc_t *doc);

/* Indicators and annotations (tiku_edit_notes.c). */

/* Move each indicator and annotation through one replacement of [s, e)
 * by n bytes.  Never allocates, and leaves an emptied indicator for
 * tiku_ed_notes_settle() to remove. */
void tiku_ed_notes_map(tiku_editdoc_t *doc, int64_t s, int64_t e, int64_t n);
/* After a change: move indicator ends inward out of the middle of a
 * CRLF, remove the emptied indicators, and put each annotation back at
 * the start of the line it is on. */
void tiku_ed_notes_settle(tiku_editdoc_t *doc);
/* Remove every indicator and annotation, keeping the tables;
 * tiku_editdoc_load() calls it. */
void tiku_ed_notes_drop(tiku_editdoc_t *doc);
/* Free the tables and the annotations' text. */
void tiku_ed_notes_free(tiku_editdoc_t *doc);

#endif /* TIKU_EDITDOC_PRIVATE_H_ */
