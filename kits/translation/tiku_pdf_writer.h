/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_writer.h - pages drawn, written as a PDF.
 *
 * Draws onto PDF pages of any size: paths filled and stroked, gradients,
 * pictures, text in the machine's own faces, links and an outline, in
 * points from each page's top left, y downwards.  A TrueType face goes in
 * as a subset of the glyphs drawn, one with PostScript outlines whole;
 * each with its widths and a ToUnicode map, so its text can be searched
 * and copied.  The result is the bytes of a whole file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_WRITER_H_
#define TIKU_PDF_WRITER_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tiku_pdf_writer tiku_pdf_writer_t;

/** @brief How a path is painted; a colour with alpha 0 is not painted. */
typedef struct {
    uint32_t fill, stroke; /* 0xAARRGGBB                        */
    double width;          /* the stroke's, in points           */
    const double *dash;    /* on, off, on ... in points; NULL   */
    int ndash;
    int cap, join; /* PDF's: butt, round, square; miter,
                      round, bevel                      */
    int evenodd;   /* the even-odd rule, not nonzero    */
    double miter;  /* the miter limit; below 1, PDF's
                      own (10)                          */
} tiku_pdf_paint_t;

/** @brief One colour along a gradient, from 0 to 1. */
typedef struct {
    double at;
    uint32_t argb;
} tiku_pdf_stop_t;

/** @brief A gradient: along a line, or out from a circle to another. */
typedef struct {
    int radial;
    double x0, y0, x1, y1;       /* the line; the circles' centres */
    double r0, r1;               /* the circles' radii           */
    const tiku_pdf_stop_t *stop; /* in order of @c at            */
    int nstop;
} tiku_pdf_gradient_t;

/** @brief A glyph of a run: which, what it stands for (0 for a second
 *         glyph of one character), and where it starts along the run. */
typedef struct {
    unsigned gid;
    uint32_t cp;
    double x; /* points from the run's start       */
} tiku_pdf_glyph_t;

/** @brief A run of text: one face at one size and colour, its baseline
 *         starting at (x, y); bold and italic made up when asked. */
typedef struct {
    int font;    /* from tiku_pdf_writer_font()       */
    double size; /* points                            */
    uint32_t argb;
    int bold, italic; /* embolden; slant                   */
    double x, y;
    int outline;   /**< hollow stroked glyphs at size/16 points */
    int invisible; /**< extraction-only text, using PDF rendering mode three */
    double horizontal; /**< positive text-width factor; zero selects one */
} tiku_pdf_run_t;

/** @brief A writer with no pages.  NULL out of memory. */
tiku_pdf_writer_t *tiku_pdf_writer_new(void);

/** @brief The writer and everything it holds given back; nothing is
 *         written. */
void tiku_pdf_writer_free(tiku_pdf_writer_t *w);

/** @brief The document's title and author for its Info (UTF-8; NULL for
 *         none). */
void tiku_pdf_writer_info(tiku_pdf_writer_t *w, const char *title,
                          const char *author);

/** @brief A new page, @p width by @p height points, drawn on from now.
 *  @return its number from 0, or -1 for a size not above 0 or when there
 *          is no memory. */
int tiku_pdf_writer_page(tiku_pdf_writer_t *w, double width, double height);

/** @brief How many pages there are so far. */
int tiku_pdf_writer_pages(const tiku_pdf_writer_t *w);

/** @brief The drawing state kept: the transform and the clip.  A save
 *         still open when its page ends is restored there. */
void tiku_pdf_writer_save(tiku_pdf_writer_t *w);
/** @brief The drawing state of the last open save given back; nothing
 *         without one. */
void tiku_pdf_writer_restore(tiku_pdf_writer_t *w);

/** @brief Draw through the matrix [a b c d e f] from now: x' = a x + c y
 *         + e, y' = b x + d y + f, in the page's points, y down. */
void tiku_pdf_writer_transform(tiku_pdf_writer_t *w, double a, double b,
                               double c, double d, double e, double f);

/** @brief A new piece of path at (@p x, @p y).  The path is built a
 *         piece at a time, and painting, clipping or a gradient uses it
 *         up. */
void tiku_pdf_writer_move(tiku_pdf_writer_t *w, double x, double y);
/** @brief A straight line on to (@p x, @p y). */
void tiku_pdf_writer_line(tiku_pdf_writer_t *w, double x, double y);
/** @brief A cubic Bezier curve on to (@p x3, @p y3), (@p x1, @p y1) and
 *         (@p x2, @p y2) its control points. */
void tiku_pdf_writer_curve(tiku_pdf_writer_t *w, double x1, double y1,
                           double x2, double y2, double x3, double y3);
/** @brief The piece of path shut, back to where it began. */
void tiku_pdf_writer_close(tiku_pdf_writer_t *w);
/** @brief A rectangle added to the path, shut. */
void tiku_pdf_writer_rect(tiku_pdf_writer_t *w, double x, double y,
                          double width, double height);

/** @brief The path filled, stroked, or both, as @p p says. */
void tiku_pdf_writer_paint(tiku_pdf_writer_t *w, const tiku_pdf_paint_t *p);

/** @brief The path made the clip, until the state is restored. */
void tiku_pdf_writer_clip(tiku_pdf_writer_t *w, int evenodd);

/** @brief The path filled with @p g.  The gradient's alpha is its
 *         stops' average: a PDF shading has colour and no alpha. */
void tiku_pdf_writer_gradient(tiku_pdf_writer_t *w,
                              const tiku_pdf_gradient_t *g);

/**
 * @brief Face @p index of the TrueType or OpenType file @p path, ready for
 *        text; it is written when the file is: a TrueType face as the
 *        glyphs drawn with it, one with PostScript outlines whole.  The
 *        same face asked again is the same one.
 * @return its number, or -1 when it cannot be read; when memory ran
 *         short the writer fails as a whole.
 */
int tiku_pdf_writer_font(tiku_pdf_writer_t *w, const char *path, int index);
/** @brief Register a copy of 12 bytes..32 MiB of immutable font input.
 *  @note The source is retained through PDF finish; file changes cannot
 *        replace it. Identical bytes and face index reuse a registration;
 *        at most 32 distinct memory faces can be registered. Face index must
 *        be nonnegative. Returns -1 on refusal. */
int tiku_pdf_writer_font_mem(tiku_pdf_writer_t *w, const void *data,
                             size_t length, int index);

/** @brief Glyphs @p g, @p n of them, as @p run says. */
void tiku_pdf_writer_text(tiku_pdf_writer_t *w, const tiku_pdf_run_t *run,
                          const tiku_pdf_glyph_t *g, int n);

/**
 * @brief A picture from its file's bytes: a JPEG of 8-bit samples and one,
 *        three or four colours goes in as it is; a PNG, a BMP or another
 *        JPEG as its pixels, alpha kept.  Drawn with tiku_pdf_writer_draw()
 *        as often as wanted.
 * @return its number, or -1 when it cannot be read; @p pw and @p ph (may
 *         be NULL) are given its size in pixels.
 */
int tiku_pdf_writer_image(tiku_pdf_writer_t *w, const void *bytes, size_t len,
                          int *pw, int *ph);

/** @brief The same from pixels, 0xAARRGGBB, top row first. */
int tiku_pdf_writer_pixels(tiku_pdf_writer_t *w, const uint32_t *px, int width,
                           int height);

/** @brief Add file bytes with an explicit PDF image interpolation request.
 * @note interpolate is 0 or 1. JPEG bytes and alpha retention match image(). */
int tiku_pdf_writer_image_sampled(tiku_pdf_writer_t *writer, const void *bytes,
                                  size_t length, int *width, int *height,
                                  int interpolate);
/** @brief Add ARGB pixels with an explicit PDF image interpolation request. */
int tiku_pdf_writer_pixels_sampled(tiku_pdf_writer_t *writer,
                                   const uint32_t *pixels, int width,
                                   int height, int interpolate);
/** @brief Picture @p image stretched over the box at (@p x, @p y). */
void tiku_pdf_writer_draw(tiku_pdf_writer_t *w, int image, double x, double y,
                          double width, double height);
/** @brief Draw an image through a clipped unit-box affine transform.
 *  @note Matrix is a,b,c,d,e,f in top-down page points. Crop is fractional
 *        left,top,right,bottom; opposing sums must be below one.
 *  @return Zero on success; -1 on invalid input or writer failure. */
int tiku_pdf_writer_draw_crop(tiku_pdf_writer_t *w, int image,
                              const double matrix[6], const double crop[4]);

/** @brief A link over the box to @p uri, outside the document. */
void tiku_pdf_writer_link(tiku_pdf_writer_t *w, double x, double y,
                          double width, double height, const char *uri);

/** @brief A link over the box to page @p page (from 0), @p top points
 *         down it; the page may not be drawn yet. */
void tiku_pdf_writer_jump(tiku_pdf_writer_t *w, double x, double y,
                          double width, double height, int page, double top);

/** @brief An outline entry, @p level deep (0 at the top), leading to
 *         @p page, @p top points down it; entries in reading order. */
void tiku_pdf_writer_mark(tiku_pdf_writer_t *w, int level, const char *title,
                          int page, double top);

/** @brief Begin one ActualText span with valid NUL-terminated UTF-8.
 *  @return 0, or -1 for invalid input, nested spans or allocation failure.
 *  @note At most 1 MiB of source bytes. End before closing the page; glyph
 *        drawing inside the span uses this source string for extraction. */
int tiku_pdf_writer_actual_begin(tiku_pdf_writer_t *w, const char *text);
/** @brief End the current ActualText span; no span is a no-op. */
void tiku_pdf_writer_actual_end(tiku_pdf_writer_t *w);

/**
 * @brief The file: every page, the faces, pictures, links and outline.
 * @return its bytes, with their length in @p len; NULL when there was no
 *         memory, no page, or an unclosed ActualText span.
 * @note The writer is freed either way; the caller frees the bytes with
 *       free().
 */
unsigned char *tiku_pdf_writer_finish(tiku_pdf_writer_t *w, size_t *len);

#endif /* TIKU_PDF_WRITER_H_ */
