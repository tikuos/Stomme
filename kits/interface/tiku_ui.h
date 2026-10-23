/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_ui.h - the R5 control set.
 *
 * Immediate-mode drawing: each call paints one control in one state and
 * keeps nothing.  Layout and hit testing are the caller's.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_UI_H_
#define TIKU_UI_H_
#ifdef TIKU_DISPLAYLESS
/* A displayless build, a board's, takes these names from the tiku_ui.h on
 * its include path, which records the calls; the painter below is the
 * desktop's. */
#include <tiku_ui.h>
#else

#include "tiku_font.h"
#include "tiku_gfx.h"

/*---------------------------------------------------------------------------*/
/* Control sizes, measured from the plain font so a control fits its text at */
/* every interface size; at the default 12 px a list row is 22 pixels and a  */
/* push button 26.                                                           */
/*---------------------------------------------------------------------------*/

/** @brief The height of a list row: the plain font's height plus 7. */
int tiku_ui_row_h(void);

/** @brief A push button's height: the plain font's height plus 11. */
int tiku_ui_button_h(void);

/**
 * @brief The width a button needs to hold @p label: its text plus a font
 *        height either side.
 *
 * @param least A floor, for a row of buttons of one width.  0 for none.
 */
int tiku_ui_button_w(const char *label, int least);

/** @brief A stepper's square half, the - or the + of a value control. */
int tiku_ui_step_w(void);

/** @brief Control states, combinable where it makes sense. */
#define TIKU_S_NORMAL    0x00u
#define TIKU_S_PRESSED   0x01u
#define TIKU_S_FOCUS     0x02u   /**< keyboard navigation ring    */
#define TIKU_S_DEFAULT   0x04u   /**< the window's default button */
#define TIKU_S_DISABLED  0x08u
#define TIKU_S_ON        0x10u   /**< checkbox, radio or switch value */
/* TIKU_S_ICON: a toolbar's icon button, flat, sunken while pressed and
 * raised while TIKU_S_HOT (under the pointer).  Its label is recorded for
 * narration and never drawn; the caller draws the icon over the button. */
#define TIKU_S_ICON      0x20u
#define TIKU_S_HOT       0x40u

/** @brief Fill @p r with the panel colour. */
void tiku_ui_panel(tiku_surface_t *s, tiku_rect_t r);

/**
 * @brief A menu's or alert's two-pixel border, in a shade that stands
 *        out from the panel in every theme.
 *
 * Drawn just inside @p r and nowhere else, so the popup keeps the hit
 * tests and damage of @p r; the inside is left as it was.
 */
void tiku_ui_popup_border(tiku_surface_t *s, tiku_rect_t r);

/** @brief A raised panel-coloured face with the standard bevel pair. */
void tiku_ui_raised(tiku_surface_t *s, tiku_rect_t r);

/** @brief A sunken document well (list bodies, text fields). */
void tiku_ui_sunken(tiku_surface_t *s, tiku_rect_t r,
                         tiku_rgb_t face);

/**
 * @brief A push button: a rounded face with @p label centred.
 *
 * TIKU_S_DEFAULT fills the face with the selection colour, TIKU_S_PRESSED
 * darkens it and moves the label a pixel down and right, and TIKU_S_ICON
 * draws only the flat toolbar face.
 */
void tiku_ui_button(tiku_surface_t *s, tiku_rect_t r,
                         const char *label, unsigned state);

/**
 * @brief A checkbox: a 13-pixel well at the left of @p r, filled with a
 *        square when TIKU_S_ON, and @p label after it.
 */
void tiku_ui_checkbox(tiku_surface_t *s, tiku_rect_t r,
                           const char *label, unsigned state);

/**
 * @brief A radio button: a 13-pixel circle at the left of @p r, with a dot
 *        when TIKU_S_ON, and @p label after it.
 */
void tiku_ui_radio(tiku_surface_t *s, tiku_rect_t r,
                        const char *label, unsigned state);

/**
 * @brief A menu field: the control a popup menu drops from.
 *
 * A raised face with a dark frame, clipped corners and a two-step bevel;
 * the label is clipped short of the marker, a stroked down arrow at the
 * right.
 */
void tiku_ui_menufield(tiku_surface_t *s, tiku_rect_t r,
                            const char *label, unsigned state);

/** @brief Which widget a window tab is carrying. */
#define TIKU_TAB_CLOSE  0
#define TIKU_TAB_ZOOM   1

/**
 * @brief A window tab's close or zoom widget.
 *
 * Each square is a diagonal gradient from white at its top-left to the tab
 * colour at its bottom-right, inside a darker outline.  The zoom widget is
 * two such squares: a small one at the top-left drawn over a larger one
 * offset down and right.
 *
 * @param face The tab colour the gradient runs to.
 * @param down Non-zero while pressed, which inverts the gradient.
 */
void tiku_ui_tab_widget(tiku_surface_t *s, tiku_rect_t r,
                             int which, tiku_rgb_t face, int down);

/** @brief A text entry field; @p caret < 0 draws none. */
void tiku_ui_textfield(tiku_surface_t *s, tiku_rect_t r,
                            const char *text, int caret, unsigned state);

/**
 * @brief The same field with a selected range drawn in reverse video.
 *
 * @param sel_a,sel_b Byte offsets; equal means no selection.
 */
void tiku_ui_textfield_sel(tiku_surface_t *s, tiku_rect_t r,
                                const char *text, int caret, int sel_a,
                                int sel_b, unsigned state);

/**
 * @brief The selectable field, with its text slid left by @p scroll_px,
 *        for an editor whose text outgrows the box.
 */
void tiku_ui_textfield_scroll(tiku_surface_t *s,
                                   tiku_rect_t r, const char *text,
                                   int caret, int sel_a, int sel_b,
                                   unsigned state, int scroll_px);

/**
 * @brief The triangle a stepper or a scrollbar end wears, centred in
 *        @p r, without the button under it.
 *
 * @p dir is 0 up, 1 down, 2 left, 3 right.  With @p enabled 0 the glyph is
 * drawn lighter, for an arrow that cannot go further.
 */
void tiku_ui_arrow(tiku_surface_t *s, tiku_rect_t r, int dir, int enabled);

/**
 * @brief A gauge: a framed well filled from the left, in the selection
 *        colour, to @p fraction of its width.
 *
 * @p fraction is clamped to 0..1, so a ratio of counts with an empty
 * denominator may be passed as it is.  It is recorded in thousandths.
 */
void tiku_ui_gauge(tiku_surface_t *s, tiku_rect_t r, float fraction);

/** @brief What a pill is saying: nothing yet, good, bad, or look here. */
#define TIKU_TONE_IDLE  0
#define TIKU_TONE_PASS  1
#define TIKU_TONE_FAIL  2
#define TIKU_TONE_WARN  3

/** @brief How wide a pill saying @p label needs to be. */
int tiku_ui_pill_w(const char *label);

/**
 * @brief A pill: a rounded lozenge in the theme colour of @p tone, with
 *        @p label centred in bold in the selection text colour.
 *
 * It has no pressed state and no bevel.
 */
void tiku_ui_pill(tiku_surface_t *s, tiku_rect_t r, const char *label,
                  int tone);

/** @brief How wide a switch labelled @p label needs to be. */
int tiku_ui_switch_w(const char *label);

/**
 * @brief A switch, for a setting that takes effect the moment it is
 *        flipped.
 *
 * @p label sits at the left of @p r and a rounded track at its right end,
 * with a round knob: at the right on the selection colour with TIKU_S_ON,
 * at the left on a darker shade of the panel without.  TIKU_S_PRESSED
 * darkens the track, TIKU_S_DISABLED greys the track, the knob and the
 * label, and TIKU_S_FOCUS rings the track in the focus colour.
 *
 * @note The caller takes a press anywhere in @p r, the label included, as
 *       a press on the switch.
 */
void tiku_ui_switch(tiku_surface_t *s, tiku_rect_t r, const char *label,
                    unsigned state);

/**
 * @brief The size of a tip saying @p text, at 0,0, for placing it before
 *        it is drawn; empty for NULL or empty text.
 */
tiku_rect_t tiku_ui_tip_size(const char *text);

/**
 * @brief A tip: a framed box in the note colour with @p text inset, on one
 *        line.
 *
 * @p r is usually tiku_ui_tip_size() moved beside what the tip explains.
 */
void tiku_ui_tip(tiku_surface_t *s, tiku_rect_t r, const char *text);

/**
 * @brief A scrollbar: an arrow button at each end and a pill-shaped thumb.
 *
 * An arrow is dimmed when the thumb is at its end, and both are when the
 * thumb fills the track.
 *
 * @param pos    Thumb start, 0..1 of the track.
 * @param frac   Thumb length, 0..1 of the track, at least 0.05.
 * @param horiz  Non-zero for a horizontal bar.
 */
void tiku_ui_scrollbar(tiku_surface_t *s, tiku_rect_t r,
                            float pos, float frac, int horiz);

/** @brief Where tiku_ui_scrollbar() draws the thumb of the bar @p r, for
 *         a caller hit testing it; the arrows are the squares at the
 *         ends, as thick as the bar. */
tiku_rect_t tiku_ui_scrollbar_thumb(tiku_rect_t r, float pos, float frac,
                                    int horiz);

/**
 * @brief The same bar with one arrow drawn held: @p held is -1 for the
 *        back arrow, +1 for the forward one, 0 for neither.
 *
 * @note The recorded bar (the display list's scrollbar op) has no held
 *       arrow, so a remote window draws both arrows up.
 */
void tiku_ui_scrollbar_held(tiku_surface_t *s, tiku_rect_t r,
                            float pos, float frac, int horiz, int held);

/** @brief A menu bar with @p n labels; @p active highlights one (-1 none). */
void tiku_ui_menubar(tiku_surface_t *s, tiku_rect_t r,
                          const char *const *items, int n, int active);

/** @brief Per-row art for a menu; drawn over the row's ground. */
typedef void (*tiku_ui_menu_icon_fn)(tiku_surface_t *s, int index,
                                          int x, int y, int size,
                                          void *context);

/**
 * @brief A dropped menu panel with an icon gutter; @p icon may be NULL for
 *        none.
 *
 * An item that begins with '-' is a separator.  A long label is clipped
 * to its row.
 */
void tiku_ui_menu_icons(tiku_surface_t *s, tiku_rect_t r,
                             const char *const *items, int n, int hot,
                             tiku_ui_menu_icon_fn icon, void *context);

/**
 * @brief A dropped menu panel with @p n items; @p hot highlights one.
 */
void tiku_ui_menu(tiku_surface_t *s, tiku_rect_t r,
                       const char *const *items, int n, int hot);

/**
 * @brief The mark on a menu row that opens a submenu.
 *
 * A small right-pointing triangle set 8 pixels in from the right edge of
 * @p item and centred in its height, drawn as horizontal runs, which the
 * display list carries as commands.
 */
void tiku_ui_submenu_arrow(tiku_surface_t *s, tiku_rect_t item,
                           tiku_rgb_t c);

/**
 * @brief One list row of @p text; a selected row takes the selection
 *        colours.  Recorded as a row with its text and selection.
 */
void tiku_ui_list_row(tiku_surface_t *s, tiku_rect_t r,
                           const char *text, int selected);

/** @brief Column headers for a list view. */
void tiku_ui_list_header(tiku_surface_t *s, tiku_rect_t r,
                              const char *const *cols, const int *widths,
                              int n, int sort_col);

/**
 * @brief A window frame with its title tab.
 *
 * @param r       The whole window including its border.
 * @param title   Tab text.
 * @param active  Non-zero for the focused window, whose tab is TIKU_C_TAB;
 *                otherwise TIKU_C_TAB_IDLE.
 * @return The content rectangle inside the frame.
 */
tiku_rect_t tiku_ui_window(tiku_surface_t *s,
                                     tiku_rect_t r, const char *title,
                                     int active);

/**
 * @brief A frame around a block of controls, with @p label in a gap in its
 *        top edge.
 *
 * The frame is etched: a dark hairline and a light one beside it.
 *
 * @return the rectangle inside the frame, for the controls.
 */
tiku_rect_t tiku_ui_group(tiku_surface_t *s, tiku_rect_t r,
                               const char *label);

/**
 * @brief Draw @p text at @p x, baseline @p y, as the @p n runs of @p span,
 *        each in its tiku_ink() colour.
 *
 * The line past the table, or all of it when @p span is NULL, is drawn
 * plain.  Each run is its own text command, so a recorded line arrives as
 * several.
 *
 * @return the total advance in pixels.
 */
int tiku_ui_text_spans(tiku_surface_t *s, const tiku_font_t *f,
                            int x, int y, const char *text,
                            const tiku_span_t *span, int n);

#endif /* TIKU_DISPLAYLESS */
#endif /* TIKU_UI_H_ */
