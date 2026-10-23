/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_ribbon.c - checked command grouping, overflow and popup geometry.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ribbon.h"
#include <string.h>

/** @brief Intersect bounded nonnegative rectangles. */
static tiku_rect_t
intersect(tiku_rect_t a, tiku_rect_t b)
{
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int end = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tiku_rect_t){x, y, end > x ? end - x : 0,
                         bottom > y ? bottom - y : 0};
}

int
tiku_ribbon_begin(tiku_ribbon_t *r, tiku_rect_t viewport, int scale, int scroll)
{
    if (!r || viewport.x < 0 || viewport.y < 0 || viewport.w < 1 ||
        viewport.h < 32 || viewport.x > 1000000 || viewport.y > 1000000 ||
        viewport.w > 1000000 || viewport.h > 1000000 || scale < 100 ||
        scale > 200)
        return -1;
    memset(r, 0, sizeof *r);
    r->viewport = viewport;
    r->scale = scale;
    r->scroll = scroll > 0 ? scroll : 0;
    return 0;
}

int
tiku_ribbon_group(tiku_ribbon_t *r, const char *label)
{
    if (!r || !label || strlen(label) >= 64 || r->groups == TIKU_RIBBON_GROUPS)
        return -1;
    if (r->groups)
        r->content += 14 * r->scale / 100;
    tiku_ribbon_group_t *g = r->group + r->groups++;
    strcpy(g->label, label);
    g->x = g->end = r->content;
    return 0;
}

int
tiku_ribbon_add(tiku_ribbon_t *r, int id, int secondary,
                tiku_ribbon_kind_t kind, tiku_ribbon_icon_t icon,
                const char *label, int width, int enabled, int checked)
{
    if (!r || !r->groups || !label || strlen(label) >= 64 || width < 16 ||
        width > 512 || kind > TIKU_RIBBON_GALLERY ||
        kind < TIKU_RIBBON_BUTTON || icon < TIKU_RIBBON_NONE ||
        icon > TIKU_RIBBON_OPEN || r->count == TIKU_RIBBON_ITEMS ||
        checked < 0 || checked > 2 || id < 0 ||
        (kind == TIKU_RIBBON_SPLIT && (secondary < 0 || id == secondary)))
        return -1;
    for (int i = 0; i < r->count; i++)
        if (r->items[i].id == id ||
            (r->items[i].kind == TIKU_RIBBON_SPLIT &&
             r->items[i].secondary == id) ||
            (kind == TIKU_RIBBON_SPLIT &&
             (r->items[i].id == secondary ||
              (r->items[i].kind == TIKU_RIBBON_SPLIT &&
               r->items[i].secondary == secondary))))
            return -1;
    tiku_ribbon_item_t *item = r->items + r->count++;
    *item = (tiku_ribbon_item_t){.id = id,
                                 .secondary = secondary,
                                 .kind = kind,
                                 .icon = icon,
                                 .group = r->groups - 1,
                                 .x = r->content,
                                 .width = width * r->scale / 100,
                                 .enabled = !!enabled,
                                 .secondary_enabled = !!enabled,
                                 .checked = checked};
    strcpy(item->label, label);
    r->content += item->width + 3 * r->scale / 100;
    r->group[item->group].end = r->content;
    return 0;
}

void
tiku_ribbon_end(tiku_ribbon_t *r)
{
    if (!r)
        return;
    int limit = r->content > r->viewport.w ? r->content - r->viewport.w : 0;
    if (r->scroll > limit)
        r->scroll = limit;
    if (r->scroll < 0)
        r->scroll = 0;
    for (int i = 0; i < r->count; i++) {
        tiku_ribbon_item_t *item = r->items + i;
        tiku_rect_t bounds = {r->viewport.x + item->x - r->scroll,
                              r->viewport.y + 3, item->width,
                              r->viewport.h - 24 * r->scale / 100};
        if (bounds.h < 8)
            bounds.h = 8;
        item->main = bounds;
        item->arrow = (tiku_rect_t){0};
        if (item->kind == TIKU_RIBBON_SPLIT) {
            int arrow = 16 * r->scale / 100;
            if (arrow > bounds.w / 2)
                arrow = bounds.w / 2;
            item->main.w -= arrow;
            item->arrow = (tiku_rect_t){bounds.x + bounds.w - arrow, bounds.y,
                                        arrow, bounds.h};
        }
        item->paint = bounds;
        item->paint_main = item->main;
        item->bounds = intersect(bounds, r->viewport);
        item->main = intersect(item->main, r->viewport);
        item->arrow = intersect(item->arrow, r->viewport);
    }
}

void
tiku_ribbon_scroll(tiku_ribbon_t *r, int delta)
{
    if (!r)
        return;
    if (delta > 1000000)
        delta = 1000000;
    if (delta < -1000000)
        delta = -1000000;
    r->scroll += delta;
    tiku_ribbon_end(r);
}

tiku_rect_t
tiku_ribbon_popup(tiku_rect_t anchor, tiku_rect_t screen, int width, int height)
{
    if (screen.w < 1 || screen.h < 1 || width < 1 || height < 1 ||
        screen.x < 0 || screen.y < 0 || screen.x > 1000000 ||
        screen.y > 1000000 || screen.w > 1000000 || screen.h > 1000000 ||
        anchor.x < -1000000 || anchor.x > 1000000 || anchor.y < -1000000 ||
        anchor.y > 1000000 || anchor.h < 0 || anchor.h > 1000000)
        return (tiku_rect_t){0};
    if (width > screen.w)
        width = screen.w;
    if (height > screen.h)
        height = screen.h;
    int x = anchor.x, y = anchor.y + anchor.h;
    if (y + height > screen.y + screen.h)
        y = anchor.y - height;
    if (x + width > screen.x + screen.w)
        x = screen.x + screen.w - width;
    if (x < screen.x)
        x = screen.x;
    if (y < screen.y)
        y = screen.y;
    if (y + height > screen.y + screen.h)
        y = screen.y + screen.h - height;
    return (tiku_rect_t){x, y, width, height};
}
