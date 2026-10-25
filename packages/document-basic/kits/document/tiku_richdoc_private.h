/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_private.h - immutable nodes and shared allocation accounting.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHDOC_PRIVATE_H_
#define TIKU_RICHDOC_PRIVATE_H_

#include "tiku_richdoc.h"
#include "tiku_richbookmark.h"
#include "tiku_richcells.h"

#include "tiku_richsession.h"
#include "tiku_richwatch.h"
#include <stdatomic.h>

/** @brief Shared allocator lifetime and aggregate allocation ceiling. */
typedef struct {
    atomic_size_t refs;         /**< references from allocation headers */
    atomic_size_t used;         /**< charged bytes, including this object */
    size_t limit;              /**< aggregate ceiling */
    tiku_rich_allocator_t hooks; /**< copied allocation callbacks */
} tiku_rd_memory_t;

/** @brief One immutable paragraph with independently retained ownership. */
typedef struct {
    atomic_size_t refs;         /**< roots retaining this paragraph */
    tiku_rich_paragraph_t value; /**< public immutable content */
} tiku_rd_paragraph_t;

/** @brief One immutable style and its owned name. */
typedef struct {
    tiku_rich_style_t value;    /**< public style properties */
    char name[64];             /**< storage used by value.name */
} tiku_rd_style_t;

/** @brief Immutable preorder structure; paragraph text is owned by the root. */
typedef struct {
    atomic_size_t refs;        /**< roots retaining this structure */
    size_t count;             /**< initialized preorder nodes */
    tiku_rich_block_t nodes[]; /**< parent IDs define ordered child lists */
} tiku_rd_blocks_t;

/** @brief Immutable named locations shared by roots with unchanged positions. */
typedef struct {
    atomic_size_t refs;           /**< roots retaining this immutable array */
    size_t count;                /**< number of initialized named points */
    tiku_rich_bookmark_t items[]; /**< insertion-order names and positions */
} tiku_rd_bookmarks_t;

/** @brief A document state shared by the live document and snapshots. */
typedef struct {
    atomic_size_t refs;         /**< live document and snapshot references */
    uint64_t body;             /**< main text body ID */
    uint64_t state;            /**< stable save-state token */
    size_t count;              /**< paragraphs in order */
    size_t nstyles;            /**< named styles in source order */
    size_t bytes;              /**< sum of paragraph text bytes */
    size_t runs;               /**< sum of paragraph run counts */
    tiku_rd_paragraph_t **paragraphs; /**< retained paragraph nodes */
    tiku_rd_style_t *styles;    /**< owned styles */
    tiku_rich_page_setup_t page; /**< normalized single-section paper */
    tiku_rd_blocks_t *blocks;  /**< NULL is an ordered paragraph-only body */
    size_t footprint; /**< cached unique byte charge; owner thread only */
    tiku_rd_bookmarks_t *bookmarks; /**< NULL when no named locations exist */
} tiku_rd_root_t;

/** @brief Validate the source hierarchy and exact paragraph traversal. */
tiku_rich_status_t tiku_rd_blocks_validate(const tiku_rich_source_t *source);
/** @brief Copy source structure and translate local indices to stable IDs. */
tiku_rich_status_t tiku_rd_blocks_build(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_source_t *source, uint64_t *next);
/** @brief Release immutable structure storage at its final reference. */
void tiku_rd_blocks_drop(tiku_rd_blocks_t *blocks);
/** @brief Allocate a zeroed structure array under the document budget. */
tiku_rd_blocks_t *tiku_rd_blocks_new(tiku_richdoc_t *doc, size_t count,
                                      tiku_rich_status_t *status);
/** @brief Locate a node by ID, or SIZE_MAX when absent. */
size_t tiku_rd_block_find(const tiku_rd_root_t *root, uint64_t id);
/** @brief Immediate structural owner of a paragraph, or zero if absent. */
uint64_t tiku_rd_container(const tiku_rd_root_t *root, uint64_t id);
/** @brief Immediate table containing a paragraph, or zero outside a cell. */
uint64_t tiku_rd_table_for(const tiku_rd_root_t *root, uint64_t paragraph);
/** @brief Exclusive end of a valid preorder subtree. */
size_t tiku_rd_subtree_end(const tiku_rd_root_t *root, size_t first);
/** @brief Direct cell ordinal containing a descendant, or SIZE_MAX. */
size_t tiku_rd_grid_cell(const tiku_rd_root_t *root, uint64_t table,
                        uint64_t paragraph);
/** @brief First leaf paragraph of a table's cell, or zero. */
uint64_t tiku_rd_grid_paragraph(const tiku_rd_root_t *root, uint64_t table,
                               size_t ordinal);
/** @brief Keep surviving paragraphs; map deleted cells to a nearby cell. */
tiku_rich_pos_t tiku_rd_grid_position(tiku_rich_pos_t position,
    uint64_t table, const tiku_rd_root_t *from, const tiku_rd_root_t *to);
/** @brief Whether paragraph indices span one uninterrupted block container. */
int tiku_rd_same_container(const tiku_rd_root_t *root, size_t first,
                            size_t last);

/** @brief One forward structural or byte-range position transformation. */
typedef struct {
    tiku_rich_edit_kind_t kind; /**< replacement, split or join */
    uint64_t first, second;    /**< leading and trailing paragraph IDs */
    size_t at;                /**< replacement start or split/join offset */
    size_t removed, inserted; /**< replacement byte lengths */
    int flat;                 /**< offsets include paragraph separators */
} tiku_rd_map_t;

/** @brief Validate source bookmark names and scalar-boundary positions. */
tiku_rich_status_t tiku_rd_bookmarks_validate(const tiku_rich_source_t *source);
/** @brief Build imported bookmarks after paragraph IDs have been assigned. */
tiku_rich_status_t tiku_rd_bookmarks_build(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_source_t *source);
/** @brief Map candidate bookmarks before history capture; no-op maps share data. */
tiku_rich_status_t tiku_rd_bookmarks_follow(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rd_map_t *maps, size_t count);
/** @brief Release a retained bookmark array; NULL is allowed. */
void tiku_rd_bookmarks_drop(tiku_rd_bookmarks_t *bookmarks);

/** @brief Update a candidate hierarchy after a successful split or join. */
tiku_rich_status_t tiku_rd_blocks_map(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rd_map_t *map);
/** @brief Insert or remove a table in an unpublished root. */
tiku_rich_status_t tiku_rd_table_edit(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit, uint64_t *next,
    tiku_rd_map_t *map);
/** @brief Replace table geometry without changing its grid or identities. */
tiku_rich_status_t tiku_rd_table_set(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit);
/** @brief Replace one cell's background in an unpublished hierarchy. */
tiku_rich_status_t tiku_rd_cell_set(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit);
/** @brief Shift valid column weights; insertion inherits their mean.
 *  @note Caller checks the index and resulting 1..64 column count. */
void tiku_rd_table_columns(tiku_rich_table_t *table, unsigned at, int insert);
/** @brief Replace one grid axis in an unpublished root. */
tiku_rich_status_t tiku_rd_grid_edit(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit, uint64_t *next,
    tiku_rd_map_t *map);

/** @brief Exact positions for an anchor present at transaction publication. */
typedef struct {
    uint64_t id;              /**< non-reused anchor identity */
    uint64_t revision;        /**< explicit position movement counter */
    tiku_rich_pos_t before;   /**< pre-transaction position */
    tiku_rich_pos_t after;    /**< post-transaction position */
    int initiating;          /**< restore regardless of explicit movement */
} tiku_rd_binding_t;

/** @brief Explicit typing changes applied to inherited run attributes. */
typedef struct {
    tiku_rich_chars_t chars;  /**< direct property overrides */
    unsigned clear;          /**< clear inherited run overrides/resets */
    uint64_t style;          /**< selected character style ID */
    int style_set;           /**< style zero explicitly removes assignment */
} tiku_rs_pending_t;

/** @brief One reversible root exchange in an undo or redo stack. */
typedef struct tiku_rd_history {
    struct tiku_rd_history *next; /**< next older history group */
    tiku_rd_root_t *root;       /**< retained other side of the change */
    size_t planned_bytes; /**< prepared peak history reserve for publication */
    size_t planned_total; /**< prepared union of current and history storage */
    struct tiku_rd_history *discard; /**< first old undo group to evict */
    tiku_rd_map_t *maps;       /**< ordered forward transformations */
    size_t map_count;         /**< initialized transformations */
    tiku_rd_binding_t *bindings; /**< records in descending anchor ID order */
    size_t binding_count;     /**< captured anchors */
    uint64_t origin;          /**< non-reused initiating session ID */
    tiku_rs_pending_t pending_before, pending_after; /**< typing state */
    int groupable;            /**< accepts compatible adjacent typing */
    int replace_top;          /**< publication replaces the newest group */
} tiku_rd_history_t;

/** @brief Live document state; mutation occurs on the owning thread. */
struct tiku_richdoc {
    size_t refs;               /**< callers retaining the document */
    tiku_rd_memory_t *memory;   /**< allocator retained by this allocation */
    tiku_rich_limits_t limits; /**< effective import ceilings */
    tiku_rd_root_t *root;       /**< currently published state */
    uint64_t generation;       /**< content publication counter */
    uint64_t next_id;          /**< next ID; 0 means exhausted */
    uint64_t saved_state;      /**< token of the last successful save/load */
    tiku_rd_history_t *undo;    /**< newest undo group */
    tiku_rd_history_t *redo;    /**< next redo group */
    size_t history_bytes;      /**< maximum extra bytes at any retained state */
    size_t history_total; /**< unique current/history storage plus records */
    tiku_richanchor_t *anchors; /**< non-owning list in descending ID order */
    size_t anchor_count;       /**< registered positions */
    uint64_t next_anchor;      /**< next anchor identity; zero at exhaustion */
    int read_only;             /**< content changes are refused when set */
    tiku_richsession_t *sessions; /**< non-owning list of editing sessions */
    tiku_richsession_t *initiator; /**< session executing a command */
    tiku_richwatch_t *watches;  /**< non-owning subscription list */
    size_t watch_count;        /**< live subscriptions */
    int notifying;            /**< callback mutation guard */
    uint64_t
        resource_identity; /**< process-local lifetime, renewed on import */
};

/** @brief Directional selection and pending character format for one host. */
struct tiku_richsession {
    tiku_richdoc_t *doc;       /**< retained by both selection anchors */
    tiku_richsession_t *next;  /**< next attached session */
    tiku_richanchor_t *anchor, *caret; /**< directional endpoints */
    tiku_rs_pending_t pending; /**< session-local typing attributes */
    tiku_rich_selection_fn selection_changed; /**< optional owner-thread hook */
    void *selection_context;  /**< borrowed until observer replacement */
    tiku_rich_pos_t observed_anchor,
        observed_caret;       /**< last reported values */
    int read_only;            /**< additional host restriction */
    int collapse;             /**< current command collapses its selection */
    int typing;               /**< current command uses pending attributes */
    int coalescing;            /**< current input may join adjacent typing */
};

/** @brief Registered owner-thread position retaining its parent document. */
struct tiku_richanchor {
    tiku_richdoc_t *doc;       /**< retained document */
    tiku_richanchor_t *next;   /**< next live anchor */
    uint64_t id;              /**< lifetime-unique identity */
    uint64_t revision;        /**< explicit movement counter */
    tiku_rich_pos_t position; /**< current valid position */
};

/** @brief Frozen state and generation with ownership independent of a view. */
struct tiku_richsnapshot {
    tiku_rd_root_t *root;       /**< retained immutable state */
    uint64_t generation;       /**< generation at capture */
    tiku_rd_memory_t *memory;   /**< document identity retained by storage */
    int fragment;             /**< partial content, not a save checkpoint */
};

/** @brief Independently owned paragraphs and required style records. */
struct tiku_richfragment {
    tiku_rd_root_t *root;      /**< owned selection root */
    tiku_rd_memory_t *memory;  /**< allocator retained by this allocation */
    uint64_t generation;      /**< generation captured from source */
    int empty;                /**< capture had equal logical endpoints */
};

/** @brief Publish synchronous callbacks after content and selections settle. */
void tiku_rd_notify(tiku_richdoc_t *doc, tiku_rich_change_kind_t kind,
                 uint64_t origin);
/** @brief Notify a session observer if directional endpoint values changed.
 *  @note Delivery allocates nothing and guards mutations during callbacks. */
void tiku_rs_notify_selection(tiku_richsession_t *session);
/** @brief Drop history without notifying; used by import and destruction. */
void tiku_rd_clear_history(tiku_richdoc_t *doc);
/** @brief Prepare compatible typing maps and original anchor bindings. */
void tiku_rd_coalesce(tiku_richdoc_t *doc, const tiku_rd_root_t *after,
                       tiku_rd_history_t *entry);
/** @brief Publish a merged group's bindings and release its middle state. */
void tiku_rd_coalesce_publish(tiku_richdoc_t *doc,
                               tiku_rd_history_t *entry);
/** @brief Apply pending overrides to an inherited insertion run. */
void tiku_rs_typing(const tiku_richsession_t *session, tiku_rich_run_t *run);
/** @brief Restore origin typing state and discard references to lost styles. */
void tiku_rs_history(tiku_richdoc_t *doc, const tiku_rd_history_t *entry,
                      int forward);
/** @brief Build sorted endpoints with the session's editing restriction. */
tiku_richcmd_context_t tiku_rs_context(const tiku_richsession_t *session);
/** @brief Whether a command changes pending formatting at an empty caret. */
int tiku_rs_caret_command(tiku_richcmd_kind_t kind);
/** @brief Apply a validated caret-format command to session-local state. */
tiku_rich_status_t tiku_rs_format(tiku_richsession_t *session,
    const tiku_richcmd_t *command, const tiku_richcmd_state_t *state);

/** @brief Create an accounting owner with one construction reference. */
tiku_rd_memory_t *tiku_rd_memory_new(const tiku_rich_limits_t *limits,
                                     tiku_rich_status_t *status);
/** @brief Release an owner reference and its storage at the final release. */
void tiku_rd_memory_drop(tiku_rd_memory_t *memory);
/** @brief Allocate zeroed, aligned storage and charge header plus payload. */
void *tiku_rd_alloc(tiku_rd_memory_t *memory, size_t bytes,
                   tiku_rich_status_t *status);
/** @brief Release an allocation and the reference held by its header. */
void tiku_rd_free(void *pointer);
/** @brief Bytes charged by one kit allocation, including its header. */
size_t tiku_rd_size(const void *pointer);
/** @brief Add/remove a census reference and update unique allocated bytes.
 *  @return Nonzero at the first addition or final removal; NULL returns zero.
 *  @note Owner thread only. Balance all references before freeing storage;
 *        no nested census or allocator callback may inspect these counters. */
int tiku_rd_account(const void *pointer, int add, size_t *bytes);
/** @brief Prepare exact history retention without allocation or publication.
 *  @note Restores its temporary allocation counters before returning. */
tiku_rich_status_t tiku_rd_history_plan(tiku_richdoc_t *doc,
    tiku_rd_root_t *after, tiku_rd_history_t *entry);
/** @brief Resolve zero ceilings to defaults and validate allocation hooks. */
tiku_rich_status_t tiku_rd_limits(const tiku_rich_limits_t *source,
                                  tiku_rich_limits_t *out);
/** @brief Validate UTF-8 without NUL; modes 1/2 reject CR, mode 1 objects. */
tiku_rich_status_t tiku_rd_utf8(const char *text, size_t length,
                                int paragraph);
/** @brief Copy a checked image; a matching owned source shares pixel data. */
const tiku_rich_image_t *tiku_rd_image_new(tiku_rd_memory_t *memory,
    const tiku_rich_image_t *source, const tiku_rich_image_t *owned,
    tiku_rich_status_t *status);
/** @brief Retain an immutable image owned by the document allocator. */
void tiku_rd_image_retain(const tiku_rich_image_t *image);
/** @brief Release an image and its pixel reference; NULL is allowed. */
void tiku_rd_image_drop(const tiku_rich_image_t *image);
/** @brief Add/remove image metadata and shared pixels in a byte census. */
void tiku_rd_image_account(const tiku_rich_image_t *image, int add,
                           size_t *bytes);
/** @brief Whether an offset falls between UTF-8 characters in valid text. */
int tiku_rd_boundary(const char *text, size_t length, size_t offset);
/** @brief Validate a nonempty, terminated UTF-8 name of at most 63 bytes. */
int tiku_rd_name(const char name[64]);
/** @brief Validate and canonicalize only present character properties. */
tiku_rich_status_t tiku_rd_chars(const tiku_rich_chars_t *in,
                                 tiku_rich_chars_t *out);
/** @brief Validate and canonicalize only present paragraph properties. */
tiku_rich_status_t tiku_rd_props(const tiku_rich_para_t *in,
                                 tiku_rich_para_t *out);
/** @brief Overlay present character properties onto an existing style. */
void tiku_rd_chars_overlay(tiku_rich_chars_t *to,
                           const tiku_rich_chars_t *from);
/** @brief Overlay present paragraph properties onto an existing style. */
void tiku_rd_props_overlay(tiku_rich_para_t *to,
                           const tiku_rich_para_t *from);
/** @brief Clear selected character properties to canonical absent values. */
void tiku_rd_chars_reset(tiku_rich_chars_t *chars, unsigned mask);
/** @brief Compare canonical character values, excluding structure padding. */
int tiku_rd_chars_equal(const tiku_rich_chars_t *a,
                        const tiku_rich_chars_t *b);
/** @brief Compare canonical paragraph values without structure padding. */
int tiku_rd_props_equal(const tiku_rich_para_t *a,
                        const tiku_rich_para_t *b);
/** @brief Validate and canonicalize direct list membership. */
tiku_rich_status_t tiku_rd_list(const tiku_rich_list_t *in,
                                tiku_rich_list_t *out);
/** @brief Compare canonical list membership values. */
int tiku_rd_list_equal(const tiku_rich_list_t *a, const tiku_rich_list_t *b);
/** @brief Copy a checked link target into retained storage; NULL means none. */
const char *tiku_rd_link_new(tiku_rd_memory_t *memory, const char *target,
                             tiku_rich_status_t *status);
/** @brief Retain a target previously created by tiku_rd_link_new. */
void tiku_rd_link_retain(const char *target);
/** @brief Release a retained target; NULL is allowed. */
void tiku_rd_link_drop(const char *target);
/** @brief Add/remove a target's allocation in a byte census; NULL is ignored. */
void tiku_rd_link_account(const char *target, int add, size_t *bytes);
/** @brief Compare canonical targets by value, including NULL. */
int tiku_rd_link_equal(const char *a, const char *b);
/** @brief Link inherited by a checked replacement or insertion range. */
const char *tiku_rd_typing_link(const tiku_rich_paragraph_t *p,
                                size_t start, size_t end);
/** @brief Preserve character appearance when changing paragraph defaults. */
void tiku_rd_preserve_chars(tiku_rich_run_t *run,
    const tiku_rich_chars_t *wanted, const tiku_rich_chars_t *base);
/** @brief Append a canonical style, consuming a local ID; parent is valid. */
tiku_rich_status_t tiku_rd_style_append(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, tiku_rd_style_t *style, uint64_t *next_id);
/** @brief Map fragment styles into a candidate root in source-array order.
 *  @note The caller owns the returned ID array; failure clears *out. */
tiku_rich_status_t tiku_rd_fragment_styles(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rd_root_t *source, uint64_t *next_id,
    uint64_t **out);
/** @brief Find a named style by ID; SIZE_MAX means absent. */
size_t tiku_rd_style_find(const tiku_rd_root_t *root, uint64_t id);
/** @brief Test a style ID's category; zero represents no named style. */
int tiku_rd_style_ref(const tiku_rd_root_t *root, uint64_t id,
                      tiku_rich_style_kind_t kind);
/** @brief Define or remove a style on an unpublished root. */
tiku_rich_status_t tiku_rd_style_edit(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit, uint64_t *next_id);
/** @brief Copy a paragraph with changed properties or named style. */
tiku_rd_paragraph_t *tiku_rd_paragraph_format(tiku_richdoc_t *doc,
    const tiku_rd_root_t *root, const tiku_rich_edit_t *edit,
    tiku_rich_status_t *status);
/** @brief Append within preallocated capacity, merging equal adjacent runs. */
void tiku_rd_append_run(tiku_rich_paragraph_t *out,
                        const tiku_rich_run_t *run);
/** @brief Copy a validated slice's runs into a preallocated paragraph. */
void tiku_rd_slice_runs(tiku_rich_paragraph_t *out,
    const tiku_rich_paragraph_t *from, size_t start, size_t end,
    const tiku_rich_chars_t *chars);
/** @brief Inherit the preceding run; byte zero uses paragraph defaults. */
tiku_rich_run_t tiku_rd_typing_run(const tiku_rich_paragraph_t *p,
                                    size_t offset);
/** @brief Count stored runs intersecting a checked half-open byte range. */
size_t tiku_rd_run_count(const tiku_rich_paragraph_t *p,
                          size_t first, size_t last);
/** @brief Allocate text/run capacity with copied paragraph properties. */
tiku_rd_paragraph_t *tiku_rd_paragraph_new(tiku_richdoc_t *doc,
    const tiku_rich_paragraph_t *from, size_t length, size_t capacity,
    tiku_rich_status_t *status);
/** @brief Validate import sizes, styles, UTF-8 and complete run coverage. */
tiku_rich_status_t tiku_rd_validate(const tiku_rich_limits_t *limits,
                                    const tiku_rich_source_t *source);
/** @brief Construct a new root; no live document state is changed. */
tiku_rd_root_t *tiku_rd_build(tiku_richdoc_t *doc,
                              const tiku_rich_source_t *source,
                              uint64_t *next_id,
                              tiku_rich_status_t *status);
/** @brief Release a root and its retained content when no owners remain. */
void tiku_rd_root_drop(tiku_rd_root_t *root);
/** @brief Release a paragraph reference and free its payload at zero. */
void tiku_rd_paragraph_drop(tiku_rd_paragraph_t *paragraph);
/** @brief Copy a root's containers while retaining unchanged paragraphs. */
tiku_rd_root_t *tiku_rd_clone(tiku_richdoc_t *doc,
                              tiku_rich_status_t *status);
/** @brief Construct one edited paragraph, retaining its ID and defaults. */
tiku_rd_paragraph_t *tiku_rd_edit(tiku_richdoc_t *doc,
                                 const tiku_rd_root_t *root,
                                 const tiku_rich_edit_t *edit,
                                 tiku_rich_status_t *status);
/** @brief Prepare a history record without changing document or history.
 *  @note Takes ownership of maps on success; the caller owns them on error. */
tiku_rd_history_t *tiku_rd_history_new(tiku_richdoc_t *doc,
                                      tiku_rd_root_t *after,
                                      tiku_rd_map_t *maps, size_t map_count,
                                      tiku_rich_status_t *status);
/** @brief Publish a prepared root and history record without allocation. */
void tiku_rd_publish(tiku_richdoc_t *doc, tiku_rd_root_t *root,
                     tiku_rd_history_t *entry);
/** @brief Split or join paragraphs in an unpublished root. */
tiku_rich_status_t tiku_rd_structure(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit, uint64_t *next_id,
    tiku_rd_map_t *map);
/** @brief Transform one position through ordered maps, forward or inverse.
 *  @note Flat maps form an entirely flat sequence in successive intermediate
 *        coordinates; from/to are the complete roots. */
tiku_rich_pos_t tiku_rd_map_position(tiku_rich_pos_t position,
    const tiku_rd_map_t *maps, size_t count, int forward,
    const tiku_rd_root_t *from, const tiku_rd_root_t *to);
/** @brief Apply a group with an optional whole-range position mapping. */
tiku_rich_status_t tiku_rd_apply(tiku_richdoc_t *doc,
    const tiku_rich_transaction_t *change, const tiku_rd_map_t *range);
/** @brief Capture anchor restoration data before publication. */
tiku_rich_status_t tiku_rd_bindings(tiku_richdoc_t *doc,
    const tiku_rd_root_t *after, tiku_rd_history_t *entry);
/** @brief Move live anchors through prepared history without allocation. */
void tiku_rd_anchors_move(tiku_richdoc_t *doc,
    const tiku_rd_history_t *entry, int forward, const tiku_rd_root_t *after);
/** @brief Reset live anchors after successful import. */
void tiku_rd_anchors_reset(tiku_richdoc_t *doc);
/** @brief Find a paragraph's order by ID; SIZE_MAX means absent. */
size_t tiku_rd_find(const tiku_rd_root_t *root, uint64_t id);
/** @brief Resolve inherited style properties from an immutable root. */
tiku_rich_status_t tiku_rd_resolve(const tiku_rd_root_t *root,
                                   size_t paragraph, size_t run,
                                   tiku_rich_chars_t *chars,
                                   tiku_rich_para_t *props);
/** @brief Resolve a run using an explicit paragraph's default properties. */
tiku_rich_status_t tiku_rd_resolve_value(const tiku_rd_root_t *root,
    const tiku_rich_paragraph_t *p, const tiku_rich_run_t *run,
    tiku_rich_chars_t *chars, tiku_rich_para_t *props);

/** @brief Linear traversal of selected direct cells and their descendants. */
typedef struct {
    const tiku_rd_root_t *root; /**< borrowed source hierarchy */
    tiku_rich_cells_t cells;   /**< checked direct-cell rectangle */
    size_t at, end, paragraph, ordinal; /**< structural traversal cursors */
    unsigned columns;        /**< source table width in cells */
    int selected;            /**< current direct cell belongs to rectangle */
} tiku_rd_cells_scan_t;
/** @brief Validate a rectangle and prepare a linear structural traversal. */
tiku_rich_status_t tiku_rd_cells_begin(const tiku_rd_root_t *root,
    const tiku_rich_cells_t *cells, tiku_rd_cells_scan_t *scan);
/** @brief Return a selected node and its paragraph index, or NULL at end. */
const tiku_rich_block_t *tiku_rd_cells_next(tiku_rd_cells_scan_t *scan,
                                           size_t *paragraph);
/** @brief Replace selected direct-cell fills in one copy-on-write hierarchy. */
tiku_rich_status_t tiku_rd_cells_set(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit);

/** @brief Capture a paragraph range, optionally retaining empty defaults. */
tiku_rich_status_t tiku_rd_fragment_create(const tiku_richsnapshot_t *snapshot,
    tiku_rich_pos_t first, tiku_rich_pos_t last, int keep_empty,
    tiku_richfragment_t **out);
/** @brief Replace a checked sibling range in an unpublished candidate.
 *  @note The caller owns root and checks final aggregate content limits.
 *        Failure may change the candidate but never publishes it. */
tiku_rich_status_t tiku_rd_paste_into(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, tiku_rich_pos_t first, tiku_rich_pos_t last,
    const tiku_richfragment_t *fragment, uint64_t *next, tiku_rd_map_t *map);
/** @brief Insert an empty table after a paragraph without adding a tail.
 *  @note Caller supplies an existing following paragraph in the same owner. */
tiku_rich_status_t tiku_rd_table_between(tiku_richdoc_t *doc,
    tiku_rd_root_t *root, const tiku_rich_edit_t *edit, uint64_t *next);
/** @brief Fill a checked equal-size rectangle in an unpublished root.
 *  @note Failure may change the candidate. Source/target shapes are validated
 *        by the caller. No history or notifications are emitted. */
tiku_rich_status_t tiku_rd_cells_fill(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rich_cells_t *target, const tiku_richsnapshot_t *snapshot,
    const tiku_rich_cells_t *source, uint64_t *next);
/** @brief Replace a cell hierarchy after its paragraph contents are copied.
 *  @note Caller validates cell IDs and the copied paragraph span. The candidate
 *        still owns its original blocks. Paragraph is the first
 *        copied paragraph index. Failure leaves the block array unchanged. */
tiku_rich_status_t tiku_rd_cell_tree(tiku_richdoc_t *doc, tiku_rd_root_t *root,
    const tiku_rd_root_t *source, uint64_t from, uint64_t to,
    size_t paragraph, uint64_t *next);

#endif /* TIKU_RICHDOC_PRIVATE_H_ */
