/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_painter_private.h - what the painter's files share: its state, its
 * saved states, and the device-space path it builds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PAINTER_PRIVATE_H_
#define TIKU_PAINTER_PRIVATE_H_

#include "tiku_painter.h"
#include "tiku_vector_raster.h"

/** @brief What save and restore keep: the transform, the alpha and the
 *         clip, a box of device pixels and, once a path narrowed it, the
 *         coverage inside that box. */
typedef struct {
    tiku_affine_t      m;           /* user to logical coordinates       */
    double             alpha;       /* every paint's alpha times this    */
    int                x0, y0;      /* the clip box's first pixel        */
    int                x1, y1;      /* and the pixel past its last       */
    tiku_vector_mask_t clip;        /* coverage over the box, or a NULL a
                                       for the whole box                 */
} tiku_pstate_t;

/** @brief A painter: its surface, the device scale, the current and the
 *         saved states, the path and the first failure. */
struct tiku_painter {
    tiku_surface_t     *s;          /* where it paints                   */
    int                 scale;      /* native pixels per logical one     */
    tiku_pstate_t       st;         /* the state in force                */
    tiku_pstate_t      *saved;      /* states save() kept, oldest first  */
    int                 nsaved, capsaved;   /* kept, room for            */
    tiku_vector_path_t  path;       /* the path, in device pixels        */
    int                 status;     /* the first TIKU_PAINTER_* failure  */
    int                 stopped;    /* 1 once a save failed: nothing more
                                       is drawn                          */
};

/* tiku_painter.c */
/** @brief The map from user coordinates to device pixels. */
tiku_affine_t tp_device(const tiku_painter_t *p);
/** @brief Failure @p status recorded, unless one is already. */
void tp_fail(tiku_painter_t *p, int status);
/** @brief The device box the path covers inside the clip, into @p box
 *         (x0 y0 x1 y1).  @return 0 when it covers none. */
int tp_box(tiku_painter_t *p, const tiku_vector_path_t *path, int box[4]);

/* tiku_painter_fill.c */
/** @brief Coverage @p mask laid over the surface with @p paint, through
 *         the clip. */
void tp_composite(tiku_painter_t *p, const tiku_vector_mask_t *mask,
                  const tiku_paint_t *paint);

#endif /* TIKU_PAINTER_PRIVATE_H_ */
