/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_chart_table.c - a chart's data table: the values the plot
 * shows, written out under it, a column to each category and a row to
 * each series, the series' names (and their legend keys, if asked) in a
 * column of their own to the left.  Its first row, the categories, takes
 * the place of the category axis' labels.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_office_chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DT_PAD 3.0f                 /* inside a cell                     */

/** @brief A row's height in the table. */
static float
row_h(const ch_chart_t *k)
{
    return ch_line_h(&k->dt_font) + 2 * DT_PAD;
}

/** @brief How many series the table has a row for. */
static int
rows(const ch_chart_t *k)
{
    int g, n = 0;

    for (g = 0; g < k->ngrp; g++) {
        n += k->grp[g].nser;
    }
    return n;
}

/** @brief Series @p s's name, or its number, into @p out. */
static void
name_of(const ch_series_t *s, char *out, size_t max)
{
    if (s->name != NULL && s->name[0]) {
        snprintf(out, max, "%s", s->name);
    } else {
        snprintf(out, max, "Series %d", s->idx + 1);
    }
}

const ch_axis_t *
ch_table_axis(const ch_chart_t *k)
{
    int i;

    for (i = 0; k->dtable && i < k->nax; i++) {
        if (k->ax[i].used && k->ax[i].horiz && k->ax[i].kind == CH_CAT_AX &&
            !k->ax[i].deleted) {
            return &k->ax[i];
        }
    }
    return NULL;
}

void
ch_table_room(ch_chart_t *k, float *left, float *below)
{
    char buf[160];
    float key = k->dt_keys ? k->dt_font.size + DT_PAD : 0;
    int g, s;

    *left = *below = 0;
    if (ch_table_axis(k) == NULL) {
        return;
    }
    for (g = 0; g < k->ngrp; g++) {
        for (s = 0; s < k->grp[g].nser; s++) {
            name_of(&k->grp[g].ser[s], buf, sizeof buf);
            *left = fmaxf(*left, ch_width(k, &k->dt_font, buf) + key +
                                 2 * DT_PAD);
        }
    }
    *below = row_h(k) * (1 + rows(k));
}

/** @brief The table's rules: between its rows and its columns, and its
 *         edge, as the file asks; the corner over the names left open. */
static void
rules(ch_chart_t *k, const ch_axis_t *a, float left, float top, int n)
{
    float rh = row_h(k), right = fmaxf(a->from, a->to), x;
    float bottom = top + rh * (n + 1);
    int r, i;

    for (r = 0; r <= n + 1; r++) {
        if (k->dt_horz || ((r == 0 || r == n + 1) && k->dt_outline)) {
            ch_segment(k, r == 0 ? fminf(a->from, a->to) : left,
                       top + rh * r, right, top + rh * r, &k->dt_line);
        }
    }
    for (i = 0; i <= k->ncat; i++) {
        x = ch_place(a, ch_cat(k, i - 0.5));
        if (k->dt_vert || ((i == 0 || i == k->ncat) && k->dt_outline)) {
            ch_segment(k, x, top, x, bottom, &k->dt_line);
        }
    }
    if (k->dt_outline || k->dt_vert) {
        ch_segment(k, left, top + rh, left, bottom, &k->dt_line);
    }
}

/** @brief Series @p s's row at @p top: its key and name from @p left,
 *         then its values under their categories. */
static void
row(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
    const ch_axis_t *a, float left, float top)
{
    float rh = row_h(k), x = left + DT_PAD, size = k->dt_font.size;
    ch_box_t b = {0, 0, size * 0.6f, size * 0.6f};
    char buf[160];
    dml_fill_t f;
    int i;

    if (k->dt_keys) {
        b.x = x;
        b.y = top + (rh - b.h) / 2;
        ch_point_fill(k, g, s, -1, &f);
        ch_rect(k, &b, &f, NULL);
        x += size;
    }
    name_of(s, buf, sizeof buf);
    ch_text(k, &k->dt_font, buf, x, top + rh / 2, 0, 1, 0);
    for (i = 0; i < s->n && i < k->ncat; i++) {
        ch_number(s->v[i], s->lbl_fmt != NULL ? s->lbl_fmt : s->fmt, buf,
                  sizeof buf);
        ch_text(k, &k->dt_font, buf, ch_place(a, ch_cat(k, i)),
                top + rh / 2, 1, 1, 0);
    }
}

void
ch_table(ch_chart_t *k, const ch_box_t *p)
{
    const ch_axis_t *a = ch_table_axis(k);
    float left, below, rh = row_h(k), top = p->y + p->h, y;
    char buf[160];
    int g, s, i;

    if (a == NULL) {
        return;
    }
    ch_table_room(k, &left, &below);
    left = fminf(a->from, a->to) - left;
    for (i = 0; i < k->ncat; i++) {
        if (k->cat != NULL && i < k->nnamed) {
            snprintf(buf, sizeof buf, "%s", k->cat[i]);
        } else {
            snprintf(buf, sizeof buf, "%d", i + 1);
        }
        ch_text(k, &k->dt_font, buf, ch_place(a, ch_cat(k, i)),
                top + rh / 2, 1, 1, 0);
    }
    y = top + rh;
    for (g = 0; g < k->ngrp; g++) {
        for (s = 0; s < k->grp[g].nser; s++, y += rh) {
            row(k, &k->grp[g], &k->grp[g].ser[s], a, left, y);
        }
    }
    rules(k, a, left, top, rows(k));
}
