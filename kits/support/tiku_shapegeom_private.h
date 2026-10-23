/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_shapegeom_private.h - what the geometry's three files share: the
 * units, an evaluation's names and refusals, and its tokens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_SHAPEGEOM_GEOM_PRIVATE_H_
#define TIKU_SHAPEGEOM_GEOM_PRIVATE_H_

#include "tiku_shapegeom.h"

#define EMU 12700.0   /* EMUs to a point                    */
#define ANGLE 60000.0 /* formula angle units to a degree    */
#define PI 3.14159265358979323846
#define DEG (PI / 180.0)
#define NAME_BYTES 32       /* a name and its NUL                 */
#define EXTENT 2147483647.0 /* a handle range left open           */
#define MAX_COORD 1e7       /* the farthest point kept, points    */
#define MAX_ITEMS 4096      /* names, handles or sites in a list  */

/** @brief A name and its value; an empty name is a free slot. */
typedef struct {
    char name[NAME_BYTES]; /* NUL-terminated; "" when free      */
    double v;              /* its value                         */
    unsigned char adjust;  /* set by av: a handle may move it   */
} slot_t;

/** @brief A token: where it starts in the definition, and its length. */
typedef struct {
    const char *s; /* its first byte                    */
    size_t n;      /* its length, bytes                 */
} tok_t;

/** @brief One evaluation: the names, its choices and its first refusal. */
typedef struct {
    slot_t *slot;                   /* the name table                    */
    size_t cap;                     /* its slots, a power of two         */
    unsigned flags;                 /* TIKU_SHAPEGEOM_GEOM_*                  */
    tiku_shapegeom_status_t status; /* the first refusal, or OK          */
    size_t points;                  /* outline points made so far        */
    size_t max_points;              /* and how many may be made          */
} env_t;

/** @brief Record @p status as the evaluation's result unless one is set. */
void sg_refuse(env_t *e, tiku_shapegeom_status_t status);
/** @brief A definition fault: refused in strict mode, read past in lenient
 *         mode.  @return 1 when the evaluation goes on. */
int sg_fault(env_t *e);
/** @brief The next space-separated token before @p end into @p t, @p p
 *         moved past it.  @return 0 at the end. */
int sg_next(const char **p, const char *end, tok_t *t);
/** @brief The next token's value, or a fault reading 0 when there is
 *         none. */
double sg_next_value(env_t *e, const char **p, const char *end);
/** @brief @p v divided by @p unit as a float, or a fault reading 0 when
 *         it is not finite or lies past MAX_COORD. */
float sg_scaled(env_t *e, double v, double unit);
/** @brief The path program @p prog run for a @p w by @p h EMU shape into
 *         @p out (tiku_shapegeom_path.c). */
void sg_run_paths(env_t *e, tiku_shapegeom_outline_t *out, const char *prog,
                  double w, double h);

#endif /* TIKU_SHAPEGEOM_GEOM_PRIVATE_H_ */
