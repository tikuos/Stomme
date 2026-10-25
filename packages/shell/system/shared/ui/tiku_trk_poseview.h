/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_poseview.h - the pose view, the listing engine every folder,
 * query and panel window draws from.
 *
 * One view serves every mode, with two pose lists behind one accessor (all
 * and filtered), a selection flag on each pose, a selection pivot, and
 * type-ahead.  Rules cite rows of docs/contracts/tracker.md, which name the
 * Tracker BPoseView code each one follows.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_POSEVIEW_H_
#define TIKU_TRK_POSEVIEW_H_

#include "tiku_attr.h"
#include "tiku_model.h"
#include "tiku_notify.h"
#include "tiku_trk_input.h"
#include "tiku_trk_match.h"
#include "tiku_trk_nodeicon.h"
#include "tiku_trk_repaint.h"
#include "tiku_trk_view_defs.h"
#include <stdint.h>

/** @brief Maximum number of entries held by one pose view. */
#define TIKU_TRK_POSES_MAX 4096

/**
 * @brief The MIME type that names a copied column layout.
 *
 * The layout clipboard is separate from the file clipboard: copying a
 * layout leaves the copied files where they are.
 */
#define TIKU_TRK_LAYOUT_CLIPBOARD_TYPE "application/tracker-columns"

/** @brief One column of a copied layout. */
typedef struct {
    int  field;                 /**< TIKU_TRK_COL_* the column shows      */
    int  width;                 /**< in pixels                            */
    int  alignment;             /**< TIKU_TRK_ALIGN_*                     */
    char title[24];             /**< the heading                          */
} tiku_trk_layout_column_t;

/** @brief A copied layout: the columns and the sort keys of one view. */
typedef struct {
    tiku_trk_layout_column_t column[TIKU_TRK_COLUMNS_MAX]; /**< in order  */
    int  count;                 /**< column[] in use                      */
    int  sort_field;            /**< the primary sort key                 */
    int  sort_secondary;        /**< the tie-break, or -1                 */
    int  reverse;               /**< descending when set                  */
    int  serial;                /**< bumped by every copy                 */
} tiku_trk_layout_clip_t;

/** @brief The longest type-ahead string or filter term, with its NUL. */
#define TIKU_TRK_TYPEAHEAD_MAX 64

/** @brief How many filter terms Shift+Space can accumulate. */
#define TIKU_TRK_FILTER_TERMS  8

/**
 * @brief Type-ahead lapses after this quiet period, in microseconds; each
 *        keystroke or backspace restarts it.
 */
#define TIKU_TRK_TYPEAHEAD_US  1000000

/** @brief A saved position or origin further out than this is ignored. */
#define TIKU_TRK_SANE_LOCATION 50000

/** @brief One entry on screen: a Model plus where it is and how it looks. */
typedef struct {
    tiku_model_t model;
    /* The node's typed descriptor, parsed once when the pose loads.  It
     * formats a reading for display ("1.5 MiB", "15h 57m") and is checked
     * against an edit before the edit is sent. */
    tiku_desc_t  desc;
    /* MA-033: the value shown for a node, cut at the first newline and at
     * 255 bytes.  Every view allocates a fixed array of poses, so each byte
     * here is paid once per pose. */
    char             value[256];    /* live value text for the Value column */
    int              value_valid;   /* value[] holds a current reading      */
    int              selected;      /* in the selection                     */
    int              index;         /* position in the pose array, which   */
                                    /* is the shown row unless filtering   */
    /* Location in unscaled 32 px space: a layout arranged at one icon size
     * keeps its arrangement at another (PVL-012). */
    int              loc_x, loc_y;
    int              placed;        /* has a position at all                */
    /* Marked invisible in its saved position record: the entry exists but
     * is not shown, and a creation notice for it adds no row. */
    int              invisible;
    /* Not set by the view: a link whose target has gone is marked in
     * model.facts.link_broken. */
    int              broken;
    /* Where the pose sat when the current drag began.  A drag is measured
     * from here: measured from the live location with snapping on, each
     * small step would round back into the same cell and the icon would
     * never move. */
    int              drag_x0, drag_y0;
    int              auto_placed;   /* placed by auto-placement             */
    /* Shown here while living somewhere else -- the Desktop's Trash and
     * its Disks icon.  Re-listing diffs leave it out, so a listing of the
     * view's own directory does not remove it. */
    int              adopted;
    /* When this pose last joined the selection.  The slow-second-click
     * rename reads it to refuse arming on the click that selected the
     * pose. */
    int64_t          sel_time;
    /* Formatted cell text, one slot per column, reused while the field,
     * the width and a hash of the pose's facts and value all match. */
    struct {
        int      field;
        int      width;
        uint64_t key;
        int      valid;
        char     text[160];
    } cell_cache[TIKU_TRK_COLUMNS_MAX];
} tiku_trk_pose_t;

/** @brief A pose view; its fields are private to the poseview family. */
typedef struct tiku_trk_poseview tiku_trk_poseview_t;

/** @brief Create a view over @p backend, showing @p path. */
tiku_trk_poseview_t *tiku_trk_poseview_new(tiku_backend_t *backend,
                                           const char *path);

/** @brief Free the view and everything it allocated. */
void tiku_trk_poseview_free(tiku_trk_poseview_t *v);

/**
 * @brief Re-read the directory into poses, in one go.
 *
 * A query view keeps its results.  When a lister takes the fetch, the rows
 * arrive later through tiku_trk_poseview_load_landed().
 *
 * @return the rows now listed, or -1.
 */
int tiku_trk_poseview_reload(tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Loading a folder in batches (PVL-088, PVN-047, PVL-089, PVL-090)          */
/*---------------------------------------------------------------------------*/

/** @brief Rows published per batch. */
#define TIKU_TRK_LOAD_CHUNK 50

/**
 * @brief The time one tiku_trk_poseview_load_step() call may spend, in
 *        microseconds, measured from that call's start: it stops after
 *        the row that crosses it, or after TIKU_TRK_LOAD_CHUNK rows.
 */
#define TIKU_TRK_LOAD_CHUNK_US 300000

/** @brief The pole appears only once a load has run this long (TS-038). */
#define TIKU_TRK_POLE_AFTER_US 500000

/**
 * @brief Begin loading the current folder.
 *
 * The entries are read now, or by the installed lister, and published in
 * batches: the window never sits blank, rows appear as they arrive, and a
 * load long enough to notice shows a barber pole.
 *
 * @return Entries staged (0 for a query view, or when the lister took the
 *         fetch), or -1.
 */
int tiku_trk_poseview_load_begin(tiku_trk_poseview_t *v, int64_t now);

/**
 * @brief Publish the next batch.
 *
 * The first row is published on its own, at once; each later call
 * publishes up to TIKU_TRK_LOAD_CHUNK rows, stopping early at the row
 * that takes the call past TIKU_TRK_LOAD_CHUNK_US.
 *
 * @return 1 while more remain or the lister's entries have not landed, 0
 *         when the load is complete.
 */
int tiku_trk_poseview_load_step(tiku_trk_poseview_t *v, int64_t now);

/** @brief Whether a load is in flight. */
int tiku_trk_poseview_loading(const tiku_trk_poseview_t *v);

/*
 * Listing elsewhere.  A shell with workers installs a lister: asked for
 * a listing (a load, or a poll's re-list), it may take the fetch off the
 * loop and hand the entries back through the landed calls, or decline,
 * and the view lists synchronously.
 */
#define TIKU_TRK_LIST_LOAD   0          /**< a whole listing to publish */
#define TIKU_TRK_LIST_RELIST 1          /**< a poll's re-list, to diff  */

/**
 * @brief A lister, asked for @p v's listing of @p kind (TIKU_TRK_LIST_*)
 *        under the load generation @p gen.
 * @return nonzero when the lister took the fetch.
 */
typedef int (*tiku_trk_poseview_lister_fn)(void *ctx, tiku_trk_poseview_t *v,
                                           int gen, int kind);

/** @brief Install the lister for every view, or NULL for none. */
void tiku_trk_poseview_set_lister(tiku_trk_poseview_lister_fn fn, void *ctx);

/**
 * @brief The entries a lister fetched for a load; @p n < 0 for a fetch
 *        that failed.  Ignored when @p gen is not the view's current
 *        listing.  @return 0 when taken, -1 when ignored or failed.
 */
int tiku_trk_poseview_load_landed(tiku_trk_poseview_t *v, int gen,
                                  const tiku_model_t *entries, int n);

/**
 * @brief The entries a lister fetched for a poll's re-list, diffed
 *        against what the view shows; @p n < 0 for a fetch that failed.
 * @return the notify flags the poll would have returned.
 */
unsigned tiku_trk_poseview_relist_landed(tiku_trk_poseview_t *v, int gen,
                                         tiku_model_t *entries, int n,
                                         int64_t now);

/** @brief Whether @p v is a view that exists: a lister's result may
 *         land after the window it was for has closed. */
int tiku_trk_poseview_alive(const tiku_trk_poseview_t *v);

/**
 * @brief Whether the barber pole should be drawn (TS-038): only while a
 *        load is in flight and once TIKU_TRK_POLE_AFTER_US has passed.
 */
int tiku_trk_poseview_pole(const tiku_trk_poseview_t *v, int64_t now);

/**
 * @brief The load's generation.
 *
 * A batch or a change notice carrying an older generation belongs to a
 * listing this window has replaced; applying it would put the old rows
 * into the new listing (PVL-089).
 */
int tiku_trk_poseview_generation(const tiku_trk_poseview_t *v);

/**
 * @brief Fill the view from a result set instead of a directory.
 *
 * Results can live anywhere in the namespace, so a result whose name
 * another result shares shows the shortest path that tells them apart.  The
 * view then stays in list mode and no longer reloads from a directory.
 *
 * @return the rows now held, or -1.
 */
int tiku_trk_poseview_set_results(tiku_trk_poseview_t *v,
                                  const tiku_model_t *m, int n);

/**
 * @brief Fill the view from a set the caller assembled (AW-018).
 *
 * Like tiku_trk_poseview_set_results(), but the view stays a folder view:
 * it keeps its mode, saves its state, and its poses are opened and dropped
 * on like any folder's.  The Desktop fills itself this way from several
 * directories.  The models' parent directories are kept: a node created
 * or moved in any of them belongs to the view.
 *
 * @return the rows now held, or -1.
 */
int tiku_trk_poseview_set_models(tiku_trk_poseview_t *v,
                                 const tiku_model_t *m, int n);

/**
 * @brief Add one pose that does not live in this view's directory.
 *
 * The Desktop shows its Trash and Disks icons this way.  An adopted pose is
 * left out of re-listing diffs, and starts unplaced, so the next placement
 * pass puts it in the next free slot.
 *
 * @return 0 when it was added, -1 when it is already here or will not fit.
 */
int tiku_trk_poseview_adopt(tiku_trk_poseview_t *v,
                            const tiku_model_t *m);

/** @brief Whether this view is showing query results. */
int tiku_trk_poseview_is_query(const tiku_trk_poseview_t *v);

/** @brief Select the saved column/frame layout shared by this query type. */
void tiku_trk_poseview_set_query_type(tiku_trk_poseview_t *v,
                                      const char *type);

/** @brief Supply the sidecar store used by synthetic query-layout keys. */
void tiku_trk_poseview_set_layout_store(tiku_trk_poseview_t *v,
                                        tiku_store_t *store);

/** @brief State key for this view (folder path or per-query-type layout). */
const char *tiku_trk_poseview_layout_path(const tiku_trk_poseview_t *v);

/** @brief Install the four first-run query frame/column templates. */
int tiku_trk_poseview_install_query_defaults(tiku_backend_t *backend,
                                             tiku_store_t *store);

/** @brief Show @p path, keeping the same backend. */
int tiku_trk_poseview_switch_dir(tiku_trk_poseview_t *v, const char *path);

/** @brief Open the parent directory, if there is one. */
int tiku_trk_poseview_go_up(tiku_trk_poseview_t *v);

/** @brief The folder the view lists; the empty string for no view. */
const char *tiku_trk_poseview_path(const tiku_trk_poseview_t *v);

/**
 * @brief How many rows the shown list has: the filtered count while a
 *        filter is on, else every pose.
 */
int tiku_trk_poseview_count(const tiku_trk_poseview_t *v);
/**
 * @brief The pose at row @p i of what is shown, filtered or not.
 *        @return NULL off the end.
 */
tiku_trk_pose_t *tiku_trk_poseview_at(tiku_trk_poseview_t *v, int i);

/** @brief Number of loaded poses, including filtered or invisible entries. */
int tiku_trk_poseview_total_count(const tiku_trk_poseview_t *);
/** @brief Borrow one loaded pose by storage index, or NULL out of range. */
const tiku_trk_pose_t *tiku_trk_poseview_entry(const tiku_trk_poseview_t *, int index);

/** @brief How many poses are selected, filtered out or not. */
int tiku_trk_poseview_selection_count(const tiku_trk_poseview_t *v);

/** @brief The pose the user acted on last, or NULL. */
tiku_trk_pose_t *tiku_trk_poseview_active(tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Selection, the band and repaint damage                                    */
/*---------------------------------------------------------------------------*/

/**
 * @brief Whether more than one pose may be selected at a time (PVS-009).
 *
 * On by default, and turned off for a picker asking for one thing.  While
 * it is off, every way of extending a selection -- the modifier clicks,
 * shift+arrow, a swept band, Select All -- makes a single selection, and
 * turning it off cuts an existing selection down to one.
 */
void tiku_trk_poseview_set_multiple(tiku_trk_poseview_t *v, int on);
/** @brief Whether more than one pose may be selected. */
int tiku_trk_poseview_multiple(const tiku_trk_poseview_t *v);

/** @brief Apply a click at row @p index (-1 = empty space) with modifiers. */
void tiku_trk_poseview_click(tiku_trk_poseview_t *v, int index,
                             unsigned mods);

/**
 * @brief Apply a click whose view-space point is known (PVS-005).
 *
 * In list mode a Shift selection is an index range; in an icon mode it is
 * the rectangle from the pivot's location to (@p x, @p y).
 * tiku_trk_poseview_click() is the coordinate-free form.
 */
void tiku_trk_poseview_click_at(tiku_trk_poseview_t *v, int index, int x,
                                int y, unsigned mods);

/**
 * @brief Select whatever now sits at @p index, after a removal (FS-050).
 *
 * List mode only.  An index past the end selects the last row.
 *
 * @return the index selected, or -1 when there was nothing to select.
 */
int tiku_trk_poseview_select_after_removal(tiku_trk_poseview_t *v, int index);

/** @brief Clear the selection; the pivot goes with it (PVS-008). */
void tiku_trk_poseview_select_none(tiku_trk_poseview_t *v);

/**
 * @brief Select every pose shown; in a single-selection view, the first.
 */
void tiku_trk_poseview_select_all(tiku_trk_poseview_t *v);

/**
 * @brief Select what was not selected and deselect what was (PVK-046).
 *
 * The pivot, the parked pivot and the last-added mark are cleared first;
 * the pivot is set again only when the first shown row ends up selected.
 */
void tiku_trk_poseview_invert_selection(tiku_trk_poseview_t *v);

/**
 * @brief Replace the selection with every pose matching @p expr (PVK-047).
 *
 * @param how     Which of the five rules to apply.
 * @param ignore_case  Fold case, the Select window's default.
 * @param invert  Select the poses that do not match.
 * @param err     Receives a regular-expression error, with the selection
 *                left as it was.
 * @return Number of poses selected, or -1 when the expression is bad.
 */
int tiku_trk_poseview_select_matching(tiku_trk_poseview_t *v,
                                      const char *expr, tiku_trk_match_t how,
                                      int ignore_case, int invert,
                                      char *err, size_t errmax);

/**
 * @brief Normalize the selection for a context menu (PVS-018).
 *
 * On a pose that is not selected, that pose becomes the whole selection; on
 * a pose already in the selection the group is kept, and a menu command
 * applies to all of it.  On empty space the selection is cleared.
 *
 * @return 1 when the selection changed.
 */
int tiku_trk_poseview_context_click(tiku_trk_poseview_t *v, int index);

/**
 * @brief Whether pose @p p is painted as selected now (PVS-023).
 *
 * An inactive window keeps its selection but does not paint it, unless
 * tiku_trk_poseview_show_selection_when_inactive() is on or a band is being
 * swept.  Drawing asks this; everything else reads the pose's flag.
 */
int tiku_trk_poseview_draws_selected(const tiku_trk_poseview_t *v,
                                     const tiku_trk_pose_t *p);

/**
 * @brief Window activation: hide or restore the painted selection.
 *
 * The pivot is parked when the window goes inactive and restored when it
 * is active again, so a later shift-click extends from the same anchor.
 */
void tiku_trk_poseview_window_activated(tiku_trk_poseview_t *v, int active);

/** @brief Keep drawing the selection while the window is inactive. */
void tiku_trk_poseview_show_selection_when_inactive(tiku_trk_poseview_t *v,
                                                    int on);

/**
 * @brief Start or end a band sweep.
 *
 * Starting it takes the snapshot of the selection the band toggles against
 * (PVS-027).  While it runs, the swept poses paint as selected even in an
 * inactive window (PVS-031).  Ending it repaints where the band lay and
 * drops a pivot the sweep left unselected (PVS-029).
 */
void tiku_trk_poseview_set_band(tiku_trk_poseview_t *v, int on);
/**
 * @brief Mark list row @p index as needing painting again.
 *
 * The rectangles a view accumulates are what the owner's next draw
 * repaints (PVL-075).
 */
void tiku_trk_poseview_damage_row(tiku_trk_poseview_t *v, int index);

/**
 * @brief Take the pending slide: the rows from *@p y down have moved by the
 *        returned pixel count, and can be blitted there.
 */
int tiku_trk_poseview_repaint_take_slide(tiku_trk_poseview_t *v, int *y);

/** @brief Take the rectangles due a repaint, up to @p max. */
int tiku_trk_poseview_repaint_take(tiku_trk_poseview_t *v,
                                   tiku_trk_repaint_rect_t *out, int max);
/** @brief Whether a band is being drawn. */
int tiku_trk_poseview_band(const tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Keyboard: navigation, type-ahead and filtering                            */
/*---------------------------------------------------------------------------*/

/** @brief Arrow / Home / End / Page navigation.  @return 1 if handled. */
int tiku_trk_poseview_key_nav(tiku_trk_poseview_t *v, int key, unsigned mods);

/**
 * @brief Feed a typed character to type-ahead.
 *
 * Scoring: a name starting with the string always wins; else a name
 * containing it scores 1/offset, so earlier occurrences win; a name not
 * containing it is not a candidate.  In list mode every column's text
 * competes as the name does (PVK-029).  While filtering, the character
 * extends the current filter term instead.
 */
void tiku_trk_poseview_type_ahead(tiku_trk_poseview_t *v, char ch);

/**
 * @brief Delete one character; restarts the timeout.  While filtering,
 *        backspacing an empty term removes the term.
 */
void tiku_trk_poseview_type_backspace(tiku_trk_poseview_t *v);

/**
 * @brief Drop the type-ahead string and every filter term; filtering
 *        itself stays on or off.
 */
void tiku_trk_poseview_type_clear(tiku_trk_poseview_t *v);

/**
 * @brief The in-progress string, shown in the count view; "" when idle.
 *        While filtering, the terms joined by '|'.
 */
const char *tiku_trk_poseview_type_string(const tiku_trk_poseview_t *v);

/**
 * @brief The distinct types the listing holds (CW-026, PVL-095).
 *
 * The type-dependent menus are built from these.  Containers are skipped.
 *
 * @return how many were written to @p out.
 */
int tiku_trk_poseview_types_present(const tiku_trk_poseview_t *v,
                                    const char **out, int max);

/** @brief Expire type-ahead if the quiet period has passed. */
void tiku_trk_poseview_type_tick(tiku_trk_poseview_t *v);

/**
 * @brief While on, typed characters filter the listing; while off, they
 *        jump the selection to the best match.  Turning it off drops the
 *        terms and scrolls back to the top.
 */
void tiku_trk_poseview_set_filtering(tiku_trk_poseview_t *v, int on);
/** @brief Whether a type-ahead filter is in force. */
int tiku_trk_poseview_is_filtering(const tiku_trk_poseview_t *v);

/**
 * @brief Begin an additional filter term (Shift+Space, PVK-031).
 *
 * A pose must match every term, and each term may match a different column.
 * Refused when not filtering, while the current term is empty (an empty
 * term matches everything), and when TIKU_TRK_FILTER_TERMS are in force.
 *
 * @return 1 when a term was started.
 */
int tiku_trk_poseview_filter_term(tiku_trk_poseview_t *v);

/** @brief How many filter terms are in force. */
int tiku_trk_poseview_filter_terms(const tiku_trk_poseview_t *v);

/**
 * @brief Return was pressed: prepare the selection, and say what to do.
 *
 * While filtering with nothing selected, the top filtered row is selected
 * (PVK-015); the caller then opens the selection.
 *
 * @return 1 when @p shift is set while filtering: the caller then ends
 *         filtering (PVK-016).
 */
int tiku_trk_poseview_return_pressed(tiku_trk_poseview_t *v, int shift);

/**
 * @brief Whether a pose passes every filter term.
 *
 * Each term must be a case-insensitive substring of the text of some
 * column, and the terms may match different columns.
 */
int tiku_trk_poseview_filter_pose(tiku_trk_poseview_t *v,
                                  const tiku_trk_pose_t *p);

/**
 * @brief The text column @p field shows for @p p.
 *
 * The filter, type-ahead and the drawing code all read it, so what can be
 * filtered on and what is shown are the same text.  Answered from the
 * pose's cell cache while the facts and the column's width are unchanged.
 */
const char *tiku_trk_poseview_cell_text(tiku_trk_poseview_t *v,
                            const tiku_trk_pose_t *p, int field,
                            char *buf, size_t max);
/** @brief How often a cell's text was answered from the cache, and not. */
void tiku_trk_poseview_cell_cache_stats(const tiku_trk_poseview_t *v,
                                        uint64_t *hits, uint64_t *misses);

/*---------------------------------------------------------------------------*/
/* Modes, icon layout and hit-testing                                        */
/*---------------------------------------------------------------------------*/

/** @brief The view's mode; list mode for a NULL view. */
int tiku_trk_poseview_mode(const tiku_trk_poseview_t *v);

/**
 * @brief Switch mode.  Positions are carried into the new icon grid
 *        (PVL-011) and leaving list mode ends filtering; a query view
 *        stays in list mode.
 */
void tiku_trk_poseview_set_mode(tiku_trk_poseview_t *v, int mode);

/** @brief The icon mode last live -- what a list detour returns to. */
int tiku_trk_poseview_last_icon_mode(const tiku_trk_poseview_t *v);

/**
 * @brief Keep this view's state record under the Desktop's attribute.
 *
 * The Desktop's slot (MA-064): its view state and that of a folder window on
 * the same directory are kept apart.  Positions stay on each entry and the
 * Desktop columns use their own attribute on the folder. Any non-empty @p key
 * selects the slot; its text is not read.  The root's Disks slot is applied
 * automatically.
 */
void tiku_trk_poseview_set_state_slot(tiku_trk_poseview_t *v,
                                      const char *key);

/**
 * @brief Turn off scrolling by keys and by a moved selection: the Desktop's
 *        setting (PVK-013).
 */
void tiku_trk_poseview_set_no_autoscroll(tiku_trk_poseview_t *v, int on);

/** @brief The icon size in pixels; 32 for no view. */
int tiku_trk_poseview_icon_size(const tiku_trk_poseview_t *v);

/**
 * @brief Step the icon size one rung of the ladder (PVL-003).
 *
 * The ladder is 32-40-48-64-96-128, and its ends do not wrap.  Icon mode
 * only: mini-icon and list modes draw at a fixed size.  The point at the
 * middle of the window stays there (PVL-010).
 *
 * @param up 1 to grow, 0 to shrink.
 * @return 1 when the size changed.
 */
int tiku_trk_poseview_step_icon_size(tiku_trk_poseview_t *v, int up);

/**
 * @brief The size icons are drawn at in the current mode.
 *
 * Icon mode uses the chosen size; mini-icon and list modes use the mini size
 * whatever that setting says.
 */
int tiku_trk_poseview_art_size(const tiku_trk_poseview_t *v);
/** @brief Icons at @p px, between 16 and 128; anything else is ignored. */
void tiku_trk_poseview_set_icon_size(tiku_trk_poseview_t *v, int px);

/**
 * @brief Tell the view the font it is being drawn in (PVL-007).
 *
 * A cell's height in an icon mode includes the label's height; until this
 * is called, icon-mode heights come from the art size alone.  A @p height
 * of 0 or less is ignored.
 */
void tiku_trk_poseview_set_font_metrics(tiku_trk_poseview_t *v, int height,
                                        int ascent);

/**
 * @brief The list-mode row height (PVL-007): the owner's when it set one,
 *        else derived from the font, else 20.
 */
int tiku_trk_poseview_row_height(const tiku_trk_poseview_t *v);

/**
 * @brief Whether folders sort ahead of files (MA-039).
 *
 * On by default; with it off, the listing is in column order alone.
 */
void tiku_trk_poseview_set_folders_first(tiku_trk_poseview_t *v, int on);
/** @brief Hide dot-prefixed entries and reload the listing when changed. */
void tiku_trk_poseview_set_hide_dotfiles(tiku_trk_poseview_t *v, int on);
/**
 * @brief Select the next name alphabetically (case-folded), or the previous
 *        one with @p backwards, wrapping at the ends.
 * @return 1 when a pose was selected; 0 while filtering or when empty.
 */
int tiku_trk_poseview_tab_next(tiku_trk_poseview_t *v, int backwards);

/**
 * @brief Tell the view the row height its owner draws with (0 = derive).
 *
 * Every row rectangle the view answers with uses this height, so a shell
 * that draws rows at another height must set it.  Derived, it is the font
 * height times 1.65, rounded up, or 20 with no font.
 */
void tiku_trk_poseview_set_row_height(tiku_trk_poseview_t *v, int h);

/** @brief Grid cell and origin offset for the current mode (PVL-013). */
void tiku_trk_poseview_layout_info(const tiku_trk_poseview_t *v,
                                   int *grid_x, int *grid_y,
                                   int *off_x, int *off_y);

/** @brief Round a point to the nearest grid cell; a no-op with no grid. */
void tiku_trk_poseview_pin_to_grid(const tiku_trk_poseview_t *v,
                                   int *x, int *y);

/**
 * @brief A pose's position in view space, scaled from the unscaled 32 px
 *        space it is stored in.
 */
void tiku_trk_poseview_pose_location(const tiku_trk_poseview_t *v,
                                     const tiku_trk_pose_t *p,
                                     int *x, int *y);

/**
 * @brief Move a pose to a view-space point, snapped to the grid when
 *        @p snap is set, and store it unscaled.
 */
void tiku_trk_poseview_move_pose(tiku_trk_poseview_t *v, tiku_trk_pose_t *p,
                                 int x, int y, int snap);

/**
 * @brief Auto-place every unplaced pose left-to-right, wrapping at the edge.
 *
 * A pose with a landing reserved by tiku_trk_poseview_drop_land() goes
 * there instead.
 *
 * @param view_w  Width available; the wrap point.
 * @return how many poses were placed.
 */
int tiku_trk_poseview_place_unplaced(tiku_trk_poseview_t *v, int view_w);

/**
 * @brief Restart auto-placement at the top-left (PVL-017).
 *
 * The view calls it on a mode change, a directory switch, a reload and a
 * resize; a hint kept across a folder switch would place the next folder's
 * icons after the last folder's.
 */
void tiku_trk_poseview_reset_hint(tiku_trk_poseview_t *v);

/** @brief Where the next auto-placed icon would go (PVL-017). */
void tiku_trk_poseview_hint(const tiku_trk_poseview_t *v, int *x, int *y);

/**
 * @brief Put pose @p at back in sorted order if its key has moved (MA-055).
 *
 * For an edited value under a value sort: the row moves to the slot its
 * new key belongs in.
 *
 * @return Where the pose ended up, or -1 for an index out of range.
 */
int tiku_trk_poseview_recheck_order(tiku_trk_poseview_t *v, int at);

/**
 * @brief Re-lay out every pose from the top-left in sort order.
 *
 * What "Arrange by" does: the order changes, so every icon moves (PVL-024).
 * Nothing happens in list mode.
 */
void tiku_trk_poseview_cleanup(tiku_trk_poseview_t *v, int view_w);

/**
 * @brief Snap every icon to its nearest grid point, keeping the order.
 *
 * What "Clean up" does: each placed icon moves to its nearest grid point,
 * so the arrangement stays roughly where it was (PVL-021).
 *
 * @return how many icons moved.
 */
int tiku_trk_poseview_snap_to_grid(tiku_trk_poseview_t *v);

/**
 * @brief Pose at a view-space point (a window point plus the origin) in an
 *        icon mode, or NULL.
 */
tiku_trk_pose_t *tiku_trk_poseview_pose_at(tiku_trk_poseview_t *v,
                                           int x, int y);

/**
 * @brief How wide @p text is, in pixels, in the owner's font.
 *
 * Without one, every pose is icon-wide and a long name is unclickable past
 * the icon (PVL-065), and no column can be fitted to its widest value.
 */
typedef int (*tiku_trk_measure_fn)(const char *text, void *ctx);

/** @brief The text-measuring callback, with its context. */
void tiku_trk_poseview_set_measure(tiku_trk_poseview_t *v,
                                   tiku_trk_measure_fn fn, void *ctx);

/**
 * @brief The rectangle a pose occupies (PVL-065).
 *
 * In an icon mode the union of the icon and its label, so it widens where
 * the label is wider than the icon; in list mode the whole row.  This is
 * the rect a band sweeps and the extent unions; a click is judged against
 * tiku_trk_poseview_pose_parts().
 */
void tiku_trk_poseview_pose_rect(tiku_trk_poseview_t *v,
                                 const tiku_trk_pose_t *p, int *x, int *y,
                                 int *w, int *h);

/**
 * @brief A pose's icon square and label box separately (PVS-019, IV-026).
 *
 * In the icon modes tiku_trk_poseview_pose_rect() is their union, at least
 * a cell wide in mini mode: a band sweeps it and the view's extent is built
 * from it.  A click is judged against the two boxes, so the gap between an
 * icon and its name hits neither.  In list mode pose_rect is the whole row.
 * A NULL @p ix skips the icon's four values, a NULL @p lx the label's.
 */
void tiku_trk_poseview_pose_parts(tiku_trk_poseview_t *v,
                                  const tiku_trk_pose_t *p,
                                  int *ix, int *iy, int *iw, int *ih,
                                  int *lx, int *ly, int *lw, int *lh);

/**
 * @brief The label a pose shows in an icon mode, elided to fit (IV-047).
 *
 * TIKU_TRK_LABEL_CHARS M's wide whatever the icon size, elided in the
 * middle.  Both shells draw what this returns, so the text matches the box
 * that selects it.  Without a measure function, the whole name.
 *
 * @return Length written.
 */
int tiku_trk_poseview_fit_label(tiku_trk_poseview_t *v,
                                const tiku_trk_pose_t *p, char *out,
                                size_t max);

/**
 * @brief Whether a point lands on an icon's opaque pixels (PVS-019).
 *
 * The view has no art, so the owner supplies this; the two shells resolve
 * art differently, one through the node-icon tier and one from the model.
 * Without it a click is judged against the icon's square.
 *
 * @param px,py Offsets inside the icon square.
 * @return nonzero when the pixel there is opaque.
 */
typedef int (*tiku_trk_hit_fn)(const tiku_model_t *m, int size, int px,
                               int py, void *ctx);

/** @brief The icon hit-test callback, with its context. */
void tiku_trk_poseview_set_icon_hit(tiku_trk_poseview_t *v,
                                    tiku_trk_hit_fn fn, void *ctx);

/*---------------------------------------------------------------------------*/
/* The viewport and the extent (PVL-049, PVL-050)                            */
/*---------------------------------------------------------------------------*/

/**
 * @brief Tell the view how big the window showing it is, in pixels.
 *
 * Scrolling, clamping and paging in an icon mode use this size.  The
 * visible row count follows from @p h, both scrolls and the origin are
 * clamped again, and a new size restarts auto-placement.
 */
void tiku_trk_poseview_set_viewport(tiku_trk_poseview_t *v, int w, int h);

/** @brief The viewport's width and height, as last set. */
void tiku_trk_poseview_viewport(const tiku_trk_poseview_t *v, int *w,
                                int *h);

/**
 * @brief The scrollable area: what there is to look at (PVL-049).
 *
 * In list mode the columns' width by the rows' height.  In an icon mode the
 * bounding box of every placed pose, outset by the layout offset, with its
 * left and top edges never greater than 0.
 */
void tiku_trk_poseview_extent(tiku_trk_poseview_t *v, int *x, int *y,
                              int *w, int *h);

/**
 * @brief Move the pixel origin of the current icon mode; a no-op in list
 *        mode.
 *
 * Each icon mode keeps its own origin, so switching to list and back
 * returns to where the icons were (PVL-009).  The origin is clamped to the
 * extent less the viewport.
 */
void tiku_trk_poseview_set_origin(tiku_trk_poseview_t *v, int x, int y);
/**
 * @brief The scroll origin in pixels: the current icon mode's origin, or in
 *        list mode the row scroll times the row height.
 */
void tiku_trk_poseview_origin(const tiku_trk_poseview_t *v, int *x, int *y);

/**
 * @brief The first and last poses inside the viewport (PVK-011, PVL-062).
 *
 * In list mode the first and last visible rows; in an icon mode the first
 * and last shown poses whose rects intersect the viewport.  Either pointer
 * may be NULL.
 *
 * @return 1 when anything is visible at all.
 */
int tiku_trk_poseview_visible_range(tiku_trk_poseview_t *v, int *first,
                                    int *last);

/**
 * @brief Where the next created entry should land, in view space (PVL-025,
 *        PVL-098).
 *
 * Consumed by the next entry a creation notice brings in while in an icon
 * mode, which is placed there instead of auto-placed.
 */
void tiku_trk_poseview_set_drop_point(tiku_trk_poseview_t *v, int x, int y);
/** @brief Forget the drop point. */
void tiku_trk_poseview_clear_drop_point(tiku_trk_poseview_t *v);

/** @brief How many landings a single drop can reserve. */
#define TIKU_TRK_LAND_MAX 32

/**
 * @brief Reserve where @p name will land when it appears (PVS-045, PVL-027).
 *
 * An entry that arrives with a reserved name, by a creation notice or a
 * reload, is placed at its point instead of auto-placed.  Reservations are
 * consumed by name; a second reservation of a name replaces the first, and
 * past TIKU_TRK_LAND_MAX names the rest are auto-placed.
 */
void tiku_trk_poseview_drop_land(tiku_trk_poseview_t *v, const char *name,
                                 int x, int y);

/** @brief Forget every reservation (a drop that was refused). */
void tiku_trk_poseview_clear_landings(tiku_trk_poseview_t *v);

/**
 * @brief Select every pose intersecting the rectangle (rubber band).
 *
 * During a band sweep each pose toggles against the snapshot set_band took;
 * outside one the rectangle replaces the selection, or adds to it with
 * Shift or Command.
 *
 * @return how many poses the rectangle touched.
 */
int tiku_trk_poseview_select_rect(tiku_trk_poseview_t *v, int x0, int y0,
                                  int x1, int y1, unsigned mods);

/*---------------------------------------------------------------------------*/
/* Spatial memory: saved positions and view state (PVL-021, MA-064)          */
/*---------------------------------------------------------------------------*/

/**
 * @brief Load the folder's saved view state and its entries' positions.
 *
 * The view state brings back the mode, the icon size, the scroll and both
 * icon origins, and the sort keys; an entry marked invisible leaves the
 * listing.
 *
 * @return poses placed.
 */
int tiku_trk_poseview_restore_state(tiku_trk_poseview_t *v);

/** @brief Apply a validated saved view, column or pose record without filesystem I/O.
 *  @return 1 when the record belongs to this loaded view, 0 otherwise. */
int tiku_trk_poseview_apply_record(tiku_trk_poseview_t *, const char *attribute,
    const char *node, const void *bytes, size_t length);
/** @brief Apply copied native column bytes through the normal layout validation. */
int tiku_trk_poseview_apply_columns(tiku_trk_poseview_t *, const void *, size_t length);

/**
 * @brief Persist positions and the view state for the current folder.
 * @return positions written; 0 when nothing changed since the last save,
 *         for a query view, or on a read-only store.
 */
int tiku_trk_poseview_save_state(tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Sorting                                                                   */
/*---------------------------------------------------------------------------*/

/** @brief Sort by @p field; the same field again reverses. */
void tiku_trk_poseview_sort_by(tiku_trk_poseview_t *v, int field);
/** @brief The column the listing is sorted by. */
int tiku_trk_poseview_sort_field(const tiku_trk_poseview_t *v);
/** @brief Whether the sort runs the other way. */
int tiku_trk_poseview_sort_reverse(const tiku_trk_poseview_t *v);

/**
 * @brief A click on a column title, with or without Shift (TS-003, TS-004).
 *
 * A plain click sorts by the column, or reverses the sort when it is the
 * primary.  Shift on the primary swaps primary and secondary, or reverses
 * the sort when there is no secondary; Shift on another column makes it the
 * secondary, or clears the secondary when it is already that column.
 */
void tiku_trk_poseview_sort_click(tiku_trk_poseview_t *v, int field,
                                  int shift);

/** @brief The tie-break column, or -1 when there is none. */
int tiku_trk_poseview_sort_secondary(const tiku_trk_poseview_t *v);
/**
 * @brief Sort within equal keys by @p field; a field with no column, or
 *        the primary itself, means no secondary.
 */
void tiku_trk_poseview_set_secondary(tiku_trk_poseview_t *v, int field);

/*---------------------------------------------------------------------------*/
/* Liveness                                                                  */
/*---------------------------------------------------------------------------*/

/** @brief What the view is telling its owner after a notice. */
#define TIKU_TRK_NOTIFY_NOTHING  0x0u
#define TIKU_TRK_NOTIFY_REDRAW   0x1u   /**< the listing changed          */
#define TIKU_TRK_NOTIFY_GONE     0x2u   /**< the folder itself was removed */
#define TIKU_TRK_NOTIFY_RETITLE  0x4u   /**< it, or an ancestor, moved     */

/**
 * @brief Whether any row this view holds lives on the volume @p dev.
 *
 * A window on a mount point stays stat-able and listable after the volume
 * under it goes, so the device is what tells (PVN-043).
 */
int tiku_trk_poseview_on_device(const tiku_trk_poseview_t *v, uint64_t dev);

/**
 * @brief Drop every row that lived on the volume @p dev.
 *
 * For windows that are not on that volume but list rows from it, such as
 * a mount point's parent (PVN-043).
 *
 * @return how many rows went.
 */
int tiku_trk_poseview_device_gone(tiku_trk_poseview_t *v, uint64_t dev);

/**
 * @brief Give the view the icon cache to keep in step (IV-038, IV-041).
 *
 * Optional.  Every view takes the same cache, since a file shown in two
 * windows has one icon; the notice handler evicts the entries a notice
 * changes or removes, whether or not this view holds the row.
 */
void tiku_trk_poseview_set_icons(tiku_trk_poseview_t *v,
                                 tiku_trk_nodeicon_t *c);
/**
 * @brief A counter every change notice bumps, so a type change is
 *        observable even when no row moves.
 */
uint64_t tiku_trk_poseview_menu_generation(const tiku_trk_poseview_t *v);
/** @brief Defer a post-mount broken-link repair by half a second (PVN-042). */
void tiku_trk_poseview_schedule_broken_repair(tiku_trk_poseview_t *v,
                                              int64_t now);
/**
 * @brief Whether the repair of broken rows fell due by @p now; answering
 *        yes clears it, so it fires once.
 */
int tiku_trk_poseview_broken_repair_due(tiku_trk_poseview_t *v,
                                        int64_t now);

/**
 * @brief Apply one change notice.
 *
 * The single entry point every notice goes through, whether it was derived
 * from a re-listing diff or built from a change record, and for query and
 * folder windows alike.
 *
 * @return TIKU_TRK_NOTIFY_* flags for the owner to act on.
 */
unsigned tiku_trk_poseview_notify(tiku_trk_poseview_t *v,
                                  const tiku_node_event_t *e);

/**
 * @brief Update a query view from a new result set, through the handler.
 *
 * Diffs the set against what the view holds and applies the difference
 * through tiku_trk_poseview_notify(), so a result that is still there
 * keeps its row and its selection.
 */
unsigned tiku_trk_poseview_apply_results(tiku_trk_poseview_t *v,
                                         const tiku_model_t *set, int n,
                                         int64_t now);

/** @brief Apply a batch in arrival order.  @return the OR of the flags. */
unsigned tiku_trk_poseview_notify_all(tiku_trk_poseview_t *v,
                                      const tiku_evlist_t *l);

/**
 * @brief Re-list and apply what changed, without rebuilding the view.
 *
 * Rows that are still there keep their selection, their place and their
 * saved position.  A query view only ages its parked notices: its owner
 * re-runs the query and calls tiku_trk_poseview_apply_results().
 *
 * @param ch       Changes the owner drained this tick, or NULL when it could
 *                 not ask.
 * @param nch      How many, or -1 when the backend cannot tell.
 * @param complete 0 when the set may be partial; the view then re-lists.
 * @return TIKU_TRK_NOTIFY_* flags.
 * @note The owner drains the changes once and passes the same set to every
 *       view: draining is destructive.
 */
unsigned tiku_trk_poseview_poll(tiku_trk_poseview_t *v, int64_t now,
                                const tiku_model_change_t *ch, int nch,
                                int complete);

/** @brief How many notices are parked for rows that have not appeared. */
int tiku_trk_poseview_parked(const tiku_trk_poseview_t *v);

/** @brief How many entries are held as seen but not readable. */
int tiku_trk_poseview_zombies(const tiku_trk_poseview_t *v);

/**
 * @brief Service the backend, then re-read the values of the rows on
 *        screen until @p timeout_ms has passed.
 *
 * The sweep resumes where the last tick stopped and reads at least one row.
 *
 * @return Number of poses whose displayed value changed; 0 when the backend
 *         has no pump.
 */
int tiku_trk_poseview_pump(tiku_trk_poseview_t *v, int timeout_ms);

/**
 * @brief Re-read the Value of @p count shown rows from @p first, for the
 *        readable, non-container nodes the read policy allows.
 * @return how many rows' values changed.
 */
int tiku_trk_poseview_refresh_values(tiku_trk_poseview_t *v, int first,
                                     int count);

/*---------------------------------------------------------------------------*/
/* Columns                                                                   */
/*---------------------------------------------------------------------------*/

/**
 * @brief The columns shown, left to right, through @p out, which may be
 *        NULL.  @return how many there are.
 */
int tiku_trk_poseview_columns(const tiku_trk_poseview_t *v,
                              const tiku_trk_column_t **out);

/** @brief Whether @p field has a column. */
int tiku_trk_poseview_has_column(const tiku_trk_poseview_t *v, int field);

/**
 * @brief The view-space rectangle the open animation grows out of (PVS-013).
 *
 * List mode: the row's slice of the measured first column.  Icon modes: an
 * art-sized square at the pose's location.  @return 0 when @p row has no
 * pose.
 */
int tiku_trk_poseview_open_anim_rect(const tiku_trk_poseview_t *v, int row,
                                     int row_h, int *x, int *y, int *w,
                                     int *h);

/**
 * @brief Where the first column's data starts.
 *
 * The strip to its left holds the row's icon.  Column offsets are x
 * positions from the row's left edge.
 */
int tiku_trk_poseview_start_offset(const tiku_trk_poseview_t *v);

/**
 * @brief Re-derive every column's offset from the start, left to right.
 *
 * Every add, remove, resize and reorder ends here: offsets are recomputed
 * from the widths and margins, never adjusted in place.
 */
void tiku_trk_poseview_reflow_columns(tiku_trk_poseview_t *v);

/**
 * @brief Set a column's width (TS-010).
 *
 * @param width Clamped up to TIKU_TRK_COL_MIN_WIDTH: a column narrower than
 *              that cannot be grabbed again to widen it.
 * @return 1 when the width changed.
 */
int tiku_trk_poseview_resize_column(tiku_trk_poseview_t *v, int field,
                                    int width);

/**
 * @brief Move the column @p field to where @p before now sits (PVL-038).
 *
 * The destination is the index @p before has before the move, so a column
 * moved right lands after @p before.
 *
 * @return 1 when the order changed.
 */
int tiku_trk_poseview_move_column(tiku_trk_poseview_t *v, int field,
                                  int before);

/**
 * @brief Move a column to index @p to, clamped to the last column.
 * @return 1 when the order changed.
 */
int tiku_trk_poseview_move_column_to(tiku_trk_poseview_t *v, int field,
                                     int to);

/**
 * @brief Put @p field immediately after @p after (TS-009).
 *
 * Used for a column dragged back into the strip and dropped on the header
 * of @p after.
 *
 * @return 1 when the order changed.
 */
int tiku_trk_poseview_column_after(tiku_trk_poseview_t *v, int field,
                                   int after);

/**
 * @brief The width that would fit the widest value in @p field (PVL-037).
 *
 * Measured over the list being shown, so fitting a filtered listing fits
 * what is on screen; floored at TIKU_TRK_COL_MIN_WIDTH.
 *
 * @return The width, or 0 with no measure function.
 */
int tiku_trk_poseview_column_widest(tiku_trk_poseview_t *v, int field);

/**
 * @brief Size a column to its widest value.
 *
 * Refused when the widest value is at the floor and the column is no wider
 * than it; the caller then treats the click as the start of a drag.
 *
 * @return 1 when the fit was taken, which consumes the click even when the
 *         width was already right; 0 when refused.
 */
int tiku_trk_poseview_fit_column(tiku_trk_poseview_t *v, int field);

/** @brief Horizontal scroll, in pixels; titles and rows share it (TS-020). */
int tiku_trk_poseview_hscroll(const tiku_trk_poseview_t *v);
/**
 * @brief Scroll the columns sideways to @p px, clamped so the last column
 *        ends at the view's edge.
 */
void tiku_trk_poseview_set_hscroll(tiku_trk_poseview_t *v, int px);

/** @brief Total width the columns occupy, for the scroll range. */
int tiku_trk_poseview_columns_width(const tiku_trk_poseview_t *v);

/**
 * @brief Persist the column layout when a column mutator changed it.
 *
 * Order, width and alignment per column; a click that only re-sorted writes
 * nothing (TS-036).  The saved offsets only order the columns on restore,
 * where positions are re-derived (PVL-033).
 *
 * @return Columns written; 0 when nothing changed or nothing was written.
 */
int tiku_trk_poseview_save_columns(tiku_trk_poseview_t *v);
/**
 * @brief Read back the saved column layout for this folder or query.
 *
 * Byte order and attribute numbering are repaired; a record out of range
 * rejects the whole layout (TS-032).  A sort key whose column did not come
 * back falls to the column leading the strip (MA-067).
 *
 * @return how many columns were restored; 0 when nothing usable was saved.
 */
int tiku_trk_poseview_restore_columns(tiku_trk_poseview_t *v);

/**
 * @brief Take @p from's column layout, for a first opening (MA-070).
 *
 * The columns are not marked changed, so the inherited layout is saved only
 * once the user changes something.
 */
void tiku_trk_poseview_copy_columns(tiku_trk_poseview_t *v,
                                    const tiku_trk_poseview_t *from);

/**
 * @brief Copy this view's columns and sort keys to the layout clipboard.
 * @return how many columns were copied.
 */
int tiku_trk_poseview_copy_layout(const tiku_trk_poseview_t *v);
/**
 * @brief Take the copied layout: its columns, widths, sort and order, minus
 *        any attribute column this view has not registered.
 * @return how many columns were taken; 0 when there was no name column or
 *         nothing copied.
 */
int tiku_trk_poseview_paste_layout(tiku_trk_poseview_t *v);
/** @brief Whether a layout has been copied. */
int tiku_trk_poseview_has_layout_clipboard(void);
/** @brief The copied layout, or NULL when none has been. */
const tiku_trk_layout_clip_t *tiku_trk_poseview_layout_clipboard(void);

/**
 * @brief The font size stored column widths are expressed at (TS-031).
 *
 * Widths are stored in units of this size and scaled by the view's font
 * size over it on the way in and out, so a layout arranged at one font size
 * comes back in proportion at another.
 */
#define TIKU_TRK_FONT_REF 12
/**
 * @brief The face size in pixels the layout is scaled for; 0 or less is
 *        ignored.
 */
void tiku_trk_poseview_set_font_size(tiku_trk_poseview_t *v, int px);

/**
 * @brief Add or remove a column, as checking an Attributes item does.
 *
 * The name column cannot be removed.  Removing the column the view is
 * sorted on sorts it, ascending, by the column then leading the strip.
 *
 * @return 1 when the set changed.
 */
int tiku_trk_poseview_toggle_column(tiku_trk_poseview_t *v, int field);

/**
 * @brief Whether @p field is the column that cannot be removed: the name
 *        column (TS-023).
 */
int tiku_trk_poseview_last_column(const tiku_trk_poseview_t *v, int field);

/**
 * @brief Register a column showing the attribute @p attr (CW-026).
 *
 * Registering gives the attribute a field id and does not show it;
 * tiku_trk_poseview_toggle_column() adds or removes the column.  An
 * attribute registered again keeps its id.
 *
 * @return the field id, or -1 when TIKU_TRK_COL_ATTR_MAX are registered.
 */
int tiku_trk_poseview_attr_column(tiku_trk_poseview_t *v, const char *attr,
                                  const char *title, int width);

/** @brief The attribute a registered column shows, or NULL. */
const char *tiku_trk_poseview_attr_of(const tiku_trk_poseview_t *v,
                                      int field);

/**
 * @brief Tell the view how many rows its window can show.
 *
 * Paging and the scroll correction after a removal count visible rows;
 * with none set they fall back to weaker rules.  A scroll past the end of
 * the rows is pulled back.
 */
void tiku_trk_poseview_set_visible_rows(tiku_trk_poseview_t *v, int rows);

/**
 * @brief Mark the row at @p index as shown or not.
 *
 * tiku_trk_poseview_save_state() keeps the bit in the entry's position
 * record, so the entry stays hidden across reloads and restarts.  The row
 * leaves the listing at the next restore.
 *
 * @return 1 when the flag changed.
 */
int tiku_trk_poseview_set_invisible(tiku_trk_poseview_t *v, int index,
                                    int hidden);

/** @brief How many rows fit the viewport, as last set. */
int tiku_trk_poseview_visible_rows(const tiku_trk_poseview_t *v);

/*---------------------------------------------------------------------------*/
/* Inline rename (PVS-014, PVS-015, MA-043), and the list scroll             */
/*---------------------------------------------------------------------------*/

/** @brief The double-click interval; a rename arms after the same delay. */
#define TIKU_TRK_CLICK_SPEED_US 500000

/**
 * @brief A click landed on the name of row @p index.
 *
 * Arming is refused when editing is not allowed, when the pose is not
 * selected, when it was selected less than one click-speed ago (the click
 * that selected it does not count), and on the second click of a pair,
 * which the caller handles as a double-click.
 *
 * @return 1 when a rename is now armed.
 */
int tiku_trk_poseview_name_clicked(tiku_trk_poseview_t *v, int index,
                                   int64_t now);

/** @brief Disarm: another click, a right-click, empty space, or a drag. */
void tiku_trk_poseview_cancel_edit_wait(tiku_trk_poseview_t *v);

/**
 * @brief Whether the armed rename is now due.
 *
 * Disarms, without an editor, when the pose has since been deselected or
 * has gone, or editing was turned off.
 *
 * @return The row to edit, or -1.  Once due it disarms whether or not it
 *         returns a row, so the same expiry cannot start two editors.
 */
int tiku_trk_poseview_edit_due(tiku_trk_poseview_t *v, int64_t now);

/** @brief Whether a rename is armed and waiting. */
int tiku_trk_poseview_edit_armed(const tiku_trk_poseview_t *v);

/**
 * @brief Allow or refuse in-place editing in this view.
 *
 * While refused, a click pair never arms a rename, as a view that chooses
 * a file needs; turning it off disarms a waiting one.
 */
void tiku_trk_poseview_allow_editing(tiku_trk_poseview_t *v, int on);

/** @brief The first row shown, in list mode. */
int tiku_trk_poseview_scroll(const tiku_trk_poseview_t *v);
/** @brief Row @p row at the top, clamped to the rows there are. */
void tiku_trk_poseview_scroll_to(tiku_trk_poseview_t *v, int row);
/**
 * @brief Scroll by the least amount that shows row @p index among
 *        @p visible_rows, unless auto-scroll is off.
 */
void tiku_trk_poseview_scroll_into_view(tiku_trk_poseview_t *v, int index,
                                        int visible_rows);

#endif /* TIKU_TRK_POSEVIEW_H_ */
