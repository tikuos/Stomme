/*
 * KintsugiDesktop v0.01 Interface Kit tests.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * test_access.c - immutable semantics, Unicode offsets and hostile wire data.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_access.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** @brief Rewrite a little-endian 32-bit packet field. */
static void
put(unsigned char *p, unsigned value)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(value >> (8 * i));
}
/** @brief Require exact owned text/geometry and refuse malformed publications.
 */
int
main(void)
{
    char text[] = "Aα😀ב";
    tiku_rect_t boxes[] = {
        {1, 2, 3, 4}, {4, 2, 5, 4}, {9, 2, 6, 4}, {15, 2, 3, 4}};
    uint32_t lines[] = {5, 5, 6, 6};
    tiku_access_node_t nodes[] = {
        {.parent = TIKU_ACCESS_NO_PARENT,
         .role = TIKU_ACCESS_WINDOW,
         .name = "Editor",
         .selection_start = -1,
         .selection_end = -1,
         .caret = -1},
        {.parent = 0,
         .role = TIKU_ACCESS_HEADING,
         .text = text,
         .state = TIKU_ACCESS_EDITABLE | TIKU_ACCESS_FOCUSED,
         .bounds = {1, 2, 17, 4},
         .caret = 2,
         .selection_start = 1,
         .selection_end = 3,
         .body = 1,
         .paragraph = 2,
         .character_bounds = boxes,
         .character_lines = lines,
         .character_count = 4}};
    tiku_access_tree_t *tree = NULL, *copy = NULL;
    assert(tiku_access_characters(text, strlen(text)) == 4 &&
           tiku_access_byte(text, 3) == 7);
    assert(tiku_access_byte(text, 5) < 0 &&
           tiku_access_characters("\xc0\xaf", 2) < 0);
    assert(tiku_access_create(17, 3, nodes, 2, &tree) == 0);
    text[0] = 'B';
    boxes[0].x = 99;
    assert(tiku_access_node(tree, 1)->text[0] == 'A' &&
           tiku_access_node(tree, 1)->character_bounds[0].x == 1);
    void *data;
    size_t size;
    assert(!tiku_access_encode(tree, &data, &size) &&
           !tiku_access_decode(data, size, &copy));
    assert(tiku_access_document(copy) == 17 &&
           tiku_access_generation(copy) == 3 && tiku_access_count(copy) == 2);
    assert(!strcmp(tiku_access_node(copy, 1)->text, "Aα😀ב") &&
           tiku_access_node(copy, 1)->caret == 2 &&
           tiku_access_node(copy, 1)->character_lines[2] == 6);
    tiku_access_release(copy);
    copy = NULL;
    unsigned char *packet = data;
    for (size_t n = 0; n < size; n++)
        assert(tiku_access_decode(data, n, &copy) < 0 && !copy);
    unsigned saved = packet[32];
    put(packet + 32, 0);
    assert(tiku_access_decode(data, size, &copy) < 0 && !copy);
    put(packet + 32, saved | 0xffffff00u);
    /* Offsets must follow the canonical bounded packet traversal. */
    unsigned original = packet[32 + 32] | (unsigned)packet[32 + 33] << 8 |
                        (unsigned)packet[32 + 34] << 16 |
                        (unsigned)packet[32 + 35] << 24;
    unsigned name = packet[32 + 28] | (unsigned)packet[32 + 29] << 8 |
                    (unsigned)packet[32 + 30] << 16 |
                    (unsigned)packet[32 + 31] << 24;
    put(packet + 32 + 32, name);
    assert(tiku_access_decode(data, size, &copy) < 0 && !copy);
    put(packet + 32 + 32, original);
    assert(!tiku_access_decode(data, size, &copy));
    tiku_access_release(copy);
    free(data);
    tiku_access_release(tree);
    nodes[1].parent = 1;
    assert(tiku_access_create(17, 3, nodes, 2, &tree) < 0 && !tree);
    puts("C4 retained hierarchy, scalar offsets, glyph bounds and hostile "
         "packets PASS");
    return 0;
}
