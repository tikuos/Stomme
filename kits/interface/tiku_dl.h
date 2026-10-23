/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_dl.h - the display list: a window recorded as its drawing calls.
 *
 * A session sends a window as this list or as its pixels.  The commands
 * are the interface's own, semantic where it is semantic (a button, a
 * gauge, a tab strip), and measuring stays at the sending end.  The
 * recorded form is the wire form, [u16 op][u16 length][payload] per
 * command, so flattening is a copy and a player steps over an op it does
 * not know.  The design is in kits/interface/doc/display-list.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DL_H_
#define TIKU_DL_H_
#ifdef TIKU_DISPLAYLESS
/* A build with no pixels, a board's, takes these names from the
 * recorder's tiku_dl.h on its include path; the declarations below are
 * for builds that paint. */
#include <tiku_dl.h>
#else

#include <stddef.h>
#include <stdint.h>

#include "tiku_gfx.h"

/**
 * @brief Which face a piece of text is in; the far end resolves it.
 *
 * Six names, then sizes.  From TIKU_DL_MONO_SIZED an ID names a baked mono
 * face: 4 times its size's place in the ladder plus its cut (regular, bold,
 * italic, bold italic).  Any other value past the six is a plain face at
 * that many pixels, 10, 12, 14 or 16, so a status line drawn smaller than
 * the body arrives at its own size.  A player that knows only the first
 * four draws the italic names in the smallest plain face.
 * @note A player without the sized mono IDs draws them as plain faces:
 *       an application and the desktop must be built together.
 */
typedef enum {
    TIKU_DL_PLAIN = 0,
    TIKU_DL_BOLD  = 1,
    TIKU_DL_MONO  = 2,
    TIKU_DL_MONO_BOLD = 3,
    TIKU_DL_MONO_ITALIC = 4,
    TIKU_DL_MONO_BOLD_ITALIC = 5,
    TIKU_DL_MONO_SIZED = 64     /**< First ID naming a mono size and cut */
} tiku_dl_face_t;

/** @brief A display list: commands kept as their wire bytes. */
typedef struct tiku_dl tiku_dl_t;

/** @brief An empty list.  NULL if there is no room for one. */
tiku_dl_t *tiku_dl_new(void);

/** @brief Free @p dl; NULL is allowed. */
void tiku_dl_free(tiku_dl_t *dl);

/** @brief Empty it, misses and counts too, keeping the room it has
 *         already taken. */
void tiku_dl_clear(tiku_dl_t *dl);

/** @brief How many commands are in it. */
int tiku_dl_count(const tiku_dl_t *dl);

/**
 * @brief Add every command of @p src to the end of @p dst, its misses
 *        too, so that a window drawn from a list records as that list.
 * @return 1 when it fit.
 */
int tiku_dl_append(tiku_dl_t *dst, const tiku_dl_t *src);

/**
 * @brief Note that something was drawn which this cannot carry.
 *
 * A recording surface calls it for a drawing with no command of its own.
 * The list stays usable but no longer describes the whole window.
 */
void tiku_dl_miss(tiku_dl_t *dl);

/** @brief How many such calls there were.  0 means the list is whole. */
int tiku_dl_misses(const tiku_dl_t *dl);

/*---------------------------------------------------------------------------*/
/* Recording.  Each mirrors the drawing call of the same name, and each     */
/* returns 1 when it was written down and 0 when there was no room.          */
/*---------------------------------------------------------------------------*/

/** @brief A fill of @p r in @p c. */
int tiku_dl_fill(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t c);
/** @brief A one-pixel outline just inside @p r, in @p c. */
int tiku_dl_frame(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t c);
/** @brief A one-pixel bevel: @p light on top and left, @p shadow
 *         elsewhere. */
int tiku_dl_bevel(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t light,
                  tiku_rgb_t shadow);
/** @brief A line @p w long, rightwards from (@p x, @p y). */
int tiku_dl_hline(tiku_dl_t *dl, int x, int y, int w, tiku_rgb_t c);
/** @brief A line @p h long, downwards from (@p x, @p y). */
int tiku_dl_vline(tiku_dl_t *dl, int x, int y, int h, tiku_rgb_t c);
/** @brief @p text in @p face, its baseline at @p y and its left edge
 *         at @p x. */
int tiku_dl_text(tiku_dl_t *dl, tiku_dl_face_t face, int x, int y,
                 const char *text, tiku_rgb_t c);
/** @brief @p text in @p face, centred in @p r. */
int tiku_dl_text_centered(tiku_dl_t *dl, tiku_dl_face_t face, tiku_rect_t r,
                          const char *text, tiku_rgb_t c);
/** @brief A clip to @p r for the commands after it. */
int tiku_dl_clip_set(tiku_dl_t *dl, tiku_rect_t r);
/** @brief The clip taken off again. */
int tiku_dl_clip_reset(tiku_dl_t *dl);

/* The controls.  One command carries a control whole: its bevels, label
 * and state. */

/** @brief A panel's ground over @p r. */
int tiku_dl_panel(tiku_dl_t *dl, tiku_rect_t r);
/** @brief A raised panel-coloured face over @p r. */
int tiku_dl_raised(tiku_dl_t *dl, tiku_rect_t r);
/** @brief A sunken well over @p r, @p face inside. */
int tiku_dl_sunken(tiku_dl_t *dl, tiku_rect_t r, tiku_rgb_t face);
/** @brief A push button with @p label, in @p state (TIKU_S_* bits). */
int tiku_dl_button(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                   unsigned state);
/** @brief A checkbox with @p label, in @p state. */
int tiku_dl_checkbox(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                     unsigned state);
/** @brief A radio button with @p label, in @p state. */
int tiku_dl_radio(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                  unsigned state);
/** @brief A gauge: its place, and its fill @p per_mille in thousandths,
 *         clamped to 0..1000. */
int tiku_dl_gauge(tiku_dl_t *dl, tiku_rect_t r, int per_mille);

/** @brief A tip: the note box and the whole of what it says. */
int tiku_dl_tip(tiku_dl_t *dl, tiku_rect_t r, const char *text);

/** @brief A text field: where it is, what is in it, and its state; the
 *         caret is not carried. */
int tiku_dl_textfield(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                      unsigned state);

/** @brief A scrollbar as its position and visible fraction, each in
 *         thousandths; @p horiz for one that lies down. */
int tiku_dl_scrollbar(tiku_dl_t *dl, tiku_rect_t r, int pos_per_mille,
                      int frac_per_mille, int horiz);

/**
 * @brief A slider: the track, and the value on its own scale.
 */
int tiku_dl_slider(tiku_dl_t *dl, tiku_rect_t r, int min, int max,
                   int value);

/**
 * @brief A menu field: the choice it is showing, and its state.
 *
 * One command carries the field and its marker, which is drawn pixel by
 * pixel.
 */
int tiku_dl_menufield(tiku_dl_t *dl, tiku_rect_t r, const char *label,
                      unsigned state);

/**
 * @brief A tab strip: the tabs by name, and which one is current.
 *
 * @p labels is @p count NUL-terminated strings one after another, as
 * tiku_tabs holds them.  @return 0 also for no labels.
 */
int tiku_dl_tabs(tiku_dl_t *dl, tiku_rect_t r, int count, int current,
                 const char *labels, size_t labels_len);

/**
 * @brief The alert's icon by @p kind, centred on (@p cx, @p cy).
 *
 * One command carries the disc and its mark; tiku_dl_play() draws it with
 * tiku_alert_icon_draw().
 */
int tiku_dl_alert_icon(tiku_dl_t *dl, int cx, int cy, int kind);

/** @brief A list row showing @p text, selected or not. */
int tiku_dl_list_row(tiku_dl_t *dl, tiku_rect_t r, const char *text,
                     int selected);

/*---------------------------------------------------------------------------*/
/* Pictures with a definition                                                */
/*---------------------------------------------------------------------------*/

/**
 * @brief How many distinct pieces of art one list may define.
 *
 * Past it, an icon with new art is not recorded and the list is marked
 * not whole.
 */
#define TIKU_DL_ART_MAX 32

/**
 * @brief Record one icon: art defined once, then placed by reference.
 *
 * The art, an HVIF blob, crosses once per list and each placement is a
 * sixteen-byte command, rasterised at the receiver's own resolution.  The
 * art's identity is a hash of the blob, so a directory's own icon in place
 * of a baked one is new art.  @p mix (0..255) of @p wash is laid over the
 * icon: 0 for none, black at 87 for a selected icon.
 *
 * @return 1 when recorded; 0 when not, and the list is then marked not
 *         whole.
 */
int tiku_dl_icon(tiku_dl_t *dl, const void *hvif, size_t hlen,
                 int x, int y, int size, unsigned mix, tiku_rgb_t wash);

/** @brief How many icon placements the list carries.  0 means none. */
int tiku_dl_icons(const tiku_dl_t *dl);

/**
 * @brief How many pictures the list carries.
 *
 * A far end that cannot play picture commands steps over them; a sender
 * checks this, as it checks tiku_dl_icons(), before sending it a list.
 */
int tiku_dl_pictures(const tiku_dl_t *dl);

/**
 * @brief The most pixels one picture command may carry.
 *
 * A thumbnail or an icon fits; larger pictures are refused.
 */
#define TIKU_DL_PICTURE_MAX 4096

/**
 * @brief A bitmap of @p w by @p h at @p x, @p y, with its own mask.
 *
 * The pixels are ARGB: a byte of alpha and three of colour.  A pixel
 * whose alpha is zero is not painted and any other is painted opaque.
 *
 * @return 1 when recorded; 0 when not (too many pixels, or no room), and
 *         the caller then marks a miss.
 */
int tiku_dl_picture(tiku_dl_t *dl, int x, int y, int w, int h,
                    const uint32_t *px);

/** @brief How the far end draws an icon command; @p mix and @p wash as
 *         for tiku_dl_icon(). */
typedef int (*tiku_dl_icon_fn)(tiku_surface_t *s, const void *hvif,
                               size_t hlen, int x, int y, int size,
                               unsigned mix, tiku_rgb_t wash);

/**
 * @brief Register the painter icon commands are played with.
 *
 * The rasteriser lives in a kit above this one, so whoever links both
 * registers it once.  With none registered an icon command is stepped
 * over.
 */
void tiku_dl_set_icon_painter(tiku_dl_icon_fn fn);

/*---------------------------------------------------------------------------*/
/* The wire, and the far end                                                 */
/*---------------------------------------------------------------------------*/

/** @brief How many bytes it would flatten to. */
size_t tiku_dl_flat_size(const tiku_dl_t *dl);

/**
 * @brief Write it into @p buf.
 *
 * @param wrote Filled with how much was used; may be NULL.
 * @return 1 when it fitted, 0 otherwise.
 */
int tiku_dl_flatten(const tiku_dl_t *dl, void *buf, size_t max, size_t *wrote);

/*---------------------------------------------------------------------------*
 * Reading the stream, without drawing it.
 *
 * A reader that draws nothing (an agent, a screen reader, a test) reads
 * the window as facts: an iterator that hands back each command as what
 * it says, and a narrator built on it that writes plain lines.
 *---------------------------------------------------------------------------*/

/** @brief What kind of fact a command is, once read. */
typedef enum {
    TIKU_DL_FACT_PAINT = 0,     /* a stroke of paint: no meaning beyond it */
    TIKU_DL_FACT_TEXT,          /* words on the window                     */
    TIKU_DL_FACT_BUTTON,
    TIKU_DL_FACT_CHECKBOX,
    TIKU_DL_FACT_RADIO,
    TIKU_DL_FACT_LIST_ROW,
    TIKU_DL_FACT_GAUGE,
    TIKU_DL_FACT_TIP,
    TIKU_DL_FACT_TEXTFIELD,
    TIKU_DL_FACT_SCROLLBAR,
    TIKU_DL_FACT_SLIDER,
    TIKU_DL_FACT_ALERT_ICON,
    TIKU_DL_FACT_TABS,
    TIKU_DL_FACT_MENUFIELD,
    TIKU_DL_FACT_PICTURE,       /* a bitmap with its own mask              */
    TIKU_DL_FACT_ICON
} tiku_dl_fact_kind_t;

/**
 * @brief One command, as the facts it carries.
 *
 * @c text points into the caller's buffer and lives as long as it does;
 * for tabs it is the first of @c v1 names laid end to end.  The numbers
 * are the op's own: a gauge's fill in thousandths; a slider's min, max
 * and value; a scrollbar's position, fraction and whether it lies down;
 * a tab strip's count and current; a picture's size; an alert's kind.
 */
typedef struct {
    tiku_dl_fact_kind_t kind;
    tiku_rect_t         rect;
    const char         *text;
    unsigned            state;
    int                 v1, v2, v3;
} tiku_dl_fact_t;

/**
 * @brief Read the next command from a flattened stream.
 *
 * @p at is the cursor, 0 to start; it advances past what was read.  A
 * malformed command (text with no terminator, a body shorter than its op
 * needs) stops the read.
 * @return 1 with @p out filled, 0 at the end or on a malformed command.
 */
int tiku_dl_read(const void *buf, size_t len, size_t *at,
                 tiku_dl_fact_t *out);

/**
 * @brief Say what the stream shows, one fact per line, into @p out.
 *
 * The lines give the words, the controls with their labels and states,
 * and the values, read from @p buf alone; paint strokes are skipped.
 *
 * @return the length written (truncated to @p max - 1 when it must be).
 */
size_t tiku_dl_narrate(const void *buf, size_t len, char *out, size_t max);

/**
 * @brief Narrate @p dl itself: flatten it and narrate the lines, then,
 *        when the list is not whole, a closing line that says how many
 *        marks the wire could not carry.
 *
 * @return the bytes written; 0, with @p out empty, when there is nothing
 *         to say or no room to say it.
 */
size_t tiku_dl_say(const tiku_dl_t *dl, char *out, size_t max);

/**
 * @brief The list as one machine line per control: kind, label, rect,
 *        state, tab-separated.
 *
 * For a program that acts on controls.  Paint is skipped; text is its pen
 * position with zero size, or for centred text the box it is set in; the
 * closing line about misses matches tiku_dl_say()'s.
 *
 * Line shape: <kind>\t"<label>"\t<x> <y> <w> <h>\t<state>\n
 */
size_t tiku_dl_facts(const tiku_dl_t *dl, char *out, size_t max);

/**
 * @brief Read a list out of @p buf, @p len bytes; @p read, when not NULL,
 *        is set to the bytes read.
 *
 * These bytes arrive from a wire: every length is checked against what
 * is left of the buffer before it is believed, and text is refused
 * unless it ends where it says it does.
 *
 * @return a list the caller frees, or NULL if the bytes are not one.
 */
tiku_dl_t *tiku_dl_unflatten(const void *buf, size_t len, size_t *read);

/**
 * @brief Draw it into @p s.
 *
 * @return how many commands were carried out; a command whose op this
 *         build does not know, or an icon it cannot draw, is stepped
 *         over and not counted.
 */
int tiku_dl_play(const tiku_dl_t *dl, tiku_surface_t *s);

#endif /* TIKU_DISPLAYLESS */
#endif /* TIKU_DL_H_ */
