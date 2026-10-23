/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_writer_private.h - what the writer's parts share: the bytes
 * written so far and where each object starts, the faces and pictures
 * the pages name, and one resource dictionary every page points at,
 * written last, when everything in it is known.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_WRITER_PRIVATE_H_
#define TIKU_PDF_WRITER_PRIVATE_H_

#include "tiku_pdf_writer.h"

#include <stdarg.h>

#define PW_CATALOG   1              /* the objects every file has        */
#define PW_PAGES     2
#define PW_RESOURCES 3
#define PW_INFO      4
#define PW_FIRST     5              /* the first number handed out       */

/** @brief Bytes growing at their end; @c bad once memory ran out. */
typedef struct {
    unsigned char *p;               /**< the bytes                       */
    size_t         n, cap;          /**< how many, and room              */
    int            bad;             /**< an append failed                */
} pw_buf_t;

/** @brief A face as the pages use it. */
typedef struct {
    char           path[1024];      /**< its file                        */
    int            index;           /**< its face in a collection        */
    int            obj;             /**< its Type0 font's number         */
    int            upem;            /**< units to the em                 */
    int            nglyph;          /**< glyphs it has                   */
    int            cff;             /**< PostScript outlines: whole file */
    unsigned short *adv;            /**< each glyph's advance, font units */
    unsigned char  *used;           /**< each glyph: drawn               */
    uint32_t       *uni;            /**< each glyph: what it stands for  */
    unsigned char *data;            /**< owned memory-source font, or NULL */
    size_t         length;          /**< owned source byte length */
} pw_font_t;

/** @brief A link: where on its page, and to what. */
typedef struct {
    int    page;                    /**< the page it is on               */
    double x0, y0, x1, y1;          /**< its box, PDF's way up           */
    char  *uri;                     /**< out of the document, or NULL    */
    int    to;                      /**< else the page it leads to       */
    double top;                     /**< and how far down it, y down     */
    int    obj;                     /**< its annotation's number         */
} pw_link_t;

/** @brief An outline entry. */
typedef struct {
    int    level;                   /**< 0 at the top                    */
    char  *title;                   /**< UTF-8                           */
    int    page;                    /**< where it leads                  */
    double top;                     /**< points down that page           */
} pw_mark_t;

/** @brief A page: its size, and its object's number. */
typedef struct {
    double w, h;                    /**< points                          */
    int    obj;                     /**< reserved when the page began    */
} pw_page_t;

/** @brief A resource every page may name: its name's prefix and number,
 *         and its object's. */
typedef struct {
    char kind;                      /**< 'F' font, 'I' image, 'G' state,
                                       'S' shading                       */
    int  n;                         /**< the n of /F<n>, /Im<n> ...      */
    int  obj;                       /**< the object it names             */
    int  key;                       /**< what it is: an alpha pair ...   */
} pw_res_t;

/** @brief A writer: the file so far, the open page's drawing and path,
 *         and what waits for the end. */
struct tiku_pdf_writer {
    pw_buf_t   out;                 /**< the file so far                 */
    pw_buf_t   content;             /**< the open page's drawing         */
    pw_buf_t   path;                /**< the path being built            */
    long      *xref;                /**< each object's offset; 0 unwritten */
    int        nobj, xcap;          /**< numbers handed out, and room    */
    pw_page_t *page;                /**< the pages, in order             */
    int        npage, pagecap;
    pw_font_t *font;                /**< the faces                       */
    int        nfont, fontcap;
    pw_res_t  *res;                 /**< everything the pages name       */
    int        nres, rescap;
    pw_link_t *link;                /**< every page's links              */
    int        nlink, linkcap;
    pw_mark_t *mark;                /**< the outline, in order           */
    int        nmark, markcap;
    int        nimage, nshade;      /**< names handed out                */
    int        actual;              /**< an open ActualText span */
    int        depth;               /**< saves not yet restored          */
    char      *title, *author;      /**< for the Info dictionary         */
};

/** @brief @p n bytes appended to @p b. */
void pw_put(pw_buf_t *b, const void *s, size_t n);
/** @brief Formatted text appended to @p b. */
void pw_printf(pw_buf_t *b, const char *fmt, ...);
/** @brief @p v appended as PDF writes a number: three decimals at most,
 *         no exponent, no trailing zeros; a space after. */
void pw_num(pw_buf_t *b, double v);
/** @brief A new object's number.  @return it, or 0 out of memory. */
int pw_reserve(tiku_pdf_writer_t *w);
/** @brief Object @p obj begins here: its offset noted, "n 0 obj" put. */
void pw_begin(tiku_pdf_writer_t *w, int obj);
/** @brief A stream object @p obj of @p n bytes, deflated when @p deflate
 *         is set and that is smaller, @p dict's entries before /Length. */
void pw_stream(tiku_pdf_writer_t *w, int obj, const char *dict,
               const unsigned char *data, size_t n, int deflate);
/** @brief Resource @p kind keyed @p key, or a new one for object @p obj
 *         (0: reserve one).  @return its index, or -1. */
int pw_resource(tiku_pdf_writer_t *w, char kind, int key, int obj);
/** @brief A face's objects written: the subset or the whole file, its
 *         widths, and what its glyphs stand for. */
void pw_font_write(tiku_pdf_writer_t *w, pw_font_t *f);
/** @brief Face @p f's tables read for measuring.  @return 0, -1 for a
 *         face it cannot read, or -2 when memory ran short. */
int pw_font_load(pw_font_t *f);
/** @brief Face @p f let go. */
void pw_font_free(pw_font_t *f);

#endif /* TIKU_PDF_WRITER_PRIVATE_H_ */
