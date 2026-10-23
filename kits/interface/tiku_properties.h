/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_properties.h - a two-column table of names and values, inside a
 * 4-pixel sunken border, with one row selected.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PROPERTIES_H_
#define TIKU_PROPERTIES_H_
#include "tiku_dialog.h"
/** @brief The rows one table holds. */
#define TIKU_PROPERTIES_MAX 32
/** @brief Row flag: the value is drawn in subdued ink. */
#define TIKU_PROPERTY_UNAVAILABLE 1u
/** @brief Row flag: the label is a bold heading across the table. */
#define TIKU_PROPERTY_HEADING 2u

/** @brief One row: a label, a value and TIKU_PROPERTY_* flags. */
typedef struct {
    const char *label, *value; /**< borrowed; kept alive through drawing */
    unsigned flags;
} tiku_property_t;
/** @brief The table: its rows, the first shown and the one selected. */
typedef struct {
    tiku_property_t row[TIKU_PROPERTIES_MAX];
    int count, top, selected, row_h; /**< row_h 0 uses the font's row */
} tiku_properties_t;

/** @brief Empty the table, with no row selected. */
void tiku_properties_init(tiku_properties_t *p);
/**
 * @brief Append a row; @p label and @p value are borrowed, and a NULL
 *        @p value shows as empty.
 *
 * @return The row's index, or -1 when the table is full or @p label is
 *         NULL
 */
int tiku_properties_add(tiku_properties_t *p, const char *label,
                         const char *value, unsigned flags);
/** @brief Height of a row: @c row_h, or the list row height when it is 0. */
int tiku_properties_row_height(const tiku_properties_t *p);
/** @brief How many whole rows fit in @p body inside its border. */
int tiku_properties_visible(const tiku_properties_t *p, tiku_rect_t body);
/**
 * @brief The rectangle of @p row, or an empty one at the corner of @p body
 *        when the row is not wholly shown.
 */
tiku_rect_t tiku_properties_row(const tiku_properties_t *p,
                                 tiku_rect_t body, int row);
/**
 * @brief The ordinary row at @p x, @p y, or -1 over a heading, the border
 *        or no row.
 */
int tiku_properties_at(const tiku_properties_t *p, tiku_rect_t body, int x, int y);
/**
 * @brief Width of the label column: the widest label, measured, plus its
 *        margins, at most half the width inside the border.
 */
int tiku_properties_label_width(const tiku_properties_t *p, tiku_rect_t body);
/**
 * @brief Select an ordinary row and scroll it into view.
 *
 * @return 1, or 0 for a heading or a row out of range
 */
int tiku_properties_select(tiku_properties_t *p, tiku_rect_t body, int row);
/**
 * @brief Add each shown ordinary row to @p dialog as a button with ID
 *        @p base + row.
 *
 * Unavailable rows are added too, so they can still be selected.
 *
 * @return 0, or -1 when an ID is negative or taken or the dialog has no
 *         room, leaving the dialog unchanged
 * @note Call after the dialog has begun.
 */
int tiku_properties_controls(const tiku_properties_t *p, tiku_rect_t body,
                              tiku_dialog_t *dialog, int base, int enabled);
#endif
