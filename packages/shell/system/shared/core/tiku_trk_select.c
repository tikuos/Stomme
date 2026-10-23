/*
 * TikuTracker -- the file manager for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_trk_select.c - the Select window's state.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_trk_select.h"
#include <string.h>

/** @brief The popup, in the order SelectionWindow builds it. */
static const struct {
    tiku_trk_match_t how;
    const char      *label;
} rules[TIKU_TRK_SELECT_RULES] = {
    { TIKU_TRK_MATCH_STARTS,   "starts with"                 },
    { TIKU_TRK_MATCH_ENDS,     "ends with"                   },
    { TIKU_TRK_MATCH_CONTAINS, "contains"                    },
    { TIKU_TRK_MATCH_GLOB,     "matches wildcard expression" },
    { TIKU_TRK_MATCH_REGEX,    "matches regular expression"  },
};

void
tiku_trk_select_init(tiku_trk_select_t *w)
{
    if (w == NULL) {
        return;
    }
    memset(w, 0, sizeof *w);
    tiku_field_start(&w->expr, "");
    /* Item 3, wildcard, marked and Ignore case checked: an expression with
     * no metacharacters is then an exact, case-blind match. */
    w->rule = 3;
    w->ignore_case = 1;
    w->invert = 0;
}

tiku_trk_match_t
tiku_trk_select_rule(int i)
{
    if (i < 0 || i >= TIKU_TRK_SELECT_RULES) {
        return TIKU_TRK_MATCH_NONE;
    }
    return rules[i].how;
}

const char *
tiku_trk_select_rule_label(int i)
{
    if (i < 0 || i >= TIKU_TRK_SELECT_RULES) {
        return "";
    }
    return rules[i].label;
}

void
tiku_trk_select_set_rule(tiku_trk_select_t *sw, int i)
{
    if (sw != NULL && i >= 0 && i < TIKU_TRK_SELECT_RULES) {
        sw->rule = i;
    }
}

void
tiku_trk_select_place(int mouse_x, int mouse_y, int w, int h, int screen_w,
                      int screen_h, int *out_x, int *out_y)
{
    int x = mouse_x - w / 2;
    int y = mouse_y - h / 2;
    int maxx = screen_w - TIKU_TRK_SELECT_MARGIN - w;
    int maxy = screen_h - TIKU_TRK_SELECT_MARGIN - h;

    if (x > maxx) { x = maxx; }
    if (y > maxy) { y = maxy; }
    if (x < TIKU_TRK_SELECT_MARGIN) { x = TIKU_TRK_SELECT_MARGIN; }
    if (y < TIKU_TRK_SELECT_MARGIN) { y = TIKU_TRK_SELECT_MARGIN; }
    if (out_x != NULL) { *out_x = x; }
    if (out_y != NULL) { *out_y = y; }
}

void
tiku_trk_select_show(tiku_trk_select_t *sw, int mouse_x, int mouse_y, int w,
                     int h, int screen_w, int screen_h)
{
    if (sw == NULL) {
        return;
    }
    tiku_trk_select_place(mouse_x, mouse_y, w, h, screen_w, screen_h, &sw->x,
                          &sw->y);
    /* Everything else is left as it was, so the last expression is offered
     * again. */
    sw->expr.active = 1;
    sw->open = 1;
    sw->ever = 1;
}

void
tiku_trk_select_hide(tiku_trk_select_t *sw)
{
    if (sw != NULL) {
        sw->open = 0;
    }
}
