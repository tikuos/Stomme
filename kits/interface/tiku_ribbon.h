/*
 * KintsugiDesktop v0.01 Interface Kit.
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 * tiku_ribbon.h - bounded grouped command layouts and local appearance tokens.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_RIBBON_H_
#define TIKU_RIBBON_H_
#include "tiku_font.h"
#include "tiku_gfx.h"

#define TIKU_RIBBON_ITEMS 64
#define TIKU_RIBBON_GROUPS 16

/** @brief Visual forms sharing explicit command geometry. */
typedef enum {
    TIKU_RIBBON_BUTTON,
    TIKU_RIBBON_SPLIT,
    TIKU_RIBBON_MENU,
    TIKU_RIBBON_COMBO,
    TIKU_RIBBON_GALLERY
} tiku_ribbon_kind_t;
/** @brief Project-drawn command glyphs; NONE leaves the caption alone. */
typedef enum {
    TIKU_RIBBON_NONE,
    TIKU_RIBBON_SAVE,
    TIKU_RIBBON_UNDO,
    TIKU_RIBBON_REDO,
    TIKU_RIBBON_COPY,
    TIKU_RIBBON_PASTE,
    TIKU_RIBBON_SEARCH,
    TIKU_RIBBON_DOCUMENT,
    TIKU_RIBBON_OPEN
} tiku_ribbon_icon_t;
/** @brief Copied per-window palette; applying it changes no global theme. */
typedef struct {
    tiku_rgb_t title, panel, canvas, input, text, muted, border;
    tiku_rgb_t hover, pressed, selected, accent, on_accent, disabled;
} tiku_ribbon_theme_t;
/** @brief One caller-owned command and its bounded physical hit rectangles. */
typedef struct {
    int id, secondary, group, x, width, enabled, checked;
    int secondary_enabled; /**< split arrow permission, independent of main */
    tiku_ribbon_kind_t kind;
    tiku_ribbon_icon_t icon;
    char label[64];
    tiku_rect_t bounds, main, arrow; /**< clipped interaction rectangles */
    tiku_rect_t paint, paint_main;   /**< unclipped drawing rectangles */
} tiku_ribbon_item_t;
/** @brief One named group spanning consecutive command items. */
typedef struct {
    char label[64];
    int x, end;
} tiku_ribbon_group_t;
/** @brief Fixed-capacity layout rebuilt before input and painting. */
typedef struct {
    tiku_rect_t viewport;
    int scale, scroll, content, count, groups;
    tiku_ribbon_item_t items[TIKU_RIBBON_ITEMS];
    tiku_ribbon_group_t group[TIKU_RIBBON_GROUPS];
} tiku_ribbon_t;

/** @brief Return the light or dark local interface palette. */
tiku_ribbon_theme_t tiku_ribbon_theme(int dark);
/** @brief Begin a 100..200 percent layout; invalid dimensions refuse. */
int tiku_ribbon_begin(tiku_ribbon_t *ribbon, tiku_rect_t viewport, int scale,
                      int scroll);
/** @brief Start a group; labels must fit whole. Returns zero or -1. */
int tiku_ribbon_group(tiku_ribbon_t *ribbon, const char *label);
/** @brief Append a 16..512 design-unit command to the active group.
 * @note checked is 0, 1 or 2 (mixed). IDs are opaque to the toolkit.
 * Failure leaves the layout unchanged.
 */
int tiku_ribbon_add(tiku_ribbon_t *ribbon, int id, int secondary,
                    tiku_ribbon_kind_t kind, tiku_ribbon_icon_t icon,
                    const char *label, int width, int enabled, int checked);
/** @brief Clamp scrolling and publish clipped main/arrow hit rectangles. */
void tiku_ribbon_end(tiku_ribbon_t *ribbon);
/** @brief Move by physical pixels and recompute bounds, clamped to content. */
void tiku_ribbon_scroll(tiku_ribbon_t *ribbon, int delta);
/** @brief Fit a popup inside the screen, preferring below its anchor. */
tiku_rect_t tiku_ribbon_popup(tiku_rect_t anchor, tiku_rect_t screen, int width,
                              int height);
/** @brief Draw one original command glyph inside its supplied rectangle. */
void tiku_ribbon_icon(tiku_surface_t *surface, tiku_rect_t bounds,
                      tiku_ribbon_icon_t icon, tiku_rgb_t color);
/** @brief Draw an item with hover/press/focus flags and its checked state. */
void tiku_ribbon_item_draw(tiku_surface_t *surface,
                           const tiku_ribbon_item_t *item,
                           const tiku_ribbon_theme_t *theme,
                           const tiku_font_t *font, unsigned state);
/** @brief Paint groups/items using the supplied local palette and fonts. */
void tiku_ribbon_draw(tiku_surface_t *surface, const tiku_ribbon_t *ribbon,
                      const tiku_ribbon_theme_t *theme, const tiku_font_t *font,
                      const tiku_font_t *caption, int hover, int pressed,
                      int focus);
#endif
