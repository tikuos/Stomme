/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_fontdir.h - a fonts folder read as families.
 *
 * Each TrueType or OpenType file in the folder joins the family named in
 * its own tables, as its regular or its bold, so "DejaVuSans.ttf" and
 * "DejaVuSans-Bold.ttf" make one entry with two weights.  The font
 * preferences and the PDF reader's font fallback read the list.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FONTDIR_H_
#define TIKU_FONTDIR_H_

#include "tiku_model.h"
#include "tiku_state.h"

/** @brief How many families the folder may offer. */
#define TIKU_FONTDIR_MAX 16

/** @brief One family in the folder and the files of its two weights. */
typedef struct {
    char name[64];                  /**< the family name the files give */
    char regular[TIKU_PATH_MAX];    /**< its path, or the bold's if none */
    char bold[TIKU_PATH_MAX];       /**< its path, empty if none         */
} tiku_fontdir_entry_t;

/** @brief The families a folder offers, sorted by name. */
typedef struct {
    tiku_fontdir_entry_t font[TIKU_FONTDIR_MAX];
    int                      count;
} tiku_fontdir_t;

/**
 * @brief Every font file in @p dir into @p out, grouped by the family each
 *        names itself, at most TIKU_FONTDIR_MAX families, sorted by name.
 *
 * Of several files of one family and weight, the first the directory
 * lists is kept.
 *
 * @return how many families were found; 0 for a folder that is not there.
 */
int tiku_fontdir_scan(tiku_fontdir_t *out, const char *dir);

/** @brief The entry called @p name, or NULL. */
const tiku_fontdir_entry_t *tiku_fontdir_find(
    const tiku_fontdir_t *dir, const char *name);

#endif /* TIKU_FONTDIR_H_ */
