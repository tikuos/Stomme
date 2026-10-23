/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richcmd.h - typed editing commands and menu availability queries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHCMD_H_
#define TIKU_RICHCMD_H_

#include "tiku_richdoc.h"

/** @brief Command identities; zero is not a command. */
typedef enum {
    TIKU_RICHCMD_REPLACE_TEXT = 1,
    TIKU_RICHCMD_DELETE,
    TIKU_RICHCMD_PARAGRAPH_BREAK,
    TIKU_RICHCMD_PASTE_FRAGMENT,
    TIKU_RICHCMD_UNDO,
    TIKU_RICHCMD_REDO,
    TIKU_RICHCMD_BOLD,
    TIKU_RICHCMD_ITALIC,
    TIKU_RICHCMD_UNDERLINE,
    TIKU_RICHCMD_STRIKE,
    TIKU_RICHCMD_CHARACTER_FORMAT,
    TIKU_RICHCMD_CLEAR_CHARACTER_OVERRIDES,
    TIKU_RICHCMD_ALIGN_LEFT,
    TIKU_RICHCMD_ALIGN_CENTER,
    TIKU_RICHCMD_ALIGN_RIGHT,
    TIKU_RICHCMD_ALIGN_JUSTIFY,
    TIKU_RICHCMD_PARAGRAPH_FORMAT,
    TIKU_RICHCMD_PARAGRAPH_STYLE,
    TIKU_RICHCMD_CHARACTER_STYLE,
    TIKU_RICHCMD_LIST_SET,
    TIKU_RICHCMD_LINK_SET,
    TIKU_RICHCMD_LINK_REMOVE,
    TIKU_RICHCMD_STYLE_DEFINE,
    TIKU_RICHCMD_STYLE_REMOVE,
    TIKU_RICHCMD_IMAGE_INSERT,
    TIKU_RICHCMD_IMAGE_SET,
    TIKU_RICHCMD_TABLE_INSERT,
    TIKU_RICHCMD_TABLE_REMOVE,
    TIKU_RICHCMD_ROW_BEFORE,
    TIKU_RICHCMD_ROW_AFTER,
    TIKU_RICHCMD_ROW_REMOVE,
    TIKU_RICHCMD_COLUMN_BEFORE,
    TIKU_RICHCMD_COLUMN_AFTER,
    TIKU_RICHCMD_COLUMN_REMOVE,
    TIKU_RICHCMD_TABLE_SET,
    TIKU_RICHCMD_CELL_SET
} tiku_richcmd_kind_t;

/** @brief Which payload member a command consumes. */
typedef enum {
    TIKU_RICHCMD_NO_PARAMETER,
    TIKU_RICHCMD_TEXT_PARAMETER,
    TIKU_RICHCMD_FRAGMENT_PARAMETER,
    TIKU_RICHCMD_CHARACTER_PARAMETER,
    TIKU_RICHCMD_PARAGRAPH_PARAMETER,
    TIKU_RICHCMD_STYLE_PARAMETER,
    TIKU_RICHCMD_LIST_PARAMETER,
    TIKU_RICHCMD_LINK_PARAMETER,
    TIKU_RICHCMD_DEFINITION_PARAMETER,
    TIKU_RICHCMD_IMAGE_PARAMETER,
    TIKU_RICHCMD_TABLE_PARAMETER,
    TIKU_RICHCMD_COLOR_PARAMETER
} tiku_richcmd_parameter_t;

/** @brief Selection requirements for a command. */
typedef enum {
    TIKU_RICHCMD_DOCUMENT_SCOPE,   /**< endpoints are ignored */
    TIKU_RICHCMD_RANGE_SCOPE,      /**< replacement may span separators */
    TIKU_RICHCMD_CHARACTER_SCOPE,  /**< selected character text is required */
    TIKU_RICHCMD_PARAGRAPH_SCOPE   /**< empty selection uses its paragraph */
} tiku_richcmd_scope_t;

/** @brief Immutable catalog entry; labels are English defaults for hosts. */
typedef struct {
    tiku_richcmd_kind_t kind;
    const char *key;              /**< stable machine-readable name */
    const char *label;
    tiku_richcmd_parameter_t parameter;
    tiku_richcmd_scope_t scope;
} tiku_richcmd_info_t;

/** @brief Selection and permission supplied by the initiating host.
 *  @note Endpoints are ordered and use live document IDs. A nonzero
 *        generation must match; zero accepts the current state. Read-only
 *        is an additional host restriction, not a security boundary. */
typedef struct {
    uint64_t generation;
    tiku_rich_pos_t first, last;
    int read_only;
} tiku_richcmd_context_t;

/** @brief Checkbox state; UNSPECIFIED is distinct from an explicit false. */
typedef enum {
    TIKU_RICHCMD_CHECK_NONE,
    TIKU_RICHCMD_CHECK_OFF,
    TIKU_RICHCMD_CHECK_ON,
    TIKU_RICHCMD_CHECK_MIXED,
    TIKU_RICHCMD_CHECK_UNSPECIFIED
} tiku_richcmd_check_t;

/** @brief Command availability for a live generation, without allocation.
 *  @note Status is OK when enabled, otherwise the execution refusal.
 *        Reason is static English text or NULL. Availability does not
 *        validate payloads or guarantee enough memory for execution. */
typedef struct {
    uint64_t generation;
    int enabled;
    tiku_richcmd_check_t check;
    tiku_rich_status_t status;
    const char *reason;
} tiku_richcmd_state_t;

/** @brief Typed command payload, borrowed for the duration of execution.
 *  @note Initialize the struct to zero and fill the member listed in the
 *        catalog. Character/paragraph formatting clears specified overrides
 *        before applying values. A list restart applies to the first
 *        selected paragraph; subsequent selected items continue the list.
 *        STYLE_DEFINE uses ID zero to create; other style IDs are local. */
typedef struct {
    tiku_richcmd_kind_t kind;
    union {
        struct { const char *data; size_t length; } text;
        const tiku_richfragment_t *fragment;
        const tiku_rich_image_t *image;
        tiku_rich_table_t table;
        uint32_t color;          /**< straight-alpha ARGB cell fill */
        struct { tiku_rich_chars_t values; unsigned clear; } character;
        struct {
            tiku_rich_para_t values;
            tiku_rich_chars_t chars;
            unsigned clear_props, clear_chars;
        } paragraph;
        uint64_t style;
        tiku_rich_list_t list;
        const char *link;
        struct { uint64_t style; tiku_rich_style_spec_t value; } definition;
    } value;
} tiku_richcmd_t;

/** @brief Number of implemented command catalog entries. */
size_t tiku_richcmd_count(void);
/** @brief Get a zero-based catalog entry, or NULL when out of range. */
const tiku_richcmd_info_t *tiku_richcmd_at(size_t index);
/** @brief Find a command identity, or NULL if unsupported. */
const tiku_richcmd_info_t *tiku_richcmd_info(tiku_richcmd_kind_t kind);
/** @brief Query a known command on the document's owner thread.
 *  @return OK with enabled/disabled state; invalid handles and unknown
 *          commands return an error and clear *out. Disabled state includes
 *          stale generations, invalid selections and host read-only state.
 *  @note Character commands at a caret are disabled: pending typing
 *        formatting belongs to a view/session, not the document. */
tiku_rich_status_t tiku_richcmd_query(const tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, tiku_richcmd_kind_t kind,
    tiku_richcmd_state_t *out);
/** @brief Execute a typed command as one atomic, undoable document change.
 *  @note Query rules are rechecked. Invalid payloads or allocation failures
 *        leave the document and history intact. Toggle commands turn a
 *        uniform explicit true off; false, absent and mixed values turn on.
 *        CLEAR_CHARACTER_OVERRIDES retains styles, links and defaults.
 *        Undo/redo exchange one existing group. Commands do not alter a
 *        host selection, open links, publish a clipboard or perform I/O. */
tiku_rich_status_t tiku_richcmd_execute(tiku_richdoc_t *doc,
    const tiku_richcmd_context_t *context, const tiku_richcmd_t *command);

#endif /* TIKU_RICHCMD_H_ */
