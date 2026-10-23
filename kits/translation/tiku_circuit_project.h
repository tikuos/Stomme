/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_project.h - multi-sheet schematics: a KiCad root and the
 * sheets it links, read through the application's resolver, or every
 * sheet of a self-contained EAGLE file.  Each sheet instance is a scene,
 * or a diagnostic saying why it is unavailable.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_PROJECT_H_
#define TIKU_CIRCUIT_PROJECT_H_

#include "tiku_circuit.h"

#define TIKU_CIRCUIT_SHEETS_MAX    64u  /* sheets, the root included     */
#define TIKU_CIRCUIT_DEPTH_MAX     16u  /* child levels below the root   */
#define TIKU_CIRCUIT_PROJECT_BYTES (64u * 1024u * 1024u)  /* one import  */

/** @brief A schematic's sheet instances, each with its scene or the
 *         diagnostic saying why it has none. */
typedef struct tiku_circuit_project tiku_circuit_project_t;

/** @brief A file the resolver read, borrowed until its release() call.
 *         Both strings are nonempty and terminated; neither is expanded. */
typedef struct {
    const unsigned char *data;      /* the file's bytes                  */
    size_t bytes;                   /* their length                      */
    char path[4096];                /* resolves the file's own links     */
    char key[128];                  /* the physical file, hard links too */
} tiku_circuit_source_t;

/**
 * @brief How the project loader reaches files.  The application enforces
 *        its own permissions and confinement.
 *
 * read() is given a NULL parent for the explicit root; otherwise its
 * reference is document data, relative to the parent's path.
 *
 * @note release() follows every read(), a failed one included.  Both run
 *       on the import thread and should return promptly once cancelled.
 */
typedef struct {
    void *ctx;                      /* handed to both calls              */
    tiku_circuit_err_t (*read)(void *ctx, const char *parent,
        const char *reference, tiku_circuit_source_t *source,
        tiku_circuit_diagnostic_t *diagnostic);   /* fills source        */
    void (*release)(void *ctx, tiku_circuit_source_t *source); /* ends it */
} tiku_circuit_resolver_t;

/** @brief One sheet instance of a project, borrowed from it. */
typedef struct {
    const char *name, *path;        /* the sheet's name and file         */
    const char *instance, *page;    /* its UUID path and page number     */
    size_t parent;                  /* the parent's index; SIZE_MAX root */
    unsigned depth;                 /* levels below the root             */
    const tiku_circuit_diagnostic_t *diagnostic;  /* why unavailable     */
} tiku_circuit_sheet_t;

/**
 * @brief Decode KiCad root @p root and the sheets it links, each instance
 *        a scene of its own; a file used twice is parsed once.
 *
 * A child sheet that fails stays in the project without a scene, and
 * tiku_circuit_project_incomplete() says so.
 *
 * @param options  font and cancellation, as for tiku_circuit_kicad()
 * @return OK, or why not with @p out untouched: the root failed, or the
 *         import was cancelled or ran out of memory or budget
 * @note An incomplete project must be shown as incomplete.
 */
tiku_circuit_err_t tiku_circuit_project_open(const char *root,
    const tiku_circuit_import_t *options,
    const tiku_circuit_resolver_t *resolver, tiku_circuit_project_t **out,
    tiku_circuit_diagnostic_t *diagnostic);

/**
 * @brief Decode every sheet of a self-contained EAGLE XML schematic.
 *
 * @p path labels the source; nothing is read through it.  Unsupported
 * content on any sheet refuses the whole project.
 *
 * @return OK, or why not with @p out untouched
 */
tiku_circuit_err_t tiku_circuit_eagle(const void *data, size_t bytes,
    const char *path, const tiku_circuit_import_t *options,
    tiku_circuit_project_t **out, tiku_circuit_diagnostic_t *diagnostic);

/** @brief Let @p project go, with its holds on the scenes; NULL is
 *         allowed. */
void tiku_circuit_project_free(tiku_circuit_project_t *project);

/** @brief How many sheet instances @p project has, unavailable ones
 *         included. */
size_t tiku_circuit_project_count(const tiku_circuit_project_t *project);

/** @brief Whether one or more sheets could not be decoded. */
int tiku_circuit_project_incomplete(const tiku_circuit_project_t *project);

/** @brief Sheet @p index, borrowed from the project, or NULL for an index
 *         out of range. */
const tiku_circuit_sheet_t *tiku_circuit_project_sheet(
    const tiku_circuit_project_t *project, size_t index);

/**
 * @brief Sheet @p index's scene, borrowed from the project, or NULL for
 *        an unavailable sheet or an index out of range.
 * @note Retain it to keep it past the project.
 */
tiku_circuit_t *tiku_circuit_project_scene(
    const tiku_circuit_project_t *project, size_t index);

#endif
