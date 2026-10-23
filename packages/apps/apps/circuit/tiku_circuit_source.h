/* tiku_circuit_source.h -- the files an import may read: the one chosen,
 * and links it names within the chosen file's directory.  The kit's
 * readers open no path themselves; they read through this resolver.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_CIRCUIT_SOURCE_H_
#define TIKU_CIRCUIT_SOURCE_H_
#include "tiku_circuit_project.h"

/** @brief One import's file access: the chosen file's directory, held. */
typedef struct {
    int directory;          /* the root's directory descriptor, or -1 */
    int (*cancel)(void *);  /* whether the import was cancelled; or NULL */
    void *ctx;              /* cancel()'s argument */
} tiku_circuitview_local_t;

/**
 * @brief The resolver the kit's readers call for every file of an import.
 *
 * The first read, with no parent, opens the chosen file and holds its
 * directory; later reads are relative links confined to that directory,
 * refused when they climb out of it, name a symlink, or are not regular
 * files.
 * @note Initialise @p local with directory -1 and close it with
 *       tiku_circuitview_local_close().
 */
tiku_circuit_resolver_t tiku_circuitview_local_resolver(
    tiku_circuitview_local_t *local);
/** @brief Let the held directory go; @p local can be used again. */
void tiku_circuitview_local_close(tiku_circuitview_local_t *local);
/**
 * @brief Read the font file at @p path, which the host chose from the
 *        system's fonts.
 *
 * @return TIKU_CIRCUIT_OK with @p data, which the caller frees, and its
 *         size in @p bytes; or the error, with nothing to free
 */
tiku_circuit_err_t tiku_circuitview_local_font(const char *path,
    tiku_circuitview_local_t *local, unsigned char **data, size_t *bytes);
#endif
