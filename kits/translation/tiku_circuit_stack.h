/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_stack.h - layer stacks: artwork and drill scenes shown
 * together, front to back, on their shared source origin, each in its
 * own colour, opacity and visibility; and the Gerber job declaring one.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_STACK_H_
#define TIKU_CIRCUIT_STACK_H_

#include "tiku_circuit_project.h"

#define TIKU_CIRCUIT_LAYERS_MAX  32u    /* layers in one stack           */
#define TIKU_CIRCUIT_LABEL_BYTES 256    /* a layer label, its zero too   */
#define TIKU_CIRCUIT_STACK_BYTES (64u * 1024u * 1024u)  /* sources, and  */
                                        /* scenes, of one stack          */

/** @brief A layer's declared format. */
typedef enum {
    TIKU_CIRCUIT_LAYER_DETECT,      /* found from the content            */
    TIKU_CIRCUIT_LAYER_GERBER,
    TIKU_CIRCUIT_LAYER_DRILL,
    TIKU_CIRCUIT_LAYER_UNSUPPORTED  /* a format no reader here takes     */
} tiku_circuit_layer_format_t;

/** @brief One layer of a stack. */
typedef struct {
    tiku_circuit_t *scene;          /* borrowed in, retained by a stack; */
                                    /* NULL for an unavailable layer     */
    const char *path, *root;        /* its file; root is empty for an    */
                                    /* explicit file, else the job's     */
    const char *label;              /* file name or the job's declared   */
                                    /* function                          */
    uint32_t colour;                /* display RGB; alpha is ignored     */
    unsigned opacity;               /* 0 to 255                          */
    int visible;                    /* drawn, searched and picked        */
    tiku_circuit_layer_format_t format;     /* declared; kept on reload  */
    char attributes[512];           /* the job's metadata for the layer  */
    tiku_circuit_diagnostic_t diagnostic;   /* why there is no scene     */
} tiku_circuit_layer_t;

/**
 * @brief A stack of @p count layers, front first: descriptors and their
 *        strings copied, scenes retained.
 *
 * A changed layer needs a new stack; a render still drawing the old one
 * keeps it.
 *
 * @return OK, or BAD, LIMIT or NOMEM with @p out unchanged
 * @note Scenes must be flat: neither stacks nor text.  A layer without a
 *       scene needs a diagnostic code.
 */
tiku_circuit_err_t tiku_circuit_stack(const tiku_circuit_layer_t *layers,
                                      size_t count, tiku_circuit_t **out);

/** @brief How many layers @p stack has; zero for a plain scene or NULL. */
size_t tiku_circuit_layer_count(const tiku_circuit_t *stack);

/** @brief Layer @p index, borrowed until the stack is released, or NULL
 *         for an index out of range. */
const tiku_circuit_layer_t *tiku_circuit_layer(const tiku_circuit_t *stack,
                                               size_t index);

/** @brief The layer holding stack object @p object, its objects counted
 *         over the visible, nonzero-opacity layers front to back; SIZE_MAX
 *         for none. */
size_t tiku_circuit_object_layer(const tiku_circuit_t *stack,
                                 size_t object);

/** @brief Whether any layer of @p stack is unavailable. */
int tiku_circuit_stack_incomplete(const tiku_circuit_t *stack);

/** @brief Colour @p index of a display palette, repeating; it says
 *         nothing about the physical layer. */
uint32_t tiku_circuit_layer_colour(size_t index);

/**
 * @brief Open a Gerber job: read its FilesAttributes through @p resolver
 *        and stack the files in the job's order.
 *
 * A file that cannot be read or decoded, one over its own limits
 * included, stays as an unavailable entry.  Allocation failure,
 * cancellation and the job's 64 MiB read and scene budgets fail the whole
 * job, leaving *out untouched; when a file's failure ends the job,
 * @p diag reads "<path>[, line N]: <message>".
 *
 * @note The resolver keeps reads confined; the job passes it the paths
 *       as written.
 */
tiku_circuit_err_t tiku_circuit_job_open(const char *root,
    const tiku_circuit_import_t *options,
    const tiku_circuit_resolver_t *resolver, tiku_circuit_t **out,
    tiku_circuit_diagnostic_t *diag);

#endif
