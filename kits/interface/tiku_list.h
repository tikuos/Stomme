/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_list.h - a list of rows and everything about it but drawing.
 *
 * Which rows there are, which are selected, which the keyboard is on,
 * which is first shown, where a row sits and which row a click lands on,
 * and the rules turning a gesture into a change of all that.  It owns no
 * names, art or columns: the caller draws each row.  A row's index is its
 * place in the list and on the screen.  The selection rules match the
 * pose view's and are stated in kits/interface/doc/lists.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LIST_H_
#define TIKU_LIST_H_

#include <stdint.h>

#include "tiku_event.h"
#include "tiku_geom.h"

/**
 * @brief How many rows one list may hold.
 *
 * The flags are a byte a row inside the struct, so a list takes room for
 * this many whether or not it is full.  tiku_list_set_count() clamps a
 * larger count and returns the clamped one.
 */
#define TIKU_LIST_MAX 1024

/** @brief How long, in microseconds, a type-ahead spelling lives before
 *         it starts over. */
#define TIKU_LIST_TYPEAHEAD_US 1000000

/** @brief The name of row @p row, asked of the caller while spelling;
 *         NULL passes the row by. */
typedef const char *(*tiku_list_name_fn)(void *ctx, int row);

/** @brief A list: its rows' flags, the cursor, the anchors and the
 *         scroll. */
typedef struct {
    int  count;
    int  top;               /* the first row shown                     */
    int  cursor;            /* where the keyboard is, or -1            */
    int  pivot;             /* what a range reaches from, or -1        */
    int  last_added;        /* the far end of the last range, or -1    */
    int  single;            /* one row at a time                       */
    int  row_h;             /* 0 asks the face                         */
    /* The spelling being typed, and when its last letter came; it lapses
     * after TIKU_LIST_TYPEAHEAD_US. */
    char    typed[32];
    int64_t typed_at;
    unsigned char sel[TIKU_LIST_MAX];   /* which rows are selected     */
    unsigned char head[TIKU_LIST_MAX];  /* which rows are headings     */
} tiku_list_t;

/** @brief An empty list.  @p single nonzero holds one row at a time. */
void tiku_list_init(tiku_list_t *l, int single);

/**
 * @brief Say how many rows there are now.
 *
 * The cursor, the top and the anchors are brought inside the new count,
 * and rows past it lose their selection and heading flags.
 *
 * @return the count it holds: @p count clamped to 0..TIKU_LIST_MAX.
 */
int tiku_list_set_count(tiku_list_t *l, int count);

/** @brief How many rows there are. */
int tiku_list_count(const tiku_list_t *l);

/** @brief How tall a row is: the caller's, or the face's if none was set. */
int tiku_list_row_h(const tiku_list_t *l);
/** @brief Set the row height; 0 or less goes back to the face's. */
void tiku_list_set_row_h(tiku_list_t *l, int h);

/** @brief How many whole rows @p body has room for. */
int tiku_list_visible_rows(const tiku_list_t *l, tiku_rect_t body);

/**
 * @brief Where row @p row sits when the list is drawn in @p body.
 *
 * A row that is not shown, above the top or below the last, gets a rect
 * outside @p body; clipping is the caller's.  A row the list does not
 * have gets a zero height.
 */
tiku_rect_t tiku_list_row_rect(const tiku_list_t *l, tiku_rect_t body,
                               int row);

/** @brief Which row @p x, @p y lands on in @p body, or -1 for none; the
 *         strip under the last whole row is on none. */
int tiku_list_at(const tiku_list_t *l, tiku_rect_t body, int x, int y);

/**
 * @brief Make @p row a heading, or an ordinary row again.
 *
 * A heading names the rows under it and keeps its index, but no click,
 * range, select-all or inversion selects it, the cursor steps over it,
 * and type-ahead passes it by.  A row made a heading is deselected.
 * @note Set headings after tiku_list_set_count(), which clears the flags
 *       of the rows it drops.
 */
void tiku_list_set_heading(tiku_list_t *l, int row, int heading);

/** @brief Whether @p row is a heading. */
int tiku_list_is_heading(const tiku_list_t *l, int row);

/** @brief Whether @p row is selected. */
int tiku_list_selected(const tiku_list_t *l, int row);
/** @brief How many rows are selected. */
int tiku_list_selection_count(const tiku_list_t *l);

/** @brief The first selected row, or -1: a single-selection list's
 *         selection. */
int tiku_list_chosen(const tiku_list_t *l);

/** @brief Select exactly @p row, with the cursor and both anchors on it;
 *         a heading or a row out of range changes nothing. */
void tiku_list_select_only(tiku_list_t *l, int row);

/** @brief Select every row but the headings, with the anchors on the
 *         first and last rows; holding one at a time, the first row that
 *         is not a heading only. */
void tiku_list_select_all(tiku_list_t *l);

/** @brief No row, and no anchors either. */
void tiku_list_select_none(tiku_list_t *l);

/**
 * @brief Flip every row but the headings; a list holding one row at a
 *        time is left alone.
 *
 * The anchors are dropped, and set to the first and last rows only when
 * row 0 ends up selected.
 */
void tiku_list_invert(tiku_list_t *l);

/**
 * @brief A click on @p row (-1 for the empty space below the rows, which
 *        selects nothing).
 *
 * TIKU_MOD_CMD toggles the one row; TIKU_MOD_SHIFT selects from the
 * anchor to @p row in place of the selection; the two together give that
 * range the state @p row did not have.  A plain click on a row of a
 * larger selection, or on the anchor, changes only the cursor, so a drag
 * can start from it.  A click on a heading does nothing.
 */
void tiku_list_click(tiku_list_t *l, int row, unsigned modifiers);

/**
 * @brief Offer @p key to the list.
 *
 * Up and Down move by one, Page by a bodyful, Home and End to the ends;
 * with TIKU_MOD_SHIFT each selects from the anchor.  Cmd-A selects
 * everything.  The top follows the cursor.  Return is not handled:
 * opening a row is the caller's.
 *
 * @return nonzero when the list took the key.
 */
int tiku_list_key(tiku_list_t *l, unsigned key, unsigned modifiers,
                  tiku_rect_t body);

/**
 * @brief Spell a name to jump to it.
 *
 * @p ch is the character typed and @p now the clock in microseconds.
 * Letters typed close together spell one word; after
 * TIKU_LIST_TYPEAHEAD_US of quiet the next letter starts a new one.  The
 * first row whose name begins with the spelling (ignoring ASCII case) is
 * selected and revealed.
 *
 * @return the row jumped to, or -1 when nothing begins that way, and then
 *         nothing moves.
 */
int tiku_list_typeahead(tiku_list_t *l, char ch, int64_t now,
                        tiku_rect_t body, tiku_list_name_fn name,
                        void *ctx);

/**
 * @brief Bring the cursor's row into @p body.
 * @return nonzero when the top moved.
 */
int tiku_list_reveal(tiku_list_t *l, tiku_rect_t body);

/**
 * @brief Show @p top first, clamped so the list cannot be scrolled off.
 * @return nonzero when the top moved.
 */
int tiku_list_scroll_to(tiku_list_t *l, int top, tiku_rect_t body);

#endif /* TIKU_LIST_H_ */
