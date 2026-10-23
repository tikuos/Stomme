/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc.h - styled paragraphs, checked import and owned snapshots.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHDOC_H_
#define TIKU_RICHDOC_H_

#include <stddef.h>
#include <stdint.h>

/** @brief A mutable document, owned by one calling thread. */
typedef struct tiku_richdoc tiku_richdoc_t;
/** @brief An immutable document state that can be read on a worker. */
typedef struct tiku_richsnapshot tiku_richsnapshot_t;
/** @brief An owner-thread position that follows document edits and history. */
typedef struct tiku_richanchor tiku_richanchor_t;
/** @brief An incremental literal search retaining one immutable state. */
typedef struct tiku_richsearch tiku_richsearch_t;
/** @brief An immutable styled selection for in-process transfer. */
typedef struct tiku_richfragment tiku_richfragment_t;

/** @brief An operation's result; refused imports leave the state unchanged. */
typedef enum {
    TIKU_RICH_OK = 0,
    TIKU_RICH_INVALID,
    TIKU_RICH_INVALID_TEXT,
    TIKU_RICH_INVALID_RANGE,
    TIKU_RICH_INVALID_STYLE,
    TIKU_RICH_READ_ONLY,
    TIKU_RICH_STALE,
    TIKU_RICH_LIMIT,
    TIKU_RICH_NO_MEMORY,
    TIKU_RICH_UNSUPPORTED,
    TIKU_RICH_NO_CHANGE,
    TIKU_RICH_PENDING,          /**< search has more work within its scope */
    TIKU_RICH_NOT_FOUND,        /**< search has exhausted its scope */
    TIKU_RICH_INVALID_LINK,    /**< malformed link target */
    TIKU_RICH_BUSY,            /**< mutation during change notification */
    TIKU_RICH_CANCELLED        /**< cooperative operation cancellation */
} tiku_rich_status_t;

/** @brief Allocation hooks; both functions NULL select malloc and free.
 *  @note The context outlives documents, snapshots, searches and fragments.
 *        Hooks return
 *        aligned storage, support cross-thread release and do not reenter. */
typedef struct {
    void *ctx;
    void *(*allocate)(void *ctx, size_t bytes);
    void (*release)(void *ctx, void *memory);
} tiku_rich_allocator_t;

/** @brief Import and aggregate allocation ceilings; zero selects defaults.
 *  @note The aggregate includes snapshots, searches, fragments, working
 *        imports, command expansion and metadata. */
typedef struct {
    size_t text_bytes;          /**< UTF-8 bytes across all paragraphs */
    size_t paragraphs;          /**< count including paragraphs in cells */
    size_t runs;                /**< formatting runs across the document */
    size_t styles;              /**< named paragraph and character styles */
    size_t memory_bytes;        /**< live allocations including overhead */
    size_t history_bytes;       /**< peak extra storage across undo/redo states */
    size_t anchors;             /**< live tracked positions */
    tiku_rich_allocator_t allocator;
} tiku_rich_limits_t;

/** @brief Character properties present in a direct or named style. */
enum {
    TIKU_RICH_BOLD       = 1u << 0,
    TIKU_RICH_ITALIC     = 1u << 1,
    TIKU_RICH_UNDERLINE  = 1u << 2,
    TIKU_RICH_STRIKE     = 1u << 3,
    TIKU_RICH_SIZE       = 1u << 4,
    TIKU_RICH_FONT       = 1u << 5,
    TIKU_RICH_FOREGROUND = 1u << 6,
    TIKU_RICH_HIGHLIGHT  = 1u << 7,
    TIKU_RICH_BASELINE   = 1u << 8
};

/** @brief Values whose presence bits are set override inherited values.
 *  @note Colours are 0xAARRGGBB. Baseline is -1, 0 or 1.
 *        A present boolean with value zero explicitly disables it. */
typedef struct {
    unsigned present;
    unsigned char bold, italic, underline, strike;
    float size;                /**< points, from 1 through 4096 */
    char font[64];              /**< NUL-terminated UTF-8 family */
    uint32_t foreground, highlight;
    int baseline;              /**< subscript, normal or superscript */
} tiku_rich_chars_t;

/** @brief How a paragraph's lines align within their available width. */
typedef enum {
    TIKU_RICH_LEFT, TIKU_RICH_CENTER, TIKU_RICH_RIGHT, TIKU_RICH_JUSTIFY
} tiku_rich_align_t;

/** @brief Paragraph properties with explicit presence. */
enum {
    TIKU_RICH_ALIGNMENT = 1u << 0,
    TIKU_RICH_INDENT    = 1u << 1,
    TIKU_RICH_SPACING   = 1u << 2,
    TIKU_RICH_KEEP      = 1u << 3,
    TIKU_RICH_PAGE_BREAK = 1u << 4,
    TIKU_RICH_OUTLINE = 1u << 5
};

/** @brief Paragraph overrides; lengths are finite document points. */
typedef struct {
    unsigned present;
    tiku_rich_align_t align;
    float left, right, first;   /**< left, right and first-line indents */
    float before, after;       /**< spacing outside the paragraph */
    unsigned char keep_next, keep_lines;
    unsigned char break_before; /**< begin on a new page; no initial blank */
    unsigned char outline;    /**< 0 is body text; 1..6 are heading levels */
} tiku_rich_para_t;

/** @brief A named style's category; inheritance stays within the category. */
typedef enum {
    TIKU_RICH_PARAGRAPH_STYLE, TIKU_RICH_CHARACTER_STYLE
} tiku_rich_style_kind_t;

/** @brief An import style. Parent is a one-based source style index, or 0. */
typedef struct {
    char name[64];
    tiku_rich_style_kind_t kind;
    size_t parent;
    tiku_rich_chars_t chars;
    tiku_rich_para_t paragraph;
} tiku_rich_style_source_t;

/** @brief A complete editing style definition; parent is an ID, not an index.
 *  @note Defining an existing style replaces its properties, retaining its
 *        ID and category. Absent properties inherit from its parent. */
typedef struct {
    char name[64];
    tiku_rich_style_kind_t kind;
    uint64_t parent;
    tiku_rich_chars_t chars;
    tiku_rich_para_t paragraph;
} tiku_rich_style_spec_t;

/** @brief Paragraph list marker categories. */
typedef enum {
    TIKU_RICH_LIST_NONE, TIKU_RICH_LIST_BULLET, TIKU_RICH_LIST_DECIMAL
} tiku_rich_list_kind_t;

/** @brief Direct paragraph list membership; NONE clears the other fields.
 *  @note Levels are 0 through 8. Restart is zero for continuation or a
 *        positive decimal starting value. Bullets require restart zero.
 *        Non-list paragraphs end the numbering sequence. */
typedef struct {
    tiku_rich_list_kind_t kind;
    unsigned level;
    uint32_t restart;
} tiku_rich_list_t;

/** @brief Maximum decoded pixels in one embedded image. */
#define TIKU_RICH_IMAGE_PIXELS_MAX (4u * 1024u * 1024u)
/** @brief An inline picture represented in paragraph text by one U+FFFC.
 *  @note Pixels are row-major, straight-alpha 0xAARRGGBB. Import and insert
 *        copy them. Snapshot pointers live with that snapshot. Display
 *        dimensions are finite 1..14400 points; alt is UTF-8, <=1023 bytes.
 *        NULL alt means empty. There is no external resource or file path. */
typedef struct {
    unsigned pixel_width, pixel_height;
    const uint32_t *pixels;
    float width, height;
    const char *alt;
} tiku_rich_image_t;

/** @brief Check image dimensions, pointer presence and alternate text. */
tiku_rich_status_t tiku_rich_image_validate(const tiku_rich_image_t *image);

/** @brief An import run; style is a character-style source index or 0. */
typedef struct {
    size_t length;             /**< positive UTF-8 byte count */
    size_t style;              /**< one-based source style index */
    tiku_rich_chars_t chars;
    unsigned reset;            /**< inherited properties reset to absent */
    const char *link;          /**< copied target; NULL/empty means none */
    const tiku_rich_image_t *image; /**< copied image; run is exactly U+FFFC */
} tiku_rich_run_source_t;

/** @brief An imported paragraph; inputs are borrowed during import.
 *  @note Runs must cover text on UTF-8 boundaries. With no supplied runs,
 *        nonempty text receives one unstyled run. LF is a soft line break. */
typedef struct {
    const char *text;
    size_t length;
    const tiku_rich_run_source_t *runs;
    size_t run_count;
    size_t style;              /**< paragraph-style source index or 0 */
    tiku_rich_chars_t chars;    /**< default character overrides */
    tiku_rich_para_t paragraph;
    tiku_rich_list_t list;     /**< direct list membership */
} tiku_rich_paragraph_source_t;

/** @brief Single-section paper dimensions and margins, in document points.
 *  @note An all-zero value selects A4 with 54-point margins. Otherwise
 *        dimensions are 72..14400 and margins leave at least 36 points of
 *        content in each direction. Every value must be finite. */
typedef struct {
    float width, height;
    float top, right, bottom, left;
} tiku_rich_page_setup_t;

/** @brief Ordered structural nodes; cells contain paragraphs or tables. */
typedef enum {
    TIKU_RICH_BLOCK_PARAGRAPH, TIKU_RICH_BLOCK_TABLE, TIKU_RICH_BLOCK_CELL
} tiku_rich_block_kind_t;

/** @brief Insert one empty grid axis or delete it with its cell contents. */
typedef enum {
    TIKU_RICH_ROW_INSERT, TIKU_RICH_ROW_REMOVE,
    TIKU_RICH_COLUMN_INSERT, TIKU_RICH_COLUMN_REMOVE
} tiku_rich_grid_op_t;

/** @brief A rectangular table with optional relative column widths.
 *  @note Rows are 1..256, columns 1..64, and cells at most 4096. Width zero
 *        fills the available interval; otherwise width is 1..14400 points.
 *        Padding is 0..72 points and border is 0..12 points. Layout expands
 *        width when padding, nested tables or pictures need more space.
 *        Weights are all zero for equal columns, or each active column has
 *        a finite weight of 0.001..1000. Unused weights must be zero. */
typedef struct {
    unsigned rows, columns;
    float width, padding, border;
    uint32_t color;            /**< straight-alpha ARGB border color */
    float weights[64];         /**< relative outer widths, including inset */
    unsigned header_rows;      /**< leading rows repeated on continuation */
} tiku_rich_table_t;

/** @brief One import node in depth-first order.
 *  @note Parent is a preceding one-based node index, or zero for main body.
 *        Paragraph indices are zero-based, consecutive in traversal order.
 *        TABLE has exactly rows*columns CELL children in row-major order;
 *        each cell has nonempty block content. Other table fields are zero.
 *        Nesting is limited to eight tables and the array to 65536 nodes. */
typedef struct {
    tiku_rich_block_kind_t kind;
    size_t parent, paragraph;
    tiku_rich_table_t table;
    uint32_t background;       /**< cell-only straight-alpha ARGB fill */
} tiku_rich_block_source_t;

/** @brief One snapshot structure node with document-local stable identities.
 *  @note A paragraph node's ID is its paragraph ID. Parent is a cell/table
 *        ID, or the main body ID for top-level blocks. */
typedef struct {
    tiku_rich_block_kind_t kind;
    uint64_t id, parent;
    tiku_rich_table_t table;
    uint32_t background;       /**< cell-only straight-alpha ARGB fill */
} tiku_rich_block_t;

/** @brief Validate finite table dimensions and bounded rectangular size. */
tiku_rich_status_t tiku_rich_table_validate(const tiku_rich_table_t *table);

/** @brief Column's share of table width; zero for invalid geometry/index. */
double tiku_rich_table_fraction(const tiku_rich_table_t *table,
                                unsigned column);

/** @brief Copy the immediate table and column owning a leaf paragraph.
 *  @note Outside a table returns INVALID_RANGE. Failure clears outputs. */
tiku_rich_status_t tiku_richsnapshot_table(const tiku_richsnapshot_t *snapshot,
    uint64_t paragraph, tiku_rich_block_t *table, unsigned *column);

/** @brief Copy a paragraph's immediate cell; failure clears *out.
 *  @return INVALID_RANGE outside a cell or for a non-paragraph identity. */
tiku_rich_status_t tiku_richsnapshot_cell(const tiku_richsnapshot_t *snapshot,
    uint64_t paragraph, tiku_rich_block_t *out);

/** @brief Maximum named point locations in a document. */
#define TIKU_RICH_BOOKMARKS_MAX 1024
/** @brief Imported point location; paragraph is a zero-based source index. */
typedef struct {
    char name[64];             /**< unique case-sensitive UTF-8 name */
    size_t paragraph, byte;    /**< scalar boundary in the source paragraph */
    unsigned after;            /**< 0 stays before insertion, 1 follows it */
} tiku_rich_bookmark_source_t;

/** @brief A complete main text body; no paragraphs creates one empty one. */
typedef struct {
    const tiku_rich_style_source_t *styles;
    size_t style_count;
    const tiku_rich_paragraph_source_t *paragraphs;
    size_t paragraph_count;
    tiku_rich_page_setup_t page; /**< zero selects default paper and margins */
    const tiku_rich_block_source_t *blocks; /**< NULL for paragraph-only body */
    size_t block_count;        /**< depth-first structural node count */
    const tiku_rich_bookmark_source_t *bookmarks;
    size_t bookmark_count;
} tiku_rich_source_t;

/** @brief A character run; its ID refers to a snapshot's named style. */
typedef struct {
    size_t length;
    uint64_t style;            /**< character style ID, or 0 */
    tiku_rich_chars_t chars;
    unsigned reset;            /**< inherited properties reset before chars */
    const char *link;          /**< immutable target, or NULL */
    const tiku_rich_image_t *image; /**< immutable inline image, or NULL */
} tiku_rich_run_t;

/** @brief A paragraph borrowed from an immutable snapshot. */
typedef struct {
    uint64_t id, style;
    const char *text;
    size_t length;
    const tiku_rich_run_t *runs;
    size_t run_count;
    tiku_rich_chars_t chars;
    tiku_rich_para_t paragraph;
    tiku_rich_list_t list;
} tiku_rich_paragraph_t;

/** @brief A named style borrowed from a snapshot, including inherited IDs. */
typedef struct {
    uint64_t id, parent;
    const char *name;
    tiku_rich_style_kind_t kind;
    tiku_rich_chars_t chars;
    tiku_rich_para_t paragraph;
} tiku_rich_style_t;

/** @brief Which side of an insertion a tracked position prefers. */
typedef enum {
    TIKU_RICH_BEFORE, TIKU_RICH_AFTER
} tiku_rich_affinity_t;

/** @brief An ID-based position; bytes are local to the paragraph. */
typedef struct {
    uint64_t body, paragraph;
    size_t byte;
    tiku_rich_affinity_t affinity;
} tiku_rich_pos_t;

/** @brief Rectangle of direct cells in one table, with zero-based origin. */
typedef struct {
    uint64_t table;
    unsigned row, column, rows, columns;
} tiku_rich_cells_t;

/** @brief Typed editing operations sharing one transaction/history path. */
typedef enum {
    TIKU_RICH_REPLACE, TIKU_RICH_FORMAT, TIKU_RICH_SPLIT, TIKU_RICH_JOIN,
    TIKU_RICH_PARAGRAPH_FORMAT, /**< overlay paragraph/default char values */
    TIKU_RICH_PARAGRAPH_STYLE_SET, /**< assign style; zero removes it */
    TIKU_RICH_CHARACTER_STYLE_SET, /**< assign style in [start,end) */
    TIKU_RICH_STYLE_DEFINE,     /**< style zero creates, nonzero updates */
    TIKU_RICH_STYLE_REMOVE,    /**< INVALID_STYLE if the style is still used */
    TIKU_RICH_LIST_SET,        /**< replace paragraph list membership */
    TIKU_RICH_LINK_SET,        /**< assign/remove a link in [start,end) */
    TIKU_RICH_PAGE_SETUP,      /**< replace paper and margins; no text range */
    TIKU_RICH_IMAGE_INSERT,    /**< replace [start,end) with one picture */
    TIKU_RICH_IMAGE_SET,       /**< replace one picture's pixels/size/alt */
    TIKU_RICH_TABLE_INSERT,    /**< insert after paragraph; single-edit group */
    TIKU_RICH_TABLE_REMOVE,    /**< remove table subtree; single-edit group */
    TIKU_RICH_TABLE_GRID,      /**< change one row/column; single-edit group */
    TIKU_RICH_TABLE_SET,       /**< replace geometry, retaining dimensions */
    TIKU_RICH_CELL_SET,        /**< replace one cell's background */
    TIKU_RICH_CELLS_SET        /**< replace rectangular cell backgrounds */
} tiku_rich_edit_kind_t;

/** @brief Replace bytes or overlay direct character formatting in [start,end).
 *  @note Offsets address the result of preceding edits in the transaction.
 *        Replace text is UTF-8; LF is a soft break, not a new paragraph.
 *        Inserted text inherits preceding character formatting, or paragraph
 *        defaults at zero, then applies chars. Links inherit inside a link
 *        or when replacing uniformly linked text, not at insertion edges.
 *        Format ignores text and length.
 *        Split requires start == end and copies paragraph properties to
 *        both halves, with a continuation list item on the right.
 *        Join requires zero offsets and joins the next
 *        paragraph, retaining the leading ID and paragraph properties.
 *        Paragraph commands require zero offsets. FORMAT clears selected
 *        direct/reset properties before overlay; PARAGRAPH_FORMAT clears
 *        defaults before overlay. Style assignment preserves overrides.
 *        DEFINE/REMOVE ignore paragraph and offsets; definition is borrowed
 *        during apply. Created IDs are available in the next snapshot. */
typedef struct {
    tiku_rich_edit_kind_t kind;
    uint64_t paragraph;
    size_t start, end;
    const char *text;
    size_t length;
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;    /**< PARAGRAPH_FORMAT overrides */
    unsigned clear_chars;     /**< FORMAT/PARAGRAPH_FORMAT inheritance */
    unsigned clear_props;     /**< PARAGRAPH_FORMAT inheritance */
    uint64_t style;            /**< assigned, updated or removed style ID */
    const tiku_rich_style_spec_t *definition; /**< STYLE_DEFINE payload */
    tiku_rich_list_t list;     /**< LIST_SET with zero start/end */
    const char *link;          /**< LINK_SET target; NULL/empty removes */
    tiku_rich_page_setup_t page; /**< PAGE_SETUP replacement */
    const tiku_rich_image_t *image; /**< borrowed IMAGE_INSERT/SET payload */
    tiku_rich_table_t table;    /**< TABLE_INSERT/SET geometry */
    uint64_t object;           /**< TABLE_REMOVE/GRID/SET stable table ID */
    uint32_t background;       /**< CELL_SET fill; object is the cell ID */
    struct {
        tiku_rich_grid_op_t operation;
        size_t index;          /**< zero-based axis; insert permits append */
    } grid;                   /**< TABLE_GRID; last-axis deletion refused */
    tiku_rich_cells_t cells;  /**< CELLS_SET rectangle; uses background */
} tiku_rich_edit_t;

/** @brief At most 1024 sequential edits published as one undoable change.
 *  @note Zero generation accepts current state; any error preserves content,
 *        generation and history. An empty transaction returns NO_CHANGE. */
typedef struct {
    uint64_t generation;
    const tiku_rich_edit_t *edits;
    size_t count;
} tiku_rich_transaction_t;

/** @brief Create one empty paragraph. Failure sets *out to NULL. */
tiku_rich_status_t tiku_richdoc_create(const tiku_rich_limits_t *limits,
                                       tiku_richdoc_t **out);
/** @brief Retain a live document on its owning thread. */
void tiku_richdoc_retain(tiku_richdoc_t *doc);
/** @brief Release a document reference; NULL is allowed.
 *  @note During change notification this does nothing; defer the release. */
void tiku_richdoc_release(tiku_richdoc_t *doc);
/** @brief Replace the document from checked source data as a file load.
 *  @note This is not an undoable editing command. Zero generation accepts
 *        the current state; another value must match. Input is copied.
 *        Success clears history and marks the loaded state as clean. */
tiku_rich_status_t tiku_richdoc_import(tiku_richdoc_t *doc,
                                      const tiku_rich_source_t *source,
                                      uint64_t generation);
/** @brief Apply a transaction on the document's owning thread. */
tiku_rich_status_t tiku_richdoc_apply(tiku_richdoc_t *doc,
                                     const tiku_rich_transaction_t *change);
/** @brief Replace an ordered main-body range with plain text atomically.
 *  @note CRLF, CR and LF create paragraphs. The leading paragraph survives;
 *        new paragraphs copy its defaults except a leading page break.
 *        Expansion is limited to 1024
 *        primitive operations. Zero generation accepts current content. */
tiku_rich_status_t tiku_richdoc_replace_range(tiku_richdoc_t *doc,
    uint64_t generation, tiku_rich_pos_t first, tiku_rich_pos_t last,
    const char *text, size_t length);
/** @brief Apply character overrides across an ordered range in one group.
 *  @note Empty selections do not change pending typing attributes. */
tiku_rich_status_t tiku_richdoc_format_range(tiku_richdoc_t *doc,
    uint64_t generation, tiku_rich_pos_t first, tiku_rich_pos_t last,
    const tiku_rich_chars_t *chars);
/** @brief Track a valid live position; failure clears *out.
 *  @note The anchor retains the document. Calls belong to its owner thread.
 *        Import moves anchors to the start of the newly loaded document. */
tiku_rich_status_t tiku_richanchor_create(tiku_richdoc_t *doc,
    tiku_rich_pos_t position, tiku_richanchor_t **out);
/** @brief Release a tracked position and its document reference.
 *  @note During change notification this does nothing; defer the release. */
void tiku_richanchor_release(tiku_richanchor_t *anchor);
/** @brief Copy the current position, or clear *out on invalid input. */
tiku_rich_status_t tiku_richanchor_get(const tiku_richanchor_t *anchor,
                                      tiku_rich_pos_t *out);
/** @brief Move a tracked position explicitly without editing document text.
 *  @note History maps a manually moved anchor from its new position. */
tiku_rich_status_t tiku_richanchor_set(tiku_richanchor_t *anchor,
                                      tiku_rich_pos_t position);
/** @brief Restore the preceding history state, or return NO_CHANGE. */
tiku_rich_status_t tiku_richdoc_undo(tiku_richdoc_t *doc);
/** @brief Restore the following history state, or return NO_CHANGE. */
tiku_rich_status_t tiku_richdoc_redo(tiku_richdoc_t *doc);
/** @brief Whether undo has a state and the document permits mutation. */
int tiku_richdoc_can_undo(const tiku_richdoc_t *doc);
/** @brief Whether redo has a state and the document permits mutation. */
int tiku_richdoc_can_redo(const tiku_richdoc_t *doc);
/** @brief Release history without changing content, save state or snapshots.
 *  @note During change notification this does nothing. */
void tiku_richdoc_clear_history(tiku_richdoc_t *doc);
/** @brief Peak unique extra history bytes across retained states; NULL is 0.
 *  @note Includes records, maps and bindings; excludes one current state.
 *        Unchanged by Undo/Redo and bounded by limits.history_bytes. */
size_t tiku_richdoc_history_memory(const tiku_richdoc_t *doc);
/** @brief Unique history bytes excluding the current state's allocations.
 *  @note May vary on Undo/Redo. External snapshot ownership is not subtracted;
 *        clearing history need not free these bytes while snapshots survive. */
size_t tiku_richdoc_history_retained_memory(const tiku_richdoc_t *doc);
/** @brief Mark a successfully saved snapshot from this document as saved.
 *  @note Call on the document thread after storage reports success.
 *        Fragment snapshots are refused, including from this document. */
tiku_rich_status_t tiku_richdoc_mark_saved(
    tiku_richdoc_t *doc, const tiku_richsnapshot_t *snapshot);
/** @brief Whether the live state differs from the last marked saved state. */
int tiku_richdoc_dirty(const tiku_richdoc_t *doc);
/** @brief Whether content changes are refused by this document. */
int tiku_richdoc_read_only(const tiku_richdoc_t *doc);
/** @brief Enable or disable mutation refusal without changing the content.
 *  @note During change notification this does nothing. */
void tiku_richdoc_set_read_only(tiku_richdoc_t *doc, int read_only);
/** @brief Current content generation, or 0 for NULL. */
uint64_t tiku_richdoc_generation(const tiku_richdoc_t *doc);
/** @brief Bytes charged to this owner, including snapshots and fragments. */
size_t tiku_richdoc_memory(const tiku_richdoc_t *doc);
/** @brief Check paper settings and normalize the all-zero default.
 *  @note Failure leaves *out unchanged. Input and output may alias. */
tiku_rich_status_t tiku_rich_page_setup(const tiku_rich_page_setup_t *input,
                                      tiku_rich_page_setup_t *out);
/** @brief Copy the immutable document's normalized paper settings. */
tiku_rich_status_t tiku_richsnapshot_page(const tiku_richsnapshot_t *snapshot,
                                        tiku_rich_page_setup_t *out);
/** @brief Retain the current immutable state; failure sets *out to NULL.
 *  @note Create the snapshot on the document's thread, then hand it off. */
tiku_rich_status_t tiku_richdoc_snapshot(const tiku_richdoc_t *doc,
                                        tiku_richsnapshot_t **out);
/** @brief Release a snapshot on any thread; NULL is allowed. */
void tiku_richsnapshot_release(tiku_richsnapshot_t *snapshot);
/** @brief The captured content generation, or 0 for NULL. */
uint64_t tiku_richsnapshot_generation(const tiku_richsnapshot_t *snapshot);
/** @brief Stable state token, or 0 for NULL and fragment snapshots. */
uint64_t tiku_richsnapshot_state(const tiku_richsnapshot_t *snapshot);
/** @brief Main body ID, or 0 for NULL. */
uint64_t tiku_richsnapshot_body(const tiku_richsnapshot_t *snapshot);
/** @brief Depth-first paragraph count including cells, or 0 for NULL. */
size_t tiku_richsnapshot_paragraphs(const tiku_richsnapshot_t *snapshot);
/** @brief Number of depth-first structure nodes, including cells. */
size_t tiku_richsnapshot_blocks(const tiku_richsnapshot_t *snapshot);
/** @brief Read one structure node, clearing *out on failure. */
tiku_rich_status_t tiku_richsnapshot_block(
    const tiku_richsnapshot_t *snapshot, size_t index, tiku_rich_block_t *out);
/** @brief Immediate cell or main-body owner of a paragraph; zero if absent. */
uint64_t tiku_richsnapshot_container(const tiku_richsnapshot_t *snapshot,
                                    uint64_t paragraph);
/** @brief Number of named styles in the snapshot, or 0 for NULL. */
size_t tiku_richsnapshot_styles(const tiku_richsnapshot_t *snapshot);
/** @brief Get a zero-based paragraph. Its pointers live with the snapshot.
 *  @return OK, or an error with *out cleared. */
tiku_rich_status_t tiku_richsnapshot_paragraph(
    const tiku_richsnapshot_t *snapshot, size_t index,
    tiku_rich_paragraph_t *out);
/** @brief Get a zero-based named style, clearing *out on failure. */
tiku_rich_status_t tiku_richsnapshot_style(
    const tiku_richsnapshot_t *snapshot, size_t index, tiku_rich_style_t *out);
/** @brief Test IDs, byte bounds, affinity and UTF-8 boundary of a position. */
tiku_rich_status_t tiku_richsnapshot_position(
    const tiku_richsnapshot_t *snapshot, tiku_rich_pos_t position);
/** @brief Compare valid positions in document order, not numeric ID order.
 *  @note Affinity does not affect order. *order is -1, 0 or 1 on success. */
tiku_rich_status_t tiku_richsnapshot_compare(
    const tiku_richsnapshot_t *snapshot, tiku_rich_pos_t a,
    tiku_rich_pos_t b, int *order);
/** @brief Resolve paragraph defaults and a run's styles and overrides.
 *  @note For an empty paragraph use run 0. Unspecified properties remain
 *        absent; the layout provider supplies its own final defaults. */
tiku_rich_status_t tiku_richsnapshot_resolve(
    const tiku_richsnapshot_t *snapshot, size_t paragraph, size_t run,
    tiku_rich_chars_t *chars, tiku_rich_para_t *props);
/** @brief Copy plain text with LF between paragraphs and a trailing NUL.
 *  @note *length excludes NUL. A short buffer is untouched and returns
 *        LIMIT with the required length. NULL with capacity 0 queries. */
tiku_rich_status_t tiku_richsnapshot_text(
    const tiku_richsnapshot_t *snapshot, char *buffer, size_t capacity,
    size_t *length);

/** @brief Get list membership and its resolved decimal ordinal, or zero.
 *  @note Scans preceding paragraphs. Shallower items reset deeper counters;
 *        nested items do not increment their parents. A category change at
 *        the same level resets its counter. Failure clears both outputs. */
tiku_rich_status_t tiku_richsnapshot_list(const tiku_richsnapshot_t *snapshot,
    size_t paragraph, tiku_rich_list_t *list, uint64_t *ordinal);
/** @brief Validate a link target without opening or resolving it.
 *  @note Accepts HTTP(S), mailto and #fragments, up to 2048 UTF-8 bytes.
 *        NULL/empty means no link. Refuses ASCII whitespace/controls,
 *        backslashes, malformed escapes and percent-encoded ASCII controls.
 *        Other schemes are UNSUPPORTED.
 *        A host must apply its own authorization before activating a link. */
tiku_rich_status_t tiku_rich_link_validate(const char *target);

/** @brief Selection properties whose values differ within the selection. */
enum {
    TIKU_RICH_MIXED_PARAGRAPH_STYLE = 1u << 0,
    TIKU_RICH_MIXED_CHARACTER_STYLE = 1u << 1,
    TIKU_RICH_MIXED_LIST = 1u << 2,
    TIKU_RICH_MIXED_LINK = 1u << 3
};

/** @brief Uniform resolved values and mixed-property masks for a selection.
 *  @note Mixed values are cleared. Uniform absence has no presence/mixed bit.
 *        The link is borrowed from the queried snapshot. Empty selections
 *        report insertion inheritance, not view-local pending formatting. */
typedef struct {
    tiku_rich_chars_t chars;
    tiku_rich_para_t props;
    unsigned mixed_chars, mixed_props, mixed;
    uint64_t paragraph_style, character_style;
    tiku_rich_list_t list;
    const char *link;
    size_t paragraphs;         /**< selected paragraph-property samples */
    int empty;
} tiku_rich_selection_t;

/** @brief Inspect an ordered half-open selection without allocation.
 *  @note A last endpoint at byte zero excludes that paragraph unless the
 *        selection is empty. Character samples use selected text; when
 *        none exists, the first paragraph's defaults are reported.
 *        Failure clears *out. Affinity does not affect inheritance. */
tiku_rich_status_t tiku_richsnapshot_selection(
    const tiku_richsnapshot_t *snapshot, tiku_rich_pos_t first,
    tiku_rich_pos_t last, tiku_rich_selection_t *out);

/** @brief Copy selected paragraphs/runs and their required style ancestors.
 *  @note The fragment shares retained link targets, not the source root or
 *        unselected paragraphs. Allocations use the snapshot's owner/budget.
 *        The allocator context must outlive the fragment and its snapshots.
 *        The fragment survives source closure; failure clears *out. */
tiku_rich_status_t tiku_richfragment_create(
    const tiku_richsnapshot_t *snapshot, tiku_rich_pos_t first,
    tiku_rich_pos_t last, tiku_richfragment_t **out);
/** @brief Release a fragment on any thread; NULL is allowed. */
void tiku_richfragment_release(tiku_richfragment_t *fragment);
/** @brief Capture a fragment's immutable contents; failure clears *out.
 *  @note IDs identify fragment content, not nodes in a paste destination. */
tiku_rich_status_t tiku_richfragment_snapshot(
    const tiku_richfragment_t *fragment, tiku_richsnapshot_t **out);
/** @brief Replace a live range with a styled fragment as one undoable change.
 *  @note Destination-boundary paragraphs with remaining text keep their
 *        paragraph properties. Copied text keeps its character appearance.
 *        Equivalent styles are reused; conflicting names receive a suffix.
 *        Imported nodes/styles get destination IDs and owned link copies.
 *        Empty fragments delete the selection. Failure leaves state intact. */
tiku_rich_status_t tiku_richdoc_paste_fragment(tiku_richdoc_t *doc,
    uint64_t generation, tiku_rich_pos_t first, tiku_rich_pos_t last,
    const tiku_richfragment_t *fragment);

/** @brief One search result in its captured document generation. */
typedef struct {
    uint64_t generation;
    tiku_rich_pos_t first, last; /**< half-open range; AFTER/BEFORE affinity */
} tiku_rich_match_t;

/** @brief Search a snapshot's ordered range for exact, case-sensitive UTF-8.
 *  @note The nonempty needle is copied. LF matches a paragraph separator or
 *        soft break; CR is refused. Matches do not overlap. The search
 *        retains its state after snapshot/document release. Creation builds
 *        a linear-time needle index; stepping performs no allocation.
 *        Search handles are single-threaded, independent of the live doc. */
tiku_rich_status_t tiku_richsearch_create(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, const char *needle,
    size_t length, tiku_richsearch_t **out);
/** @brief Require Unicode word and grapheme boundaries at both match ends. */
#define TIKU_RICH_SEARCH_WHOLE_WORD 1u
/** @brief Create literal search with checked optional boundary filtering.
 *  @note flags is zero or WHOLE_WORD. Boundary preparation advances within
 *        next()'s work budget and is charged to the snapshot's allocator.
 *        Uses Unicode 17 default word boundaries without language tailoring. */
tiku_rich_status_t tiku_richsearch_create_options(
    const tiku_richsnapshot_t *snapshot, tiku_rich_pos_t first,
    tiku_rich_pos_t last, const char *needle, size_t length,
    unsigned flags, tiku_richsearch_t **out);
/** @brief Advance by at most work character-comparison steps (work > 0).
 *  @return OK with a match, PENDING to resume, or NOT_FOUND at scope end.
 *          Non-OK clears *out. A zero budget does not advance the search. */
tiku_rich_status_t tiku_richsearch_next(tiku_richsearch_t *search,
    size_t work, tiku_rich_match_t *out);
/** @brief Cancel/release a search; NULL is allowed. */
void tiku_richsearch_release(tiku_richsearch_t *search);

#endif /* TIKU_RICHDOC_H_ */
