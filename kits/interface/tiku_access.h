/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ACCESS_H_
#define TIKU_ACCESS_H_
#include "tiku_gfx.h"
#include <stddef.h>
#include <stdint.h>
#define TIKU_ACCESS_NODES_MAX 65536u
#define TIKU_ACCESS_BYTES_MAX (64u * 1024u * 1024u)
#define TIKU_ACCESS_NO_PARENT UINT32_MAX
/** @brief Semantic roles, independent of the native accessibility API. */
typedef enum {
    TIKU_ACCESS_WINDOW,
    TIKU_ACCESS_DOCUMENT,
    TIKU_ACCESS_BODY,
    TIKU_ACCESS_TABLE,
    TIKU_ACCESS_CELL,
    TIKU_ACCESS_PARAGRAPH,
    TIKU_ACCESS_HEADING,
    TIKU_ACCESS_LINK,
    TIKU_ACCESS_IMAGE,
    TIKU_ACCESS_SLIDE,
    TIKU_ACCESS_BUTTON
} tiku_access_role_t;
/** @brief State flags captured with the interaction geometry. */
enum {
    TIKU_ACCESS_VISIBLE = 1u,
    TIKU_ACCESS_FOCUSED = 2u,
    TIKU_ACCESS_EDITABLE = 4u,
    TIKU_ACCESS_SELECTED = 8u,
    TIKU_ACCESS_DISABLED = 16u,
    TIKU_ACCESS_ACTIONABLE = 32u /**< exposes a native content action */
};
/** @brief One preorder node; strings are borrowed from the input or tree.
 * Parent precedes the node, except the root's NO_PARENT. Text offsets count
 * Unicode scalars; negative selection/caret values mean absent. Source IDs
 * and byte ranges are opaque to the host and returned with explicit actions. */
typedef struct {
    uint32_t parent, role, state;
    tiku_rect_t bounds;
    const char *name, *text, *description, *target;
    int32_t selection_start, selection_end, caret;
    uint32_t row, column, rows, columns;
    uint64_t owner, body, paragraph;
    uint32_t first_byte, last_byte;
    const tiku_rect_t *character_bounds; /**< scalar boxes, or NULL */
    const uint32_t
        *character_lines;     /**< optional retained layout line per scalar */
    uint32_t character_count; /**< zero without scalar boxes */
} tiku_access_node_t;
typedef struct tiku_access_tree tiku_access_tree_t;
/** @brief Build an immutable owned tree, validating preorder, Unicode and
 * ranges. Returns zero, -1 invalid, -2 memory refusal or -3 profile limit.
 * Failure clears out. Empty trees are refused; source strings need only survive
 * call. */
int tiku_access_create(uint64_t document, uint64_t generation,
                       const tiku_access_node_t *nodes, size_t count,
                       tiku_access_tree_t **out);
/** @brief Hold a tree on any thread. */
void tiku_access_retain(tiku_access_tree_t *tree);
/** @brief Release one hold; NULL is allowed. */
void tiku_access_release(tiku_access_tree_t *tree);
/** @brief Number of logical nodes, zero for NULL. */
size_t tiku_access_count(const tiku_access_tree_t *tree);
/** @brief Borrow a node until the tree is released; NULL outside range. */
const tiku_access_node_t *tiku_access_node(const tiku_access_tree_t *tree,
                                           size_t index);
/** @brief Whether a visible node intersects its publication's root viewport. */
int tiku_access_showing(const tiku_access_tree_t *tree, size_t index);

/** @brief Document identity token, zero for NULL. */
uint64_t tiku_access_document(const tiku_access_tree_t *tree);
/** @brief Publication generation, zero for NULL. */
uint64_t tiku_access_generation(const tiku_access_tree_t *tree);
/** @brief Count shortest-form UTF-8 scalars; -1 for invalid UTF-8 or NUL within
 * length. */
int tiku_access_characters(const char *text, size_t bytes);
/** @brief Byte position at a scalar offset; -1 on an invalid offset/input. */
long tiku_access_byte(const char *text, size_t characters);
/** @brief Encode a versioned little-endian tree; caller frees bytes. */
int tiku_access_encode(const tiku_access_tree_t *tree, void **bytes,
                       size_t *size);
/** @brief Decode a bounded packet, validating offsets before copying strings.
 */
int tiku_access_decode(const void *bytes, size_t size,
                       tiku_access_tree_t **out);
/** @brief Explicit semantic action; arguments are scalar offsets. */
enum {
    TIKU_ACCESS_FOCUS = 1,
    TIKU_ACCESS_SELECT,
    TIKU_ACCESS_REPLACE,
    TIKU_ACCESS_ACTIVATE,
    TIKU_ACCESS_INSERT,
    TIKU_ACCESS_DELETE
};
/** @brief Action on the currently published tree, with an optional UTF-8
 * payload. */
typedef int (*tiku_access_action_fn)(void *context, uint32_t node,
                                     uint64_t document, uint64_t generation,
                                     unsigned action, int first, int last,
                                     const char *text);
#endif
