/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_painter.h - anti-aliased vector painting onto a surface: paths
 * filled and stroked with solid colours and gradients, clipped, through
 * an affine transform, and images drawn through it.
 *
 * Coordinates are the surface's logical ones, mapped through the current
 * transform; the painter draws at the surface's scale, inside its clip.
 * Colours are 0xAARRGGBB with straight alpha, laid over the surface's
 * pixels by coverage.  A path stays until tiku_painter_new_path().  No
 * display-list command carries what a painter draws: a recording
 * surface's list is marked not whole.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PAINTER_H_
#define TIKU_PAINTER_H_

#include <stdint.h>

#include "tiku_gfx.h"

/** @brief An affine map: x' = a x + c y + e, y' = b x + d y + f. */
typedef struct {
    double a, b, c, d, e, f;
} tiku_affine_t;

/** @brief The identity map. */
tiku_affine_t tiku_affine_identity(void);
/** @brief @p m applied after @p n: the map x -> m(n(x)). */
tiku_affine_t tiku_affine_mul(const tiku_affine_t *m, const tiku_affine_t *n);
/** @brief The inverse of @p m into @p out.  @return 0 when @p m has none. */
int tiku_affine_invert(const tiku_affine_t *m, tiku_affine_t *out);
/** @brief Point (@p x, @p y) mapped through @p m. */
void tiku_affine_apply(const tiku_affine_t *m, double x, double y,
                       double *ox, double *oy);

/** @brief How a paint colours what it covers. */
typedef enum {
    TIKU_PAINT_SOLID,           /**< one colour */
    TIKU_PAINT_LINEAR,          /**< along the line from p0 to p1 */
    TIKU_PAINT_RADIAL           /**< from circle (p0, r0) to (p1, r1) */
} tiku_paint_kind_t;

/** @brief Gradient stops a paint holds at most. */
#define TIKU_PAINT_MAX_STOPS 16

/** @brief A gradient stop: where along the gradient, 0 to 1, and its
 *         colour. */
typedef struct {
    double offset;
    uint32_t argb;
} tiku_paint_stop_t;

/** @brief A paint.  Gradient points are in user space when the paint is
 *         used; past its ends a gradient keeps its end colours.  Stops are
 *         in ascending offset. */
typedef struct {
    tiku_paint_kind_t kind;
    uint32_t argb;              /**< SOLID */
    double x0, y0, r0, x1, y1, r1;
    int stop_count;             /**< 1 to TIKU_PAINT_MAX_STOPS */
    tiku_paint_stop_t stop[TIKU_PAINT_MAX_STOPS];
} tiku_paint_t;

/** @brief A solid paint of @p argb. */
tiku_paint_t tiku_paint_solid(uint32_t argb);

/** @brief Which areas of a path are inside. */
typedef enum {
    TIKU_FILL_NONZERO, TIKU_FILL_EVENODD
} tiku_fill_rule_t;

/** @brief Line caps and joins, in the rasteriser's numbering. */
enum { TIKU_CAP_BUTT, TIKU_CAP_ROUND, TIKU_CAP_SQUARE };
enum { TIKU_JOIN_MITER, TIKU_JOIN_ROUND, TIKU_JOIN_BEVEL };

/** @brief Dash lengths a stroke holds at most. */
#define TIKU_STROKE_MAX_DASHES 16

/** @brief How a path is stroked, in user units.  A transform that is not
 *         a rotation and uniform scale strokes with the width its area
 *         scale gives. */
typedef struct {
    double width;
    int cap, join;
    double miter;               /**< miter limit; 0 for 10 */
    int dash_count;             /**< 0: solid */
    double dash[TIKU_STROKE_MAX_DASHES];
    double dash_offset;
} tiku_stroke_t;

/** @brief Why a painter stopped drawing something. */
enum {
    TIKU_PAINTER_OK,
    TIKU_PAINTER_NOMEM,         /**< storage was refused */
    TIKU_PAINTER_LIMIT,         /**< a path past the rasteriser's ceilings,
                                     or a coordinate past a million pixels */
    TIKU_PAINTER_BAD            /**< a value no paint can take */
};

/** @brief A painter onto one surface. */
typedef struct tiku_painter tiku_painter_t;

/** @brief A painter onto @p s, inside its clip, with no transform; NULL
 *         when storage was refused.  @p s outlives the painter. */
tiku_painter_t *tiku_painter_new(tiku_surface_t *s);
/** @brief The painter and its path, clips and saved states given back;
 *         NULL is allowed. */
void tiku_painter_free(tiku_painter_t *p);
/** @brief The surface @p p paints onto. */
tiku_surface_t *tiku_painter_surface(const tiku_painter_t *p);
/** @brief The first failure since the painter was made, or OK.  What
 *         failed was not drawn; the painter goes on with the next call,
 *         unless it was a save. */
int tiku_painter_status(const tiku_painter_t *p);

/** @brief The transform, clip and alpha kept, for tiku_painter_restore()
 *         to bring back.  A save refused storage stops the painter: it
 *         draws nothing more, as its restore could not bring them back. */
void tiku_painter_save(tiku_painter_t *p);
/** @brief The state of the last save brought back; without one, nothing
 *         happens. */
void tiku_painter_restore(tiku_painter_t *p);
/** @brief @p m put before the current transform: user coordinates go
 *         through @p m, then through what was the transform. */
void tiku_painter_transform(tiku_painter_t *p, const tiku_affine_t *m);
/** @brief The current transform replaced by @p m. */
void tiku_painter_set_transform(tiku_painter_t *p, const tiku_affine_t *m);
/** @brief The current transform, from user to logical coordinates. */
tiku_affine_t tiku_painter_get_transform(const tiku_painter_t *p);
/** @brief Every paint's alpha multiplied by @p alpha, 0 to 1. */
void tiku_painter_set_alpha(tiku_painter_t *p, double alpha);

/** @brief The path discarded. */
void tiku_painter_new_path(tiku_painter_t *p);
/** @brief A subpath begun at (@p x, @p y). */
void tiku_painter_move(tiku_painter_t *p, double x, double y);
/** @brief A line to (@p x, @p y). */
void tiku_painter_line(tiku_painter_t *p, double x, double y);
/** @brief A quadratic curve through control (@p cx, @p cy). */
void tiku_painter_quad(tiku_painter_t *p, double cx, double cy, double x,
                       double y);
/** @brief A cubic curve through (@p ax, @p ay) and (@p bx, @p by). */
void tiku_painter_cubic(tiku_painter_t *p, double ax, double ay, double bx,
                        double by, double x, double y);
/** @brief The subpath closed back to its start. */
void tiku_painter_close(tiku_painter_t *p);
/** @brief A closed rectangle subpath. */
void tiku_painter_rect(tiku_painter_t *p, double x, double y, double w,
                       double h);
/** @brief A closed ellipse subpath of four cubics. */
void tiku_painter_ellipse(tiku_painter_t *p, double cx, double cy, double rx,
                          double ry);

/** @brief The path's inside, by @p rule, painted with @p paint. */
void tiku_painter_fill(tiku_painter_t *p, const tiku_paint_t *paint,
                       tiku_fill_rule_t rule);
/** @brief The path's outline, as @p stroke draws it, painted with
 *         @p paint. */
void tiku_painter_stroke(tiku_painter_t *p, const tiku_paint_t *paint,
                         const tiku_stroke_t *stroke);
/** @brief The clip narrowed to the path's inside, by @p rule. */
void tiku_painter_clip(tiku_painter_t *p, tiku_fill_rule_t rule);
/**
 * @brief Image @p argb of @p w by @p h pixels, @p stride pixels a row,
 *        drawn with pixel (i, j) covering user square (i, j)-(i+1, j+1).
 *
 * @p smooth samples between pixels; otherwise each takes the nearest.
 * Alpha is straight.
 */
void tiku_painter_image(tiku_painter_t *p, const uint32_t *argb, int w,
                        int h, int stride, int smooth);

#endif /* TIKU_PAINTER_H_ */
