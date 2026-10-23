/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richdoc_maps.c - forward and inverse position transformations.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_richdoc_private.h"

/** @brief Map a boundary or interior position through byte replacement. */
static tiku_rich_pos_t
replace_position(tiku_rich_pos_t p, const tiku_rd_map_t *map, int forward)
{
    size_t removed = forward ? map->removed : map->inserted;
    size_t inserted = forward ? map->inserted : map->removed;
    size_t end = map->at + removed;

    if ((!map->flat && p.paragraph != map->first) || p.byte < map->at)
        return p;
    if (p.byte > end || (removed && p.byte == end))
        p.byte = p.byte - end + map->at + inserted;
    else
        p.byte = map->at + (p.affinity == TIKU_RICH_AFTER ? inserted : 0);
    return p;
}

/** @brief Convert a local position to an offset including separators. */
static size_t
flat_offset(const tiku_rd_root_t *root, tiku_rich_pos_t p)
{
    size_t i, offset = 0;

    for (i = 0; i < root->count; i++) {
        if (root->paragraphs[i]->value.id == p.paragraph)
            return offset + p.byte;
        offset += root->paragraphs[i]->value.length + 1;
    }
    return offset;
}

/** @brief Convert a valid flat offset back to a paragraph-local position. */
static tiku_rich_pos_t
local_position(const tiku_rd_root_t *root, tiku_rich_pos_t p)
{
    size_t i, length;

    for (i = 0; i < root->count; i++) {
        length = root->paragraphs[i]->value.length;
        if (p.byte <= length) {
            p.paragraph = root->paragraphs[i]->value.id;
            p.body = root->body;
            return p;
        }
        p.byte -= length + 1;
    }
    p.paragraph = 0;
    return p;
}

/** @brief Map a position across a paragraph boundary insertion or removal. */
static tiku_rich_pos_t
structure_position(tiku_rich_pos_t p, const tiku_rd_map_t *map, int split)
{
    if (split) {
        if (p.paragraph == map->first && (p.byte > map->at ||
            (p.byte == map->at && p.affinity == TIKU_RICH_AFTER))) {
            p.paragraph = map->second;
            p.byte -= map->at;
        }
    } else if (p.paragraph == map->second) {
        p.paragraph = map->first;
        p.byte += map->at;
    }
    return p;
}

tiku_rich_pos_t
tiku_rd_map_position(tiku_rich_pos_t position, const tiku_rd_map_t *maps,
                      size_t count, int forward, const tiku_rd_root_t *from,
                      const tiku_rd_root_t *to)
{
    size_t i;
    const tiku_rd_map_t *map;

    if (count && maps[0].flat) {
        position.byte = flat_offset(from, position);
        for (i = 0; i < count; i++)
            position = replace_position(position,
                &maps[forward ? i : count - i - 1], forward);
        return local_position(to, position);
    }
    for (i = 0; i < count; i++) {
        map = &maps[forward ? i : count - i - 1];
        if (map->kind == TIKU_RICH_TABLE_GRID) {
            position = tiku_rd_grid_position(position, map->first, from, to);
            continue;
        }
        if (map->kind == TIKU_RICH_REPLACE)
            position = replace_position(position, map, forward);
        else if (map->kind == TIKU_RICH_SPLIT || map->kind == TIKU_RICH_JOIN)
            position = structure_position(position, map,
                (map->kind == TIKU_RICH_SPLIT) == !!forward);
    }
    return position;
}
