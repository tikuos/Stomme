/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_artwork.h - the renderer's drawing helpers: a path pen
 * that clips to the viewport, flash outlines and holes at the view's
 * tolerance, dark and clear coverage, and coverage turned into the
 * artwork colour.  They know nothing of any file format.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_ARTWORK_H_
#define TIKU_CIRCUIT_ARTWORK_H_

#include "tiku_circuit_private.h"
#include "tiku_vector_raster.h"

/* Radians between the points of a curve whose radius is at most
 * CIRCUIT_CURVE_ERROR, which the error bound leaves unconstrained. */
#define CIRCUIT_COARSE_STEP (M_PI / 4)

/* The largest coordinate, in pixels, a strict vector path takes
 * (tiku_vector_raster.c); a pen keeps its points within it. */
#define CIRCUIT_RASTER_MAX  1e6

/* Pixels outside the viewport a pen keeps, so antialiased edges along
 * the viewport's sides come out as they would unclipped. */
#define CIRCUIT_CLIP_MARGIN 2

/** @brief One edge's stage of polygon clipping. */
typedef struct {
    double fx, fy;                  /* the subpath's first point here    */
    double px, py;                  /* the point before                  */
    int started;                    /* a point has reached this stage    */
} circuit_clip_stage_t;

/** @brief A path builder that clips what it is given to a rectangle round
 *         the viewport, so no far-off point reaches the rasteriser. */
typedef struct {
    tiku_vector_path_t *path;       /* receives what survives            */
    double x0, y0, x1, y1;          /* the rectangle, in pixels          */
    int area;                       /* clip filled areas, not lines      */
    int pass;                       /* it all lies inside: copy as given */
    circuit_clip_stage_t stage[4];  /* area clipping, one per edge       */
    int drawn;                      /* points out of the last stage      */
    double fx, fy;                  /* line clipping: the subpath's      */
    double px, py;                  /* first point, and the point before */
    int pen;                        /* the last piece ends at px, py     */
    int cut;                        /* part of this subpath was cut away */
} circuit_clip_t;

/**
 * @brief Start pen @p c onto @p path for a @p width by @p height
 *        viewport, keeping @p margin pixels beyond each side.
 *
 * An @p area is clipped as a filled polygon, which keeps its coverage
 * inside the rectangle; otherwise lines are cut into pieces at the
 * rectangle, which draw as they would unclipped while a stroke's half
 * width is within @p margin.  With @p pass the caller has found
 * everything inside, and points go to the path as they are.
 */
void circuit_clip_start(circuit_clip_t *c, tiku_vector_path_t *path,
    double margin, int width, int height, int area, int pass);

/** @brief Empty the pen's path and forget the subpath under way. */
void circuit_clip_clear(circuit_clip_t *c);

/** @brief Begin a subpath at (@p x, @p y), ending any under way. */
void circuit_clip_move(circuit_clip_t *c, double x, double y);

/** @brief Continue the subpath to (@p x, @p y). */
void circuit_clip_line(circuit_clip_t *c, double x, double y);

/** @brief Close the subpath back to its first point. */
void circuit_clip_close(circuit_clip_t *c);

/** @brief Finish the pen: an area's open subpath is closed, as a fill
 *         closes it. */
void circuit_clip_end(circuit_clip_t *c);

/**
 * @brief Add @p shape's artwork outline through @p pen in @p view's
 *        pixels.
 *
 * An obround replaces the path with its capsule; a flash hole is added as
 * a further contour, so an even-odd fill leaves it open.
 *
 * @return 0, or the vector path's failure
 */
int circuit_artwork_outline(const circuit_shape_t *shape,
    const tiku_circuit_view_t *view, circuit_clip_t *pen);

/** @brief Add @p mask to a layer's @p coverage, whose rows are @p width
 *         bytes, or take it away when @p clear. */
void circuit_artwork_apply(unsigned char *coverage, int width,
    const tiku_vector_mask_t *mask, int clear);

/** @brief Paint @p image the artwork colour at @p coverage over the paper
 *         colour. */
void circuit_artwork_colour(tiku_image_t *image,
                            const unsigned char *coverage);

#endif
