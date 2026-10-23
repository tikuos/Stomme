/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_find.h - the Find panel.
 *
 * Three modes: by name, by attribute, by formula.  Attribute mode is a grid
 * of rows, each an attribute, an operator and a value; the and/or joining a
 * row to the next is stored on the row above, and the rows fold left to
 * right with no precedence, so "a or b and c" means ((a or b) and c).
 * Switching into formula mode fills the field with the predicate the
 * controls produce.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_FIND_H_
#define TIKU_TRK_FIND_H_

#include "tiku_menu.h"
#include "tiku_query.h"
#include "tiku_text_input.h"
#include "tiku_trk_types.h"

struct tiku_store;

/** @brief How many attribute rows the panel holds. */
#define TIKU_TRK_FIND_ROWS_MAX   4
/** @brief How many places the scope menu offers. */
#define TIKU_TRK_FIND_PLACES_MAX 8
/** @brief The longest name text or row value, with its NUL. */
#define TIKU_TRK_FIND_TEXT_MAX   64

/** @brief Which control has the caret. */
typedef enum {
    TIKU_TRK_FIND_FOCUS_NONE = 0,
    TIKU_TRK_FIND_FOCUS_NAME,
    TIKU_TRK_FIND_FOCUS_ROW,       /* focus_row says which                 */
    TIKU_TRK_FIND_FOCUS_FORMULA
} tiku_trk_find_focus_t;

/** @brief What a click landed on. */
typedef enum {
    TIKU_TRK_FIND_HIT_NONE = 0,
    TIKU_TRK_FIND_HIT_MODE,
    TIKU_TRK_FIND_HIT_FIELD,       /* the attribute menu of a row          */
    TIKU_TRK_FIND_HIT_OP,          /* its operator menu                    */
    TIKU_TRK_FIND_HIT_VALUE,       /* its text field                       */
    TIKU_TRK_FIND_HIT_JOIN,        /* the and/or between this row and next */
    TIKU_TRK_FIND_HIT_ADD,
    TIKU_TRK_FIND_HIT_REMOVE,
    TIKU_TRK_FIND_HIT_SCOPE,
    TIKU_TRK_FIND_HIT_TRASH,
    TIKU_TRK_FIND_HIT_TYPE,        /* the "of type" menu (Q-004)           */
    TIKU_TRK_FIND_HIT_SEARCH,
    TIKU_TRK_FIND_HIT_SAVE,
    TIKU_TRK_FIND_HIT_QUERIES      /* the templates and recent queries     */
} tiku_trk_find_hit_t;

/** @brief One attribute row: an attribute, an operator and a value. */
typedef struct {
    tiku_qfield_t field;       /* the attribute tested                 */
    tiku_qop_t    op;          /* the operator                         */
    char              value[TIKU_TRK_FIND_TEXT_MAX];    /* typed value  */
    tiku_qjoin_t  join;        /* joins this row to the next one       */
} tiku_trk_find_row_t;

/** @brief How many past runs the Queries menu offers (Q-032). */
#define TIKU_TRK_FIND_RECENT_MAX 10

/** @brief How many types the most-used list keeps at the top (Q-016). */
#define TIKU_TRK_FIND_MOST_USED 4
/** @brief How many types one opening of the type popup can offer. */
#define TIKU_TRK_FIND_TYPE_CHOICES TIKU_TRK_TYPES_MAX
/** @brief How many supertype submenus the type popup holds. */
#define TIKU_TRK_FIND_TYPE_SUPERS 8

/** @brief The Find panel's state. */
typedef struct {
    tiku_qmode_t      mode;     /* by name, by attribute or by formula */
    /* The type every term is narrowed to, or "" for all; held as the type
     * string, which a query stores and a saved one returns. */
    char                  type[TIKU_TYPE_MAX];
    char                  name_text[TIKU_TRK_FIND_TEXT_MAX]; /* by name   */
    tiku_trk_find_row_t   row[TIKU_TRK_FIND_ROWS_MAX];       /* by attr   */
    int                   row_count;    /* row[] in use; at least 1     */
    char                  formula[TIKU_QUERY_PRED_MAX];      /* by formula */
    /* Scope.  "Everywhere" and a set of places are mutually exclusive the
     * way "All disks" and individual volumes are. */
    char                  place[TIKU_TRK_FIND_PLACES_MAX][TIKU_NAME_MAX];
    uint64_t              place_dev[TIKU_TRK_FIND_PLACES_MAX]; /* volume id */
    char                  place_volume[TIKU_TRK_FIND_PLACES_MAX]
                                      [TIKU_NAME_MAX];  /* volume name   */
    int                   place_count;  /* place[] in use               */
    unsigned              place_mask;   /* the places searched, a bit each */
    /* Set when 'Select folders...' was chosen: the shell answers with
     * its folder panel and adds the choice as a place (Q-019). */
    int                   wants_folder;
    int                   everywhere;   /* the search is not scoped     */
    int                   include_trash;    /* the Trash is searched too */
    /* Whether the panel's window has the keyboard, set by the owner before
     * drawing: the default button's look and the caret show only while it
     * is set (NeXT UIG ch7). */
    int                   has_keyboard;
    tiku_trk_find_focus_t focus;        /* the field keys go into       */
    int                   focus_row;    /* which row, for a row value   */
    /* A popup currently showing, and what it belongs to. */
    tiku_menu_t       popup;
    tiku_trk_find_hit_t   popup_for;    /* none when no popup shows     */
    int                   popup_row;    /* the row it belongs to        */
    tiku_rect_t      popup_frame;       /* where it is drawn            */
    /* The type popup's submenus live in the panel, since menu items hold
     * pointers to their submenus. */
    tiku_menu_t       type_sub[TIKU_TRK_FIND_TYPE_SUPERS];
    int                   type_sub_count;   /* type_sub[] in use         */
    tiku_menu_t      *popup_sub;        /* the open submenu, or NULL    */
    tiku_rect_t      popup_sub_frame;   /* where it is drawn            */
    char                  type_choice[TIKU_TRK_FIND_TYPE_CHOICES]
                                     [TIKU_TYPE_MAX];   /* by command  */
    int                   type_choice_count;    /* type_choice[] in use */
    /* What the last Queries-menu action did, for the shell's status
     * line. */
    char                  note[80];
    /* The record the panel was loaded from, and its kind.  edit_template
     * is set only by the in-place template edit, after which Save writes
     * the template back. */
    char                  loaded_name[128];
    tiku_qrec_t       loaded_kind;
    int                   edit_template;
    /* Where the most-used type list and the saved searches live. */
    struct tiku_store *store;
    const tiku_trk_types_t *types;      /* type database, or NULL       */
} tiku_trk_find_t;

/**
 * @brief Append UTF-8 to the focused field without running the query.
 * @return nonzero when appended; 0 while a popup is open or with no field
 *         focused.
 */
int tiku_trk_find_text(tiku_trk_find_t *f, const char *text);

/**
 * @brief Load the record @p name into the panel (Q-031, Q-032, Q-035).
 *
 * Templates and queries alike fill the panel; nothing runs.
 *
 * @return 1 when it loaded whole, 0 when it loaded with something lost,
 *         -1 when there is no such record.
 */
int tiku_trk_find_load_named(tiku_trk_find_t *f, const char *name);

/**
 * @brief Save what the panel holds as a template under @p name (Q-028).
 *
 * A template fills the panel when it is opened, instead of running.
 *
 * @return 0 when written.
 */
int tiku_trk_find_save_template(tiku_trk_find_t *f, const char *name,
                                int64_t now);

/** @brief Load a template specifically for replacement in place. */
int tiku_trk_find_edit_template(tiku_trk_find_t *f, const char *name);

/** @brief Save back to the explicitly attached query or template. */
int tiku_trk_find_save_current(tiku_trk_find_t *f, int64_t now);

/** @brief Load (or create) the unattached template used by a fresh panel. */
int tiku_trk_find_open_default(tiku_trk_find_t *f, int64_t now);

/** @brief Whether the separate Save action has an attached destination. */
int tiku_trk_find_can_save_current(const tiku_trk_find_t *f);

/** @brief Whether the panel is the special Edit Query template window. */
int tiku_trk_find_editing_template(const tiku_trk_find_t *f);

/** @brief Whether the default action saves the attached template only. */
int tiku_trk_find_primary_is_save(const tiku_trk_find_t *f);

/**
 * @brief Whether one of the panel's popups is showing.
 *
 * @note While one is, the shell sends every click to the panel: a popup
 *       can hang below the panel's window, over whatever is behind it.
 */
int tiku_trk_find_popup_open(const tiku_trk_find_t *f);

/** @brief What the last Queries-menu action did, for a status line. */
const char *tiku_trk_find_note(const tiku_trk_find_t *f);

/** @brief Forget it, once it has been said. */
void tiku_trk_find_clear_note(tiku_trk_find_t *f);

/** @brief Tell the panel where its most-used list lives (Q-016). */
void tiku_trk_find_set_store(tiku_trk_find_t *f, struct tiku_store *s);

/** @brief Supply the installed type declarations used by both type menus. */
void tiku_trk_find_set_types(tiku_trk_find_t *f,
                             const tiku_trk_types_t *types);

/** @brief Start a panel with one attribute row and no scope restriction. */
void tiku_trk_find_init(tiku_trk_find_t *f);

/**
 * @brief Whether the type menu is shown at all (Q-012).
 *
 * Hidden in formula mode, where the formula alone says what is asked.
 */
int tiku_trk_find_type_shown(const tiku_trk_find_t *f);

/** @brief The label the type control shows. */
const char *tiku_trk_find_type_label(const tiku_trk_find_t *f);

/**
 * @brief Choose a type, and promote it in the most-used list (Q-016).
 *
 * @param store Where the list lives, or NULL not to keep one.
 */
void tiku_trk_find_set_type(tiku_trk_find_t *f, const char *type,
                            struct tiku_store *store);

/**
 * @brief The model's types, most-used first: the type menu when no type
 *        database is installed (Q-015).
 *
 * The menu's fixed first item, All files and folders, is not in the list.
 *
 * @return how many were written.
 */
int tiku_trk_find_type_menu(struct tiku_store *store,
                            const char **out, int max);

/** @brief Offer @p name as a place the search can be scoped to. */
int tiku_trk_find_add_place(tiku_trk_find_t *f, const char *name);

/** @brief Take (and clear) the pending 'Select folders...' request. */
int tiku_trk_find_take_folder_request(tiku_trk_find_t *f);

/** @brief Add @p path as a place and scope the search to it (Q-019). */
int tiku_trk_find_scope_to_folder(tiku_trk_find_t *f, const char *path);

/** @brief Offer a place together with the volume identity it belongs to. */
int tiku_trk_find_add_place_identity(tiku_trk_find_t *f, const char *name,
                                     uint64_t dev, const char *volume);

/**
 * @brief Change mode.
 *
 * Entering formula mode fills the field with the predicate the controls
 * describe; leaving it keeps the rows and the formula.
 */
void tiku_trk_find_set_mode(tiku_trk_find_t *f, tiku_qmode_t mode);

/** @brief Add a row.  @return 1 when one was added. */
int tiku_trk_find_add_row(tiku_trk_find_t *f);

/** @brief Remove the last row; refused while only one remains. */
int tiku_trk_find_remove_row(tiku_trk_find_t *f);

/** @brief Build the query the panel is describing. */
void tiku_trk_find_build(const tiku_trk_find_t *f, tiku_query_t *q);

/**
 * @brief Fill the panel from a query, for Edit query (Q-026).
 *
 * The inverse of tiku_trk_find_build(): the mode, the rows with their
 * joins, the type, and the name text of a by-name query come back.  A scope
 * naming a place the panel offers ticks it; when none of the scopes is
 * offered, the panel searches everywhere.
 *
 * @return 1 when every scope in the query was recognised.
 */
int tiku_trk_find_from_query(tiku_trk_find_t *f, const tiku_query_t *q);

/**
 * @brief The name a saved query would get.
 *
 * A description of the criteria, then " - " and a timestamp, sanitised of
 * anything a path cannot carry.
 */
int tiku_trk_find_default_name(const tiku_trk_find_t *f, long stamp,
                               char *out, size_t max);

/** @brief The scope summary shown on the button: a name, "N places", or all. */
int tiku_trk_find_scope_label(const tiku_trk_find_t *f, char *out,
                              size_t max);

/** @brief The panel's natural height for its mode and rows. */
int tiku_trk_find_height(const tiku_trk_find_t *f);

/** @brief The whole panel into @p frame: mode, rows, scope, buttons. */
void tiku_trk_find_draw(tiku_trk_find_t *f, tiku_surface_t *s,
                        tiku_rect_t frame);

/**
 * @brief Where the draggable query icon sits (Q-034), or an empty rect.
 *
 * Empty in template-edit mode.
 */
tiku_rect_t tiku_trk_find_icon_rect(const tiku_trk_find_t *f,
                                         tiku_rect_t frame);

/**
 * @brief What is under a point.
 *
 * @param row Receives the row index for row-owned parts.
 */
tiku_trk_find_hit_t tiku_trk_find_hit(const tiku_trk_find_t *f,
                                      tiku_rect_t frame, int x, int y,
                                      int *row);

/**
 * @brief Handle a click.
 *
 * @return The hit, so the caller can act on Search and Save; everything else
 *         is handled inside the panel.
 */
tiku_trk_find_hit_t tiku_trk_find_click(tiku_trk_find_t *f,
                                        tiku_rect_t frame, int x, int y);

/** @brief Feed a character to whichever field has the caret. */
void tiku_trk_find_key(tiku_trk_find_t *f, char ch);

/** @brief One character off the end of the focused field. */
void tiku_trk_find_backspace(tiku_trk_find_t *f);

#endif /* TIKU_TRK_FIND_H_ */
