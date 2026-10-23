/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_menu.h - the interactive menu widget: items with marks, shortcuts,
 * submenus, layout and tracking, drawn through the control set.
 *
 * tiku_menuset_t in tiku_window.h is the flat description an application
 * publishes of its menus; this is the menu the shell tracks a pointer
 * through and draws.  The geometry is the menu kit's own: padding, item
 * height, the mark gutter, the stroked check, the submenu arrow, the
 * two-line separator and the one-pixel seam between items.  The caller
 * sets a menu's labels and enabled states each time it opens.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_MENU_H_
#define TIKU_MENU_H_

#include "tiku_gfx.h"
#include "tiku_font.h"

#define TIKU_MENU_ITEMS_MAX  40     /**< items in one menu                 */
#define TIKU_MENU_LABEL_MAX  48     /**< bytes of a label, NUL included    */
#define TIKU_MENU_DEPTH_MAX   4     /**< submenu levels the Tracker builds */

/* Modifier bits; Shift and Cmd have the values of TIKU_MOD_SHIFT and
 * TIKU_MOD_CMD in tiku_event.h, and Option does not have TIKU_MOD_OPTION's. */
#define TIKU_MENU_MOD_SHIFT  0x1u
#define TIKU_MENU_MOD_CMD    0x2u
#define TIKU_MENU_MOD_CTRL   0x4u
#define TIKU_MENU_MOD_OPTION 0x8u

/** @brief One entry.  A separator carries no label and no command. */
typedef struct tiku_menu_item {
    char                  label[TIKU_MENU_LABEL_MAX];
    int                   command;      /**< 0 for a separator or a submenu  */
    char                  shortcut;     /**< key character, 0 for none       */
    unsigned              mods;         /**< modifiers drawn with the key    */
    int                   enabled;
    int                   marked;       /**< draws the check                 */
    int                   separator;
    char                  icon[32];     /**< icon name, "" for none          */
    struct tiku_menu *submenu;          /**< not owned                       */
} tiku_menu_item_t;

/** @brief A menu: its title, its items and the state of one opening. */
typedef struct tiku_menu {
    char             title[TIKU_MENU_LABEL_MAX];
    tiku_menu_item_t item[TIKU_MENU_ITEMS_MAX];
    int              count;
    int              width;             /**< set by tiku_menu_measure()      */
    int              height;            /**< set by tiku_menu_measure()      */
    int              open_index;        /**< item whose submenu is showing   */
    int              hot;               /**< highlighted item, or -1         */
    int              mark;              /**< nonzero: the bar draws the
                                             TikuOS mark before the title  */
} tiku_menu_t;

/** @brief Remove every item, keeping the title; nothing is hot or open. */
void tiku_menu_clear(tiku_menu_t *m);

/**
 * @brief Append an item.
 *
 * @param sc   Shortcut character, or 0.
 * @param mods Modifiers drawn with the shortcut, TIKU_MENU_MOD_*.
 * @return Its index, or -1 when the menu is full.
 */
int tiku_menu_add(tiku_menu_t *m, const char *label, int command,
                      char sc, unsigned mods, int enabled);

/**
 * @brief Append a separator, which is disabled and never hit.
 *
 * @return Its index, or -1 when the menu is full.
 */
int tiku_menu_add_separator(tiku_menu_t *m);

/**
 * @brief Append an item that opens submenu @p sub, which is not owned;
 *        the item sends no command.
 *
 * @return Its index, or -1 when the menu is full.
 */
int tiku_menu_add_submenu(tiku_menu_t *m, const char *label,
                              tiku_menu_t *sub, int enabled);

/**
 * @brief Insert an item at @p at, shifting the rest down.
 *
 * @return @p at, or -1 when the menu is full or @p at is out of range.
 */
int tiku_menu_insert(tiku_menu_t *m, int at, const char *label,
                         int command, char sc, unsigned mods, int enabled);

/** @brief Remove the item at @p at. */
void tiku_menu_remove(tiku_menu_t *m, int at);

/** @brief Index of the first item carrying @p command, or -1. */
int tiku_menu_find(const tiku_menu_t *m, int command);

/** @brief Set the check on the item carrying @p command. */
void tiku_menu_mark(tiku_menu_t *m, int command, int marked);

/**
 * @brief Check the item for @p chosen and clear the rest of @p commands,
 *        as a radio set does.
 */
void tiku_menu_mark_radio(tiku_menu_t *m, const int *commands, int n,
                              int chosen);

/** @brief Enable or disable the item carrying @p command. */
void tiku_menu_enable(tiku_menu_t *m, int command, int enabled);

/**
 * @brief Give the item carrying @p command the icon named @p icon, or none
 *        for NULL; the name is cut at 31 bytes.
 */
void tiku_menu_set_icon(tiku_menu_t *m, int command, const char *icon);

/**
 * @brief How a menu paints a named icon; @p dimmed for a disabled item.
 *
 * The menu layer has no rasteriser of its own; with no painter set, or
 * when the painter paints nothing, the slot is a plain square (IV-054).
 *
 * @return Nonzero when the art was painted.
 */
typedef int (*tiku_menu_icon_fn)(tiku_surface_t *s,
    const char *icon, int x, int y, int size, int dimmed);

/** @brief Paint every menu's icons with @p fn; NULL draws plain squares. */
void tiku_menu_set_icon_painter(tiku_menu_icon_fn fn);

/**
 * @brief Replace the label, command, shortcut and modifiers of the item at
 *        @p at together, so its label never names another command.
 */
void tiku_menu_relabel(tiku_menu_t *m, int at, const char *label,
                           int command, char sc, unsigned mods);

/** @brief Compute width and height into the menu.  Idempotent. */
void tiku_menu_measure(tiku_menu_t *m);

/** @brief Height of the item at @p i, or 0 when there is none. */
int tiku_menu_item_height(const tiku_menu_t *m, int i);

/** @brief Top of the item at @p i, relative to the menu's own top. */
int tiku_menu_item_top(const tiku_menu_t *m, int i);

/**
 * @brief The item at a point inside the menu, or -1.
 *
 * Separators and the one-pixel seams between items are misses; a disabled
 * item is a hit.
 */
int tiku_menu_item_at(const tiku_menu_t *m, tiku_rect_t frame,
                          int x, int y);

/**
 * @brief Move the hot row, the one tiku_menu_draw() highlights, to the
 *        enabled item under the pointer.
 *
 * Outside the frame, and over a separator or a disabled item, the hot row
 * clears.
 *
 * @return Nonzero when the hot row changed and the menu needs repainting.
 */
int tiku_menu_track(tiku_menu_t *m, tiku_rect_t frame,
                        int x, int y);

/**
 * @brief Measure the menu and place it at @p x, @p y, moved to stay on
 *        @p screen.
 */
tiku_rect_t tiku_menu_place(tiku_menu_t *m, int x, int y,
                                     tiku_rect_t screen);

/**
 * @brief Where the submenu of item @p i sits: off the right edge of the
 *        menu at @p frame, or flipped to its left when it would leave
 *        @p screen.
 *
 * @return The submenu's frame, or an empty rectangle when item @p i has
 *         no submenu.
 */
tiku_rect_t tiku_menu_place_sub(tiku_menu_t *m, int i,
                                         tiku_rect_t frame,
                                         tiku_rect_t screen);

/**
 * @brief Measure the menu and draw it in @p frame: the card, the items,
 *        the hot row, checks, icons, shortcuts and submenu arrows.
 */
void tiku_menu_draw(tiku_menu_t *m, tiku_surface_t *s,
                        tiku_rect_t frame);

/*---------------------------------------------------------------------------*/
/* MENU BAR                                                                  */
/*---------------------------------------------------------------------------*/

/* Six application menus (TIKU_MENUSET_MENUS) plus the desktop's system and
 * Devices menus. */
#define TIKU_MENUBAR_MAX 8

/** @brief A row of menu titles; the menus are not owned. */
typedef struct {
    tiku_menu_t *menu[TIKU_MENUBAR_MAX];
    int              count;
    int              open;               /**< index of the open menu, or -1 */
    int              height;             /**< pixels, from the plain font  */
} tiku_menubar_t;

/** @brief Empty the bar, with no menu open. */
void tiku_menubar_init(tiku_menubar_t *b);

/**
 * @brief Append menu @p m.
 *
 * @return Its index, or -1 when the bar is full.
 */
int tiku_menubar_add(tiku_menubar_t *b, tiku_menu_t *m);

/**
 * @brief Insert menu @p m at @p at.
 *
 * @return @p at, or -1 when the bar is full or @p at is out of range.
 */
int tiku_menubar_insert(tiku_menubar_t *b, int at,
                            tiku_menu_t *m);

/** @brief Remove menu @p m from the bar. */
void tiku_menubar_remove(tiku_menubar_t *b, tiku_menu_t *m);

/** @brief Title index at a point on the bar, or -1. */
int tiku_menubar_title_at(const tiku_menubar_t *b,
                              tiku_rect_t frame, int x, int y);

/** @brief How far the titles reach from the bar's left edge, in pixels;
 *         what a bar may draw beside them starts there. */
int tiku_menubar_width(const tiku_menubar_t *b);

/** @brief Where the open menu's panel sits, under its title. */
tiku_rect_t tiku_menubar_panel(tiku_menubar_t *b,
                                        tiku_rect_t frame,
                                        tiku_rect_t screen);

/** @brief Draw the bar in @p frame, the open title as a pill. */
void tiku_menubar_draw(tiku_menubar_t *b, tiku_surface_t *s,
                           tiku_rect_t frame);

#endif /* TIKU_MENU_H_ */
