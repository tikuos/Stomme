/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar_prefs.h - the Deskbar's preferences: their values, the
 * stored record, and the rules its preferences window follows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_DESKBAR_PREFS_H_
#define TIKU_TRK_DESKBAR_PREFS_H_

#include "tiku_state.h"
#include "tiku_trk_deskbar.h"

#define TIKU_TRK_DB_ICON_MIN 16          /**< smallest list icon, px      */
#define TIKU_TRK_DB_ICON_MAX 96          /**< largest list icon, px       */
#define TIKU_TRK_DB_RECENT_LIMIT 50      /**< longest recent list         */

/** @brief The Deskbar settings, every one covered by Defaults and Revert
 *         (DB-062). */
typedef struct tiku_trk_deskbar_settings {
    int sort_apps;          /* the application list in name order */
    int tracker_first;      /* Tracker's own row ahead of the rest */
    int show_expander;      /* the arrow that opens a team's windows */
    int expand_new_apps;    /* a team arriving starts expanded */
    int hide_app_names;     /* icons only in the list */
    int icon_size;          /* the list's icons, in px */
    /* The recent-items menus, one flag and one length per kind
     * (TIKU_TRK_DB_RECENT_*): shown at all, and how many it keeps. */
    int recent_enabled[TIKU_TRK_DB_RECENT_KINDS];
    int recent_count[TIKU_TRK_DB_RECENT_KINDS];
    int always_on_top;      /* the bar stays above every window */
    int auto_raise;         /* it comes up when the pointer reaches it */
    int auto_hide;          /* it withdraws when the pointer leaves */
    int show_clock;         /* the clock at the bottom of the tray */
    int show_seconds;       /* the clock with seconds */
    int show_day_of_week;   /* the clock with the weekday */
    int show_timezone;      /* the clock with the zone */
    int font_size;      /* UI text size in px: even, 10 to 16 */
    int font_family;    /* which baked family the interface is drawn in */
    int show_replicants;    /* the tray's replicant rows are shown */
} tiku_trk_deskbar_settings_t;

/** @brief The settings being edited, and what they were when the window
 *         opened. */
typedef struct {
    tiku_trk_deskbar_settings_t value;  /* as edited                */
    tiku_trk_deskbar_settings_t opened; /* what Revert goes back to */
} tiku_trk_deskbar_prefs_t;

/** @brief The settings a fresh Deskbar has. */
void tiku_trk_deskbar_settings_default(tiku_trk_deskbar_settings_t *settings);
/** @brief Clamp the settings into range: icon size, recent counts, flags. */
void tiku_trk_deskbar_settings_normalize(
    tiku_trk_deskbar_settings_t *settings);
/** @brief Defaults for the value and for what the window opened with. */
void tiku_trk_deskbar_prefs_init(tiku_trk_deskbar_prefs_t *prefs);
/** @brief Take the current value as what the window opened with. */
void tiku_trk_deskbar_prefs_mark_opened(tiku_trk_deskbar_prefs_t *prefs);
/** @brief Whether the value differs from the defaults. */
int tiku_trk_deskbar_prefs_defaultable(
    const tiku_trk_deskbar_prefs_t *prefs);
/** @brief Whether the value differs from what the window opened with. */
int tiku_trk_deskbar_prefs_revertable(
    const tiku_trk_deskbar_prefs_t *prefs);
/** @brief Set the value back to the defaults. */
void tiku_trk_deskbar_prefs_defaults(tiku_trk_deskbar_prefs_t *prefs);
/** @brief Set the value back to what the window opened with. */
void tiku_trk_deskbar_prefs_revert(tiku_trk_deskbar_prefs_t *prefs);
/** @brief Whether recent items of kind @p which are shown at all. */
int tiku_trk_deskbar_prefs_recent_enabled(
    const tiku_trk_deskbar_prefs_t *prefs, tiku_trk_db_recent_t which);
/** @brief Whether expanders apply: only in the two expanded docks. */
int tiku_trk_deskbar_prefs_expanders_enabled(
    const tiku_trk_deskbar_prefs_t *prefs, tiku_trk_db_dock_t dock);
/** @brief Whether auto-raise applies: never while always on top. */
int tiku_trk_deskbar_prefs_auto_raise_enabled(
    const tiku_trk_deskbar_prefs_t *prefs);
/**
 * @brief Read the settings record from @p store, older layouts accepted,
 *        and take it as both the value and what was opened.
 * @return 0, or -1 for no store or no usable record.
 */
int tiku_trk_deskbar_prefs_load(tiku_trk_deskbar_prefs_t *prefs,
                                tiku_store_t *store);
/**
 * @brief Write the settings record to @p store.
 * @return tiku_state_write()'s result, or -1 for no prefs or no store.
 */
int tiku_trk_deskbar_prefs_save(const tiku_trk_deskbar_prefs_t *prefs,
                                tiku_store_t *store);

/**
 * @brief Map a point in the dock picker's monitor to a dock: the left and
 *        right thirds give the side docks (expanded in the middle band,
 *        mini above and below it), the middle third the top or bottom bar.
 */
tiku_trk_db_dock_t tiku_trk_deskbar_prefs_dock_at(
    int x, int y, int width, int height, tiku_trk_db_dock_t current);

/** @brief Map the selector's arrow/numpad key names to a dock.
 *  @return 1 with the dock in @p dock, 0 for a key that names none. */
int tiku_trk_deskbar_prefs_dock_key(const char *key,
                                    tiku_trk_db_dock_t *dock);

/** @brief The preferences window's frame: the saved one when @p saved and
 *         it lies wholly on the screen, else centred. */
tiku_rect_t tiku_trk_deskbar_prefs_frame(int saved, int x, int y,
                                              int screen_w, int screen_h,
                                              int width, int height);

#endif /* TIKU_TRK_DESKBAR_PREFS_H_ */
