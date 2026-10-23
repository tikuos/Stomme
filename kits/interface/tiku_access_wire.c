/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_access_wire.c - validated semantic publication without native pointers.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include <stdlib.h>
#include <string.h>
#define HEADER 32u
#define RECORD 112u
/** @brief Write one little-endian integer without alignment assumptions. */
static void
put(unsigned char *p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++)
        p[i] = (unsigned char)(value >> (8 * i));
}
/** @brief Read one little-endian integer from a validated fixed record. */
static uint64_t
get(const unsigned char *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++)
        value |= (uint64_t)p[i] << (8 * i);
    return value;
}
/** @brief Encode a logical rectangle. */
static void
rect_put(unsigned char *p, tiku_rect_t rect)
{
    put(p, (uint32_t)rect.x, 4);
    put(p + 4, (uint32_t)rect.y, 4);
    put(p + 8, (uint32_t)rect.w, 4);
    put(p + 12, (uint32_t)rect.h, 4);
}
/** @brief Decode a logical rectangle. */
static tiku_rect_t
rect_get(const unsigned char *p)
{
    return (tiku_rect_t){(int32_t)get(p, 4), (int32_t)get(p + 4, 4),
                         (int32_t)get(p + 8, 4), (int32_t)get(p + 12, 4)};
}
int
tiku_access_encode(const tiku_access_tree_t *tree, void **bytes, size_t *size)
{
    if (!bytes || !size)
        return -1;
    *bytes = NULL;
    *size = 0;
    size_t count = tiku_access_count(tree);
    if (!count || count > TIKU_ACCESS_NODES_MAX)
        return -1;
    size_t total = HEADER + RECORD * count;
    for (size_t i = 0; i < count; i++) {
        const tiku_access_node_t *node = tiku_access_node(tree, i);
        const char *strings[] = {node->name, node->text, node->description,
                                 node->target};
        for (unsigned j = 0; j < 4; j++) {
            size_t n = strlen(strings[j]) + 1;
            if (n > TIKU_ACCESS_BYTES_MAX - total)
                return -3;
            total += n;
        }
        if (node->character_count > (TIKU_ACCESS_BYTES_MAX - total) / 20u)
            return -3;
        total += 20u * node->character_count;
    }
    unsigned char *packet = calloc(1, total);
    if (!packet)
        return -2;
    memcpy(packet, "TA12", 4);
    put(packet + 4, 2, 4);
    put(packet + 8, count, 4);
    put(packet + 12, total, 4);
    put(packet + 16, tiku_access_document(tree), 8);
    put(packet + 24, tiku_access_generation(tree), 8);
    size_t next = HEADER + count * RECORD;
    for (size_t i = 0; i < count; i++) {
        unsigned char *p = packet + HEADER + i * RECORD;
        const tiku_access_node_t *n = tiku_access_node(tree, i);
        put(p, n->parent, 4);
        put(p + 4, n->role, 4);
        put(p + 8, n->state, 4);
        rect_put(p + 12, n->bounds);
        const char *strings[] = {n->name, n->text, n->description, n->target};
        for (unsigned j = 0; j < 4; j++) {
            size_t length = strlen(strings[j]) + 1;
            put(p + 28 + j * 4, next, 4);
            memcpy(packet + next, strings[j], length);
            next += length;
        }
        put(p + 44, (uint32_t)n->selection_start, 4);
        put(p + 48, (uint32_t)n->selection_end, 4);
        put(p + 52, (uint32_t)n->caret, 4);
        put(p + 56, n->row, 4);
        put(p + 60, n->column, 4);
        put(p + 64, n->rows, 4);
        put(p + 68, n->columns, 4);
        put(p + 72, n->owner, 8);
        put(p + 80, n->body, 8);
        put(p + 88, n->paragraph, 8);
        put(p + 96, n->first_byte, 4);
        put(p + 100, n->last_byte, 4);
        put(p + 104, n->character_count, 4);
        put(p + 108, next, 4);
        for (uint32_t k = 0; k < n->character_count; k++, next += 20) {
            rect_put(packet + next, n->character_bounds[k]);
            put(packet + next + 16,
                n->character_lines ? n->character_lines[k] : UINT32_MAX, 4);
        }
    }
    *bytes = packet;
    *size = total;
    return 0;
}
int
tiku_access_decode(const void *bytes, size_t size, tiku_access_tree_t **out)
{
    if (!out)
        return -1;
    *out = NULL;
    if (!bytes || size < HEADER || size > TIKU_ACCESS_BYTES_MAX)
        return -1;
    const unsigned char *packet = bytes;
    size_t count = get(packet + 8, 4), floor = HEADER + count * RECORD;
    if (memcmp(packet, "TA12", 4) || get(packet + 4, 4) != 2 ||
        get(packet + 12, 4) != size || !count ||
        count > TIKU_ACCESS_NODES_MAX || floor > size)
        return -1;
    tiku_access_node_t *nodes = calloc(count, sizeof *nodes);
    if (!nodes)
        return -2;
    int status = -1;
    size_t next = floor, allocated = count * sizeof *nodes;
    for (size_t i = 0; i < count; i++) {
        const unsigned char *p = packet + HEADER + i * RECORD;
        tiku_access_node_t *n = nodes + i;
        n->parent = get(p, 4);
        n->role = get(p + 4, 4);
        n->state = get(p + 8, 4);
        n->bounds = rect_get(p + 12);
        const char **strings[] = {&n->name, &n->text, &n->description,
                                  &n->target};
        for (unsigned j = 0; j < 4; j++) {
            size_t at = get(p + 28 + j * 4, 4);
            if (at != next || at >= size)
                goto done;
            const unsigned char *end = memchr(packet + at, 0, size - at);
            if (!end)
                goto done;
            next = (size_t)(end - packet) + 1;
            *strings[j] = (const char *)packet + at;
        }
        n->selection_start = (int32_t)get(p + 44, 4);
        n->selection_end = (int32_t)get(p + 48, 4);
        n->caret = (int32_t)get(p + 52, 4);
        n->row = get(p + 56, 4);
        n->column = get(p + 60, 4);
        n->rows = get(p + 64, 4);
        n->columns = get(p + 68, 4);
        n->owner = get(p + 72, 8);
        n->body = get(p + 80, 8);
        n->paragraph = get(p + 88, 8);
        n->first_byte = get(p + 96, 4);
        n->last_byte = get(p + 100, 4);
        n->character_count = get(p + 104, 4);
        size_t at = get(p + 108, 4);
        if (at != next || at > size || n->character_count > (size - at) / 20u)
            goto done;
        if (n->character_count > (TIKU_ACCESS_BYTES_MAX - allocated) /
                                     (sizeof(tiku_rect_t) + sizeof(uint32_t))) {
            status = -3;
            goto done;
        }
        allocated +=
            n->character_count * (sizeof(tiku_rect_t) + sizeof(uint32_t));
        next += n->character_count * 20u;
        if (n->character_count) {
            tiku_rect_t *boxes =
                malloc(n->character_count * (sizeof *boxes + sizeof(uint32_t)));
            if (!boxes) {
                status = -2;
                goto done;
            }
            n->character_bounds = boxes;
            uint32_t *lines = (uint32_t *)(boxes + n->character_count);
            int present = get(packet + at + 16, 4) != UINT32_MAX;
            n->character_lines = present ? lines : NULL;
            for (uint32_t k = 0; k < n->character_count; k++) {
                boxes[k] = rect_get(packet + at + k * 20u);
                lines[k] = get(packet + at + k * 20u + 16, 4);
                if ((lines[k] != UINT32_MAX) != present)
                    goto done;
            }
        }
    }
    if (next != size)
        goto done;
    status = tiku_access_create(get(packet + 16, 8), get(packet + 24, 8), nodes,
                                count, out);
done:
    for (size_t i = 0; i < count; i++)
        free((void *)nodes[i].character_bounds);
    free(nodes);
    return status;
}
