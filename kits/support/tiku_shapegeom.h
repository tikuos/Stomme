/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_shapegeom.h - shape geometry from DrawingML's formula language.
 *
 * A geometry is written the way ECMA-376 Part 1 (20.1.9) writes preset
 * and custom shapes: adjust values and guides as formulas over the
 * shape's size, handles that move adjust values, connection sites, a text
 * rectangle and paths.  Evaluating one for a size gives its outline in
 * points.  The standard preset catalogue is built in
 * (tiku_shapegeom_presets.inl); custom geometry uses the same record.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHAPEGEOM_GEOM_H_
#define TIKU_SHAPEGEOM_GEOM_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Shared geometry evaluation result. */
typedef enum {
    TIKU_SHAPEGEOM_OK,
    TIKU_SHAPEGEOM_INVALID,
    TIKU_SHAPEGEOM_INVALID_GEOMETRY,
    TIKU_SHAPEGEOM_LIMIT,
    TIKU_SHAPEGEOM_NO_MEMORY
} tiku_shapegeom_status_t;

/** @brief A geometry in DrawingML's formula language: lengths in EMUs,
 *         angles in 60000ths of a degree, each list a string of items
 *         separated by ';'.  kits/deck/README.md gives each field's form
 *         and how arcs, closes and paths' own spaces are read.
 *  @note Strings are borrowed by every call that takes the record. */
typedef struct {
    const char *name; /**< preset name; custom geometry: NULL */
    const char *av;   /**< adjust values and their defaults */
    const char *gd;   /**< guides, evaluated in order after av */
    const char *ah;   /**< adjust handles */
    const char *cxn;  /**< connection sites */
    const char *rect; /**< text rectangle; empty: "l t r b" */
    const char *path; /**< path program */
} tiku_shapegeom_def_t;

/** @brief A shape's own value for one adjust value, over its default.
 *  @note A NULL formula uses value; a formula ("val 25000", or any
 *        formula over the size) is evaluated where av is. */
typedef struct {
    const char *name;
    const char *formula;
    double value;
} tiku_shapegeom_adjust_t;

/** @brief How a path is filled: not at all, normally, or lightened or
 *         darkened from the shape's fill, ECMA-376's ST_PathFillMode. */
enum {
    TIKU_SHAPEGEOM_PATH_NO_FILL = 0,
    TIKU_SHAPEGEOM_PATH_FILL,
    TIKU_SHAPEGEOM_PATH_LIGHTEN,
    TIKU_SHAPEGEOM_PATH_LIGHTEN_LESS,
    TIKU_SHAPEGEOM_PATH_DARKEN,
    TIKU_SHAPEGEOM_PATH_DARKEN_LESS
};

/** @brief Outline operations: MOVE and LINE take one point, CUBIC three
 *         (two controls, then the end), CLOSE none. */
enum {
    TIKU_SHAPEGEOM_MOVE = 0,
    TIKU_SHAPEGEOM_LINE,
    TIKU_SHAPEGEOM_CUBIC,
    TIKU_SHAPEGEOM_CLOSE
};

/** @brief One path of an outline, in points in the shape's own space. */
typedef struct {
    unsigned char fill;   /**< TIKU_SHAPEGEOM_PATH_* */
    unsigned char stroke; /**< 1 when the path is stroked */
    unsigned char *ops;   /**< TIKU_SHAPEGEOM_MOVE ... */
    size_t op_count;
    float *points;      /**< x, y pairs, as the ops consume them */
    size_t point_count; /**< pairs */
} tiku_shapegeom_path_t;

/** @brief A handle moves its adjust values: x and y, or radius and angle. */
typedef enum {
    TIKU_SHAPEGEOM_HANDLE_XY,
    TIKU_SHAPEGEOM_HANDLE_POLAR
} tiku_shapegeom_handle_kind_t;

/** @brief An evaluated handle.  Index 0 is x or radius, 1 y or angle;
 *         an empty ref is a direction the handle does not move. */
typedef struct {
    tiku_shapegeom_handle_kind_t kind;
    float x, y;            /**< where it is drawn, in points */
    char ref[2][32];       /**< adjust value names, or "" */
    double min[2], max[2]; /**< the ranges, evaluated for this size */
    double value[2];       /**< the adjust values now */
} tiku_shapegeom_handle_t;

/** @brief A connection site: a point and the direction a connector
 *         leaves it, in degrees clockwise from +x. */
typedef struct {
    float x, y;
    float angle;
} tiku_shapegeom_site_t;

/** @brief An evaluated geometry.  Owns its arrays until
 *         tiku_shapegeom_outline_free(); a zeroed outline is empty. */
typedef struct {
    tiku_shapegeom_path_t *paths;
    size_t path_count;
    float text[4]; /**< text rectangle: left top right bottom */
    tiku_shapegeom_handle_t *handles;
    size_t handle_count;
    tiku_shapegeom_site_t *sites;
    size_t site_count;
} tiku_shapegeom_outline_t;

/** @brief Evaluation choices. */
enum {
    /** Read foreign files as their writers mean them: an unknown name
     *  reads 0, an unknown operator is "val", a short operand list reads
     *  0 for what is missing, a non-finite result reads 0, and a path
     *  command before any "P" opens a filled, stroked path.  Without it
     *  each of these refuses the geometry. */
    TIKU_SHAPEGEOM_GEOM_LENIENT = 1u << 0,
    /** Guides, text rectangle, handles and sites only: no paths. */
    TIKU_SHAPEGEOM_GEOM_NO_PATHS = 1u << 1
};

/** @brief Output points an evaluation may make by default. */
#define TIKU_SHAPEGEOM_GEOM_MAX_POINTS ((size_t)1 << 20)

/** @brief Options for an evaluation; NULL or zeroes select the defaults. */
typedef struct {
    unsigned flags;    /**< TIKU_SHAPEGEOM_GEOM_* */
    size_t max_points; /**< 0: TIKU_SHAPEGEOM_GEOM_MAX_POINTS */
} tiku_shapegeom_opt_t;

/** @brief The built-in preset called @p name, or NULL. */
const tiku_shapegeom_def_t *tiku_shapegeom_preset(const char *name);
/** @brief How many presets are built in. */
size_t tiku_shapegeom_preset_count(void);
/** @brief The built-in preset at @p index, by name order, or NULL. */
const tiku_shapegeom_def_t *tiku_shapegeom_preset_at(size_t index);

/**
 * @brief Evaluate @p def for a @p width by @p height point box.
 *
 * Adjust values take their defaults from av, then @p adjust; guides
 * follow.  A NULL @p def is a rectangle.  Width and height must be finite
 * and not negative.
 *
 * @return OK with *out filled; INVALID_GEOMETRY for a definition or
 *         adjust name refused, or a point not finite or beyond ten million
 *         points (strict mode); INVALID for a size, a non-finite adjust
 *         value or an argument refused (either mode); LIMIT past
 *         max_points points or 4096 names, handles or sites; NO_MEMORY.
 *         On failure *out is empty.
 */
tiku_shapegeom_status_t tiku_shapegeom_eval(
    const tiku_shapegeom_def_t *def, const tiku_shapegeom_adjust_t *adjust,
    size_t adjust_count, float width, float height,
    const tiku_shapegeom_opt_t *options, tiku_shapegeom_outline_t *out);

/** @brief Release an outline's arrays and empty it; NULL is allowed. */
void tiku_shapegeom_outline_free(tiku_shapegeom_outline_t *outline);

/**
 * @brief Whether @p def is a geometry a deck may hold: strict evaluation
 *        succeeds at several sizes, including an empty one, and every
 *        handle names an adjust value.
 * @return OK, or the refusal strict evaluation gave.
 */
tiku_shapegeom_status_t tiku_shapegeom_check(const tiku_shapegeom_def_t *def);

/**
 * @brief The adjust values that bring handle @p index nearest to (@p x,
 *        @p y), in points in the shape's own space.
 *
 * Each value the handle moves is searched within its range, with the
 * handle's other value held, and rounded to a whole number as DrawingML
 * stores adjust values.  @p values[0] and @p values[1] receive the values
 * for the handle's two refs; an empty ref leaves its slot at the current
 * value.
 *
 * @return OK; INVALID for a handle index or point refused; the
 *         evaluation's status otherwise.
 */
tiku_shapegeom_status_t tiku_shapegeom_drag(
    const tiku_shapegeom_def_t *def, const tiku_shapegeom_adjust_t *adjust,
    size_t adjust_count, float width, float height, size_t index, float x,
    float y, const tiku_shapegeom_opt_t *options, double values[2]);

/** @brief Apply DrawingML path light/dark fill modes, retaining alpha. */
uint32_t tiku_shapegeom_tone(uint32_t color, unsigned mode);
#endif /* TIKU_SHAPEGEOM_GEOM_H_ */
