/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_form_row.h - a labelled row split into its label and its
 * control.
 *
 * A row of a form is a label and the control beside it.  The label is
 * given as much as its words need, measured through the seam, and the
 * control takes the rest; on a board and on the desktop the same call
 * gives the same split, because both measure the same faces.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_FORM_ROW_H_
#define TIKU_FORM_ROW_H_

#include "tiku_geom.h"

/**
 * @brief Split @p r into the label's rect and the control's, the label
 *        wide enough for @p label in the plain face plus @p gap between.
 */
void tiku_form_row_split(tiku_rect_t r, const char *label, int gap,
                         tiku_rect_t *label_out, tiku_rect_t *control_out);

#endif /* TIKU_FORM_ROW_H_ */
