/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_view_defs.h - shared view and column vocabulary.
 *
 * These declarations describe saved state and commands, not the PoseView
 * implementation.  Menus and type records may use them without importing
 * the complete pose engine API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TRK_VIEW_DEFS_H_
#define TIKU_TRK_VIEW_DEFS_H_

/** @brief The most columns a list view holds. */
#define TIKU_TRK_COLUMNS_MAX 6

/** @brief A view's modes: a list, large icons, or mini icons. */
#define TIKU_TRK_MODE_LIST 0
#define TIKU_TRK_MODE_ICON 1
#define TIKU_TRK_MODE_MINI 2

/** @brief The mini-icon size, and the gap kept between placed icons, in
 *         pixels. */
#define TIKU_TRK_MINI_ICON 16
#define TIKU_TRK_PLACE_GAP  3

/** @brief A column's margins and narrowest width, in pixels. */
#define TIKU_TRK_COL_LEFT_MARGIN   11
#define TIKU_TRK_COL_RIGHT_MARGIN   5
#define TIKU_TRK_COL_MARGIN        (TIKU_TRK_COL_LEFT_MARGIN + \
                                    TIKU_TRK_COL_RIGHT_MARGIN)
#define TIKU_TRK_COL_MIN_WIDTH     20
/** @brief How many M's wide a pose label may be in an icon mode (IV-047). */
#define TIKU_TRK_LABEL_CHARS       30
#define TIKU_TRK_COL_ROOM_FOR_LINE  2

/** @brief How a column aligns its cells. */
#define TIKU_TRK_ALIGN_LEFT   0
#define TIKU_TRK_ALIGN_RIGHT  1
#define TIKU_TRK_ALIGN_CENTER 2

/** @brief One column of a list view. */
typedef struct {
    const char *title;
    int         width;
    int         field;      /* a TIKU_TRK_COL_* field, or an attribute's */
    int         offset;     /* where it starts across the row            */
    int         alignment;  /* a TIKU_TRK_ALIGN_* value                  */
} tiku_trk_column_t;

/** @brief The fields a column can show; TIKU_TRK_COL_KINDS counts them. */
#define TIKU_TRK_COL_NAME  0
#define TIKU_TRK_COL_KIND  1
#define TIKU_TRK_COL_VALUE 2
#define TIKU_TRK_COL_SIZE  3
#define TIKU_TRK_COL_CAP   4
#define TIKU_TRK_COL_DATE  5
#define TIKU_TRK_COL_PATH  6
#define TIKU_TRK_COL_PERM  7
#define TIKU_TRK_COL_ORIGINAL 8
#define TIKU_TRK_COL_KINDS 9

/** @brief Attribute columns: the first one's field number, how many a view
 *         holds, and the room for an attribute's name. */
#define TIKU_TRK_COL_ATTR_BASE 16
#define TIKU_TRK_COL_ATTR_MAX   8
#define TIKU_TRK_ATTR_NAME_MAX 32

/**
 * @brief How a column's cell elides when it does not fit (MA-026).
 *
 * Generic strings (names, values, paths, whatever an attribute column
 * carries) keep both ends and give up the middle, because either end may
 * be the distinguishing part.  Formatted values (sizes, capabilities,
 * dates, permission strings) elide at the end: their head is the value.
 *
 * @return 0 for middle (TIKU_TRUNC_MIDDLE), 1 for end.
 */
static inline int
tiku_trk_col_trunc_end(int field)
{
    return field == TIKU_TRK_COL_SIZE || field == TIKU_TRK_COL_CAP ||
           field == TIKU_TRK_COL_DATE || field == TIKU_TRK_COL_PERM;
}

#endif /* TIKU_TRK_VIEW_DEFS_H_ */
