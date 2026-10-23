/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fontset_private.h - what finding faces shares: the index of every
 * face found, built once from the files' own tables, and the characters
 * each draws, read the first time fs_covers() asks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FONTSET_PRIVATE_H_
#define TIKU_FONTSET_PRIVATE_H_

#include "tiku_fontset.h"

#include <pthread.h>
#include <stddef.h>

/** @brief One face as indexed. */
typedef struct {
    char     *path;                 /**< the file; the index owns it     */
    int       index;                /**< the face in a collection        */
    char      family[64];           /**< its family, as it names itself  */
    char     *names;                /**< family names, folded: "a\0b\0\0" */
    int       weight;               /**< 100 to 900                      */
    int       width;                /**< 1 condensed to 9 expanded; 5 normal */
    int       italic;               /**< italic or oblique               */
    int       mono;                 /**< fixed pitch                     */
    int       serif;                /**< 1 serif, 0 sans, -1 not said    */
    int       cff;                  /**< PostScript outlines             */
    int       ncover;               /**< ranges it draws; -1 until read  */
    uint32_t *cover;                /**< first, last, first, last ...    */
} fs_face_t;

/** @brief Every face found, built on first use and then only read, but
 *         for each face's characters, read under the lock. */
typedef struct {
    fs_face_t      *face;           /**< in the order found              */
    int             n, cap;         /**< how many, and room              */
    pthread_mutex_t lock;           /**< over reading a face's characters */
} fs_index_t;

/** @brief The index, built from the folders the first time it is asked
 *         for. */
fs_index_t *fs_index(void);

/** @brief The folders tiku_fontset_dirs() was given, or "". */
const char *fs_extra_dirs(void);

/** @brief @p name folded for comparing: ASCII lowercased, spaces, '-',
 *         '_' and quotes dropped; other bytes kept.  Into @p out. */
void fs_fold(const char *name, char *out, size_t max);

/** @brief Whether face @p f draws @p cp; its character map is read the
 *         first time. */
int fs_covers(fs_index_t *ix, fs_face_t *f, uint32_t cp);

#endif /* TIKU_FONTSET_PRIVATE_H_ */
