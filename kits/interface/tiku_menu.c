/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_menu.c - menu layout, drawing and hit-testing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tiku_logo.h"
#include "tiku_menu.h"
#include "tiku_ui.h"

/* The measurements the control look gives for the 12 px face, where the
 * default label spacing, half the font's size, is 6.  They are fixed and
 * do not follow the interface size. */
#define LABEL_SPACING  6
#define PAD_LEFT      14      /* the mark gutter, kept when nothing is marked */
#define PAD_TOP        2
#define PAD_RIGHT     20
#define PAD_BOTTOM     0
#define SEAM           1      /* items do not tile: one unpainted row       */
#define FONT_SIZE     12      /* one em, the room for a shortcut's modifiers */
#define SEP_H          8      /* max(4, floor((size * 0.8) / 2) * 2)        */
#define MARK_TINT   0.75f     /* the check and the arrow are drawn lighter  */
                              /* than the label                             */

/** @brief A stroked line with a square pen, for the check. */
static void
pen_line(tiku_surface_t *s, int x0, int y0, int x1, int y1, int pen,
         tiku_rgb_t c)
{
    int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    int dy = (y1 > y0) ? y1 - y0 : y0 - y1;
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        int a, b;

        for (a = 0; a < pen; a++) {
            for (b = 0; b < pen; b++) {
                tiku_pixel(s, x0 + a, y0 + b, c);
            }
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            int e2 = 2 * err;

            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
}

void
tiku_menu_clear(tiku_menu_t *m)
{
    if (m != NULL) {
        m->count = 0;
        m->open_index = -1;
        m->hot = -1;
    }
}

int
tiku_menu_add(tiku_menu_t *m, const char *label, int command, char sc,
                  unsigned mods, int enabled)
{
    tiku_menu_item_t *it;

    if (m == NULL || m->count >= TIKU_MENU_ITEMS_MAX) {
        return -1;
    }
    it = &m->item[m->count];
    memset(it, 0, sizeof *it);
    snprintf(it->label, sizeof it->label, "%s", label != NULL ? label : "");
    it->command = command;
    it->shortcut = sc;
    it->mods = mods;
    it->enabled = enabled;
    return m->count++;
}

int
tiku_menu_add_separator(tiku_menu_t *m)
{
    int i = tiku_menu_add(m, "", 0, 0, 0u, 0);

    if (i >= 0) {
        /* Disabled, and never hit or highlighted: tiku_menu_item_at() and
         * tiku_menu_track() both pass over it. */
        m->item[i].separator = 1;
        m->item[i].enabled = 0;
    }
    return i;
}

int
tiku_menu_add_submenu(tiku_menu_t *m, const char *label,
                          tiku_menu_t *sub, int enabled)
{
    int i = tiku_menu_add(m, label, 0, 0, 0u, enabled);

    if (i >= 0) {
        m->item[i].submenu = sub;
    }
    return i;
}

int
tiku_menu_insert(tiku_menu_t *m, int at, const char *label,
                     int command, char sc, unsigned mods, int enabled)
{
    int i;

    if (m == NULL || m->count >= TIKU_MENU_ITEMS_MAX || at < 0 ||
        at > m->count) {
        return -1;
    }
    for (i = m->count; i > at; i--) {
        m->item[i] = m->item[i - 1];
    }
    m->count++;
    memset(&m->item[at], 0, sizeof m->item[at]);
    snprintf(m->item[at].label, sizeof m->item[at].label, "%s",
             label != NULL ? label : "");
    m->item[at].command = command;
    m->item[at].shortcut = sc;
    m->item[at].mods = mods;
    m->item[at].enabled = enabled;
    return at;
}

void
tiku_menu_remove(tiku_menu_t *m, int at)
{
    int i;

    if (m == NULL || at < 0 || at >= m->count) {
        return;
    }
    for (i = at; i < m->count - 1; i++) {
        m->item[i] = m->item[i + 1];
    }
    m->count--;
}

int
tiku_menu_find(const tiku_menu_t *m, int command)
{
    int i;

    for (i = 0; m != NULL && i < m->count; i++) {
        if (m->item[i].command == command && !m->item[i].separator) {
            return i;
        }
    }
    return -1;
}

void
tiku_menu_mark(tiku_menu_t *m, int command, int marked)
{
    int i = tiku_menu_find(m, command);

    if (i >= 0) {
        m->item[i].marked = marked ? 1 : 0;
    }
}

void
tiku_menu_mark_radio(tiku_menu_t *m, const int *commands, int n,
                         int chosen)
{
    int i;

    for (i = 0; i < n; i++) {
        tiku_menu_mark(m, commands[i], commands[i] == chosen);
    }
}

void
tiku_menu_enable(tiku_menu_t *m, int command, int enabled)
{
    int i = tiku_menu_find(m, command);

    if (i >= 0) {
        m->item[i].enabled = enabled ? 1 : 0;
    }
}

static tiku_menu_icon_fn icon_painter;

void
tiku_menu_set_icon_painter(tiku_menu_icon_fn fn)
{
    icon_painter = fn;
}

void
tiku_menu_set_icon(tiku_menu_t *m, int command, const char *icon)
{
    int i = tiku_menu_find(m, command);
    if (i >= 0) {
        snprintf(m->item[i].icon, sizeof m->item[i].icon, "%s",
                 icon != NULL ? icon : "");
    }
}

void
tiku_menu_relabel(tiku_menu_t *m, int at, const char *label,
                      int command, char sc, unsigned mods)
{
    if (m == NULL || at < 0 || at >= m->count) {
        return;
    }
    snprintf(m->item[at].label, sizeof m->item[at].label, "%s",
             label != NULL ? label : "");
    m->item[at].command = command;
    m->item[at].shortcut = sc;
    m->item[at].mods = mods;
}

int
tiku_menu_item_height(const tiku_menu_t *m, int i)
{
    const tiku_font_t *f = tiku_font_plain();

    if (m == NULL || i < 0 || i >= m->count) {
        return 0;
    }
    if (m->item[i].separator) {
        return SEP_H;
    }
    return tiku_text_height(f) + PAD_TOP + PAD_BOTTOM;
}

int
tiku_menu_item_top(const tiku_menu_t *m, int i)
{
    int k, y = 0;

    for (k = 0; k < i && k < m->count; k++) {
        y += tiku_menu_item_height(m, k) + SEAM;
    }
    return y;
}

void
tiku_menu_measure(tiku_menu_t *m)
{
    const tiku_font_t *f = tiku_font_plain();
    int i, w = 0, any_sub = 0, any_mod = 0, item_h;

    if (m == NULL) {
        return;
    }
    item_h = tiku_text_height(f) + PAD_TOP + PAD_BOTTOM;
    for (i = 0; i < m->count; i++) {
        int iw;

        if (m->item[i].separator) {
            continue;
        }
        iw = tiku_text_width(f, m->item[i].label) + PAD_LEFT + PAD_RIGHT;
        if (m->item[i].icon[0] != '\0') iw += 18;
        /* A shortcut costs an em only when it has modifiers; room for the
         * modifiers themselves is added once for the whole menu. */
        if (m->item[i].shortcut != 0 && m->item[i].mods != 0u) {
            iw += FONT_SIZE;
        }
        if (iw > w) {
            w = iw;
        }
        if (m->item[i].submenu != NULL) {
            any_sub = 1;
        }
        if (m->item[i].mods != 0u) {
            any_mod = 1;
        }
    }
    if (any_mod) {
        w += FONT_SIZE + 1;
    }
    if (any_sub) {
        /* Room for the arrow is added once for the whole menu, so one
         * submenu widens every row alike. */
        w += item_h / 2;
    }
    m->width = w;
    m->height = (m->count > 0)
                ? tiku_menu_item_top(m, m->count - 1) +
                  tiku_menu_item_height(m, m->count - 1)
                : 0;
}

int
tiku_menu_item_at(const tiku_menu_t *m, tiku_rect_t frame,
                      int x, int y)
{
    int i;

    if (m == NULL || x < frame.x || x >= frame.x + frame.w) {
        return -1;
    }
    for (i = 0; i < m->count; i++) {
        /* The same +1 the draw uses to clear the frame's top edge, so
         * each hit band is the band the item is painted in. */
        int top = frame.y + 1 + tiku_menu_item_top(m, i);
        int h = tiku_menu_item_height(m, i);

        if (y >= top && y < top + h) {
            /* A separator hits nothing; the seam below each item falls
             * outside every band. */
            return m->item[i].separator ? -1 : i;
        }
    }
    return -1;
}

tiku_rect_t
tiku_menu_place(tiku_menu_t *m, int x, int y, tiku_rect_t screen)
{
    tiku_rect_t r;

    tiku_menu_measure(m);
    r.x = x;
    r.y = y;
    r.w = m->width;
    r.h = m->height + 2;
    if (r.x + r.w > screen.x + screen.w) {
        r.x = screen.x + screen.w - r.w;
    }
    if (r.y + r.h > screen.y + screen.h) {
        r.y = screen.y + screen.h - r.h;
    }
    if (r.x < screen.x) { r.x = screen.x; }
    if (r.y < screen.y) { r.y = screen.y; }
    return r;
}

tiku_rect_t
tiku_menu_place_sub(tiku_menu_t *m, int i, tiku_rect_t frame,
                        tiku_rect_t screen)
{
    tiku_rect_t r = { 0, 0, 0, 0 };
    tiku_menu_t *sub;

    if (m == NULL || i < 0 || i >= m->count ||
        (sub = m->item[i].submenu) == NULL) {
        return r;
    }
    tiku_menu_measure(sub);
    /* Anchored one pixel down and right of the parent item's top-right
     * corner; when that would leave the screen it is flipped to the
     * parent's left. */
    r.x = frame.x + frame.w + 1;
    r.y = frame.y + tiku_menu_item_top(m, i) + 1;
    r.w = sub->width;
    r.h = sub->height + 2;
    if (r.x + r.w > screen.x + screen.w) {
        r.x = frame.x - r.w - 1;
    }
    if (r.y + r.h > screen.y + screen.h) {
        r.y = screen.y + screen.h - r.h;
    }
    if (r.x < screen.x) { r.x = screen.x; }
    if (r.y < screen.y) { r.y = screen.y; }
    return r;
}

/** @brief The check: a three-point stroked polyline in the left gutter. */
static void
draw_mark(tiku_surface_t *s, tiku_rect_t item, tiku_rgb_t c)
{
    int gap = PAD_LEFT / 4;
    int left = item.x + gap / 3;
    int right = item.x + PAD_LEFT - gap;
    int cx = (left + right) / 2;
    int cy = item.y + item.h / 2;
    int size = (item.h - 2 < right - left) ? item.h - 2 : right - left;

    if (size < 4) {
        return;
    }
    pen_line(s, cx - size / 2, cy, cx - size / 6, cy + size / 3, 2, c);
    pen_line(s, cx - size / 6, cy + size / 3, cx + size / 2, cy - size / 3,
             2, c);
}

/**
 * @brief The submenu arrow, drawn by tiku_ui_submenu_arrow() as
 *        horizontal runs, which the display list carries as commands.
 */
static void
draw_arrow(tiku_surface_t *s, tiku_rect_t item, tiku_rgb_t c)
{
    tiku_ui_submenu_arrow(s, item, c);
}

void
tiku_menu_draw(tiku_menu_t *m, tiku_surface_t *s,
                   tiku_rect_t frame)
{
    const tiku_font_t *f = tiku_font_plain();
    tiku_rgb_t base = TIKU_C_PANEL;
    int i;

    if (m == NULL) {
        return;
    }
    tiku_menu_measure(m);
    /* A card: a shadow, rounded corners, a hairline. */
    tiku_shadow(s, frame, 10, 4, 0.35f);
    tiku_fill_round(s, frame, 4, TIKU_CORNER_ALL, base);
    tiku_frame_round(s, frame, 4, TIKU_CORNER_ALL, tiku_tint(base, 1.40f));

    for (i = 0; i < m->count; i++) {
        tiku_menu_item_t *it = &m->item[i];
        tiku_rect_t r;
        tiku_rgb_t ink;
        int base_y;

        r.x = frame.x + 1;
        r.y = frame.y + 1 + tiku_menu_item_top(m, i);
        r.w = frame.w - 2;
        r.h = tiku_menu_item_height(m, i);

        if (it->separator) {
            /* Two hairlines, dark over light, inset one pixel at each end. */
            int mid = r.y + r.h / 2;

            tiku_hline(s, r.x + 1, mid, r.w - 2,
                            tiku_tint(base, TIKU_DARKEN_1));
            tiku_hline(s, r.x + 1, mid + 1, r.w - 2,
                            tiku_tint(base, TIKU_LIGHTEN_MAX));
            continue;
        }
        if (i == m->hot && it->enabled) {
            tiku_fill_round(s, tiku_inset(r, 1), 3, TIKU_CORNER_ALL,
                            TIKU_C_SELECT);
            ink = TIKU_C_SELTEXT;
        } else {
            /* Disabled ink is the text pulled halfway toward the panel
             * colour (tiku_dim()), which recedes on light and dark
             * themes alike. */
            ink = it->enabled ? TIKU_C_TEXT
                              : tiku_dim(TIKU_C_TEXT, base);
        }
        base_y = r.y + PAD_TOP + f->ascent;
        if (it->marked) {
            draw_mark(s, r, tiku_tint(ink, MARK_TINT));
        }
        if (it->icon[0] != '\0') {
            /* The named art, dimmed when the item is disabled; without a
             * painter, or when it paints nothing, the slot is a plain
             * square (IV-054). */
            if (icon_painter == NULL ||
                !icon_painter(s, it->icon, r.x + PAD_LEFT, r.y + 2, 14,
                              !it->enabled)) {
                tiku_fill(s, (tiku_rect_t){r.x + PAD_LEFT,
                    r.y + 2, 14, 14}, it->enabled ? TIKU_C_SELECT :
                    tiku_dim(TIKU_C_TEXT, base));
            }
        }
        tiku_text(s, f, r.x + PAD_LEFT + (it->icon[0] != '\0' ? 18 : 0),
                       base_y, it->label, ink);

        if (it->submenu != NULL) {
            draw_arrow(s, r, tiku_tint(ink, MARK_TINT));
        } else if (it->shortcut != 0) {
            char sc[8];
            int n = 0;

            /* The modifiers read before the key, Ctrl as '#', Option as
             * '~' and Shift as '^'; Cmd draws no mark.  The text is placed
             * from the right edge inward. */
            if (it->mods & TIKU_MENU_MOD_CTRL) { sc[n++] = '#'; }
            if (it->mods & TIKU_MENU_MOD_OPTION) { sc[n++] = '~'; }
            if (it->mods & TIKU_MENU_MOD_SHIFT) { sc[n++] = '^'; }
            sc[n++] = it->shortcut;
            sc[n] = '\0';
            tiku_text(s, f,
                           r.x + r.w - tiku_text_width(f, sc) - 6,
                           base_y, sc, ink);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* MENU BAR                                                                  */
/*---------------------------------------------------------------------------*/

void
tiku_menubar_init(tiku_menubar_t *b)
{
    if (b != NULL) {
        memset(b, 0, sizeof *b);
        b->open = -1;
        b->height = tiku_text_height(tiku_font_plain()) + 6;
    }
}

int
tiku_menubar_add(tiku_menubar_t *b, tiku_menu_t *m)
{
    if (b == NULL || m == NULL || b->count >= TIKU_MENUBAR_MAX) {
        return -1;
    }
    b->menu[b->count] = m;
    return b->count++;
}

int
tiku_menubar_insert(tiku_menubar_t *b, int at, tiku_menu_t *m)
{
    int i;

    if (b == NULL || m == NULL || b->count >= TIKU_MENUBAR_MAX ||
        at < 0 || at > b->count) {
        return -1;
    }
    for (i = b->count; i > at; i--) {
        b->menu[i] = b->menu[i - 1];
    }
    b->menu[at] = m;
    b->count++;
    return at;
}

void
tiku_menubar_remove(tiku_menubar_t *b, tiku_menu_t *m)
{
    int i, j;

    for (i = 0; b != NULL && i < b->count; i++) {
        if (b->menu[i] == m) {
            for (j = i; j < b->count - 1; j++) {
                b->menu[j] = b->menu[j + 1];
            }
            b->count--;
            if (b->open == i) {
                b->open = -1;
            } else if (b->open > i) {
                b->open--;
            }
            return;
        }
    }
}

/** @brief Width a title occupies on the bar. */
static int
title_width(const tiku_menu_t *m)
{
    const tiku_font_t *f = tiku_font_plain();

    return tiku_text_width(f, m->title) + 2 * LABEL_SPACING + 4 +
           (m->mark ? 14 : 0);
}

int
tiku_menubar_title_at(const tiku_menubar_t *b, tiku_rect_t frame,
                          int x, int y)
{
    int i, tx = frame.x + 2;

    if (b == NULL || y < frame.y || y >= frame.y + frame.h) {
        return -1;
    }
    for (i = 0; i < b->count; i++) {
        int w = title_width(b->menu[i]);

        if (x >= tx && x < tx + w) {
            return i;
        }
        tx += w;
    }
    return -1;
}

int
tiku_menubar_width(const tiku_menubar_t *b)
{
    int i, w = 2;

    if (b == NULL) {
        return 0;
    }
    for (i = 0; i < b->count; i++) {
        w += title_width(b->menu[i]);
    }
    return w;
}

tiku_rect_t
tiku_menubar_panel(tiku_menubar_t *b, tiku_rect_t frame,
                       tiku_rect_t screen)
{
    tiku_rect_t r = { 0, 0, 0, 0 };
    int i, tx = frame.x + 2;

    if (b == NULL || b->open < 0 || b->open >= b->count) {
        return r;
    }
    for (i = 0; i < b->open; i++) {
        tx += title_width(b->menu[i]);
    }
    /* The panel drops from the title's bottom-left corner, one pixel
     * down and right. */
    return tiku_menu_place(b->menu[b->open], tx + 1,
                               frame.y + frame.h + 1, screen);
}

void
tiku_menubar_draw(tiku_menubar_t *b, tiku_surface_t *s,
                      tiku_rect_t frame)
{
    const tiku_font_t *f = tiku_font_plain();
    int i, tx = frame.x + 2;

    if (b == NULL) {
        return;
    }
    tiku_fill(s, frame, TIKU_C_PANEL);
    tiku_hline(s, frame.x, frame.y + frame.h - 1, frame.w,
                    tiku_tint(TIKU_C_PANEL, TIKU_DARKEN_2));
    for (i = 0; i < b->count; i++) {
        int w = title_width(b->menu[i]);
        tiku_rect_t t = { tx, frame.y, w, frame.h - 1 };
        tiku_rgb_t ink = TIKU_C_TEXT;

        if (i == b->open) {
            /* The open title as a pill, a pixel in from the bar's edges. */
            tiku_fill_round(s, (tiku_rect_t){ t.x, t.y + 1, t.w, t.h - 2 },
                            3, TIKU_CORNER_ALL, TIKU_C_SELECT);
            ink = TIKU_C_SELTEXT;
        }
        if (b->menu[i]->mark) {
            /* The mark in the title's own ink, one colour, since at twelve
             * pixels the coloured faces melt into the bar.  The ground
             * and the face take the row's colour, so only the edges
             * show. */
            tiku_logo_palette_t mono;
            tiku_rgb_t bg = (i == b->open) ? TIKU_C_SELECT
                                                : TIKU_C_PANEL;

            mono.ground = bg;
            mono.tint = bg;
            mono.dark = ink;
            mono.grey = ink;
            mono.accent = ink;
            mono.mix_a = ink;
            mono.mix_b = ink;
            tiku_logo_paint_with(s, (tiku_rect_t){ tx + LABEL_SPACING,
                                      frame.y + (frame.h - 13) / 2, 12,
                                      12 },
                                      0u, &mono, 1.0f);
        }
        tiku_text(s, f,
                       tx + LABEL_SPACING + 2 +
                           (b->menu[i]->mark ? 14 : 0),
                       frame.y + (frame.h - f->height) / 2 + f->ascent,
                       b->menu[i]->title, ink);
        tx += w;
    }
}

int
tiku_menu_track(tiku_menu_t *m, tiku_rect_t frame, int x,
                    int y)
{
    int at;

    if (m == NULL) {
        return 0;
    }
    at = tiku_menu_item_at(m, frame, x, y);
    if (at >= 0 && (m->item[at].separator || !m->item[at].enabled)) {
        at = -1;                /* nothing to choose there: no highlight */
    }
    if (at == m->hot) {
        return 0;
    }
    m->hot = at;
    return 1;
}
