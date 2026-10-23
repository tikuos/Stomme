/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
struct tiku_access_tree {
    atomic_size_t refs;
    uint64_t document, generation;
    size_t count;
    tiku_access_node_t *nodes;
    char *strings;
    tiku_rect_t *boxes;
};
/** @brief Decode one scalar without reading beyond the supplied length. */
static int
scalar(const unsigned char *s, size_t n, size_t *used)
{
    if (!n || !s[0])
        return -1;
    unsigned c = s[0], extra, minimum;
    if (c < 128) {
        *used = 1;
        return c;
    }
    if (c >= 0xc2 && c <= 0xdf) {
        extra = 1;
        c &= 31;
        minimum = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
        extra = 2;
        c &= 15;
        minimum = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
        extra = 3;
        c &= 7;
        minimum = 65536;
    } else
        return -1;
    *used = extra + 1;
    if (*used > n)
        return -1;
    for (unsigned i = 1; i <= extra; i++) {
        if ((s[i] & 0xc0) != 0x80)
            return -1;
        c = (c << 6) | (s[i] & 63);
    }
    return c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ? -1
                                                                       : (int)c;
}
int
tiku_access_characters(const char *text, size_t bytes)
{
    if (!text && bytes)
        return -1;
    size_t at = 0;
    int count = 0;
    while (at < bytes) {
        size_t used;
        if (scalar((const unsigned char *)text + at, bytes - at, &used) < 0 ||
            count == INT_MAX)
            return -1;
        at += used;
        count++;
    }
    return count;
}
long
tiku_access_byte(const char *text, size_t characters)
{
    if (!text)
        return characters ? -1 : 0;
    size_t at = 0, bytes = strlen(text);
    for (size_t i = 0; i < characters; i++) {
        size_t used;
        if (scalar((const unsigned char *)text + at, bytes - at, &used) < 0)
            return -1;
        at += used;
    }
    return at > LONG_MAX ? -1 : (long)at;
}
/** @brief Require coordinates whose unions remain in the native integer range.
 */
static int
rectangle_valid(tiku_rect_t box)
{
    return box.x >= -100000000 && box.x <= 100000000 && box.y >= -100000000 &&
           box.y <= 100000000 && box.w >= 0 && box.w <= 200000000 &&
           box.h >= 0 && box.h <= 200000000;
}
/** @brief Validate one node and reserve the copied string pool. */
static int
check(const tiku_access_node_t *node, size_t index, size_t *bytes)
{
    if (node->role > TIKU_ACCESS_BUTTON || (node->state & ~63u) ||
        (index ? node->parent >= index
               : node->parent != TIKU_ACCESS_NO_PARENT) ||
        !rectangle_valid(node->bounds) || node->first_byte > node->last_byte ||
        node->row > INT_MAX || node->column > INT_MAX || node->rows > INT_MAX ||
        node->columns > INT_MAX ||
        (node->role == TIKU_ACCESS_TABLE &&
         (uint64_t)node->rows * node->columns > INT_MAX) ||
        node->selection_start < -1 || node->selection_end < -1 ||
        node->caret < -1)
        return -1;
    const char *strings[] = {node->name, node->text, node->description,
                             node->target};
    int characters = 0;
    for (unsigned i = 0; i < 4; i++) {
        size_t length =
            strings[i] ? strnlen(strings[i], TIKU_ACCESS_BYTES_MAX + 1u) : 0;
        if (length >= TIKU_ACCESS_BYTES_MAX ||
            length + 1 > TIKU_ACCESS_BYTES_MAX - *bytes)
            return -3;
        int count = tiku_access_characters(strings[i], length);
        if (count < 0)
            return -1;
        if (i == 1)
            characters = count;
        *bytes += length + 1;
    }
    if (node->caret > characters || node->selection_start > characters ||
        node->selection_end > characters ||
        ((node->selection_start == -1) != (node->selection_end == -1)) ||
        (node->selection_start >= 0 &&
         node->selection_end < node->selection_start))
        return -1;
    if ((node->character_count && !node->character_bounds) ||
        (node->character_count &&
         node->character_count != (uint32_t)characters))
        return -1;
    if (node->character_count > (TIKU_ACCESS_BYTES_MAX - *bytes) /
                                    (sizeof(tiku_rect_t) + sizeof(uint32_t)))
        return -3;
    *bytes += node->character_count * (sizeof(tiku_rect_t) + sizeof(uint32_t));
    for (uint32_t i = 0; i < node->character_count; i++)
        if (node->character_bounds[i].w < 0 ||
            node->character_bounds[i].h < 0 ||
            (node->character_lines && node->character_lines[i] == UINT32_MAX))
            return -1;
    return 0;
}
int
tiku_access_create(uint64_t document, uint64_t generation,
                   const tiku_access_node_t *nodes, size_t count,
                   tiku_access_tree_t **out)
{
    if (!out)
        return -1;
    *out = NULL;
    if (!document || !generation || !nodes || !count)
        return -1;
    if (count > TIKU_ACCESS_NODES_MAX)
        return -3;
    size_t bytes = sizeof(tiku_access_tree_t) + count * sizeof *nodes;
    if (bytes > TIKU_ACCESS_BYTES_MAX)
        return -3;
    size_t base = bytes, box_count = 0;
    for (size_t i = 0; i < count; i++) {
        int status = check(nodes + i, i, &bytes);
        if (status)
            return status;
        if (nodes[i].role == TIKU_ACCESS_TABLE &&
            (!nodes[i].rows || !nodes[i].columns))
            return -1;
        if (nodes[i].role == TIKU_ACCESS_CELL) {
            if (!i || nodes[i].parent == TIKU_ACCESS_NO_PARENT)
                return -1;
            const tiku_access_node_t *parent = nodes + nodes[i].parent;
            if (parent->role != TIKU_ACCESS_TABLE ||
                nodes[i].row >= parent->rows ||
                nodes[i].column >= parent->columns ||
                nodes[i].rows > parent->rows - nodes[i].row ||
                nodes[i].columns > parent->columns - nodes[i].column ||
                ((nodes[i].state & TIKU_ACCESS_VISIBLE) &&
                 (!nodes[i].rows || !nodes[i].columns)))
                return -1;
        }
        box_count += nodes[i].character_count;
    }
    tiku_access_tree_t *tree = calloc(1, sizeof *tree);
    if (!tree)
        return -2;
    tree->nodes = malloc(count * sizeof *nodes);
    tree->strings = malloc(
        bytes - base - box_count * (sizeof(tiku_rect_t) + sizeof(uint32_t)));
    tree->boxes =
        box_count ? malloc(box_count * (sizeof(tiku_rect_t) + sizeof(uint32_t)))
                  : NULL;
    if (!tree->nodes || !tree->strings || (box_count && !tree->boxes)) {
        free(tree->nodes);
        free(tree->strings);
        free(tree->boxes);
        free(tree);
        return -2;
    }
    atomic_init(&tree->refs, 1);
    tree->count = count;
    tree->document = document;
    tree->generation = generation;
    char *next = tree->strings;
    tiku_rect_t *next_box = tree->boxes;
    uint32_t *next_line =
        box_count ? (uint32_t *)(tree->boxes + box_count) : NULL;
    for (size_t i = 0; i < count; i++) {
        tree->nodes[i] = nodes[i];
        tree->nodes[i].character_bounds =
            nodes[i].character_count ? next_box : NULL;
        tree->nodes[i].character_lines =
            nodes[i].character_lines ? next_line : NULL;
        if (nodes[i].character_count) {
            if (nodes[i].character_lines)
                memcpy(next_line, nodes[i].character_lines,
                       nodes[i].character_count * sizeof *next_line);
            next_line += nodes[i].character_count;
            memcpy(next_box, nodes[i].character_bounds,
                   nodes[i].character_count * sizeof *next_box);
            next_box += nodes[i].character_count;
        }
        const char *source[] = {nodes[i].name, nodes[i].text,
                                nodes[i].description, nodes[i].target};
        const char **target[] = {&tree->nodes[i].name, &tree->nodes[i].text,
                                 &tree->nodes[i].description,
                                 &tree->nodes[i].target};
        for (unsigned j = 0; j < 4; j++) {
            size_t length = source[j] ? strlen(source[j]) : 0;
            if (length)
                memcpy(next, source[j], length);
            next[length] = 0;
            *target[j] = next;
            next += length + 1;
        }
    }
    *out = tree;
    return 0;
}
void
tiku_access_retain(tiku_access_tree_t *tree)
{
    if (tree)
        atomic_fetch_add_explicit(&tree->refs, 1, memory_order_relaxed);
}
void
tiku_access_release(tiku_access_tree_t *tree)
{
    if (tree &&
        atomic_fetch_sub_explicit(&tree->refs, 1, memory_order_acq_rel) == 1) {
        free(tree->nodes);
        free(tree->strings);
        free(tree->boxes);
        free(tree);
    }
}
size_t
tiku_access_count(const tiku_access_tree_t *tree)
{
    return tree ? tree->count : 0;
}
const tiku_access_node_t *
tiku_access_node(const tiku_access_tree_t *tree, size_t index)
{
    return tree && index < tree->count ? tree->nodes + index : NULL;
}
uint64_t
tiku_access_document(const tiku_access_tree_t *tree)
{
    return tree ? tree->document : 0;
}
uint64_t
tiku_access_generation(const tiku_access_tree_t *tree)
{
    return tree ? tree->generation : 0;
}

int
tiku_access_showing(const tiku_access_tree_t *tree, size_t index)
{
    const tiku_access_node_t *node = tiku_access_node(tree, index),
                             *root = tiku_access_node(tree, 0);
    if (!node || !(node->state & TIKU_ACCESS_VISIBLE))
        return 0;
    return (int64_t)node->bounds.x + node->bounds.w > root->bounds.x &&
           (int64_t)node->bounds.y + node->bounds.h > root->bounds.y &&
           (int64_t)root->bounds.x + root->bounds.w > node->bounds.x &&
           (int64_t)root->bounds.y + root->bounds.h > node->bounds.y;
}
