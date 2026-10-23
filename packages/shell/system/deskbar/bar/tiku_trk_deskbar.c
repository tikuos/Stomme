/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_deskbar.c - the heights of the bar's rows.
 *
 * Each follows the interface face, which is a preference the Deskbar
 * applies to itself, and is never less than its minimum: the height it has
 * at the 12 px face.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_deskbar_private.h"

/** @brief The larger of @p want and @p least. */
static int
at_least(int want, int least)
{
    return (want > least) ? want : least;
}

/** @brief The leaf row's height at the face in force, never under LEAF_MIN. */
int db_leaf_row(void)
{
    return at_least(tiku_font_plain()->height + 19, LEAF_MIN);
}

/** @brief The clock row's height in pixels: the face's height plus five,
 *         never under CLOCK_MIN. */
int db_clock_row(void)
{
    return at_least(tiku_font_plain()->height + 5, CLOCK_MIN);
}

/** @brief A team row's height at the face in force, never under TEAM_MIN. */
int db_team_row(void)
{
    return at_least(tiku_font_plain()->height + 5, TEAM_MIN);
}

/** @brief A window row's height at the face in force, never under WIN_MIN. */
int db_win_row(void)
{
    return at_least(tiku_font_plain()->height + 4, WIN_MIN);
}
