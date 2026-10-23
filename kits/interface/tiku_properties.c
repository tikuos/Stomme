/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_properties.c - the two-column table of names and values.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tiku_properties.h"
#include "tiku_measure.h"
#include <limits.h>
#include <string.h>

void tiku_properties_init(tiku_properties_t *p)
{ memset(p, 0, sizeof *p); p->selected = -1; }
int tiku_properties_add(tiku_properties_t *p, const char *label,
                         const char *value, unsigned flags)
{
    if (p->count == TIKU_PROPERTIES_MAX || !label) return -1;
    int at = p->count++;
    p->row[at] = (tiku_property_t){label, value ? value : "", flags};
    return at;
}
int tiku_properties_row_height(const tiku_properties_t *p)
{ return p->row_h > 0 ? p->row_h : tiku_measure_row_h(); }
int tiku_properties_visible(const tiku_properties_t *p, tiku_rect_t body)
{
    return body.w > 8 && body.h > 8
           ? (body.h - 8) / tiku_properties_row_height(p) : 0;
}
tiku_rect_t tiku_properties_row(const tiku_properties_t *p,
                                 tiku_rect_t body, int row)
{
    if (row < p->top || row < 0 || row >= p->count ||
        row - p->top >= tiku_properties_visible(p, body))
        return (tiku_rect_t){body.x, body.y, 0, 0};
    int h = tiku_properties_row_height(p);
    return (tiku_rect_t){body.x + 4, body.y + 4 + (row - p->top) * h,
                         body.w - 8, h};
}
int tiku_properties_at(const tiku_properties_t *p, tiku_rect_t body, int x, int y)
{
    if (x < body.x + 4 || x >= body.x + body.w - 4 || y < body.y + 4) return -1;
    int row = p->top + (y - body.y - 4) / tiku_properties_row_height(p);
    tiku_rect_t r = tiku_properties_row(p, body, row);
    if (!r.h || y >= r.y + r.h || (p->row[row].flags & TIKU_PROPERTY_HEADING))
        return -1;
    return row;
}
int tiku_properties_label_width(const tiku_properties_t *p, tiku_rect_t body)
{
    int widest = 0, limit = body.w > 8 ? (body.w - 8) / 2 : 0;
    for (int i = 0; i < p->count; i++) {
        if (p->row[i].flags & TIKU_PROPERTY_HEADING) continue;
        int w = tiku_measure_width(TIKU_MEASURE_PLAIN, p->row[i].label) + 28;
        if (w > widest) widest = w;
    }
    return widest < limit ? widest : limit;
}
int tiku_properties_select(tiku_properties_t *p, tiku_rect_t body, int row)
{
    if (row < 0 || row >= p->count || (p->row[row].flags & TIKU_PROPERTY_HEADING))
        return 0;
    int visible = tiku_properties_visible(p, body);
    p->selected = row;
    if (visible < 1) visible = 1;
    if (row < p->top) p->top = row;
    if (row >= p->top + visible) p->top = row - visible + 1;
    int max_top = p->count > visible ? p->count - visible : 0;
    if (p->top > max_top) p->top = max_top;
    return 1;
}
int tiku_properties_controls(const tiku_properties_t *p, tiku_rect_t body,
                              tiku_dialog_t *d, int base, int enabled)
{
    if (base < 0 || base > INT_MAX - p->count) return -1;
    int needed = 0;
    for (int i = 0; i < p->count; i++) {
        if (!tiku_properties_row(p, body, i).h ||
            (p->row[i].flags & TIKU_PROPERTY_HEADING)) continue;
        for (int j = 0; j < d->count; j++)
            if (d->control[j].id == base + i) return -1;
        needed++;
    }
    if (needed > TIKU_DIALOG_MAX - d->count) return -1;
    for (int i = 0; i < p->count; i++) {
        tiku_rect_t r = tiku_properties_row(p, body, i);
        if (r.h && !(p->row[i].flags & TIKU_PROPERTY_HEADING))
            tiku_dialog_add(d, base + i, r, TIKU_DIALOG_BUTTON, enabled);
    }
    return 0;
}
