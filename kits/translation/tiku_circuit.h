/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit.h - electronics drawings read into immutable scenes: a
 * subset of KiCad schematics, Excellon drill files and Gerber artwork,
 * with picking, search, fit and zoom on one millimetre transform and a
 * bounded, cancellable renderer.  A decoder reads the bytes it is given;
 * it opens no file and follows no link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_H_
#define TIKU_CIRCUIT_H_

#include <stddef.h>

#include "tiku_image.h"

/* The most pixels one render draws, in all and along either side; a larger
 * viewport is refused with TIKU_CIRCUIT_LIMIT, and tiku_circuit_fit()
 * refuses to fit one. */
#define TIKU_CIRCUIT_RENDER_PIXELS (4u * 1024u * 1024u)
#define TIKU_CIRCUIT_RENDER_SIDE   8192

typedef struct tiku_circuit tiku_circuit_t;

/** @brief Why an import or a render failed. */
typedef enum {
    TIKU_CIRCUIT_OK,
    TIKU_CIRCUIT_FORMAT,            /* not a document this reader takes  */
    TIKU_CIRCUIT_BAD,               /* malformed data or a bad argument  */
    TIKU_CIRCUIT_UNSUPPORTED,       /* a feature outside the subset      */
    TIKU_CIRCUIT_MISSING,           /* a font, definition or file absent */
    TIKU_CIRCUIT_LIMIT,             /* a size or work budget exceeded    */
    TIKU_CIRCUIT_NOMEM,
    TIKU_CIRCUIT_IO,                /* a linked file could not be read   */
    TIKU_CIRCUIT_CANCELLED
} tiku_circuit_err_t;

/** @brief A box in world millimetres; x grows right and y down. */
typedef struct { double x0, y0, x1, y1; } tiku_circuit_box_t;

/** @brief A viewport: a pixel is world millimetres times scale, plus the
 *         translation. */
typedef struct {
    int width, height;              /* viewport size in pixels           */
    double scale, x, y;             /* pixels per mm; pixel translation  */
} tiku_circuit_view_t;

/** @brief One selectable object, as its reader names it. */
typedef struct {
    const char *name, *value;       /* borrowed; never NULL when valid   */
    tiku_circuit_box_t bounds;      /* painted extent, stroke included   */
} tiku_circuit_object_t;

/** @brief What a decoder may use besides the document's bytes.  The font
 *         is copied into a scene that draws visible text. */
typedef struct {
    const unsigned char *font_data; /* substitute TrueType font, or NULL */
    size_t font_bytes;              /* its length, at most 8 MiB         */
    int (*cancel)(void *);          /* nonzero stops the import; or NULL */
    void *ctx;                      /* handed to cancel                  */
} tiku_circuit_import_t;

/** @brief Why an import failed, and where. */
typedef struct {
    tiku_circuit_err_t code;        /* the failure; OK when there is none */
    unsigned line, column;          /* 1-based source position, 0 if none */
    char message[256];              /* one sentence for a person         */
} tiku_circuit_diagnostic_t;

/**
 * @brief Decode the supported subset of a self-contained KiCad
 *        .kicad_sch schematic (doc/circuit.md lists it).
 *
 * Linked sheets need tiku_circuit_project_open().  Visible text needs the
 * font in @p options.  Unsupported content refuses the whole document.
 *
 * @param data     the document's bytes, at most 8 MiB
 * @param bytes    their length
 * @param options  font and cancellation; NULL for a drawing without text
 * @param out      receives the scene; untouched on failure
 * @param diag     receives the failure and its source position, or NULL
 * @return TIKU_CIRCUIT_OK, or why not; no partial scene is returned
 */
tiku_circuit_err_t tiku_circuit_kicad(const void *data, size_t bytes,
    const tiku_circuit_import_t *options, tiku_circuit_t **out,
    tiku_circuit_diagnostic_t *diag);

/**
 * @brief Read the explicit-decimal subset of an XNC/Excellon drill file:
 *        holes, slots and routes.
 *
 * Nonzero coordinates need a decimal point unless the file declares
 * KiCad's decimal FORMAT comment; ambiguous fixed-point input is refused.
 * The source origin is kept; scene units are millimetres with y down.
 *
 * @param options  cancellation only; the font is not used; may be NULL
 * @return TIKU_CIRCUIT_OK with @p out set, or why not with @p out
 *         untouched and @p diag (which may be NULL) filled in
 */
tiku_circuit_err_t tiku_circuit_drill(const void *data, size_t bytes,
    const tiku_circuit_import_t *options, tiku_circuit_t **out,
    tiku_circuit_diagnostic_t *diag);

/**
 * @brief Decode one Gerber artwork layer: the command subset doc/circuit.md
 *        lists.
 *
 * The scene keeps the layer's dark and clear operations in order, so it
 * is drawn through tiku_circuit_render() as a whole.
 *
 * @return as tiku_circuit_drill(): @p out untouched on failure
 */
tiku_circuit_err_t tiku_circuit_gerber(const void *data, size_t bytes,
    const tiku_circuit_import_t *options, tiku_circuit_t **out,
    tiku_circuit_diagnostic_t *diag);

/** @brief The import's warning about what the preview approximates or
 *         leaves out, or an empty string; borrowed until release. */
const char *tiku_circuit_warning(const tiku_circuit_t *doc);

/**
 * @brief The first object at or after @p start whose name, value or drawn
 *        text contains @p text, ignoring ASCII case.
 *
 * A layer stack searches its visible layers.  The search does not wrap.
 *
 * @return the object's index, or SIZE_MAX when nothing matches
 */
size_t tiku_circuit_find(const tiku_circuit_t *doc, const char *text,
                         size_t start);

/** @brief A small authored circuit, for trying navigation without a
 *         file.  @p out is untouched on failure. */
tiku_circuit_err_t tiku_circuit_example(tiku_circuit_t **out);

/** @brief One more holder of @p doc, which may be NULL; a scene is
 *         immutable, so a worker may hold it while others read it. */
tiku_circuit_t *tiku_circuit_retain(tiku_circuit_t *doc);

/** @brief One holder fewer; the last frees the scene.  NULL is allowed. */
void tiku_circuit_release(tiku_circuit_t *doc);

/** @brief How many selectable objects @p doc has; zero for NULL. */
size_t tiku_circuit_count(const tiku_circuit_t *doc);

/** @brief The bytes charged to the scene, a stack's layer scenes included.
 *         The scene's copy of the substitute font is not counted. */
size_t tiku_circuit_storage_bytes(const tiku_circuit_t *doc);

/** @brief Object @p index, borrowed until release, or NULL for an index
 *         out of range. */
const tiku_circuit_object_t *tiku_circuit_object(const tiku_circuit_t *doc,
                                                 size_t index);

/**
 * @brief Object @p index's plain-text description, borrowed until
 *        release; an empty string for a bad index or no description.
 * @note The text is untrusted document data, to be shown as plain text.
 */
const char *tiku_circuit_details(const tiku_circuit_t *doc, size_t index);

/** @brief The drawing's painted extent; a zero box for NULL or an empty
 *         scene. */
tiku_circuit_box_t tiku_circuit_bounds(const tiku_circuit_t *doc);

/**
 * @brief The object nearest world point (@p x, @p y): a visible path
 *        within @p tolerance millimetres, or a filled interior.
 *
 * Artwork answers with its topmost operation; a clear one answers none.
 *
 * @return the object's index, or SIZE_MAX for no hit or a bad argument
 */
size_t tiku_circuit_hit(const tiku_circuit_t *doc, double x, double y,
                        double tolerance);

/**
 * @brief The view that centres the drawing in @p width by @p height
 *        pixels, keeping its aspect ratio.
 * @return 1 with @p out set; 0 with it untouched for a bad size or a view
 *         the renderer would refuse
 */
int tiku_circuit_fit(const tiku_circuit_t *doc, int width, int height,
                     tiku_circuit_view_t *out);

/**
 * @brief Scale @p view by @p factor about viewport pixel (@p x, @p y),
 *        keeping the world point under that pixel where it is.
 *
 * The scale is held to 0.01-4096 pixels per millimetre.
 *
 * @return 1, or 0 with @p view unchanged for a bad argument or a view the
 *         renderer would refuse
 */
int tiku_circuit_zoom(tiku_circuit_view_t *view, double factor,
                      double x, double y);

/** @brief The world point under viewport pixel (@p x, @p y).
 *  @return 1 with @p wx, @p wy set; 0 with them untouched for a bad view
 *          or point */
int tiku_circuit_world(const tiku_circuit_view_t *view, double x, double y,
                       double *wx, double *wy);

/**
 * @brief Draw @p view of @p doc into new opaque pixels.
 *
 * On success the caller owns out->px and frees it with tiku_image_free();
 * on failure @p out is untouched.  Geometry past the viewport is clipped
 * before it is drawn.
 *
 * @param cancel  checked on the rendering thread, or NULL
 * @param ctx     handed to @p cancel
 * @return OK; BAD for a bad view or a nonempty @p out; LIMIT for a
 *         viewport over its limits or a stroke too wide to draw at this
 *         scale; NOMEM; CANCELLED
 * @note @p out must be empty.  Once @p cancel has returned nonzero it
 *       must keep doing so.
 */
tiku_circuit_err_t tiku_circuit_render(const tiku_circuit_t *doc,
    const tiku_circuit_view_t *view, int (*cancel)(void *), void *ctx,
    tiku_image_t *out);

/** @brief A sentence for @p error, unknown codes included; static. */
const char *tiku_circuit_error(tiku_circuit_err_t error);

#endif
