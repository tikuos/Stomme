/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_glyphpath.h - one outline, however it was described.
 *
 * TrueType describes a glyph in quadratics and CFF in cubics, but a
 * rasteriser wants edges.  The font readers push their curves in here,
 * in device space (pixels, y downwards from the baseline), and this
 * flattens each curve into straight pieces by its size on the screen.
 *
 * Coverage is signed area accumulated per cell and resolved with a
 * prefix sum.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GLYPHPATH_H_
#define TIKU_GLYPHPATH_H_

typedef struct tiku_path tiku_path_t;

/** @brief The most zones a hint can carry. */
#define TIKU_HINT_ZONES 3

/**
 * @brief How font units become device pixels, vertically grid-fitted.
 *
 * X is scaled and left alone, so advances and letter spacing stay as the
 * designer drew them.  Y is scaled and then nudged, so that the baseline
 * and the tops of the lowercase and the capitals land on pixel
 * boundaries.  With @c zones at 0 this is a plain scale.
 */
typedef struct {
    float scale;                  /* device pixels per font unit */
    int zones;                    /* how many of the zones are used */
    float from[TIKU_HINT_ZONES];  /* font units, ascending */
    float shift[TIKU_HINT_ZONES]; /* device pixels to nudge by */
} tiku_hint_t;

/** @brief The device y for font-unit @p y: scaled, nudged by the zones
 *         and sign flipped for the screen. */
float tiku_hint_y(const tiku_hint_t *hint, float y);

/** @brief The device x for font-unit @p x. */
float tiku_hint_x(const tiku_hint_t *hint, float x);

/** @brief A new, empty path, or NULL when memory runs out. */
tiku_path_t *tiku_path_new(void);

/** @brief Free @p path and its edges; NULL is allowed. */
void tiku_path_free(tiku_path_t *path);

/** @brief Forget the outline and any failure, keeping the room it was
 *         built in and the transform. */
void tiku_path_reset(tiku_path_t *path);

/**
 * @brief Whether anything has gone wrong since the last reset.
 *
 * The builder calls return nothing: an edge that could not be kept (too
 * many edges, or no memory) is recorded here, to be asked about once,
 * at the end.  A NULL path counts as failed.
 */
int tiku_path_failed(const tiku_path_t *path);

/**
 * @brief Apply @p m, a PDF-style matrix [a b c d e f], to every point
 *        pushed from now on: x' = a x + c y + e, y' = b x + d y + f.
 *
 * For an outline drawn at a size and slant the hint alone cannot say;
 * NULL takes it off again.  A reset keeps it.
 */
void tiku_path_transform(tiku_path_t *path, const float *m);

/** @brief Start a new contour at (@p x, @p y), closing the last one. */
void tiku_path_move(tiku_path_t *path, float x, float y);
/** @brief A straight edge from the pen to (@p x, @p y). */
void tiku_path_line(tiku_path_t *path, float x, float y);
/** @brief A quadratic through one control point to (x, y). */
void tiku_path_quad(tiku_path_t *path, float cx, float cy, float x, float y);
/** @brief A cubic through two control points to (x, y). */
void tiku_path_cubic(tiku_path_t *path, float ax, float ay, float bx, float by,
                     float x, float y);

/** @brief Shut the contour back to where the last move put the pen. */
void tiku_path_close(tiku_path_t *path);

/**
 * @brief The whole outline's extent, in whole pixels, with a margin; all
 *        zero for an empty path, and no wider or taller than 1024.
 *
 * The margin is the accumulator's: an edge writes into the cell past the
 * one it covers, and that cell has to be inside the map for the prefix
 * sum to resolve the last column of ink.
 */
void tiku_path_bounds(const tiku_path_t *path, int *x0, int *y0, int *w,
                      int *h);

/** @brief Unrounded flattened-outline bounds without raster padding.
 * @note Writes x, y, width, height; empty paths write zeros. Returns zero
 * for NULL/failed paths or NULL output, otherwise one. */
int tiku_path_extent(const tiku_path_t *path, double out[4]);

/**
 * @brief Coverage for the box @p x0,@p y0,@p w,@p h.
 *
 * @param darken Nonzero to lift partial coverage toward ink, so that a
 *               thin stem reads darker at small sizes.  Zero draws the
 *               linear coverage.
 * @return w*h bytes the caller frees, or NULL, also for a box wider or
 *         taller than 1024.
 */
unsigned char *tiku_path_render(const tiku_path_t *path, int x0, int y0, int w,
                                int h, int darken);

#endif /* TIKU_GLYPHPATH_H_ */
