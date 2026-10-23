/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_edit_decorations.h - marks a document keeps on its text: line
 * markers and annotations, which follow their lines through edits,
 * indicators, which follow a range of text, and the execution line, which
 * never moves and is current only for the text it was set for.
 *
 * What a kind of mark means, and which line a runtime stopped at, are the
 * application's to decide; the document keeps them on its text.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_EDIT_DECORATIONS_H_
#define TIKU_EDIT_DECORATIONS_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_editdoc.h"

/** @brief Kinds of line marker; kind k is bit k of a set of kinds. */
#define TIKU_EDIT_MARKER_KINDS 32

/** @brief A marker on one line, valid until it is removed. */
typedef struct {
    uint32_t slot;              /* index + 1; 0 is none                 */
    uint32_t generation;        /* the slot's life it belongs to        */
} tiku_edit_marker_t;

/**
 * @brief Mark @p line with @p kind, from 0 to TIKU_EDIT_MARKER_KINDS - 1.
 *
 * A marker follows its line's text.  Text inserted at the start of the
 * line moves it down past the line breaks the text brings, and an edit
 * that removes the line break before its line moves it to the line the
 * edit starts on.  Undo does not move it back, and a load removes every
 * marker.  @p marker may be NULL.
 */
tiku_edit_status_t tiku_editdoc_marker_add(tiku_editdoc_t *doc, int64_t line,
                                           unsigned kind,
                                           tiku_edit_marker_t *marker);
/** @brief The line @p marker is on now, or -1 once it is removed. */
int64_t tiku_editdoc_marker_line(const tiku_editdoc_t *doc,
                                 tiku_edit_marker_t marker);
/** @brief Remove @p marker; its handle stops working. */
void tiku_editdoc_marker_remove(tiku_editdoc_t *doc,
                                tiku_edit_marker_t marker);
/** @brief The kinds marked on @p line as a set; 0 for a line that is not
 *         there. */
uint32_t tiku_editdoc_markers_on(const tiku_editdoc_t *doc, int64_t line);
/** @brief The first line at or after @p from marked with a kind in
 *         @p kinds, or -1. */
int64_t tiku_editdoc_markers_next(const tiku_editdoc_t *doc, int64_t from,
                                  uint32_t kinds);
/** @brief Remove the markers of @p kinds on @p line, or on every line when
 *         @p line is -1.  @return how many were removed. */
size_t tiku_editdoc_markers_clear(tiku_editdoc_t *doc, int64_t line,
                                  uint32_t kinds);

/**
 * @brief Mark @p line as the line a runtime stopped at, for the text as
 *        it is now.
 *
 * The mark is never moved.  Any edit or load makes it stale, and an undo
 * or redo back to the text it was set for makes it current again.
 */
tiku_edit_status_t tiku_editdoc_execution_set(tiku_editdoc_t *doc,
                                              int64_t line);
/** @brief Remove the execution mark. */
void tiku_editdoc_execution_clear(tiku_editdoc_t *doc);
/** @brief The execution line, in @p line: TIKU_EDIT_OK while it is
 *         current, TIKU_EDIT_STALE while it is stale, and
 *         TIKU_EDIT_NOTHING, @p line -1, when none is set. */
tiku_edit_status_t tiku_editdoc_execution(const tiku_editdoc_t *doc,
                                          int64_t *line);

/** @brief Kinds of indicator and annotation; kind k is bit k of a set. */
#define TIKU_EDIT_INDICATOR_KINDS 8

/** @brief An indicator on a range of text, valid until it is removed. */
typedef struct {
    uint32_t slot;              /* index + 1; 0 is none                 */
    uint32_t generation;        /* the slot's life it belongs to        */
} tiku_edit_indicator_t;

/**
 * @brief Mark the text in @p range, which must hold some, with @p kind,
 *        from 0 to TIKU_EDIT_INDICATOR_KINDS - 1.
 *
 * The range follows its text: text put in inside it widens it, text put
 * in at either end stays outside, and an edit that takes all of its text
 * removes it.  Undo does not bring it back, and a load removes every
 * indicator.  @p indicator may be NULL.
 */
tiku_edit_status_t tiku_editdoc_indicator_add(tiku_editdoc_t *doc,
                                              tiku_edit_range_t range,
                                              unsigned kind,
                                              tiku_edit_indicator_t *indicator);
/** @brief Where @p indicator is now; TIKU_EDIT_NOTHING once it is gone. */
tiku_edit_status_t tiku_editdoc_indicator_range(const tiku_editdoc_t *doc,
                                                tiku_edit_indicator_t indicator,
                                                tiku_edit_range_t *range);
/** @brief Remove @p indicator; its handle stops working. */
void tiku_editdoc_indicator_remove(tiku_editdoc_t *doc,
                                   tiku_edit_indicator_t indicator);
/** @brief Remove every indicator of @p kinds.  @return how many. */
size_t tiku_editdoc_indicators_clear(tiku_editdoc_t *doc, uint32_t kinds);
/**
 * @brief The indicator of @p kinds starting first after @p pos, or with
 *        @p backward starting last before it, in @p range and @p kind,
 *        either of which may be NULL.
 *
 * Indicators starting at the same place are taken widest first.
 * @return TIKU_EDIT_NOTHING when there is none that way.
 */
tiku_edit_status_t tiku_editdoc_indicator_next(const tiku_editdoc_t *doc,
                                               tiku_edit_pos_t pos,
                                               uint32_t kinds, int backward,
                                               tiku_edit_range_t *range,
                                               unsigned *kind);
/** @brief Called for an indicator; nonzero stops the visit. */
typedef int (*tiku_edit_indicator_fn)(void *ctx, tiku_edit_range_t range,
                                      unsigned kind);
/** @brief Call @p fn for each indicator of @p kinds that overlaps
 *         @p span, in no set order. */
void tiku_editdoc_indicators_each(const tiku_editdoc_t *doc,
                                  tiku_edit_range_t span, uint32_t kinds,
                                  tiku_edit_indicator_fn fn, void *ctx);

/**
 * @brief Show @p text under @p line as an annotation of @p kind, after
 *        any the line has; NULL or "" adds none.
 *
 * Each LF in @p text starts another row.  An annotation follows its
 * line's text: text inserted at the start of the line carries it along,
 * and an edit that removes the line break before its line moves it to
 * the line the edit starts on.  A load removes it; undo does not.
 */
tiku_edit_status_t tiku_editdoc_annotation_add(tiku_editdoc_t *doc,
                                               int64_t line,
                                               const char *text,
                                               unsigned kind);
/** @brief The @p index'th annotation under @p line, from 0, and its kind;
 *         NULL past the last. */
const char *tiku_editdoc_annotation(const tiku_editdoc_t *doc, int64_t line,
                                    size_t index, unsigned *kind);
/** @brief Remove the annotations of @p kinds under @p line, or under
 *         every line when @p line is -1.  @return how many. */
size_t tiku_editdoc_annotations_clear(tiku_editdoc_t *doc, int64_t line,
                                      uint32_t kinds);
/** @brief How many annotations the document has, and a number that
 *         changes whenever one is added or removed. */
size_t tiku_editdoc_annotations(const tiku_editdoc_t *doc, uint64_t *serial);

#endif /* TIKU_EDIT_DECORATIONS_H_ */
