/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_private.h - the scene as the readers build it: shapes in
 * world millimetres, the limits every scene is admitted under, and the
 * construction and painting calls shared by the readers and the renderer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_PRIVATE_H_
#define TIKU_CIRCUIT_PRIVATE_H_

#include "tiku_circuit.h"

#include <stdatomic.h>

#include "tiku_ttf.h"
#include "tiku_circuit_stack.h"

/* Every scene is admitted under these; a limit is checked before the
 * allocation it bounds. */
#define CIRCUIT_BYTES_MAX   (16u * 1024u * 1024u)   /* accounted storage */
#define CIRCUIT_POINTS_MAX  65536u      /* stored points in one scene    */
#define CIRCUIT_OBJECTS_MAX 4096u       /* drawing objects in one scene  */
#define CIRCUIT_DETAILS_MAX 4096u       /* bytes of one description      */
#define CIRCUIT_NAME_BYTES  256         /* a name or value, its zero too */
#define CIRCUIT_TEXT_MAX    4096        /* bytes of one text object      */
#define CIRCUIT_FONT_MAX    (8u * 1024u * 1024u)    /* a substitute font */
#define CIRCUIT_COORD_MAX   10000.0     /* mm from the origin, each way  */
#define CIRCUIT_WIDTH_MAX   1000.0      /* mm of stroke width            */
#define CIRCUIT_SCALE_MIN   0.01        /* pixels per mm, fully out      */
#define CIRCUIT_SCALE_MAX   4096        /* pixels per mm, fully in       */
#define CIRCUIT_CURVE_ERROR 0.2         /* px a tessellated curve may be */
                                        /* off its true path             */

/* The farthest a view may move the drawing, in pixels: far enough to
 * bring any in-range point, its stroke included, to any viewport pixel at
 * the largest scale.  Fit, zoom and render share it. */
#define CIRCUIT_SHIFT_MAX ((CIRCUIT_COORD_MAX + CIRCUIT_WIDTH_MAX) * \
                           CIRCUIT_SCALE_MAX + TIKU_CIRCUIT_RENDER_SIDE)

#define CIRCUIT_OPAQUE      0xff000000u /* the alpha of an opaque colour */
#define CIRCUIT_PAPER       0xfffffff7u /* the viewport's background     */
#define CIRCUIT_COPPER      0xffa06626u /* artwork's own colour          */

/** @brief A point in world millimetres. */
typedef struct { double x, y; } circuit_point_t;

/** @brief What a shape is drawn as. */
enum {
    CIRCUIT_POLYLINE,               /* points, stroked or filled         */
    CIRCUIT_ARC,                    /* circular arc or full circle       */
    CIRCUIT_TEXT,                   /* outline text through matrix       */
    CIRCUIT_OBROUND,                /* capsule between points 0 and 1    */
    CIRCUIT_GROUP                   /* another scene's coverage, offset  */
};

/** @brief One drawing object.  Fields a kind does not use stay zero. */
typedef struct {
    tiku_circuit_object_t info;     /**< owned name/value; painted bounds */
    circuit_point_t *points;        /**< owned world-coordinate vertices */
    size_t count;                   /**< number of vertices              */
    double width;                   /**< stroke width in mm; unused by fills */
    uint32_t colour;                /**< opaque ARGB colour              */
    int closed, fill;               /**< close the path; fill, not stroke */
    int kind;                       /**< CIRCUIT_POLYLINE ...            */
    /** cx, cy: an arc's, obround's or flash hole's centre; a group's
     * offset; for text, its alignment (-1 left, 0 centre, 1 right) and
     * first baseline in em.  r, start, sweep: arc or obround radius and
     * angles in radians. */
    double cx, cy, r, start, sweep;
    double end_r;                   /**< an arc's radius at its end, or 0 */
    double matrix[6];               /**< text em coordinates to world mm */
    char *text;                     /**< owned string of a CIRCUIT_TEXT  */
    char *details;                  /**< owned plain-text description    */
    int bold;                       /**< draw a heavier substitute outline */
    int clear;                      /**< removes coverage; artwork only  */
    double hole;                    /**< diameter of a see-through hole  */
    tiku_circuit_t *group;          /**< retained scene of a CIRCUIT_GROUP */
} circuit_shape_t;

/** @brief A scene: shapes, or the layers of a stack. */
struct tiku_circuit {
    atomic_uint refs;               /**< holders; snapshots retain it    */
    circuit_shape_t *shapes;        /**< owned; immutable once published */
    size_t count;                   /**< selectable objects              */
    size_t bytes;                   /**< accounted storage               */
    size_t points;                  /**< stored points                   */
    tiku_circuit_box_t bounds;      /**< union of the painted bounds     */
    tiku_ttf_t *font;               /**< owned copy of the substitute font */
    char warning[256];              /**< what the preview leaves out     */
    int coverage;                   /**< ordered dark/clear artwork layer */
    tiku_circuit_layer_t *layers;   /**< a stack's owned descriptors     */
    size_t layer_count;             /**< nonzero only for a stack        */
};

/** @brief A new empty scene with one holder, or NULL without memory. */
tiku_circuit_t *circuit_new(void);

/**
 * @brief Append @p child's coverage, offset by (@p x, @p y), as one
 *        object: an aperture macro's flash.
 * @return OK, BAD, LIMIT or NOMEM; @p doc gains nothing on failure
 * @note @p child must be flat: no font, layers or groups of its own.
 */
tiku_circuit_err_t circuit_group(tiku_circuit_t *doc, const char *name,
    const char *value, tiku_circuit_t *child, double x, double y);

/**
 * @brief Append a copy of @p shape moved by (@p x, @p y), description
 *        included: a step-and-repeat copy.
 *
 * @p shape may belong to @p doc.  Text is refused.
 *
 * @return OK, BAD, LIMIT or NOMEM
 */
tiku_circuit_err_t circuit_clone(tiku_circuit_t *doc,
    const circuit_shape_t *shape, double x, double y);

/**
 * @brief Whether closed contour @p p of @p n points can be filled: simple
 *        loops, joined only by coincident axis-aligned cut-in pairs, with
 *        holes nested inside and wound opposite to their outline.
 * @return OK, BAD for a contour that is not, LIMIT past 5000 points,
 *         NOMEM or CANCELLED (through @p options, which may be NULL)
 */
tiku_circuit_err_t circuit_contour(const circuit_point_t *p, size_t n,
    const tiku_circuit_import_t *options);

/**
 * @brief Give object @p index the description @p text, replacing any.
 * @return OK, BAD, LIMIT or NOMEM, the old description kept on failure
 * @note The scene must not yet be shared.
 */
tiku_circuit_err_t circuit_details(tiku_circuit_t *doc, size_t index,
                                   const char *text);

/** @brief The advance of @p text's first line, to its newline or end, in
 *         em; -1 when the font lacks a glyph it needs or is absent. */
double circuit_text_line_width(const tiku_circuit_t *doc, const char *text);

/** @brief The em width of @p text's widest line, and the em height of its
 *         lines as circuit_text() sets them.  @return 1, or 0 when the
 *         font lacks a glyph it needs or is absent */
int circuit_text_extent(const tiku_circuit_t *doc, const char *text,
                        double *width, double *height);

/**
 * @brief Append a path of @p count points, copied, as one object.
 *
 * Bounds include half the stroke unless @p fill.
 *
 * @return OK; BAD for a bad point or width, fewer than two points or a
 *         shared @p doc; LIMIT for a long name or a full scene; NOMEM
 * @note @p doc must not yet be shared.
 */
tiku_circuit_err_t circuit_add(tiku_circuit_t *doc, const char *name,
    const char *value, const circuit_point_t *points, size_t count,
    double width, uint32_t colour, int closed, int fill);

/**
 * @brief Append a circular arc centred on (@p x, @p y): @p sweep radians
 *        from @p start, either way, at most a full turn.
 * @return as circuit_add(); UNSUPPORTED for a filled partial arc
 */
tiku_circuit_err_t circuit_arc(tiku_circuit_t *doc, const char *name,
    const char *value, double x, double y, double r, double start,
    double sweep, double width, uint32_t colour, int fill);

/**
 * @brief Append outline text: @p matrix maps em coordinates, y down, to
 *        world millimetres.
 *
 * @p justify and @p vertical place the box: -1 left or top, 0 centre,
 * 1 right or bottom.  The scene's font draws it.
 *
 * @return OK; MISSING without a font; UNSUPPORTED when the font lacks a
 *         glyph; LIMIT past 4096 bytes; BAD; NOMEM
 */
tiku_circuit_err_t circuit_text(tiku_circuit_t *doc, const char *name,
    const char *value, const char *text, const double matrix[6],
    int justify, int vertical, int bold, uint32_t colour);

/** @brief Grow the scene's bounds to take in @p box; while the scene has
 *         no objects, @p box replaces them. */
void circuit_bounds_add(tiku_circuit_t *doc, tiku_circuit_box_t box);

/** @brief Paint text @p shape of @p doc into @p image through @p view.
 *  @return 0, NOMEM or CANCELLED */
int circuit_text_paint(const tiku_circuit_t *doc, const circuit_shape_t *shape,
    const tiku_circuit_view_t *view, tiku_image_t *image,
    int (*cancel)(void *), void *ctx);

/** @brief Turn stack object @p index into the layer scene holding it and
 *         the index there; a plain scene is checked in place.
 *  @return 1, or 0 for an index past the visible objects */
int circuit_stack_child(const tiku_circuit_t **doc, size_t *index);

/**
 * @brief Render one flat scene.
 *
 * With @p transparent, the pixels are @p colour at the scene's coverage
 * as alpha, for compositing; otherwise opaque on the paper colour.
 *
 * @return as tiku_circuit_render()
 */
tiku_circuit_err_t circuit_render_flat(const tiku_circuit_t *doc,
    const tiku_circuit_view_t *view, int (*cancel)(void *), void *ctx,
    tiku_image_t *out, int transparent, uint32_t colour);

/** @brief Render a stack: each visible layer's coverage in its colour and
 *         opacity, back to front.  @return as tiku_circuit_render() */
tiku_circuit_err_t circuit_stack_render(const tiku_circuit_t *doc,
    const tiku_circuit_view_t *view, int (*cancel)(void *), void *ctx,
    tiku_image_t *out);

/**
 * @brief A project of @p count self-contained sheets: each scene retained
 *        and named, all with the source @p path.
 * @return OK; BAD for a missing scene or name; LIMIT past 64 sheets, a
 *         long name or path; NOMEM
 */
tiku_circuit_err_t circuit_project_sheets(tiku_circuit_t *const *scenes,
    const char *const *names, const char *path, size_t count,
    tiku_circuit_project_t **out);

#endif
