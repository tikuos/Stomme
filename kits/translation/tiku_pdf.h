/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf.h - reading a PDF: its pages, what they say, how they look.
 *
 * A document opens from a file or from bytes, encrypted or not.  A page
 * gives its text as lines, or as characters with their boxes, and its
 * look as pixels at any scale; the outline, links and signatures are
 * read too.  tiku_pdf_view.h is a viewer built on this for any program's
 * window (kits/translation/doc/pdf.md compares the two).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_H_
#define TIKU_PDF_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tiku_pdf tiku_pdf_t;

/** @brief Why a file did not open. */
typedef enum {
    TIKU_PDF_OK = 0,
    TIKU_PDF_NOT_PDF,           /* no %PDF header                       */
    TIKU_PDF_UNSUPPORTED,       /* encrypted by a handler this lacks    */
    TIKU_PDF_BAD,               /* unreadable, or beyond repair         */
    TIKU_PDF_PASSWORD,          /* encrypted: the password does not open it */
    TIKU_PDF_CERTIFICATE,       /* sealed to certificates: the key given,
                                   if any, is none of theirs           */
    TIKU_PDF_KEY_FILE           /* the key file is not one this reads   */
} tiku_pdf_err_t;

/** @brief Whether @p path holds a PDF, by its first bytes. */
int tiku_pdf_is_pdf(const char *path);

/**
 * @brief Open @p path.
 *
 * A cross-reference table that lies is rebuilt by reading the whole
 * file, so a damaged document opens as far as its objects allow.
 *
 * @return TIKU_PDF_OK with @p out set; @p out is untouched otherwise.
 */
tiku_pdf_err_t tiku_pdf_open(const char *path, tiku_pdf_t **out);

/** @brief Open from bytes, which are copied. */
tiku_pdf_err_t tiku_pdf_decode(const void *bytes, size_t len,
                               tiku_pdf_t **out);

/**
 * @brief The same for an encrypted document, with @p password (UTF-8),
 *        its user's or its owner's.
 *
 * The standard handler: RC4 and AES-128 (revisions 2 to 4) and AES-256
 * (5 and 6).  tiku_pdf_open() and tiku_pdf_decode() use the empty
 * password.
 */
tiku_pdf_err_t tiku_pdf_open_password(const char *path, const char *password,
                                      tiku_pdf_t **out);
/** @brief The same from bytes, which are copied. */
tiku_pdf_err_t tiku_pdf_decode_password(const void *bytes, size_t len,
                                        const char *password,
                                        tiku_pdf_t **out);

/**
 * @brief The same for a document sealed to certificates (Adobe.PubSec),
 *        with the reader's key file @p keyfile -- PKCS#12 (.p12, .pfx) or
 *        PEM, holding an RSA key -- unlocked by @p password.
 * @return TIKU_PDF_PASSWORD when the password does not open the key file;
 *         TIKU_PDF_KEY_FILE when it is not a key file this reads;
 *         TIKU_PDF_CERTIFICATE when its key is none the document was
 *         sealed to; otherwise as tiku_pdf_open().
 */
tiku_pdf_err_t tiku_pdf_open_key(const char *path, const char *keyfile,
                                 const char *password, tiku_pdf_t **out);
/** @brief The same from bytes, the key file's too; both are copied. */
tiku_pdf_err_t tiku_pdf_decode_key(const void *bytes, size_t len,
                                   const void *key, size_t keylen,
                                   const char *password, tiku_pdf_t **out);

/** @brief Whether the document's author allows its text to be copied
 *         out; always, for one not encrypted or opened by its owner. */
int tiku_pdf_may_copy(const tiku_pdf_t *doc);

/** @brief Free the document and every object read from it. */
void tiku_pdf_close(tiku_pdf_t *doc);

/** @brief How many pages the document has. */
int tiku_pdf_pages(const tiku_pdf_t *doc);

/** @brief Whether the cross-reference table had to be rebuilt. */
int tiku_pdf_repaired(const tiku_pdf_t *doc);

/**
 * @brief The size of @p page (from 0) in points, and its rotation.
 *
 * The CropBox within the MediaBox, each inherited from the page tree when
 * the page has none; a page without a MediaBox is Letter.  @p rotate is
 * 0, 90, 180 or 270.
 * @return 0, or -1 when there is no such page.
 */
int tiku_pdf_page_size(tiku_pdf_t *doc, int page, double *w, double *h,
                       int *rotate);

/**
 * @brief The words on @p page, as UTF-8 lines, in reading order as far
 *        as the page's own order goes.
 *
 * Text is placed where the content stream puts it: a jump down the
 * page ends a line, a gap along it becomes a space.  Fonts without a
 * text mapping give what their encoding says; a symbol font with
 * neither gives nothing for those glyphs.
 * @return the text, "" for a page without text, or NULL when the page
 *         could not be read.
 * @note The caller frees the text.
 */
char *tiku_pdf_page_text(tiku_pdf_t *doc, int page);

/** @brief The document's title from its Info dictionary, or NULL. */
char *tiku_pdf_title(tiku_pdf_t *doc);

/** @brief One character of a page's text, and the box it stands in on
 *         the page as drawn at scale 1: points, rotation applied, y down. */
typedef struct {
    uint32_t cp;                    /* a made-up break has an empty box   */
    float    x0, y0, x1, y1;
} tiku_pdf_char_t;

/**
 * @brief The characters of @p page, in the order and with the spaces and
 *        line breaks tiku_pdf_page_text() gives, each where it is drawn.
 * @return how many, with @p out; -1 when the page is unreadable.
 * @note The caller frees @p out with free().
 */
long tiku_pdf_page_chars(tiku_pdf_t *doc, int page, tiku_pdf_char_t **out);

/** @brief Where a link or an outline entry leads: a page and the point to
 *         bring into view on it (as drawn at scale 1), or an address. */
typedef struct {
    int    page;                    /* from 0; -1 when not in the document */
    double left, top;
    char  *uri;                     /* an address outside it, or NULL     */
} tiku_pdf_dest_t;

/** @brief One entry of the outline (a document's bookmarks). */
typedef struct {
    char           *title;          /* UTF-8                              */
    int             level;          /* 0 at the top                       */
    int             below;          /* how many entries follow under it   */
    int             open;           /* the document shows it unfolded     */
    tiku_pdf_dest_t to;
} tiku_pdf_outline_t;

/**
 * @brief The outline, depth first: an entry, then everything under it.
 * @return how many entries, with @p out; 0 for a document without one,
 *         -1 out of memory.
 * @note The caller gives @p out back with tiku_pdf_outline_free().
 */
int tiku_pdf_outline(tiku_pdf_t *doc, tiku_pdf_outline_t **out);
/** @brief The @p n entries of @p items given back, their titles and
 *         addresses with them. */
void tiku_pdf_outline_free(tiku_pdf_outline_t *items, int n);

/** @brief A link on a page: its area on the page as drawn at scale 1. */
typedef struct {
    float           x0, y0, x1, y1;
    tiku_pdf_dest_t to;
} tiku_pdf_link_t;

/**
 * @brief The links on @p page that lead somewhere: a place in the
 *        document, by name or directly, or an address outside it.
 * @return how many, with @p out; -1 when there is no such page or memory
 *         is short.
 * @note The caller gives @p out back with tiku_pdf_links_free().
 */
int tiku_pdf_links(tiku_pdf_t *doc, int page, tiku_pdf_link_t **out);
/** @brief The @p n links of @p links given back, their addresses with
 *         them. */
void tiku_pdf_links_free(tiku_pdf_link_t *links, int n);

/** @brief How a signature stands: the values of tiku_cms.h's
 *         TIKU_SIG_*, which the checker's verdict is copied from. */
enum { TIKU_PDF_SIG_GOOD = 1,       /* what its certificate's key signed  */
       TIKU_PDF_SIG_CHANGED,        /* the bytes it covers were changed   */
       TIKU_PDF_SIG_BROKEN,         /* not its certificate's key's        */
       TIKU_PDF_SIG_UNCHECKED };    /* in a form this does not check      */

/** @brief What a signature is for. */
enum { TIKU_PDF_SIG_APPROVAL, TIKU_PDF_SIG_CERTIFY, TIKU_PDF_SIG_TIMESTAMP };

/** @brief One signature on the document, as read and checked. */
typedef struct {
    char    *field;                 /* its field's full name, UTF-8       */
    char    *signer;                /* whom its certificate names         */
    char    *org, *issuer;          /* the signer's organisation, and whom
                                       the certificate is from; or NULL   */
    char    *reason, *location, *contact;  /* as the signer gave them    */
    char    *stamp_by;              /* a timestamp's authority, or NULL   */
    char     why[96];               /* what is wrong, "" for nothing      */
    int64_t  signed_at;             /* the signer's own claim: seconds
                                       since 1970, UTC; 0 for none        */
    int64_t  stamped_at;            /* a timestamp's time, or 0           */
    int      status;                /* TIKU_PDF_SIG_GOOD ...              */
    int      stamp;                 /* the timestamp's own status, or 0   */
    int      kind;                  /* TIKU_PDF_SIG_APPROVAL ...          */
    int      whole;                 /* nothing was added to the file after */
    int      self_signed;           /* its certificate vouches for itself */
    int      page;                  /* the page its field is on, or -1    */
    float    x0, y0, x1, y1;        /* its box there, as drawn at scale 1;
                                       empty for an invisible one         */
} tiku_pdf_signature_t;

/**
 * @brief The document's signatures in the order they were made, each
 *        checked: whether the bytes it covers are what was signed, by the
 *        key of the certificate it carries, and whether anything was
 *        added after.  The certificate is not checked against any
 *        trusted root.
 * @return how many, with @p out; 0 for none.
 * @note The caller gives @p out back with tiku_pdf_signatures_free().
 */
int tiku_pdf_signatures(tiku_pdf_t *doc, tiku_pdf_signature_t **out);
/** @brief The @p n signatures of @p sigs given back, their strings with
 *         them. */
void tiku_pdf_signatures_free(tiku_pdf_signature_t *sigs, int n);

/** @brief A page drawn: 0xAARRGGBB pixels, top row first, opaque. */
typedef struct {
    int       w, h;
    uint32_t *px;                   /* w*h; tiku_pdf_pixmap_free() frees  */
} tiku_pdf_pixmap_t;

/**
 * @brief Draw @p page at @p scale pixels per point, its own rotation
 *        applied and then @p rotate degrees clockwise more: the view's
 *        turn, a multiple of 90 (-90 is 270).
 *
 * Content this renderer does not draw is left out; the rest of the page
 * is drawn.
 * @return 0 with @p out filled, or -1 when there is no such page, no
 *         room for it, or @p rotate is not a multiple of 90.
 */
int tiku_pdf_render(tiku_pdf_t *doc, int page, double scale, int rotate,
                    tiku_pdf_pixmap_t *out);

/**
 * @brief Draw the window @p x0, @p y0, @p w by @p h pixels of the page as
 *        tiku_pdf_render() lays it out at @p scale and @p rotate.
 *
 * Only the window's pixels are held, so a page can be drawn a band of
 * rows at a time.
 * @return 0 with @p out filled, or -1 as tiku_pdf_render().
 */
int tiku_pdf_render_rect(tiku_pdf_t *doc, int page, double scale,
                         int rotate, int x0, int y0, int w, int h,
                         tiku_pdf_pixmap_t *out);

/** @brief @p page drawn as tiku_pdf_render() draws it unturned, on paper
 *         of colour @p paper (0xAARRGGBB) in place of white: drawn on
 *         white and on black, the two give each pixel's coverage.
 *  @return 0 with @p out filled, or -1 as tiku_pdf_render(). */
int tiku_pdf_render_paper(tiku_pdf_t *doc, int page, double scale,
                          uint32_t paper, tiku_pdf_pixmap_t *out);

/** @brief The size tiku_pdf_render() draws @p page at, at @p scale and
 *         @p rotate.  @return 0, or -1 for no such page, a @p scale not
 *         above 0, or a @p rotate not a multiple of 90. */
int tiku_pdf_render_size(tiku_pdf_t *doc, int page, double scale,
                         int rotate, int *w, int *h);

/**
 * @brief Where the point @p x, @p y of a page as drawn at scale 1 -- a
 *        text box's or a link's corner, @p w by @p h points in all --
 *        lands when the view turns it @p rotate degrees clockwise more;
 *        with @p back, the other way, from the turned page to its own.
 */
void tiku_pdf_turn_point(double w, double h, int rotate, int back,
                         double *x, double *y);

/** @brief Give a drawn page's pixels back. */
void tiku_pdf_pixmap_free(tiku_pdf_pixmap_t *p);

/**
 * @brief Where to find faces to stand in for fonts the file does not
 *        embed: a folder of TrueType files, such as the desktop's fonts
 *        folder.
 *
 * When it gives no face, faces at fixed macOS and Linux paths are tried;
 * without any, such text is not drawn.
 */
void tiku_pdf_fonts(tiku_pdf_t *doc, const char *dir);

/**
 * @brief Where to find CMaps a file names without carrying (Chinese,
 *        Japanese and Korean encodings): a folder laid out as
 *        poppler-data's, cMap/<collection>/<name> and cidToUnicode.
 *
 * NULL looks in poppler-data's folders under /usr/share, /usr/local/share
 * and /opt/homebrew/share, "" nowhere; without one found, such text is
 * read by its code space alone.
 */
void tiku_pdf_cmaps(tiku_pdf_t *doc, const char *dir);

#endif /* TIKU_PDF_H_ */
