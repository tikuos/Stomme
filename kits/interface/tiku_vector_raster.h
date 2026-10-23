/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_vector_raster.h - paths in device space, filled into coverage
 * masks and stroked into outlines.
 *
 * Knows no document: the PDF renderer, the circuit viewer and the painter
 * each build their paths here.  A strict path refuses a coordinate the rasteriser
 * cannot hold; a lenient one, which the PDF renderer uses, clamps it and
 * draws on.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_VECTOR_RASTER_H_
#define TIKU_VECTOR_RASTER_H_

#include <stddef.h>
#include <stdint.h>

/** @brief An affine matrix as PDF writes one, [a b c d e f], for row
 *         vectors: x' = a x + c y + e and y' = b x + d y + f. */
typedef struct {
    double a, b, c, d, e, f;        /* the six numbers, in that order    */
} tiku_vector_mat_t;

/** @brief Coverage, 0 to 255 a pixel, over a box of device pixels. */
typedef struct {
    int            x0, y0;          /* the box's top-left pixel          */
    int            w, h;            /* its size; 0 by 0 for none         */
    unsigned char *a;               /* w * h bytes, row by row, or NULL  */
} tiku_vector_mask_t;

/** @brief Why a path failed, as its failed field holds it; 0 while it
 *         has not. */
enum {
    TIKU_VECTOR_NOMEM = 1,          /* an allocation failed              */
    TIKU_VECTOR_LIMIT,              /* more points, subpaths or halvings
                                       than a path may take              */
    TIKU_VECTOR_BAD                 /* a strict path's coordinate, or a
                                       stroke's width, dash, miter limit
                                       or phase, that cannot be drawn    */
};

/**
 * @brief A path flattened to straight pieces in device space.
 *
 * Subpaths are runs of points; a closed one is marked, and an open one is
 * closed when it is filled.  A failed path takes no more points and fills
 * to nothing.  Set strict and max_points after tiku_vector_path_init();
 * tiku_vector_path_free() sets them back to 0.
 */
typedef struct {
    float *pt;                      /* x, y pairs                        */
    int    n, cap;                  /* points, and room for them         */
    int   *start;                   /* where each subpath begins         */
    int   *closed;                  /* per subpath: 1 once it is shut    */
    int    nsub, subcap;            /* subpaths, and room for them       */
    float  cx, cy;                  /* the current point                 */
    float  sx, sy;                  /* the subpath's first point         */
    int    failed;                  /* 0, or a TIKU_VECTOR_* reason      */
    /* Nonzero: a coordinate past 1e6 or not finite fails the path, and
     * curves and discs are flattened to within 0.2 px.  Zero: such a
     * coordinate is clamped and a curve is flattened by its size. */
    int    strict;
    /* The most points the path holds, and subpaths; 0 for 4,000,000,
     * which is also the ceiling for points. */
    int    max_points;
} tiku_vector_path_t;

/** @brief The matrix [@p a @p b @p c @p d @p e @p f]. */
tiku_vector_mat_t tiku_vector_mat(double a, double b, double c, double d,
                                  double e, double f);

/** @brief @p m then @p n: a point through the result goes through @p m
 *         first, as PDF's "cm" composes them. */
tiku_vector_mat_t tiku_vector_mat_mul(tiku_vector_mat_t m,
                                      tiku_vector_mat_t n);

/** @brief The point (@p x, @p y) through @p m, into (@p ox, @p oy). */
void tiku_vector_mat_apply(const tiku_vector_mat_t *m, double x, double y,
                           double *ox, double *oy);

/** @brief How much @p m scales a length, on average: the root of its
 *         determinant, or its first row's length when that is zero. */
double tiku_vector_mat_scale(const tiku_vector_mat_t *m);

/** @brief @p p made empty: lenient, the default point limit, owning no
 *         memory. */
void tiku_vector_path_init(tiku_vector_path_t *p);

/** @brief @p p emptied for the next path: its memory, strictness and
 *         limit kept, its failure forgotten. */
void tiku_vector_path_clear(tiku_vector_path_t *p);

/** @brief @p p's memory given back, @p p left as tiku_vector_path_init()
 *         leaves it. */
void tiku_vector_path_free(tiku_vector_path_t *p);

/** @brief A new subpath, begun at (@p x, @p y). */
void tiku_vector_path_moveto(tiku_vector_path_t *p, double x, double y);

/** @brief A straight piece to (@p x, @p y); with no subpath yet, one
 *         begun there. */
void tiku_vector_path_lineto(tiku_vector_path_t *p, double x, double y);

/** @brief A cubic from the current point by (@p x1, @p y1) and (@p x2,
 *         @p y2) to (@p x3, @p y3), flattened into straight pieces. */
void tiku_vector_path_curveto(tiku_vector_path_t *p, double x1, double y1,
                              double x2, double y2, double x3, double y3);

/** @brief The subpath shut: marked closed, the current point back at its
 *         first. */
void tiku_vector_path_close(tiku_vector_path_t *p);

/**
 * @brief @p p's extent in whole pixels, clipped to @p w by @p h.
 * @return 1 with the box in @p x0, @p y0, @p bw and @p bh; 0 when none of
 *         @p p is inside.
 */
int tiku_vector_path_bounds(const tiku_vector_path_t *p, int w, int h,
                            int *x0, int *y0, int *bw, int *bh);

/**
 * @brief Coverage of @p p within the box, by @p evenodd or nonzero.
 * @return the mask; one with no bytes and no size for an empty or failed
 *         path, a box with no area, or when memory is short.
 * @note The caller frees the mask with tiku_vector_mask_free().
 */
tiku_vector_mask_t tiku_vector_path_fill(const tiku_vector_path_t *p,
                                         int evenodd, int x0, int y0, int w,
                                         int h);

/**
 * @brief tiku_vector_path_fill(), asking @p cancel with @p ctx before
 *        each pixel row; @p cancel may be NULL.
 * @return the mask, or one with no bytes and no size once @p cancel
 *         answers nonzero.
 */
tiku_vector_mask_t tiku_vector_path_fill_cancel(const tiku_vector_path_t *p,
                                                int evenodd, int x0, int y0,
                                                int w, int h,
                                                int (*cancel)(void *),
                                                void *ctx);

/**
 * @brief The outline of stroking @p p, into @p out as a path to fill
 *        nonzero.
 *
 * @p cap is 0 butt, 1 round, 2 square; @p join is 0 miter (bevelled past
 * @p miter), 1 round, 2 bevel.  @p ndash entries of @p dash cut the line
 * from @p phase on, each after the one the phase falls in walked at least
 * 1e-3 px long and a zero one 0.01 px; all zeros draw it solid, and a
 * dashed line has no joins.  A lenient @p out is at least 0.8 px wide and
 * has no joins under 1.2 px.
 *
 * @note A width or dash entry below zero or not finite, a miter limit or
 *       phase not finite, an @p ndash below zero, or entries with a NULL
 *       @p dash fail @p out with TIKU_VECTOR_BAD; a failed @p p gives
 *       @p out its own reason.
 */
void tiku_vector_path_stroke(const tiku_vector_path_t *p, double width,
                             int cap, int join, double miter,
                             const double *dash, int ndash, double phase,
                             tiku_vector_path_t *out);

/** @brief @p m's bytes given back; one with none is fine. */
void tiku_vector_mask_free(tiku_vector_mask_t *m);

/** @brief @p m narrowed to what @p clip also covers; a NULL @p clip, or
 *         one with no bytes, leaves it as it is. */
void tiku_vector_mask_intersect(tiku_vector_mask_t *m,
                                const tiku_vector_mask_t *clip);

/**
 * @brief A mask over all of @p w by @p h at (0, 0), every pixel covered.
 * @return it; one with no bytes and no size when @p w or @p h is not
 *         positive, the size cannot be held, or memory is short.
 */
tiku_vector_mask_t tiku_vector_mask_full(int w, int h);

#endif /* TIKU_VECTOR_RASTER_H_ */
