/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_prefs.h - the settings, their file, and what a page owes.
 *
 * The settings are a small typed table, and the preferences window's rules
 * are each one loop over it: whether any page differs from its defaults,
 * whether any differs from what it was when the window opened, and which
 * page to show in bold.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_PREFS_H_
#define TIKU_TRK_PREFS_H_

#include <stddef.h>
#include <stdint.h>

/** @brief The four pages the window has (TS-058). */
typedef enum {
    TIKU_TRK_PAGE_DESKTOP = 0,
    TIKU_TRK_PAGE_WINDOWS,
    TIKU_TRK_PAGE_VOLUMES,
    TIKU_TRK_PAGE_MOUNT,
    TIKU_TRK_PAGE_NONE          /* settings with no control of their own */
} tiku_trk_page_t;

/** @brief How many pages have controls. */
#define TIKU_TRK_PAGES 4

/** @brief What a setting holds. */
typedef enum {
    TIKU_TRK_PREF_BOOL = 0,
    TIKU_TRK_PREF_INT,
    TIKU_TRK_PREF_COLOUR,
    TIKU_TRK_PREF_TEXT          /* a name; "" is the default            */
} tiku_trk_pref_type_t;

/** @brief The room for a TEXT setting's value. */
#define TIKU_TRK_PREF_TEXT_MAX 64

/** @brief One setting: its name in the file, its page, and three values. */
typedef struct {
    const char          *name;
    tiku_trk_pref_type_t type;
    tiku_trk_page_t      page;
    int32_t              def;       /* the compiled-in default            */
    int32_t              value;     /* what it is now                     */
    int32_t              opened;    /* what it was when the window opened */
    char                 text[TIKU_TRK_PREF_TEXT_MAX]; /* a TEXT's value */
} tiku_trk_pref_t;

/** @brief Every setting, plus the file's lines this build does not know. */
typedef struct {
    tiku_trk_pref_t *pref;
    int              n;
    /* Lines from the file that name no setting this build knows, kept and
     * written back so an older or newer Tracker's file round-trips
     * (TS-062). */
    char           **unknown;
    int              nunknown;
} tiku_trk_prefs_t;

/** @brief The table of settings, each at its default and opened at it. */
void tiku_trk_prefs_init(tiku_trk_prefs_t *p);
/** @brief Release the table and the lines kept for other builds. */
void tiku_trk_prefs_free(tiku_trk_prefs_t *p);

/** @brief A setting by name, or NULL. */
tiku_trk_pref_t *tiku_trk_prefs_find(tiku_trk_prefs_t *p, const char *name);

/** @brief The value of @p name, or 0 for a setting this build lacks. */
int32_t tiku_trk_prefs_get(tiku_trk_prefs_t *p, const char *name);
/** @brief Set @p name, then the interlocks that follow from it. */
void tiku_trk_prefs_set(tiku_trk_prefs_t *p, const char *name, int32_t v);
/** @brief A TEXT setting's value, "" when unset or not a TEXT. */
const char *tiku_trk_prefs_text(tiku_trk_prefs_t *p, const char *name);
/** @brief Set a TEXT setting; NULL or "" puts it back to the default. */
void tiku_trk_prefs_set_text(tiku_trk_prefs_t *p, const char *name,
                             const char *text);

/** @brief Whether a control answers clicks, by the interlocks (TS-046). */
int tiku_trk_prefs_control_enabled(const tiku_trk_prefs_t *p,
                                   const tiku_trk_pref_t *pref);

/**
 * @brief Read the settings file; missing is not an error.
 *
 * A line naming no setting this build knows is kept for the next save.
 *
 * @return 0, or -1 when @p p or @p path is NULL.
 */
int tiku_trk_prefs_load(tiku_trk_prefs_t *p, const char *path);

/**
 * @brief Write it, booleans as on/off and colours as 0x%08x (TS-045).
 *
 * Only settings that differ from their default are written, then the lines
 * kept from the last load.  The file is replaced whole, so a reader sees
 * the old settings or the new ones and never a part of either.
 *
 * @return 0, or -1 on failure.
 */
int tiku_trk_prefs_save(tiku_trk_prefs_t *p, const char *path);

/** @brief Take the current values as the revert baseline (TS-059). */
void tiku_trk_prefs_mark_opened(tiku_trk_prefs_t *p);

/** @brief The next colour a clicked swatch walks to (alpha preserved). */
int32_t tiku_trk_prefs_colour_next(int32_t value);

/**
 * @brief How many positions a numbered setting has.
 *
 * The table has no room for a range, so this is asked by name; DarkMode is
 * the one such setting.
 *
 * @return the count, or 0 for a setting that is not one of these.
 */
int tiku_trk_prefs_seats(const char *name);

/** @brief Whether @p page has anything differing from its defaults; never
 *         for the Mount page (TS-065). */
int tiku_trk_prefs_defaultable(tiku_trk_prefs_t *p, tiku_trk_page_t page);

/** @brief Whether @p page differs from what it was when the window opened. */
int tiku_trk_prefs_revertable(tiku_trk_prefs_t *p, tiku_trk_page_t page);

/** @brief Whether any page is defaultable; what enables the button. */
int tiku_trk_prefs_any_defaultable(tiku_trk_prefs_t *p);
/** @brief Whether any page is revertable; what enables the button. */
int tiku_trk_prefs_any_revertable(tiku_trk_prefs_t *p);

/** @brief Put @p page back to its defaults; the Mount page is left alone
 *         (TS-065). */
void tiku_trk_prefs_defaults(tiku_trk_prefs_t *p, tiku_trk_page_t page);
/** @brief Put @p page back to what it was when the window opened. */
void tiku_trk_prefs_revert(tiku_trk_prefs_t *p, tiku_trk_page_t page);

/** @brief The name a page shows in the list. */
const char *tiku_trk_prefs_page_name(tiku_trk_page_t page);

/*---------------------------------------------------------------------------*/
/* The window's layout (TS-058, TS-059, TS-060)                              */
/*---------------------------------------------------------------------------*/

/** @brief What a click in the window landed on. */
typedef enum {
    TIKU_TRK_PREF_HIT_NONE = 0,
    TIKU_TRK_PREF_HIT_PAGE,     /* a page in the list; `which` is which  */
    TIKU_TRK_PREF_HIT_SETTING,  /* a control; `which` indexes the table  */
    TIKU_TRK_PREF_HIT_DEFAULTS,
    TIKU_TRK_PREF_HIT_REVERT
} tiku_trk_pref_hit_t;

/** @brief The window's size, and the width of its page list. */
#define TIKU_TRK_PREF_W        420
#define TIKU_TRK_PREF_H        280
#define TIKU_TRK_PREF_LIST_W   120

/** @brief The rectangle of page @p k in the list. */
void tiku_trk_prefs_page_rect(int k, int *x, int *y, int *w, int *h);

/**
 * @brief The rectangle of the @p nth control shown on a page.
 *
 * Controls are laid out in the order they appear in the table.
 */
void tiku_trk_prefs_control_rect(int nth, int *x, int *y, int *w, int *h);

/** @brief Settings shown on @p page, in order.  @return how many. */
int tiku_trk_prefs_page_controls(tiku_trk_prefs_t *p, tiku_trk_page_t page,
                                 int *out, int max);

/** @brief The rectangle of the Defaults or Revert button. */
void tiku_trk_prefs_button_rect(int revert, int *x, int *y, int *w, int *h);

/**
 * @brief What a point hits.
 *
 * @param page  The page currently shown, since the controls depend on it.
 * @param which Receives the page index or the setting index.
 */
tiku_trk_pref_hit_t tiku_trk_prefs_hit(tiku_trk_prefs_t *p,
                                       tiku_trk_page_t page, int x, int y,
                                       int *which);

#endif /* TIKU_TRK_PREFS_H_ */
