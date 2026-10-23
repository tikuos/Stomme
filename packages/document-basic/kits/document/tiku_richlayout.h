/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_richlayout.h - bounded continuous layout and shared text geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RICHLAYOUT_H_
#define TIKU_RICHLAYOUT_H_

#include "tiku_richdoc.h"

/** @brief Incrementally constructed immutable layout retaining its source. */
typedef struct tiku_richlayout tiku_richlayout_t;
/** @brief One glyph positioned relative to a cluster's baseline origin. */
typedef struct {
    uint32_t font, glyph;       /**< provider-local font and glyph identities */
    double x, y;               /**< offsets in points; y increases downward */
} tiku_rich_glyph_t;
/** @brief A logical grapheme boundary within a shaped cluster. */
typedef struct {
    size_t byte;               /**< offset in the provider's input */
    double x;                  /**< offset in points from cluster start */
} tiku_rich_shape_caret_t;
/** @brief Bounded shaping result in document points, without borrowed arrays.
 *  @note Consume one or more complete graphemes. Carets cover each grapheme
 *        boundary, from byte/x zero through bytes/advance. This profile is
 *        left-to-right; unsupported shaping must be reported in warnings. */
typedef struct {
    size_t bytes, glyph_count, caret_count;
    double advance, ascent, descent;
    tiku_rich_glyph_t glyphs[32];
    tiku_rich_shape_caret_t carets[34];
    unsigned warnings;
} tiku_rich_shape_t;
/** @brief Layout diagnostics; text and stored formatting remain unchanged. */
enum {
    TIKU_RICH_LAYOUT_FALLBACK = 1u,
    TIKU_RICH_LAYOUT_SHAPING = 2u,
    TIKU_RICH_LAYOUT_OVERFLOW = 4u,
    TIKU_RICH_LAYOUT_CLUSTER_STYLE = 8u
};
/** @brief Font provider copied by the layout, without a format-kit dependency.
 *  @note Shape receives valid UTF-8, resolved/defaulted values and one flag
 *        byte per input byte plus its endpoint. Bit 0 denotes a grapheme
 *        boundary. The provider must return promptly and not reenter layout.
 *        Optional retain/release must be supplied together. Otherwise context
 *        must outlive layouts. Workers require an independent safe context. */
typedef struct {
    void *context;
    tiku_rich_status_t (*shape)(void *context, const tiku_rich_chars_t *chars,
        const char *text, size_t length, const uint8_t *boundaries,
        tiku_rich_shape_t *out);
    void (*retain)(void *context);
    void (*release)(void *context);
    uint64_t generation;       /**< host changes this when fonts change */
} tiku_rich_provider_t;
/** @brief Continuous layout settings; zero ceilings select defaults.
 *  @note Width is 1..1,000,000 points. Default font size is 12 points.
 *        Layout storage also counts against the source allocation owner.
 *        Wrapping uses spaces, hyphens and CJK opportunities with grapheme
 *        fallback; this is not a full Unicode line-breaking profile. */
typedef struct {
    double width;
    tiku_rich_chars_t defaults;
    double leading;            /**< additional line height, 0..4096 points */
    double tab;                /**< tab spacing; zero selects 48 points */
    size_t memory_bytes;       /**< default 128 MiB, includes temporary data */
    size_t lines;              /**< default 1,000,000 */
    tiku_rich_provider_t provider;
} tiku_richlayout_options_t;
/** @brief Document-point rectangle. */
typedef struct { double x, y, width, height; } tiku_rich_rect_t;
/** @brief A table or cell box in retained document-point geometry.
 *  @note Lines remain in document traversal order, not sorted vertically
 *        across columns. Top-level tables have depth zero. */
typedef struct {
    uint64_t id;
    tiku_rich_block_kind_t kind;
    tiku_rich_rect_t box;
    float border;
    uint32_t color;
    uint32_t background;       /**< straight-alpha ARGB cell fill */
    size_t line, line_count;
    unsigned depth;
} tiku_rich_grid_box_t;
/** @brief Number of table/cell boxes, or zero before layout completes. */
size_t tiku_richlayout_grids(const tiku_richlayout_t *layout);
/** @brief Read a table/cell box, clearing *out on failure. */
tiku_rich_status_t tiku_richlayout_grid(const tiku_richlayout_t *layout,
    size_t index, tiku_rich_grid_box_t *out);

/** @brief Internal column divider and its permitted document-point interval. */
typedef struct {
    uint64_t table;            /**< table whose adjacent columns resize */
    unsigned column;          /**< zero-based column to the divider's left */
    tiku_rich_rect_t line;     /**< zero-width vertical line across the table */
    double minimum, maximum;  /**< permitted x coordinates */
} tiku_rich_divider_t;

/** @brief Find a nearby resizable divider, preferring the deepest table.
 *  @note Coordinates and tolerance (0..32) are document points. Bounds keep
 *        adjacent columns large enough for padding, pictures and nesting.
 *  @return OK, NOT_FOUND, or a refusal; failure clears *out. */
tiku_rich_status_t tiku_richlayout_divider(const tiku_richlayout_t *layout,
    double x, double y, double tolerance, tiku_rich_divider_t *out);
/** @brief One visual line; positions exclude its soft newline if present. */
typedef struct {
    uint64_t paragraph;
    size_t first, last;        /**< byte offsets, last is an inclusive caret */
    size_t cluster, count;     /**< indices in the layout cluster array */
    tiku_rich_rect_t box;
    double baseline;
    char marker[32];           /**< list label on a paragraph's first line */
    double marker_x;          /**< label's left edge, in document points */
    tiku_rich_chars_t marker_chars; /**< paragraph's resolved label style */
    size_t marker_glyph, marker_count; /**< shaped label glyphs */
    size_t paragraph_index;    /**< paragraph index in the retained snapshot */
} tiku_rich_line_t;
/** @brief One shaped cluster sharing geometry for paint, hit and selection. */
typedef struct {
    uint64_t paragraph;
    size_t first, last;        /**< half-open paragraph byte range */
    tiku_rich_rect_t box;
    double baseline;
    tiku_rich_chars_t chars;
    size_t glyph, glyph_count; /**< indices in the layout glyph array */
    size_t caret, caret_count; /**< indices in the layout caret array */
    unsigned flags;            /**< internal whitespace/break classification */
    const tiku_rich_image_t *image; /**< borrowed for the layout lifetime */
} tiku_rich_cluster_t;

/** @brief Create an unpublished layout job; failure clears *out. */
tiku_rich_status_t tiku_richlayout_create(const tiku_richsnapshot_t *snapshot,
    const tiku_richlayout_options_t *options, tiku_richlayout_t **out);
/** @brief Create a job reusing compatible paragraphs from a completed layout.
 *  @note NULL, incomplete or incompatible previous layouts are ignored.
 *        A usable previous layout is retained until this job stops; callers
 *        may release their reference immediately. Its storage remains charged
 *        to the source owner, separately from this job's memory_bytes limit.
 *        Provider generation must change when shaping results change. */
tiku_rich_status_t tiku_richlayout_create_reusing(
    const tiku_richsnapshot_t *snapshot,
    const tiku_richlayout_options_t *options,
    const tiku_richlayout_t *previous, tiku_richlayout_t **out);
/** @brief Number of completed paragraphs copied from compatible geometry. */
size_t tiku_richlayout_reused(const tiku_richlayout_t *layout);
/** @brief Process at most work scalar/cluster/line-construction units.
 *  @return PENDING, OK when finished, or a sticky refusal status.
 *  @note Release cancels a pending job. Geometry is queryable only at OK. */
tiku_rich_status_t tiku_richlayout_step(tiku_richlayout_t *layout, size_t work);
/** @brief Release a complete, failed or pending layout and its resources. */
void tiku_richlayout_release(tiku_richlayout_t *layout);
/** @brief Source generation; zero for NULL. */
uint64_t tiku_richlayout_generation(const tiku_richlayout_t *layout);
/** @brief Retain the immutable model state used by a layout.
 *  @note Works while layout is pending. Caller releases *out as a snapshot.
 *        Failure clears *out; allocation counts against the source budget. */
tiku_rich_status_t tiku_richlayout_snapshot(const tiku_richlayout_t *layout,
                                           tiku_richsnapshot_t **out);
/** @brief Provider generation copied at creation; zero for NULL. */
uint64_t tiku_richlayout_fonts(const tiku_richlayout_t *layout);
/** @brief Whether context, shaping callback and font generation match. */
int tiku_richlayout_provider_is(const tiku_richlayout_t *layout,
                               const tiku_rich_provider_t *provider);
/** @brief Layout's completion/refusal status; INVALID for NULL. */
tiku_rich_status_t tiku_richlayout_status(const tiku_richlayout_t *layout);
/** @brief Accumulated diagnostics; zero for NULL. */
unsigned tiku_richlayout_warnings(const tiku_richlayout_t *layout);
/** @brief Finished line count; zero while pending or refused. */
size_t tiku_richlayout_lines(const tiku_richlayout_t *layout);
/** @brief Finished cluster count; zero while pending or refused. */
size_t tiku_richlayout_clusters(const tiku_richlayout_t *layout);
/** @brief Finished document height, or zero before completion. */
double tiku_richlayout_height(const tiku_richlayout_t *layout);
/** @brief Copy a line by index, clearing *out on refusal. */
tiku_rich_status_t tiku_richlayout_line(const tiku_richlayout_t *layout,
    size_t index, tiku_rich_line_t *out);
/** @brief Copy a cluster by index, clearing *out on refusal. */
tiku_rich_status_t tiku_richlayout_cluster(const tiku_richlayout_t *layout,
    size_t index, tiku_rich_cluster_t *out);
/** @brief Copy a glyph by index, clearing *out on refusal. */
tiku_rich_status_t tiku_richlayout_glyph(const tiku_richlayout_t *layout,
    size_t index, tiku_rich_glyph_t *out);
/** @brief Find a grapheme-safe caret from document coordinates.
 *  @note Coordinates outside content clamp to the nearest visual line/caret. */
tiku_rich_status_t tiku_richlayout_hit(const tiku_richlayout_t *layout,
    double x, double y, tiku_rich_pos_t *out);
/** @brief Find a picture whose actual box contains the point.
 *  @return OK with a borrowed image cluster, NOT_FOUND, or a refusal. */
tiku_rich_status_t tiku_richlayout_image_at(const tiku_richlayout_t *layout,
    double x, double y, tiku_rich_cluster_t *out);
/** @brief Find the image occupying exactly the ordered selection. */
tiku_rich_status_t tiku_richlayout_image_selection(
    const tiku_richlayout_t *layout, tiku_rich_pos_t first,
    tiku_rich_pos_t last, tiku_rich_cluster_t *out);
/** @brief Caret rectangle from the retained drawing clusters.
 *  @note BEFORE uses the previous line at a soft-wrap boundary; AFTER uses
 *        the next line. Non-grapheme positions return INVALID_RANGE. */
tiku_rich_status_t tiku_richlayout_caret(const tiku_richlayout_t *layout,
    tiku_rich_pos_t position, tiku_rich_rect_t *out);
/** @brief Snap a valid UTF-8 position to a grapheme boundary in current layout.
 *  @note forward is 0 for the preceding boundary, 1 for the following one.
 *        Exact boundaries and affinity are preserved. Failure clears *out. */
tiku_rich_status_t tiku_richlayout_snap(const tiku_richlayout_t *layout,
    tiku_rich_pos_t position, int forward, tiku_rich_pos_t *out);
/** @brief One clipped selection rectangle for a specified visual line.
 *  @return NO_CHANGE when the ordered selection does not touch that line. */
tiku_rich_status_t tiku_richlayout_selection(const tiku_richlayout_t *layout,
    tiku_rich_pos_t first, tiku_rich_pos_t last, size_t line,
    tiku_rich_rect_t *out);

/** @brief Logical grapheme and visual-line movement in this LTR profile. */
typedef enum {
    TIKU_RICH_MOVE_LEFT, TIKU_RICH_MOVE_RIGHT,
    TIKU_RICH_MOVE_UP, TIKU_RICH_MOVE_DOWN,
    TIKU_RICH_MOVE_HOME, TIKU_RICH_MOVE_END,
    TIKU_RICH_MOVE_START, TIKU_RICH_MOVE_FINISH
} tiku_rich_motion_t;
/** @brief Move a grapheme-safe caret, crossing paragraph boundaries.
 *  @note preferred_x is used for Up/Down; NaN uses the current caret x.
 *        NO_CHANGE at a document edge returns the unchanged position.
 *        Other failures clear *out. */
tiku_rich_status_t tiku_richlayout_move(const tiku_richlayout_t *layout,
    tiku_rich_pos_t position, tiku_rich_motion_t motion, double preferred_x,
    tiku_rich_pos_t *out);

#endif /* TIKU_RICHLAYOUT_H_ */
