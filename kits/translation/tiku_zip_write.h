/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_zip_write.h - stored ZIP packages from borrowed memory entries.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_ZIP_WRITE_H_
#define TIKU_ZIP_WRITE_H_
#include "tiku_archive.h"
#include <stdio.h>

/** @brief One regular file; data and name remain borrowed during writing. */
typedef struct {
    const char *name;
    const void *data;
    size_t length;
} tiku_zip_entry_t;

/** @brief Write a ZIP32 archive with CRCs and uncompressed regular files.
 *  @note At most 1024 unique names, each 1..240 ASCII bytes using letters,
 *        digits, '.', '_', '-' and '/'. Empty, '.' and '..' components
 *        are refused. Total output must be smaller than UINT32_MAX.
 *        The stream starts at the archive origin; no seeking is needed.
 *  @return OK or a failure. Invalid inputs write nothing; I/O failures can
 *          leave partial output. The caller flushes and closes the stream. */
tiku_archive_err_t tiku_zip_write(FILE *out,
    const tiku_zip_entry_t *entries, size_t count);
#endif
