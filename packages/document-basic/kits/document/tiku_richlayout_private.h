/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout_private.h - retained layout arrays and incremental cursors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHLAYOUT_PRIVATE_H_
#define TIKU_RICHLAYOUT_PRIVATE_H_

#include "tiku_richlayout.h"
#include "tiku_richdoc_private.h"
#include "tiku_richtext_private.h"

/** @brief Special cluster classifications used by line flow and hit testing. */
enum { RL_SPACE = 1, RL_TAB = 2, RL_BREAK = 4, RL_WRAP = 8 };
/** @brief Work phases, each advancing one bounded input or output unit. */
enum { RL_MINIMUM, RL_PREPARE, RL_BOUNDARIES, RL_SHAPE, RL_FLOW,
       RL_METRICS, RL_PLACE, RL_CACHE_STYLES, RL_CACHE_INDEX,
       RL_REUSE_GLYPHS, RL_REUSE_CARETS, RL_REUSE_CLUSTERS, RL_REUSE_LINES };
/** @brief First geometry indices of a paragraph, with one final sentinel. */
typedef struct {
    size_t cluster, line, glyph, caret;
    double x, width, top, bottom; /**< owner interval and paragraph extent */
    double list_offset;       /**< list label and level space before text */
    uint64_t owner;           /**< immediate cell, or the document body */
    unsigned warnings;       /**< diagnostics belonging to this paragraph */
} tiku_rl_paragraph_t;
/** @brief One open table/cell during depth-first incremental line flow. */
typedef struct {
    tiku_rich_block_t node;    /**< immutable structural properties */
    size_t grid, cells;       /**< retained box index and opened cell count */
    size_t row_boxes[64];     /**< cell box indices in the current row */
    double x, width;          /**< outer horizontal interval */
    double row_top, row_bottom; /**< tallest content in the current row */
} tiku_rl_container_t;
/** @brief Incremental layout construction state retaining immutable content. */
struct tiku_richlayout {
    atomic_size_t refs;        /**< callers and pending cache consumers */
    tiku_richsnapshot_t source; /**< embedded retained immutable source */
    tiku_richlayout_options_t options; /**< checked settings and provider */
    tiku_rich_status_t status; /**< pending, complete or sticky error */
    unsigned warnings;        /**< accumulated visible-layout limitations */
    size_t used;              /**< layout allocation charge */
    tiku_rl_paragraph_t *paragraphs; /**< paragraph geometry ranges */
    tiku_rich_cluster_t *clusters; /**< shaped and positioned clusters */
    size_t nclusters, cclusters; /**< used/capacity */
    tiku_rich_line_t *lines;   /**< positioned visual lines */
    size_t nlines, clines;     /**< used/capacity */
    tiku_rich_glyph_t *glyphs; /**< provider glyph identities and offsets */
    size_t nglyphs, cglyphs;   /**< used/capacity */
    tiku_rich_shape_caret_t *carets; /**< cluster-relative caret boundaries */
    size_t ncarets, ccarets;   /**< used/capacity */
    uint8_t *boundaries;      /**< current paragraph's grapheme boundaries */
    tiku_rt_grapheme_t grapheme; /**< incremental boundary state */
    size_t paragraph, byte;   /**< current source position */
    size_t run, run_end;      /**< current character formatting run */
    size_t shape_end;         /**< cached provider input endpoint */
    size_t paragraph_start;   /**< first cluster of current paragraph */
    size_t line_start;        /**< next unpositioned cluster */
    int phase;                /**< prepare, boundaries, shape or flow */
    int tail_line;            /**< empty line after trailing soft newline */
    size_t flow, end, wrap;   /**< flow scan, selected end and wrap candidate */
    double width;            /**< scanned advance */
    double left, available;  /**< current line's horizontal interval */
    double top, bottom;      /**< largest ascent/descent on this line */
    double x, justify;       /**< placement cursor and added space width */
    size_t spaces;           /**< stretchable spaces on the current line */
    uint64_t numbers[9];     /**< list counters by level */
    tiku_rich_list_kind_t kinds[9]; /**< current list kinds by level */
    char marker[32];         /**< current paragraph's list marker */
    size_t marker_glyph, marker_count; /**< current label's glyph array slice */
    double marker_width;     /**< shaped label advance */
    tiku_rich_para_t props;   /**< current resolved paragraph settings */
    tiku_rich_chars_t defaults; /**< current paragraph's resolved defaults */
    double ascent, descent;   /**< empty-line metrics */
    double y;                 /**< next line's document coordinate */
    double content_x, content_width; /**< active paragraph's owner interval */
    tiku_rl_container_t *containers; /**< at most eight nested tables */
    size_t depth, node;       /**< open container count and preorder cursor */
    tiku_rich_grid_box_t *grids; /**< retained table/cell rectangles */
    size_t ngrids, cgrids;    /**< used and allocated grid boxes */
    double *minimums;         /**< minimum outer width by structural node */
    size_t min_node, min_paragraph, min_run; /**< intrinsic-width cursors */
    size_t min_stack[16], min_depth; /**< open intrinsic-width containers */
    size_t min_cells[16];      /**< closed direct cells per width container */
    const tiku_richlayout_t *previous; /**< completed cache source, retained */
    size_t *cache_index;       /**< paragraph-ID hash slots, index plus one */
    size_t cache_capacity, cache_cursor; /**< hash capacity and work cursor */
    size_t cache_paragraph;    /**< source paragraph being copied */
    size_t reused;             /**< completed cached paragraphs */
    int cache_next_phase;      /**< initial phase after cache preparation */
};

/** @brief Start a job with an optional retained completed layout as cache. */
tiku_rich_status_t tiku_rl_cache_attach(tiku_richlayout_t *layout,
                                       const tiku_richlayout_t *previous);
/** @brief Compare one style or index one old paragraph for reuse. */
tiku_rich_status_t tiku_rl_cache_prepare(tiku_richlayout_t *layout);
/** @brief Begin copying matching paragraph geometry, returning one on reuse. */
int tiku_rl_cache_begin(tiku_richlayout_t *layout);
/** @brief Copy one cached glyph, caret, cluster or line. */
tiku_rich_status_t tiku_rl_cache_step(tiku_richlayout_t *layout);
/** @brief Release a job's cache reference and hash-index storage. */
void tiku_rl_cache_drop(tiku_richlayout_t *layout);
/** @brief Record one paragraph's diagnostics and accumulated layout warnings. */
void tiku_rl_warn(tiku_richlayout_t *layout, unsigned warnings);
/** @brief Finish paragraph geometry and advance or close structural scopes. */
void tiku_rl_paragraph_done(tiku_richlayout_t *layout);

/** @brief Measure one structural node or image run for minimum table widths. */
tiku_rich_status_t tiku_rl_minimum(tiku_richlayout_t *layout);

/** @brief Advance structural scopes to the next paragraph, or close at EOF. */
tiku_rich_status_t tiku_rl_containers(tiku_richlayout_t *layout, int finish);
/** @brief Select the nearest visual line, including table cell ownership. */
size_t tiku_rl_hit_line(const tiku_richlayout_t *layout, double x, double y);

/** @brief Allocate layout-owned storage with local and aggregate ceilings. */
void *tiku_rl_alloc(tiku_richlayout_t *layout, size_t bytes);
/** @brief Release layout storage and subtract its charge. */
void tiku_rl_free(tiku_richlayout_t *layout, void *pointer);
/** @brief Grow one array under checked capacity and peak-allocation limits. */
int tiku_rl_grow(tiku_richlayout_t *layout, void **array, size_t *capacity,
                  size_t needed, size_t item);
/** @brief Shape and append one cluster from the current paragraph. */
tiku_rich_status_t tiku_rl_shape(tiku_richlayout_t *layout);
/** @brief Place one line and advance the paragraph flow state. */
tiku_rich_status_t tiku_rl_line(tiku_richlayout_t *layout);
/** @brief Begin line flow for the current paragraph or its next visual line. */
void tiku_rl_flow_start(tiku_richlayout_t *layout);
/** @brief Validate a provider result against consumed grapheme boundaries. */
int tiku_rl_valid_shape(const tiku_rich_shape_t *shape,
                       const uint8_t *boundaries, size_t length);
/** @brief Shape the bounded list label and retain its glyph identities. */
tiku_rich_status_t tiku_rl_marker(tiku_richlayout_t *layout);
/** @brief Find the x coordinate of a grapheme caret on a specified line. */
tiku_rich_status_t tiku_rl_x(const tiku_richlayout_t *layout,
    const tiku_rich_line_t *line, size_t byte, double *out);

#endif /* TIKU_RICHLAYOUT_PRIVATE_H_ */
