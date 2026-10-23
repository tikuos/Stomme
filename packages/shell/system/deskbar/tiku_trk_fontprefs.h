/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_fontprefs.h - the interface font's preferences.
 *
 * The family and size kept in the Deskbar's settings record, and the name
 * of a family dropped into the fonts folder, kept beside the record.  The
 * Fonts window's Defaults and Revert act on these values only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FONTPREFS_H_
#define TIKU_TRK_FONTPREFS_H_

#include "tiku_state.h"

#define TIKU_TRK_FONT_MIN  10   /**< smallest baked size, px        */
#define TIKU_TRK_FONT_MAX  16   /**< largest baked size, px         */
#define TIKU_TRK_FONT_STEP 2    /**< between one baked size and the next */

/** @brief What the Fonts window sets. */
typedef struct {
    int  family;                /* the baked family, when no file is named */
    int  size;                  /* the interface's text size, px */
    char dropped[64];           /* a family from the fonts folder, or "" */
} tiku_trk_fontprefs_values_t;

/** @brief How many families the Fonts window offers at most. */
#define TIKU_TRK_FONT_OFFER_MAX 20

/** @brief The families on offer: the baked ones, then the dropped. */
typedef struct {
    char name[TIKU_TRK_FONT_OFFER_MAX][64];
    int  dropped[TIKU_TRK_FONT_OFFER_MAX];   /* from the fonts folder */
    int  count;
} tiku_trk_fontprefs_offer_t;

/** @brief The font values being edited, and what they were when the
 *         window opened. */
typedef struct {
    tiku_trk_fontprefs_values_t value;  /* as edited                */
    tiku_trk_fontprefs_values_t opened; /* what Revert goes back to */
} tiku_trk_fontprefs_t;

/** @brief The font values a fresh Deskbar has. */
void tiku_trk_fontprefs_default(tiku_trk_fontprefs_values_t *values);
/** @brief Defaults for the value and for what the window opened with. */
void tiku_trk_fontprefs_init(tiku_trk_fontprefs_t *prefs);

/**
 * @brief Read the family and size from the Deskbar's record, and the
 *        dropped family's name from beside it.
 *
 * @return 1 when either was stored, 0 when the defaults stand.
 */
int tiku_trk_fontprefs_load(tiku_trk_fontprefs_t *prefs,
                            tiku_store_t *store);

/**
 * @brief Write the family and size back into the Deskbar's record,
 *        leaving every other setting in it.
 *
 * The record is read, its two font fields changed and the record written
 * back; the dropped family's name is written beside it.
 *
 * @return 1 when the record was written.
 */
int tiku_trk_fontprefs_save(const tiku_trk_fontprefs_t *prefs,
                            tiku_store_t *store);

/** @brief Take the current value as what the window opened with. */
void tiku_trk_fontprefs_mark_opened(tiku_trk_fontprefs_t *prefs);
/** @brief Whether the value differs from the defaults. */
int tiku_trk_fontprefs_defaultable(const tiku_trk_fontprefs_t *prefs);
/** @brief Whether the value differs from what the window opened with. */
int tiku_trk_fontprefs_revertable(const tiku_trk_fontprefs_t *prefs);
/** @brief Set the value back to the defaults. */
void tiku_trk_fontprefs_defaults(tiku_trk_fontprefs_t *prefs);
/** @brief Set the value back to what the window opened with. */
void tiku_trk_fontprefs_revert(tiku_trk_fontprefs_t *prefs);

/** @brief Choose baked family @p family, clearing any dropped one.
 *  @return 1 when the value changed. */
int tiku_trk_fontprefs_set_family(tiku_trk_fontprefs_t *prefs, int family);

/**
 * @brief Step the size by TIKU_TRK_FONT_STEP.  @return 1 when it changed.
 *
 * @param up Nonzero to grow.  At either end the size stays put.
 */
int tiku_trk_fontprefs_step_size(tiku_trk_fontprefs_t *prefs, int up);

/** @brief How many baked families there are. */
int tiku_trk_fontprefs_family_count(void);

/** @brief What baked family @p family is called, or NULL past the last. */
const char *tiku_trk_fontprefs_family_name(int family);

/**
 * @brief The families a person may choose: baked first, dropped after.
 *
 * @return how many.
 */
int tiku_trk_fontprefs_offer(tiku_trk_fontprefs_offer_t *out);

/** @brief Choose row @p row of @p offer.
 *  @return 1 when the value changed. */
int tiku_trk_fontprefs_choose(tiku_trk_fontprefs_t *prefs,
                              const tiku_trk_fontprefs_offer_t *offer,
                              int row);

/** @brief Which row of @p offer is in force, or -1. */
int tiku_trk_fontprefs_chosen(const tiku_trk_fontprefs_t *prefs,
                              const tiku_trk_fontprefs_offer_t *offer);

/**
 * @brief Put the chosen face into force everywhere.
 *
 * A dropped family is looked up in the folder by name each time; when it
 * is no longer there, the baked family is used.
 */
void tiku_trk_fontprefs_apply(const tiku_trk_fontprefs_t *prefs);

#endif /* TIKU_TRK_FONTPREFS_H_ */
