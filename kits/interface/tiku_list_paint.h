/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_list_paint.h - the list's rows, drawn.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_LIST_PAINT_H_
#define TIKU_LIST_PAINT_H_

#include "tiku_list.h"
#include "tiku_gfx.h"

/**
 * @brief Draw the rows in @p body as plain named rows.
 *
 * One line of text a row, selected rows in the selection colours, each
 * drawn by tiku_ui_list_row(), which records it as a row with its name
 * and selection.  A heading is drawn and recorded as bold text.  Columns,
 * art or a second line are drawn by the caller from tiku_list_row_rect().
 */
void tiku_list_draw(const tiku_list_t *l, tiku_surface_t *s,
                    tiku_rect_t body, tiku_list_name_fn name, void *ctx);

#endif /* TIKU_LIST_PAINT_H_ */
