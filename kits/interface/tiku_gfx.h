/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_gfx.h - the drawing surface and the R5 colour vocabulary.
 *
 * A surface is a plain 32-bit buffer with a clip rectangle.  Every widget
 * draws through these calls, onto an X11 or AppKit window's surface, a PNG
 * dump or a display list.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_GFX_H_
#define TIKU_GFX_H_
#ifdef TIKU_DISPLAYLESS
/* A build with no pixels, a board's, takes these names from the
 * recorder's tiku_gfx.h on its include path; the declarations below are
 * for builds that paint. */
#include <tiku_gfx.h>
#else

#include <stddef.h>
#include <stdint.h>

#include "tiku_geom.h"

/**
 * @brief The theme: every semantic colour the toolkit and its
 *        applications paint with, in one runtime table.
 *
 * The TIKU_C_* names read through the live theme, so a swapped table
 * recolours every widget.  The default table is the BeOS R5 look, which
 * the pixel tests pin.  Pictorial art (icon bodies, badge glyphs) and
 * shades derived with tiku_tint() are not in it.
 */
typedef struct {
    tiku_rgb_t panel;      /* control surfaces                     */
    tiku_rgb_t doc;        /* document/list ground                 */
    tiku_rgb_t text;       /* ink                                  */
    tiku_rgb_t tab;        /* the active window tab                */
    tiku_rgb_t tab_idle;
    tiku_rgb_t select;     /* selection ground                     */
    tiku_rgb_t seltext;    /* ink over a selection                 */
    tiku_rgb_t focus;      /* keyboard-navigation marks            */
    tiku_rgb_t backdrop;   /* the desktop behind everything        */
    tiku_rgb_t danger;     /* the stop severity                    */
    tiku_rgb_t note;       /* tooltip/expander paper               */
    tiku_rgb_t tab_text;   /* ink over the active tab                */
    /*
     * The ink of a program's text, read on the document ground: a word
     * the language reserves, a number, text in quotes, a remark.  The
     * three tables in tiku_theme.c initialise by position, so a role
     * inserted before the last one shifts every value after it and still
     * compiles.
     */
    tiku_rgb_t code_word;
    tiku_rgb_t code_num;
    tiku_rgb_t code_str;
    tiku_rgb_t code_rem;
    tiku_rgb_t success;    /* a verdict that went right              */
    tiku_rgb_t warn;       /* a verdict that needs a look            */
} tiku_theme_t;

/** @brief The live theme.  Never NULL. */
const tiku_theme_t *tiku_theme(void);

/** @brief Swap the theme; NULL restores the default (the R5 look). */
void tiku_theme_set(const tiku_theme_t *theme);

/** @brief The built-in dark table. */
const tiku_theme_t *tiku_theme_dark(void);

/** @brief The built-in table with no hue in it. */
const tiku_theme_t *tiku_theme_mono(void);

/**
 * @brief Whether the live table names no hue anywhere.
 *
 * Pictorial art (icon bodies, thumbnails) is not in the theme; it asks
 * this to drain its own colour under a table with no hue.  The answer is
 * worked out from the table when the theme is set.
 */
int tiku_theme_achromatic(void);

/** @brief @p c at its luminance: the same brightness, no hue. */
tiku_rgb_t tiku_grey(tiku_rgb_t c);

/* The live theme's roles, by their classic names. */
#define TIKU_C_PANEL      (tiku_theme()->panel)
#define TIKU_C_DOC        (tiku_theme()->doc)
#define TIKU_C_TEXT       (tiku_theme()->text)
#define TIKU_C_TAB        (tiku_theme()->tab)
#define TIKU_C_TAB_IDLE   (tiku_theme()->tab_idle)
#define TIKU_C_SELECT     (tiku_theme()->select)
#define TIKU_C_SELTEXT    (tiku_theme()->seltext)
#define TIKU_C_FOCUS      (tiku_theme()->focus)
#define TIKU_C_BACKDROP   (tiku_theme()->backdrop)
#define TIKU_C_DANGER     (tiku_theme()->danger)
#define TIKU_C_NOTE       (tiku_theme()->note)
#define TIKU_C_TABTEXT    (tiku_theme()->tab_text)
#define TIKU_C_CODE_WORD  (tiku_theme()->code_word)
#define TIKU_C_CODE_NUM   (tiku_theme()->code_num)
#define TIKU_C_CODE_STR   (tiku_theme()->code_str)
#define TIKU_C_CODE_REM   (tiku_theme()->code_rem)
#define TIKU_C_SUCCESS    (tiku_theme()->success)
#define TIKU_C_WARN       (tiku_theme()->warn)

/* BeOS tint constants for tiku_tint(): below 1 lightens toward white,
 * above 1 darkens toward black. */
#define TIKU_LIGHTEN_MAX  0.00f
#define TIKU_LIGHTEN_2    0.70f
#define TIKU_LIGHTEN_1    0.90f
#define TIKU_NO_TINT      1.00f
#define TIKU_DARKEN_1     1.02f
#define TIKU_DARKEN_2     1.06f
#define TIKU_DARKEN_3     1.07f
#define TIKU_DARKEN_4     1.08f
#define TIKU_DARKEN_MAX   2.00f

struct tiku_dl;

/**
 * @brief A drawing surface: native pixels, a logical size and clip, a
 *        scale, and an optional recorder.
 */
typedef struct {
    tiku_rgb_t  *px;        /* native pixels, row-major */
    int               w, h;      /* logical size             */
    tiku_rect_t  clip;      /* logical, like every rect */
    /* Native pixels per logical pixel (0 reads as 1).  Every call here
     * takes logical coordinates; a scaled surface keeps px at
     * (w*scale) x (h*scale) and paints scale x scale blocks, except text,
     * which an even scale draws from a face made at twice the size. */
    int               scale;
    /* When set, every drawing call below also records itself here (see
     * tiku_dl.h), so a window is recorded by being drawn.  NULL for a
     * surface that does not record. */
    struct tiku_dl   *record;
    /* 1 while drawing is inside a recorded call, else 0.  Only the
     * outermost call records and sets it: a frame is drawn as four lines
     * and a line as a fill, and those inner calls are not recorded again. */
    int               record_depth;
    tiku_rect_t        damage; /* optional outer repaint clip */
    int                damage_active; /* nonzero while damage is in force */
} tiku_surface_t;

/**
 * @brief Enter a drawing call on @p s.
 * @return nonzero when this is the outermost call on a recording surface
 *         and records itself; hand it to tiku_gfx_rec_leave().
 * @note For the kit's own drawing calls; an application sets @c record
 *       and draws.
 */
int tiku_gfx_rec_enter(tiku_surface_t *s);
/**
 * @brief Mark the list not whole: pixels went here which no command can
 *        carry.
 *
 * A painter of pixels with no command of its own calls it.  Inside a
 * recorded call it does nothing: the pixels belong to that call.
 */
void tiku_gfx_rec_miss(tiku_surface_t *s);

/** @brief Leave a drawing call; @p on is what tiku_gfx_rec_enter()
 *         returned. */
void tiku_gfx_rec_leave(tiku_surface_t *s, int on);

/** @brief A @p w by @p h surface at scale 1, filled with @p bg, or NULL
 *         on failure. */
tiku_surface_t *tiku_surface_new(int w, int h, tiku_rgb_t bg);

/** @brief Give a surface @p scale (1 to 4) native pixels per logical
 *         one, its pixels replaced and filled with @p bg.
 *  @return 0, or -1 for an invalid scale or no memory, leaving @p s
 *          unchanged. */
int tiku_surface_rescale(tiku_surface_t *s, int scale,
                              tiku_rgb_t bg);

/** @brief The logical pixel at (x, y), or 0 outside the surface. */
tiku_rgb_t tiku_peek(const tiku_surface_t *s, int x, int y);

/** @brief Replace a surface's pixels with @p w by @p h logical pixels at
 *         its scale, filled with @p bg; the clip is reset and the repaint
 *         region dropped.
 *  @return 0, or -1 for a bad size or no memory, leaving @p s unchanged. */
int tiku_surface_resize(tiku_surface_t *s, int w, int h,
                             tiku_rgb_t bg);

/** @brief Free @p s and its pixels; NULL is allowed. */
void tiku_surface_free(tiku_surface_t *s);

/** @brief Restrict drawing to @p r, cut to the surface and to any repaint
 *         region. */
void tiku_clip_set(tiku_surface_t *s, tiku_rect_t r);

/** @brief Reset the local clip, retaining any outer repaint boundary. */
void tiku_clip_reset(tiku_surface_t *s);
/** @brief Restrict all nested clips to a repaint region; NULL removes it. */
void tiku_clip_damage(tiku_surface_t *s, const tiku_rect_t *region);
/** @brief Bounding rectangle of two regions; empty regions are ignored. */
tiku_rect_t tiku_rect_union(tiku_rect_t a, tiku_rect_t b);

/** @brief Apply a BeOS tint to a colour (see the constants above). */
tiku_rgb_t tiku_tint(tiku_rgb_t c, float tint);

/**
 * @brief Disabled ink: @p ink pulled halfway toward the @p ground under it.
 *
 * The midpoint of ink and ground is half the enabled contrast on a light
 * theme and on a dark one.
 */
tiku_rgb_t tiku_dim(tiku_rgb_t ink, tiku_rgb_t ground);

/**
 * @brief What a run of text means.
 *
 * A widget that paints meaning takes one of these and asks tiku_ink()
 * for the shade under the live theme.
 */
typedef enum {
    TIKU_INK_PLAIN = 0,   /* the document's own ink                    */
    TIKU_INK_FAINT,       /* receded: a line number, a gutter mark     */
    TIKU_INK_WORD,        /* a word the language reserves              */
    TIKU_INK_NUM,         /* a number written down                     */
    TIKU_INK_STR,         /* text in quotes                            */
    TIKU_INK_REMARK,      /* a remark the machine ignores              */
    TIKU_INK_WRONG,       /* what the reader must see is wrong         */
    TIKU_INK_PATH,        /* a name in the namespace: an address       */
    TIKU_INK_LINK         /* words that go somewhere when pressed      */
} tiku_ink_t;

/**
 * @brief One run of @p len bytes that means @p ink.
 *
 * A table of runs covers a line from its first byte, each run starting
 * where the one before ended; the bytes past the last run are plain.
 */
typedef struct {
    int        len;
    tiku_ink_t ink;
} tiku_span_t;

/**
 * @brief The shade @p which means, under the live theme.
 *
 * FAINT is the document's ink pulled halfway toward its ground by
 * tiku_dim(); WRONG is the danger colour; PATH is the string ink met
 * halfway with the selection colour, and LINK the selection colour.  An
 * ink this build does not know is the document's own.
 */
tiku_rgb_t tiku_ink(tiku_ink_t which);

/**
 * @brief Draw a @p w by @p h ARGB bitmap at @p x, @p y.
 *
 * A pixel with zero alpha is not painted and any other is painted
 * opaque.  A picture of at most TIKU_DL_PICTURE_MAX pixels records itself
 * as a picture command; a larger one marks the list not whole.
 */
void tiku_picture(tiku_surface_t *s, int x, int y, int w, int h,
                       const uint32_t *px);

/**
 * @brief Move a block of pixels within the surface (CopyBits).
 *
 * A list moves the rows below an inserted or removed row with it.  The
 * source is cut to the surface and the destination to the clip, and the
 * two may overlap.  Marks the list not whole.
 *
 * @param src Where the pixels are now.
 * @param dx  How far right to put them, negative for left.
 * @param dy  How far down, negative for up.
 */
void tiku_copy_bits(tiku_surface_t *s, tiku_rect_t src,
                         int dx, int dy);

/**
 * @brief Put @p src's pixels into @p dst at @p x, @p y, within the clip;
 *        marks the list not whole.
 *
 * A window with pixels of its own keeps them in @p src between frames
 * and is put on the screen whole each time.
 */
void tiku_blit(tiku_surface_t *dst, int x, int y,
                    const tiku_surface_t *src);

/** @brief A copy of the pixels under @p r, as a surface of its own. */
tiku_surface_t *tiku_grab(const tiku_surface_t *s, tiku_rect_t r);

/**
 * @brief Lay @p src over @p dst at @p x, @p y, @p amount of the way, 0
 *        to 1; surfaces of different scales are not blended.
 *
 * A fade lays a picture taken before back over the screen at one minus
 * the time.  Marks the list not whole.
 */
void tiku_blend(tiku_surface_t *dst, int x, int y,
                const tiku_surface_t *src, float amount);

/** @brief The same, with @p src stretched or shrunk to fill @p at, by
 *         the nearest source pixel. */
void tiku_blend_scaled(tiku_surface_t *dst, tiku_rect_t at,
                       const tiku_surface_t *src, float amount);

/**
 * @brief A soft shadow cast by @p r onto whatever is around it.
 *
 * Laid outside the shape only (the rectangle with its corners rounded by
 * @p corner), two pixels lower than it, falling off over @p radius pixels
 * up to 32 native ones; @p strength is how dark it is at the edge, 0 to
 * 1.  It blends over what is drawn there.  Marks the list not whole.
 */
void tiku_shadow(tiku_surface_t *s, tiku_rect_t r, int radius, int corner,
                 float strength);

/* Which corners a rounded rectangle rounds. */
#define TIKU_CORNER_TL  0x1u
#define TIKU_CORNER_TR  0x2u
#define TIKU_CORNER_BL  0x4u
#define TIKU_CORNER_BR  0x8u
#define TIKU_CORNER_ALL 0xfu
#define TIKU_CORNER_TOP (TIKU_CORNER_TL | TIKU_CORNER_TR)

/** @brief Fill @p r with the @p corners rounded by @p radius, softly. */
void tiku_fill_round(tiku_surface_t *s, tiku_rect_t r, int radius,
                     unsigned corners, tiku_rgb_t c);

/** @brief A hairline around @p r, the @p corners rounded by @p radius. */
void tiku_frame_round(tiku_surface_t *s, tiku_rect_t r, int radius,
                      unsigned corners, tiku_rgb_t c);

/**
 * @brief Mix @p c over @p r at @p amount, 0 for nothing and 1 for solid.
 *
 * A screen is taken to black and back with it, without what is
 * underneath being drawn again.  Marks the list not whole.
 */
void tiku_veil(tiku_surface_t *s, tiku_rect_t r, tiku_rgb_t c,
               float amount);

/** @brief Fill @p r with @p c, within the clip. */
void tiku_fill(tiku_surface_t *s, tiku_rect_t r,
                    tiku_rgb_t c);

/** @brief One-pixel outline just inside @p r. */
void tiku_frame(tiku_surface_t *s, tiku_rect_t r,
                     tiku_rgb_t c);

/** @brief A line @p len long, rightwards from (@p x, @p y). */
void tiku_hline(tiku_surface_t *s, int x, int y, int len,
                     tiku_rgb_t c);
/** @brief A line @p len long, downwards from (@p x, @p y). */
void tiku_vline(tiku_surface_t *s, int x, int y, int len,
                     tiku_rgb_t c);
/** @brief Set one logical pixel, within the clip; marks the list not
 *         whole, clipped or not. */
void tiku_pixel(tiku_surface_t *s, int x, int y, tiku_rgb_t c);

/** @brief Expand a logical surface into a native framebuffer: a whole
 *         number scale repeats each pixel, any other takes the nearest. */
void tiku_scale_pixels(tiku_rgb_t *destination,
                            int destination_width, int destination_height,
                            const tiku_rgb_t *source,
                            int source_width, int source_height);

/**
 * @brief Invert the pixels along a rectangle's outline.
 *
 * The outline shows over whatever it lands on, and a second stroke undoes
 * it, so a rubber band is drawn and erased without saving what was under
 * it.  Marks the list not whole.
 */
void tiku_invert_frame(tiku_surface_t *s, tiku_rect_t r);

/** @brief A one-pixel bevel: @p light on top and left, @p shadow
 *         elsewhere. */
void tiku_bevel(tiku_surface_t *s, tiku_rect_t r,
                     tiku_rgb_t light, tiku_rgb_t shadow);

/** @brief Shrink a rectangle by @p n on every side; its size stops at
 *         zero. */
tiku_rect_t tiku_inset(tiku_rect_t r, int n);

/** @brief Write the surface's native pixels as an uncompressed PNG.
 *  @return 0, or -1 on failure. */
int tiku_surface_png(const tiku_surface_t *s, const char *path);

#endif /* TIKU_DISPLAYLESS */
#endif /* TIKU_GFX_H_ */
