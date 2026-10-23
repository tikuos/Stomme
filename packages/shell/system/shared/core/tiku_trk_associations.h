/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_associations.h - the File Associations model: which application
 * opens each document type, edited as a draft over the saved preferences.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_ASSOCIATIONS_H_
#define TIKU_TRK_ASSOCIATIONS_H_

#include "tiku_state.h"
#include "tiku_trk_launch.h"
#include "tiku_trk_types.h"
/** @brief The most document types the model lists. */
#define TIKU_TRK_ASSOC_MAX 128
/** @brief One document type, its saved handler and the draft choice. */
typedef struct {
    char type[TIKU_TYPE_MAX];
    char label[64];                     /* what the list shows */
    char saved[TIKU_TRK_APP_NAME_MAX];  /* the stored preference */
    char chosen[TIKU_TRK_APP_NAME_MAX]; /* empty means automatic */
} tiku_trk_association_t;
/** @brief Every listed type, sorted by label. */
typedef struct {
    tiku_trk_association_t row[TIKU_TRK_ASSOC_MAX];
    int count;
} tiku_trk_associations_t;
/**
 * @brief Fill @p model with every document type some application opens.
 *
 * Types come from the type database, the compiled-in type table and the
 * applications' own lists.  Each row's draft starts as its saved preference.
 */
void tiku_trk_associations_load(tiku_trk_associations_t *model,
    const tiku_trk_apps_t *apps, const tiku_trk_types_t *types,
    tiku_store_t *store);
/**
 * @brief Save the rows whose draft differs from the saved preference.
 *
 * @return 1 when saved; 0 when a write or the flush failed, after putting
 *         the previous preferences back.
 */
int tiku_trk_associations_save(tiku_trk_associations_t *model,
    tiku_store_t *store);
/** @brief Whether any draft differs from its saved preference. */
int tiku_trk_associations_changed(const tiku_trk_associations_t *model);
#endif
