/*
 * TikuDesktop Document Kit.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcells.h - rectangular cell queries, text export and atomic edits.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHCELLS_H_
#define TIKU_RICHCELLS_H_
#include "tiku_richdoc.h"

/** @brief Resolve two direct cell IDs to their inclusive table rectangle.
 *  @note Both cells must have the same immediate table. Failure clears out. */
tiku_rich_status_t tiku_richsnapshot_cells(const tiku_richsnapshot_t *snapshot,
    uint64_t first, uint64_t last, tiku_rich_cells_t *out);
/** @brief Read one rectangle member in row-major order; failure clears out. */
tiku_rich_status_t tiku_richsnapshot_cell_at(
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *cells,
    size_t index, tiku_rich_block_t *out);
/** @brief Export quoted tab-separated cells, doubling embedded quotes.
 *  @note Paragraphs use LF inside quoted fields; rows use LF between fields.
 *        Pictures and nested tables return UNSUPPORTED. NULL/0 queries size;
 *        length excludes NUL. Failure leaves the buffer untouched. */
tiku_rich_status_t tiku_richsnapshot_cells_text(
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *cells,
    char *buffer, size_t capacity, size_t *length);
/** @brief Export quoted TSV with each picture replaced by [Picture].
 *  @note This is a lossy text representation for clipboard interchange.
 *        Nested paragraphs become LF-separated text inside the outer field.
 *        Table structure is lost; sizing/refusal match cells_text. */
tiku_rich_status_t tiku_richsnapshot_cells_fallback(
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *cells,
    char *buffer, size_t capacity, size_t *length);
/** @brief Clear text and inline pictures, retaining cell/paragraph structure.
 *  @note Includes nested cell paragraphs. At most 1024 nonempty paragraphs
 *        can be cleared in one atomic undo group. Refusal changes nothing.
 *        Zero generation accepts current state. */
tiku_rich_status_t tiku_richdoc_cells_clear(tiku_richdoc_t *doc,
    uint64_t generation, const tiku_rich_cells_t *cells);
/** @brief Paste rectangular TSV as one undoable replacement of cell contents.
 *  @note Quoted fields support doubled quotes and LF/CRLF/CR paragraphs.
 *        A one-cell selection expands within its table; other shapes must
 *        match exactly. Nested target tables are refused. Shading and first
 *        paragraph defaults are retained, but replaced runs are plain text.
 *        At most 1024 edit primitives. Zero generation accepts current state.
 *        Optional out receives the affected rectangle, cleared on refusal. */
tiku_rich_status_t tiku_richdoc_cells_paste_text(tiku_richdoc_t *doc,
    uint64_t generation, const tiku_rich_cells_t *cells,
    const char *text, size_t length, tiku_rich_cells_t *out);
/** @brief Replace cells with snapshot content as one undoable rich edit.
 *  @note Copies text, paragraph/run styles, lists, links, pictures and shading.
 *        Keeps destination table geometry and cell identities. A one-cell
 *        target expands to source size; other selections must match exactly.
 *        Copies nested trees up to eight table levels. Source may be this doc.
 *        Failure changes nothing and clears optional out; zero generation
 *        accepts current state. Source snapshot remains caller-owned. */
tiku_rich_status_t tiku_richdoc_cells_paste(tiku_richdoc_t *doc,
    uint64_t generation, const tiku_rich_cells_t *target,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source,
    tiku_rich_cells_t *out);
/** @brief Insert copied cells as a new table at a body or cell text caret.
 *  @note Splits the paragraph at at, preserving its prefix/suffix formatting.
 *        Retains source border/padding, selected column proportions, width
 *        share and selected header rows. Combined depth cannot exceed eight
 *        tables. One undo group; failure changes nothing and clears out.
 *        Zero generation accepts current state. Optional out gets the grid. */
tiku_rich_status_t tiku_richdoc_cells_insert(tiku_richdoc_t *doc,
    uint64_t generation, tiku_rich_pos_t at,
    const tiku_richsnapshot_t *snapshot, const tiku_rich_cells_t *source,
    tiku_rich_cells_t *out);
#endif
