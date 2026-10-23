/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_manifest.h - an application bundle's manifest, read without
 * running any of the application's code.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MANIFEST_H
#define TIKU_MANIFEST_H
#include <stddef.h>

/**
 * @brief An application bundle's manifest: its id, name, module (a .so
 *        file in the bundle), category, icon, the types it opens, its
 *        startup item and its help page.
 *
 * Each field is NUL-terminated and "" when the file leaves it out; a
 * missing icon reads as "application".
 */
typedef struct {
    char id[64], name[96], module[64], category[32], icon[64];
    char types[256], startup[96], help[128];
} tiku_manifest_t;

/**
 * @brief Read the version-1 key=value manifest at @p path into @p out.
 *
 * Blank lines and lines starting with '#' are skipped.  An unknown,
 * repeated, overlong or malformed field, a missing version, id, name or
 * module, a module that is not a leaf name ending in .so, or a category
 * outside the known ones refuses the whole file.
 *
 * @param error  a message when refused, "" when read; may be NULL.
 * @return 0, or -1 when refused, with @p out left as it was.
 */
int tiku_manifest_read(const char *path, tiku_manifest_t *out,
                       char *error, size_t max);
#endif
