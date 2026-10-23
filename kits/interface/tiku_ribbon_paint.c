/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_ribbon_paint.c - local light/dark surfaces and original command glyphs.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_ribbon.h"
#include "tiku_ui.h"
#include <string.h>

/** @brief Intersect nested clips while retaining their nonnegative sizes. */
static tiku_rect_t
clip(tiku_rect_t a, tiku_rect_t b)
{
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int right = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int bottom = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return (tiku_rect_t){x, y, right > x ? right - x : 0,
                         bottom > y ? bottom - y : 0};
}

tiku_ribbon_theme_t
tiku_ribbon_theme(int dark)
{
    return dark ? (tiku_ribbon_theme_t){0x182427, 0x223135, 0x10191C, 0x182427,
                                        0xEAF1F1, 0xB1C2C5, 0x567077, 0x304A50,
                                        0x3A5E62, 0x345C57, 0x83DECA, 0x10251F,
                                        0x8D9DA1}
                : (tiku_ribbon_theme_t){0xEDF3F2, 0xF8FAF9, 0xDCE5E3, 0xFFFFFF,
                                        0x202D30, 0x52666B, 0xA0B4B5, 0xE0EEEA,
                                        0xCADFD8, 0xD6ECE4, 0x146A5B, 0xFFFFFF,
                                        0x708084};
}

void
tiku_ribbon_icon(tiku_surface_t *s, tiku_rect_t r, tiku_ribbon_icon_t icon,
                 tiku_rgb_t color)
{
    int x = r.x + r.w / 2 - 7, y = r.y + r.h / 2 - 7;
    if (icon == TIKU_RIBBON_NONE)
        return;
    if (icon == TIKU_RIBBON_SAVE) {
        tiku_frame(s, (tiku_rect_t){x, y, 15, 15}, color);
        tiku_frame(s, (tiku_rect_t){x + 3, y, 8, 6}, color);
        tiku_frame(s, (tiku_rect_t){x + 3, y + 9, 9, 6}, color);
    } else if (icon == TIKU_RIBBON_UNDO || icon == TIKU_RIBBON_REDO) {
        int left = icon == TIKU_RIBBON_UNDO;
        tiku_hline(s, x + 1, y + 4, 13, color);
        tiku_vline(s, left ? x + 13 : x + 1, y + 4, 8, color);
        tiku_hline(s, x + 4, y + 11, 7, color);
        for (int i = 0; i < 4; i++)
            tiku_vline(s, left ? x + i : x + 13 - i, y + 4 - i, 2 * i + 1,
                       color);
    } else if (icon == TIKU_RIBBON_COPY || icon == TIKU_RIBBON_PASTE) {
        tiku_frame(s, (tiku_rect_t){x + 3, y + 3, 11, 12}, color);
        if (icon == TIKU_RIBBON_COPY)
            tiku_frame(s, (tiku_rect_t){x, y, 10, 11}, color);
        else
            tiku_frame(s, (tiku_rect_t){x + 5, y, 7, 5}, color);
    } else if (icon == TIKU_RIBBON_SEARCH) {
        tiku_frame_round(s, (tiku_rect_t){x, y, 11, 11}, 5, TIKU_CORNER_ALL,
                         color);
        for (int i = 0; i < 5; i++)
            tiku_fill(s, (tiku_rect_t){x + 9 + i, y + 9 + i, 2, 2}, color);
    } else if (icon == TIKU_RIBBON_OPEN) {
        tiku_frame(s, (tiku_rect_t){x, y + 3, 15, 11}, color);
        tiku_hline(s, x + 1, y, 7, color);
        tiku_vline(s, x, y, 4, color);
    } else {
        tiku_frame(s, (tiku_rect_t){x + 2, y, 11, 15}, color);
        tiku_hline(s, x + 4, y + 6, 7, color);
        tiku_hline(s, x + 4, y + 9, 7, color);
    }
}

void
tiku_ribbon_item_draw(tiku_surface_t *s, const tiku_ribbon_item_t *item,
                      const tiku_ribbon_theme_t *t, const tiku_font_t *font,
                      unsigned state)
{
    if (!item->bounds.w || !item->bounds.h)
        return;
    tiku_rect_t old = s->clip, r = item->paint.w ? item->paint : item->bounds;
    tiku_clip_set(s, clip(old, item->bounds));
    tiku_rgb_t face = state & TIKU_S_PRESSED ? t->pressed
                      : state & TIKU_S_HOT   ? t->hover
                      : item->checked        ? t->selected
                                             : t->panel;
    tiku_rgb_t ink = item->enabled ? t->text : t->disabled;
    tiku_fill_round(s, r, 4, TIKU_CORNER_ALL, face);
    if (item->checked || item->kind == TIKU_RIBBON_COMBO ||
        item->kind == TIKU_RIBBON_GALLERY)
        tiku_frame_round(s, tiku_inset(r, 1), 4, TIKU_CORNER_ALL,
                         item->checked ? t->accent : t->border);
    if (state & TIKU_S_FOCUS)
        tiku_frame_round(
            s, tiku_inset(item->kind == TIKU_RIBBON_SPLIT ? item->main : r, 2),
            3, TIKU_CORNER_ALL, t->accent);
    tiku_rect_t main = item->paint_main.w ? item->paint_main : item->main;
    int tall = r.h >= font->height * 3;
    if (item->icon && tall)
        tiku_ribbon_icon(s, (tiku_rect_t){main.x, main.y + 4, main.w, 24},
                         item->icon, item->enabled ? t->accent : t->disabled);
    else if (item->icon)
        tiku_ribbon_icon(s, (tiku_rect_t){main.x, main.y, 28, main.h},
                         item->icon, ink);
    const char *label = item->label;
    int text_x = main.x + 6;
    if (!tall && item->icon)
        text_x += 23;
    int width = tiku_text_width(font, label);
    if (tall)
        text_x = main.x + (main.w - width) / 2;
    int text_y = tall ? main.y + main.h - 8
                      : main.y + (main.h - font->height) / 2 + font->ascent;
    tiku_text(s, font, text_x, text_y, label, ink);
    if (item->checked == 2)
        tiku_hline(s, main.x + 6, main.y + 5, main.w > 12 ? main.w - 12 : 1,
                   t->accent);
    if (item->kind == TIKU_RIBBON_SPLIT || item->kind == TIKU_RIBBON_MENU ||
        item->kind == TIKU_RIBBON_COMBO) {
        int x = r.x + r.w - 11, y = r.y + r.h / 2;
        tiku_rgb_t arrow =
            item->kind == TIKU_RIBBON_SPLIT
                ? (item->secondary_enabled ? t->text : t->disabled)
                : ink;
        for (int i = 0; i < 4; i++)
            tiku_hline(s, x + i, y + i, 7 - i * 2, arrow);
        if (item->arrow.w)
            tiku_vline(s, item->arrow.x, r.y + 4, r.h - 8, t->border);
    }
    tiku_clip_set(s, old);
}

void
tiku_ribbon_draw(tiku_surface_t *s, const tiku_ribbon_t *r,
                 const tiku_ribbon_theme_t *t, const tiku_font_t *font,
                 const tiku_font_t *caption, int hover, int pressed, int focus)
{
    tiku_rect_t old = s->clip;
    tiku_clip_set(s, clip(old, r->viewport));
    tiku_fill(s, r->viewport, t->panel);
    for (int i = 0; i < r->groups; i++) {
        const tiku_ribbon_group_t *g = r->group + i;
        int x = r->viewport.x + g->x - r->scroll;
        int center = x + (g->end - g->x) / 2;
        int y = r->viewport.y + r->viewport.h - caption->height - 2;
        tiku_text(s, caption, center - tiku_text_width(caption, g->label) / 2,
                  y + caption->ascent, g->label, t->muted);
        if (i)
            tiku_vline(s, x - 7, r->viewport.y + 8, r->viewport.h - 16,
                       t->border);
    }
    for (int i = 0; i < r->count; i++) {
        const tiku_ribbon_item_t *item = r->items + i;
        unsigned state =
            (item->id == hover ||
             (item->kind == TIKU_RIBBON_SPLIT && item->secondary == hover))
                ? TIKU_S_HOT
                : 0;
        if (item->id == pressed ||
            (item->kind == TIKU_RIBBON_SPLIT && item->secondary == pressed))
            state |= TIKU_S_PRESSED;
        if (item->id == focus)
            state |= TIKU_S_FOCUS;
        tiku_ribbon_item_draw(s, item, t, font, state);
        if (item->kind == TIKU_RIBBON_SPLIT && item->secondary == focus &&
            item->arrow.w > 4 && item->arrow.h > 4)
            tiku_frame_round(s, tiku_inset(item->arrow, 2), 3, TIKU_CORNER_ALL,
                             t->accent);
    }
    tiku_clip_set(s, old);
}
