/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskprefs.h - what the Desktop itself looks like.
 *
 * The picture behind it and how that picture is laid are kept in the
 * Desktop node's own record, not in the settings file, so they travel with
 * the Desktop.  The icon size is the Desktop view's, and Reduce motion is a
 * setting; the window edits all four as one set of values.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKPREFS_H_
#define TIKU_TRK_DESKPREFS_H_

#include "tiku_state.h"
#include "tiku_trk_background.h"

/** @brief How many icon sizes the Desktop offers (PVL-003). */
#define TIKU_TRK_DESK_ICON_STEPS 6

/** @brief How many pictures the chooser will list. */
#define TIKU_TRK_DESK_PICTURES 12

/** @brief The pictures the chooser lists, sorted by path. */
typedef struct {
    char picture[TIKU_TRK_DESK_PICTURES][TIKU_PATH_MAX];
    int  count;
} tiku_trk_deskprefs_pictures_t;

/** @brief One set of the Desktop's settings. */
typedef struct {
    tiku_trk_bg_t bg;
    int           have_bg;      /* whether a picture is shown            */
    int           icon_size;
    /* Reduce motion: kept in the settings file, not the Desktop's record,
     * and here so Defaults and Revert cover it. */
    int           reduce_motion;
} tiku_trk_deskprefs_values_t;

/** @brief The values the window shows, and those it opened with. */
typedef struct {
    tiku_trk_deskprefs_values_t value;
    tiku_trk_deskprefs_values_t opened;
} tiku_trk_deskprefs_t;

/** @brief Icon size @p i of the TIKU_TRK_DESK_ICON_STEPS offered sizes,
 *         smallest first; 0 when @p i is out of range. */
int tiku_trk_deskprefs_icon_step(int i);

/** @brief The defaults: no picture, scaled, 32 px icons, and motion on. */
void tiku_trk_deskprefs_default(tiku_trk_deskprefs_values_t *values);
/** @brief Start at the defaults, and take those as the opened values. */
void tiku_trk_deskprefs_init(tiku_trk_deskprefs_t *prefs);

/**
 * @brief Read @p node's picture and mode, with @p icon_size from the view.
 *
 * The values read are also taken as the opened values.  An @p icon_size of
 * 0 keeps the default.
 *
 * @return 1 when the node has a background with a picture.
 */
int tiku_trk_deskprefs_load(tiku_trk_deskprefs_t *prefs,
                            tiku_store_t *store, const char *node,
                            int icon_size);

/**
 * @brief Write the picture and its mode onto @p node.
 *
 * @return 1 when the record was written.
 * @note The icon size and Reduce motion are the caller's to apply.
 */
int tiku_trk_deskprefs_save(const tiku_trk_deskprefs_t *prefs,
                            tiku_store_t *store, const char *node);

/** @brief Take the current values as the ones the window opened with. */
void tiku_trk_deskprefs_mark_opened(tiku_trk_deskprefs_t *prefs);
/** @brief Whether the values differ from the defaults. */
int tiku_trk_deskprefs_defaultable(const tiku_trk_deskprefs_t *prefs);
/** @brief Whether the values differ from what the window opened with. */
int tiku_trk_deskprefs_revertable(const tiku_trk_deskprefs_t *prefs);
/** @brief Put the values back to the defaults. */
void tiku_trk_deskprefs_defaults(tiku_trk_deskprefs_t *prefs);
/** @brief Put the values back to what the window opened with. */
void tiku_trk_deskprefs_revert(tiku_trk_deskprefs_t *prefs);

/** @brief Show @p path, keeping whatever mode is already chosen. */
void tiku_trk_deskprefs_set_picture(tiku_trk_deskprefs_t *prefs,
                                    const char *path);
/** @brief Show no picture; the mode is kept for the next one. */
void tiku_trk_deskprefs_clear_picture(tiku_trk_deskprefs_t *prefs);
/** @brief Choose how the picture is laid on the Desktop. */
void tiku_trk_deskprefs_set_mode(tiku_trk_deskprefs_t *prefs,
                                 tiku_trk_bg_mode_t mode);

/**
 * @brief Move the icon size to the next larger or smaller offered size.
 *        A size not among them counts as the smallest; the ends do not
 *        wrap.  @return 1 when the size changed.
 *
 * @param up Nonzero to grow.
 */
int tiku_trk_deskprefs_step_icon(tiku_trk_deskprefs_t *prefs, int up);

/**
 * @brief The pictures in @p dirs, up to TIKU_TRK_DESK_PICTURES, sorted by
 *        path.
 *
 * A file counts by its content, not its name: a .txt holding a PNG is
 * listed, and an empty .jpg is not.  Hidden files are skipped.
 *
 * @param n How many directories.
 * @return how many were found.
 */
int tiku_trk_deskprefs_pictures(tiku_trk_deskprefs_pictures_t *out,
                                const char *const *dirs, int n);

#endif /* TIKU_TRK_DESKPREFS_H_ */
