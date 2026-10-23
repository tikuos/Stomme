/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_circuit_job_private.h - a Gerber job's file list as read from its
 * JSON, kept apart from resolving the files and from their geometry.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_CIRCUIT_JOB_PRIVATE_H_
#define TIKU_CIRCUIT_JOB_PRIVATE_H_

#include "tiku_circuit_stack.h"

/** @brief One FilesAttributes entry; an absent string is empty. */
typedef struct {
    char path[4096];                /**< Path, as the job wrote it       */
    char function[192];             /**< FileFunction                    */
    char polarity[32];              /**< FilePolarity                    */
    char format[32];                /**< FileFormat                      */
} circuit_job_entry_t;

/** @brief A job's entries, in the job's order. */
typedef struct {
    circuit_job_entry_t entries[TIKU_CIRCUIT_LAYERS_MAX];  /**< the files */
    size_t count;                   /**< entries read                    */
} circuit_job_list_t;

/**
 * @brief Read a job's JSON into @p list: every value is checked, and the
 *        FilesAttributes entries' Path, FileFunction, FilePolarity and
 *        FileFormat are kept.
 * @return 0, or the error, described in @p diag (which may be NULL).
 */
tiku_circuit_err_t circuit_job_parse(const void *data, size_t bytes,
                                     const tiku_circuit_import_t *options,
                                     circuit_job_list_t *list,
                                     tiku_circuit_diagnostic_t *diag);

#endif /* TIKU_CIRCUIT_JOB_PRIVATE_H_ */
