/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_private.h - what the PDF kit's files share: the object
 * model, the document, and the seams between lexer, cross-reference,
 * filters, pages, text, and the drawing side: raster, colour, glyphs,
 * images and the interpreter that runs a page through them.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_PRIVATE_H_
#define TIKU_PDF_PRIVATE_H_

#include <stddef.h>
#include <stdint.h>

#include "tiku_pdf.h"
#include "tiku_cms.h"
#include "tiku_cff.h"
#include "tiku_ttf.h"
#include "tiku_type1.h"
#include "tiku_vector_raster.h"

/* The file as a whole, and any one decoded stream. */
#define PDF_FILE_MAX    (256L * 1024L * 1024L)
#define PDF_STREAM_MAX  (64L * 1024L * 1024L)
#define PDF_NEST_MAX    64          /* arrays in dicts in arrays ...     */
#define PDF_OBJECTS_MAX 4000000     /* a /Size past this is a lie        */
#define PDF_PAGES_MAX   20000
#define PDF_SUBST_MAX   12          /* stand-in faces one document opens */

/** @brief What an object is: one of PDF's kinds, or an operator. */
typedef enum {
    PDF_NULL, PDF_BOOL, PDF_INT, PDF_REAL, PDF_STRING, PDF_NAME,
    PDF_ARRAY, PDF_DICT, PDF_REF, PDF_STREAM, PDF_OP
} pdf_kind_t;

typedef struct pdf_obj pdf_obj_t;

/**
 * @brief One object, of any kind.
 *
 * Strings and names own their bytes; a name is kept without its slash
 * and with #xx pairs resolved.  A stream is its dictionary and where its
 * raw bytes lie in the file (a stream is never inside an object stream).
 * An operator (PDF_OP) only occurs while reading a content stream.
 */
struct pdf_obj {
    pdf_kind_t kind;                /**< which member of u is live       */
    union {
        int    b;                   /**< PDF_BOOL                        */
        long   i;                   /**< PDF_INT                         */
        double r;                   /**< PDF_REAL                        */
        struct { unsigned char *p; size_t n; } s;       /**< string, name, op */
        struct { pdf_obj_t **item; int n; } a;
        struct { char **key; pdf_obj_t **val; int n; } d;
        struct { long num, gen; } ref;
        struct { pdf_obj_t *dict; long off, len, num, gen; } st;
                                    /* len < 0 unknown; num < 0 unnumbered */
    } u;
};

/** @brief Where an indirect object lives, from the cross-reference. */
typedef struct {
    long off;                       /**< file offset, or the object stream */
    long idx;                       /**< index within that stream        */
    unsigned char kind;             /**< 0 free, 1 in file, 2 in a stream */
    unsigned char set;              /**< an entry has been read for it   */
    unsigned char seen;             /**< set while parsing: a cycle guard */
} pdf_xref_t;

/** @brief One object stream, decoded once and kept while it is used. */
typedef struct {
    long           num;             /**< the stream's object number      */
    unsigned char *data;            /**< decoded, or NULL for a free slot */
    long           len, first, count;   /**< bytes; /First; /N          */
} pdf_objstm_t;

/** @brief Object streams a document keeps decoded at once. */
#define PDF_OBJSTM_KEEP 8

typedef struct pdf_crypt pdf_crypt_t;

/** @brief A page's dictionary and its number, sorted by address. */
typedef struct {
    pdf_obj_t *dict;                /**< the page's dictionary, cached   */
    int        index;               /**< its number, from 0              */
} pdf_pageref_t;

/** @brief An open document: its bytes, its table and objects, and what
 *         reading and drawing it keep. */
struct tiku_pdf {
    unsigned char *data;            /**< the whole file, NUL-terminated  */
    long           len;
    pdf_xref_t    *xref;            /**< one entry per object number     */
    long           nxref;
    pdf_obj_t    **cache;           /**< parsed indirect objects, by number */
    pdf_obj_t     *trailer;         /**< the newest, with Root and Info  */
    pdf_obj_t     *old_trailer;     /**< the one before a repair, kept   */
    pdf_obj_t    **grave;           /**< the cache before a repair, kept */
    long           ngrave;
    pdf_objstm_t   objstm[PDF_OBJSTM_KEEP];  /**< decoded object streams */
    int            objstm_next;     /**< the slot the next one takes     */
    pdf_obj_t    **page;            /**< each page's dictionary, cached  */
    int            npage;
    pdf_pageref_t *pageref;         /**< the same, by object; made on use */
    int            repaired;        /**< the table was rebuilt by scanning */
    int            encrypted;
    pdf_crypt_t   *crypt;           /**< its key, when it is encrypted   */
    int            depth;           /**< resolve() recursion, a cycle guard */
    int            nested;          /**< filters reading a parameter's
                                       stream, one within another        */
    char           fontdir[512];    /**< faces to stand in for fonts   */
    tiku_ttf_t    *subst[PDF_SUBST_MAX];    /**< the stand-ins opened  */
    char           subst_path[PDF_SUBST_MAX][512];  /**< each one's file */
    int            nsubst;
    char           subst_pick[6][512];  /**< sans, serif, fixed; x bold */
    unsigned char  subst_chosen[6]; /**< that pick was made ("" is none) */
    char           cjk_pick[8][512];    /**< CJK stand-ins, by collection */
    unsigned char  cjk_chosen[8];
    char           cmapdir[512];    /**< CMaps the file names ("": none) */
    int            cmapdir_set;     /**< it replaces poppler-data's     */
    uint32_t      *cid_uni[6];      /**< CID -> Unicode, by collection  */
    unsigned char  cid_uni_tried[6];
    struct pdf_fontkeep *fonts;     /**< fonts read, kept between pages  */
    int            nfonts, cfonts;
    unsigned long  epoch;           /**< pages read or drawn so far      */
    pdf_obj_t    **oc_off;          /**< optional content groups set off */
    int            noc_off;
    int            oc_read;         /**< the configuration has been read */
    pdf_obj_t     *annot_res;       /**< what drawn annotations' text uses */
};

/* The lexer and the objects it makes: tiku_pdf_lex.c                        */

/** @brief A cursor over bytes; the lexer and parser read through one. */
typedef struct {
    const unsigned char *p;         /**< the bytes                       */
    long                 n, pos;    /**< how many, and where the reader is */
} pdf_cur_t;

/** @brief A new object of @p kind, empty; NULL when memory is short. */
pdf_obj_t *pdf_new(pdf_kind_t kind);
/** @brief Free @p o and everything it holds; a stream's dictionary too. */
void pdf_free(pdf_obj_t *o);
/**
 * @brief One object at the cursor, or NULL at the end or on a failure.
 *
 * A keyword the syntax has no place for (an operator in a content
 * stream, "endobj", "stream") comes back as PDF_OP for the caller to
 * judge.  The cursor always moves past what was read.
 */
pdf_obj_t *pdf_parse(pdf_cur_t *c, int depth);
/** @brief Skip white space and comments.  @return 0 at the end. */
int pdf_skip_ws(pdf_cur_t *c);
/** @brief Whether @p word stands whole at the cursor. */
int pdf_keyword_at(const pdf_cur_t *c, const char *word);
/** @brief Set @p key of @p d to @p val, which @p d now owns; -1 when
 *         full or memory is short. */
int pdf_dict_set(pdf_obj_t *d, const char *key, pdf_obj_t *val);
/** @brief The value under @p key, unresolved; NULL when absent. */
pdf_obj_t *pdf_dict_raw(const pdf_obj_t *d, const char *key);
/** @brief Append @p item to @p a, which owns it from here; -1 when full
 *         or memory is short. */
int pdf_array_push(pdf_obj_t *a, pdf_obj_t *item);
/** @brief Whether @p o is the name @p name. */
int pdf_is_name(const pdf_obj_t *o, const char *name);
/** @brief @p o as a number, or @p dflt when it is not one. */
double pdf_number(const pdf_obj_t *o, double dflt);
/** @brief @p o as an int, or @p dflt when it is not a number or is one
 *         no int holds: a count or a size that large is damage. */
int pdf_int(const pdf_obj_t *o, int dflt);
/** @brief The next @p word at or after @p from, or -1. */
long pdf_find(const unsigned char *p, long n, long from, const char *word);

/* The document, its table and its objects: tiku_pdf_doc.c                  */

/** @brief @p o with references followed; NULL for one that goes nowhere. */
pdf_obj_t *pdf_resolve(tiku_pdf_t *doc, pdf_obj_t *o);
/** @brief A text string (a title, an outline entry) as UTF-8: UTF-16 or
 *        UTF-8 with its mark, else PDFDocEncoding.
 *  @note The caller frees it. */
char *pdf_text_string(const pdf_obj_t *s);
/** @brief UTF-8 @p in in PDFDocEncoding, as a password of revisions 2 to
 *         4 is hashed, into @p out (@p cap bytes, no NUL).  @return its
 *         length, or -1 for a character it has no byte for or no room. */
long pdf_docenc(const char *in, unsigned char *out, size_t cap);
/** @brief The value under @p key, resolved; NULL when absent. */
pdf_obj_t *pdf_get(tiku_pdf_t *doc, pdf_obj_t *dict, const char *key);
/**
 * @brief Indirect object @p num, parsed once and kept by the document.
 *
 * A table that points at something else is rebuilt from the file once,
 * and the object asked for again.
 */
pdf_obj_t *pdf_object(tiku_pdf_t *doc, long num);
/** @brief The catalogue, or NULL. */
pdf_obj_t *pdf_root(tiku_pdf_t *doc);
/**
 * @brief The raw bytes of @p stream in the file.
 *
 * Length may be indirect and may lie; when "endstream" is not where it
 * says, the keyword is searched for from the start of the data and the
 * bytes before it are the stream.
 */
int pdf_stream_raw(tiku_pdf_t *doc, pdf_obj_t *stream,
                   const unsigned char **p, long *n);

/* The filters: tiku_pdf_filter.c                                            */

/**
 * @brief The decoded bytes of @p stream, through every standard filter.
 *
 * DCT and JPX end the chain: what is left is returned as it stands and
 * @p image names the filter; NULL when the chain ended in bytes.  CCITT
 * fax and JBIG2 are decoded here, into rows of bits.
 *
 * @note The caller frees @p out.
 */
int pdf_stream_data(tiku_pdf_t *doc, pdf_obj_t *stream, unsigned char **out,
                    long *n, const char **image);
/** @brief The same for @p raw bytes that @p dict describes (an inline
 *        image: its filters may go by their short names). */
int pdf_filter_bytes(tiku_pdf_t *doc, pdf_obj_t *dict,
                     const unsigned char *raw, long rawn, unsigned char **out,
                     long *n, const char **image);

/* Encryption: tiku_pdf_crypt.c                                             */

/**
 * @brief The security handler for @p doc: the standard one with
 *        @p password (UTF-8; "" for the empty password), or Adobe.PubSec
 *        with @p key (NULL for none).
 * @return 0 with doc->crypt set; -1 for a handler or cipher this does
 *         not read; -2 when the password opens it neither as its user nor
 *         as its owner; -3 when @p key is none it was sealed to.
 */
int pdf_crypt_open(tiku_pdf_t *doc, const char *password,
                   const tiku_key_t *key);
/** @brief Whether @p doc is sealed to certificates (Adobe.PubSec). */
int pdf_crypt_certificate(tiku_pdf_t *doc);
/** @brief The handler given back; @p c may be NULL. */
void pdf_crypt_free(pdf_crypt_t *c);
/** @brief Whether the author lets text be copied out (P bit 5), or the
 *         owner's password opened it; 1 for a document not encrypted. */
int pdf_crypt_may_copy(const pdf_crypt_t *c);
/** @brief The strings of object @p num @p gen, as read, decrypted. */
void pdf_crypt_object(tiku_pdf_t *doc, pdf_obj_t *o, long num, long gen);
/** @brief Stream @p st's @p n raw bytes decrypted in place.  @return how
 *         many remain: AES drops its IV and padding. */
long pdf_crypt_stream(tiku_pdf_t *doc, pdf_obj_t *st, unsigned char *data,
                      long n);

/* The pages: tiku_pdf_page.c                                                */

/** @brief Walk the page tree into the document's page list. */
int pdf_pages_build(tiku_pdf_t *doc);
/** @brief The dictionary of @p page (from 0), or NULL. */
pdf_obj_t *pdf_page_dict(tiku_pdf_t *doc, int page);
/** @brief @p key on @p page or the nearest ancestor that has it. */
pdf_obj_t *pdf_page_inherited(tiku_pdf_t *doc, pdf_obj_t *page,
                              const char *key);
/** @brief The page's content streams decoded and joined.
 *  @note The caller frees @p out. */
int pdf_page_content(tiku_pdf_t *doc, pdf_obj_t *page, unsigned char **out,
                     long *n);
/** @brief Which page @p page (a reference, dictionary or number) is, or
 *        -1 for none of this document's. */
int pdf_page_index(tiku_pdf_t *doc, pdf_obj_t *page);

/* Composite fonts' CMaps: tiku_pdf_cmap.c                                  */

typedef struct pdf_cmap pdf_cmap_t;

/**
 * @brief The CMap a Type 0 font's /Encoding names or carries.
 *
 * Identity and the Unicode schemes are known without data; any other
 * name is read from the CMap folders, or is its code space alone and
 * knows no CIDs.  NULL only when memory is short.
 */
pdf_cmap_t *pdf_cmap_load(tiku_pdf_t *doc, pdf_obj_t *enc);
/** @brief The map given back; @p m may be NULL. */
void pdf_cmap_free(pdf_cmap_t *m);
/** @brief The code at @p s, @p n bytes left, by the map's code space.
 *  @return its bytes (1 to 4), or 0 when the string cuts it short. */
int pdf_cmap_next(const pdf_cmap_t *m, const unsigned char *s, size_t n,
                  uint32_t *code);
/** @brief The bytes of code value @p code: the fewest that hold it. */
int pdf_cmap_len(const pdf_cmap_t *m, uint32_t code);
/** @brief The CID @p code (@p len bytes) selects; -1 when the map knows
 *         none, as a Unicode scheme read without its file. */
long pdf_cmap_cid(const pdf_cmap_t *m, uint32_t code, int len);
/** @brief Whether the map's codes are Unicode (UCS-2, UTF-16, -32, -8). */
int pdf_cmap_unicode_codes(const pdf_cmap_t *m);
/** @brief The character @p code is when they are; 0 otherwise. */
uint32_t pdf_cmap_unicode(const pdf_cmap_t *m, uint32_t code, int len);
/** @brief Whether text in the map is written down the page (WMode 1). */
int pdf_cmap_vertical(const pdf_cmap_t *m);
/** @brief CID to character for @p collection ("Adobe-Japan1"): 65536
 *         entries the document keeps, or NULL when it has none. */
const uint32_t *pdf_cid_unicode(tiku_pdf_t *doc, const char *collection);
/** @brief The document's CID tables given back. */
void pdf_cid_unicode_free(tiku_pdf_t *doc);

/* Fonts, as far as their text goes: tiku_pdf_font.c                        */

/** @brief A run of CIDs written down the page with the same metrics. */
typedef struct {
    long   first, last;
    double w1, vx, vy;              /**< advance, and the vertical origin */
} pdf_vrun_t;

/**
 * @brief A font as text extraction needs it: codes to text and widths.
 *
 * A glyph is named by its code in a simple font, and in a composite one
 * by its CID -- or by its character, when the CMap's codes are Unicode
 * and its CIDs are not known (by_unicode).  Widths go by that name.
 */
typedef struct {
    int       two_byte;             /**< composite (Type 0): read by CMap */
    int       type3;
    double    missing;              /**< width of a code without one     */
    double    matrix_x;             /**< unused: pdf_tfont_load() keeps the
                                       Type 3 FontMatrix scale itself    */
    uint32_t *uni;                  /**< code -> Unicode, 0 for none     */
    uint32_t *uni2;                 /**< a second character (a ligature) */
    double   *width;                /**< glyph -> advance in text space, -1 */
    long      codes;                /**< 256 or 65536 entries            */
    pdf_cmap_t *cmap;               /**< composite: codes to CIDs        */
    const uint32_t *cid_uni;        /**< CID -> Unicode, its collection's */
    int       by_unicode;           /**< glyphs named by character       */
    int       vertical;             /**< written down the page           */
    double    dw2_vy, dw2_w1;       /**< DW2: the origin and advance usual */
    pdf_vrun_t *w2;                 /**< W2, sorted by CID               */
    long      nw2;
} pdf_tfont_t;

/** @brief One glyph of a shown string, as reading and drawing need it. */
typedef struct {
    uint32_t code;                  /**< as the string holds it          */
    int      len, word;             /**< its bytes; a one-byte space (Tw) */
    long     key;                   /**< the glyph: code, CID or character */
    double   w;                     /**< its advance across, in ems      */
    double   w1, vx, vy;            /**< down the page: advance and origin */
    uint32_t uni, uni2;             /**< its characters, 0 for none known */
} pdf_shown_t;

/** @brief The glyph at @p s, @p n bytes left, in @p f (NULL: a byte,
 *         half an em).  @return the bytes it took; 0 at the end. */
int pdf_tfont_next(const pdf_tfont_t *f, const unsigned char *s, size_t n,
                   pdf_shown_t *g);
/** @brief A composite font's glyphs to characters, for a stand-in that
 *         draws them by character: 65536 of them, or NULL.
 *  @note The caller frees the table. */
uint32_t *pdf_tfont_glyph_unicode(const pdf_tfont_t *f);

/** @brief Read @p font's mapping and widths; NULL when memory is short. */
pdf_tfont_t *pdf_tfont_load(tiku_pdf_t *doc, pdf_obj_t *font);
/** @brief Free what pdf_tfont_load() gave. */
void pdf_tfont_free(pdf_tfont_t *f);
/** @brief The character a glyph name stands for, or 0 for none known. */
uint32_t pdf_glyph_unicode(const char *name);

/* The text of a page: tiku_pdf_text.c holds tiku_pdf_page_text()          */

/* Drawing: tiku_vector_raster.c fills and strokes, tiku_pdf_raster.c        */
/* paints, and tiku_pdf_render.c runs a page through them                    */

/** @brief A matrix as PDF writes one: [a b c d e f], row vectors. */
typedef tiku_vector_mat_t pdf_mat_t;

/** @brief @p m then @p n, as "cm" composes them. */
pdf_mat_t pdf_mat_mul(pdf_mat_t m, pdf_mat_t n);
/** @brief The matrix [@p a @p b @p c @p d @p e @p f]. */
pdf_mat_t pdf_mat(double a, double b, double c, double d, double e,
                  double f);
/** @brief The point (x, y) through @p m. */
void pdf_mat_apply(const pdf_mat_t *m, double x, double y, double *ox,
                   double *oy);
/** @brief How much @p m scales a length, on average. */
double pdf_mat_scale(const pdf_mat_t *m);
/**
 * @brief The matrix laying @p page out at @p scale, its rotation and
 *        @p turn degrees clockwise more applied and y downwards, with the
 *        window's corner (@p x0, @p y0) at (0, 0); and the whole page's
 *        size in pixels.  Text boxes and links use it at scale 1 and no
 *        turn: the page as drawn, in points.
 * @return 0, or -1 for no such page or a @p turn not a multiple of 90.
 */
int pdf_page_matrix(tiku_pdf_t *doc, int page, double scale, int turn,
                    int x0, int y0, pdf_mat_t *m, int *w, int *h);

/** @brief A page's pixels, 0xAARRGGBB, how paints go onto them now; and
 *         a coverage mask over them. */
typedef struct {
    int       w, h;                 /**< the pixels across and down      */
    uint32_t *px;                   /**< w * h of them                   */
    int       blend;                /**< the blend mode paints use       */
    int       knockout;             /**< each paint on the ground instead,
                                       in place of what was put before   */
    uint32_t  ground;               /**< a knockout group's backdrop     */
} pdf_pix_t;
/** @brief Coverage over a box of device pixels, as the rasteriser makes
 *         it. */
typedef tiku_vector_mask_t pdf_mask_t;

/**
 * @brief A path flattened to straight pieces in device space.
 *
 * Subpaths are runs of points; a closed one is marked, and an open one
 * is closed when it is filled, as PDF fills it.
 */
typedef tiku_vector_path_t pdf_path_t;

/** @brief The points one path holds; one more fails it, and the path is
 *         then neither filled nor stroked. */
#define PDF_POINTS_MAX  (1 << 21)
/** @brief The dash entries a stroke reads; any past these are not read. */
#define PDF_DASH_MAX    16

/** @brief @p p made empty, to hold up to PDF_POINTS_MAX points. */
void pdf_path_init(pdf_path_t *p);
/** @brief @p p emptied for the next path, its memory kept. */
void pdf_path_clear(pdf_path_t *p);
/** @brief @p p's memory given back, @p p left as pdf_path_init() leaves
 *         it. */
void pdf_path_free(pdf_path_t *p);
/** @brief A new subpath at device point (@p x, @p y). */
void pdf_path_moveto(pdf_path_t *p, double x, double y);
/** @brief A straight piece to device point (@p x, @p y). */
void pdf_path_lineto(pdf_path_t *p, double x, double y);
/** @brief A cubic in device space, flattened as its size needs. */
void pdf_path_curveto(pdf_path_t *p, double x1, double y1, double x2,
                      double y2, double x3, double y3);
/** @brief Shut the subpath: marked closed, the current point back where
 *         it began. */
void pdf_path_close(pdf_path_t *p);
/** @brief The path's extent in whole pixels, clipped to @p w by @p h. */
int pdf_path_bounds(const pdf_path_t *p, int w, int h, int *x0, int *y0,
                    int *bw, int *bh);

/**
 * @brief Coverage of @p p within the box, by @p evenodd or nonzero.
 * @return the mask; one with no bytes when nothing is covered, the path
 *         failed, or memory is short.
 * @note The caller frees the mask with pdf_mask_free().
 */
pdf_mask_t pdf_path_fill(const pdf_path_t *p, int evenodd, int x0, int y0,
                         int w, int h);
/**
 * @brief The outline of stroking @p p, as a path to fill nonzero.
 *
 * Whatever the line state holds is drawn: a width, dash entry or miter
 * limit below zero or not a number counts as zero, an infinite one as the
 * largest a double holds, and a phase that is not finite as zero.  So
 * "-1 w" draws the thinnest line and "[3 -1] 0 d" a nearly solid one.
 */
void pdf_path_stroke(const pdf_path_t *p, double width, int cap, int join,
                     double miter, const double *dash, int ndash,
                     double phase, pdf_path_t *out);
/** @brief Give a mask's bytes back; one with none is fine. */
void pdf_mask_free(pdf_mask_t *m);
/** @brief @p m narrowed to what @p clip also covers (clip may be NULL). */
void pdf_mask_intersect(pdf_mask_t *m, const pdf_mask_t *clip);
/** @brief A mask over the whole of @p w by @p h, all covered. */
pdf_mask_t pdf_mask_full(int w, int h);
/** @brief Paint @p colour through @p m (and @p clip) at @p alpha. */
void pdf_paint(pdf_pix_t *pix, const pdf_mask_t *m, const pdf_mask_t *clip,
               uint32_t colour, double alpha);
/** @brief The blend modes (ISO 32000 11.3.5), Normal first. */
enum { PDF_BM_NORMAL, PDF_BM_MULTIPLY, PDF_BM_SCREEN, PDF_BM_OVERLAY,
       PDF_BM_DARKEN, PDF_BM_LIGHTEN, PDF_BM_DODGE, PDF_BM_BURN,
       PDF_BM_HARDLIGHT, PDF_BM_SOFTLIGHT, PDF_BM_DIFFERENCE,
       PDF_BM_EXCLUSION, PDF_BM_HUE, PDF_BM_SATURATION, PDF_BM_COLOR,
       PDF_BM_LUMINOSITY };
/** @brief The mode named @p name; Normal for a name that is not one. */
int pdf_blend_mode(const char *name);
/**
 * @brief @p colour laid over pixel @p d of @p pix with coverage @p a, 0
 *        to 255, through the pixmap's blend mode.  In a knockout group
 *        (ISO 32000 11.4.8) it is laid on the group's ground and put in
 *        place of what was there as far as @p shape, the paint's coverage
 *        before its opacity.
 */
void pdf_blend(const pdf_pix_t *pix, uint32_t *d, uint32_t colour, int a,
               int shape);

/* Shadings: tiku_pdf_shade.c                                                */

typedef struct pdf_shade pdf_shade_t;
/** @brief Shading @p dict, its colour space named in @p res: types 1 to 3
 *        drawn per pixel, the meshes (4 to 7) read into triangles; a mesh
 *        that cannot be read paints its Background or one tone. */
pdf_shade_t *pdf_shade_load(tiku_pdf_t *doc, pdf_obj_t *dict, pdf_obj_t *res);
/** @brief The shading given back; @p s may be NULL. */
void pdf_shade_free(pdf_shade_t *s);
/**
 * @brief Paint @p s, its space taken to the device by @p to, through
 *        coverage @p m, or over all @p clip leaves when @p m is NULL.
 *
 * With @p background its Background fills what the shading leaves out,
 * as a pattern's fill wants and sh does not.
 */
void pdf_shade_paint(pdf_pix_t *pix, const pdf_shade_t *s,
                     const pdf_mat_t *to, const pdf_mask_t *m,
                     const pdf_mask_t *clip, double alpha, int background);

/* Glyphs: tiku_pdf_glyph.c                                                  */

/** @brief The program a drawn font has: none, TrueType or OpenType,
 *         bare CFF, Type 1, or Type 3 glyph procedures. */
enum { GF_NONE, GF_TTF, GF_CFF, GF_T1, GF_T3 };
/** @brief A font as drawing needs it: its program, and how a code
 *        reaches a glyph in it. */
typedef struct {
    int            kind;            /**< GF_TTF, GF_CFF, GF_T1, GF_T3     */
    tiku_ttf_t    *ttf;             /**< TrueType, OpenType, or a stand-in */
    tiku_cff_t    *cff;             /**< a bare CFF (FontFile3)           */
    tiku_t1_t     *t1;              /**< Type 1 (FontFile)                */
    unsigned char *blob;            /**< the program's bytes the CFF borrows */
    long           bloblen;
    int            two_byte, symbolic, substitute, owns_ttf;
                                    /* composite; symbolic; ttf a stand-in;
                                       ttf freed with the font           */
    int            by_unicode;      /**< composite glyphs are characters  */
    uint32_t      *own_uni;         /**< a stand-in's glyph to character  */
    char          *names[256];      /**< a simple font's code to glyph name */
    int            names_from_pdf;  /**< the PDF gave an Encoding         */
    const uint32_t *uni;            /**< code to character, borrowed      */
    const double  *width;           /**< code to advance, borrowed        */
    unsigned short *cid2gid;        /**< a CID font's map, NULL: identity */
    long           ncid2gid;
    double         upem;            /**< the program's units per em       */
    pdf_obj_t     *charprocs, *t3res;   /**< Type 3: its glyph programs  */
    pdf_mat_t      fontmatrix;      /**< Type 3: glyph space to text space */
} pdf_gfont_t;

/** @brief Font @p dict as drawn, borrowing its text view @p tf. */
pdf_gfont_t *pdf_gfont_load(tiku_pdf_t *doc, pdf_obj_t *dict,
                            const pdf_tfont_t *tf);
/** @brief Given back; the borrowed text view is left alone. */
void pdf_gfont_free(pdf_gfont_t *g);
/**
 * @brief Widths for a simple font with no /Widths and no embedded
 *        program, such as one of the standard 14: the stand-in face's
 *        advances.
 * @return 1 when any were set into @p width, by code through @p uni.
 */
int pdf_standin_widths(tiku_pdf_t *doc, pdf_obj_t *font, const uint32_t *uni,
                       double *width);
/** @brief The same for a composite font whose glyphs go by character:
 *         the stand-in's advance for each of the first 65536. */
int pdf_standin_char_widths(tiku_pdf_t *doc, pdf_obj_t *font, double *width);
/**
 * @brief Coverage of glyph @p code drawn through @p m, which takes text
 *        space (one unit the em) to the device.
 * @return 1 with @p out, 0 for no ink.
 * @note The caller frees @p out with pdf_mask_free().
 */
int pdf_gfont_mask(pdf_gfont_t *g, long code, const pdf_mat_t *m,
                   pdf_mask_t *out);

/* Optional content: tiku_pdf_oc.c                                          */

/** @brief Whether @p oc -- a group, a membership dictionary, or NULL for
 *         content that is no layer's -- shows. */
int pdf_oc_visible(tiku_pdf_t *doc, pdf_obj_t *oc);
/** @brief The property list a BDC's @p operand names in @p res, or is. */
pdf_obj_t *pdf_oc_props(tiku_pdf_t *doc, pdf_obj_t *operand, pdf_obj_t *res);

/* Annotations drawn without an appearance: tiku_pdf_annot.c               */

/** @brief Content in page space that draws annotation @p a, which
 *         stores no appearance, from its kind, rectangle, colours, points
 *         or text; NULL for a kind with nothing to draw.
 *  @note The caller frees it. */
unsigned char *pdf_annot_content(tiku_pdf_t *doc, pdf_obj_t *a, long *n);
/** @brief The resources that content names: Helvetica, and the
 *         opacities, plain and multiplying, made once for the document. */
pdf_obj_t *pdf_annot_resources(tiku_pdf_t *doc);
/** @brief Whether widget @p a, a text or choice field's, is to be drawn
 *         afresh from its value, its appearance stale: the form's
 *         NeedAppearances. */
int pdf_annot_redraw(tiku_pdf_t *doc, pdf_obj_t *a);

/* Fonts kept for the document: tiku_pdf_glyph.c                             */

/** @brief A page is about to be read or drawn: of the fonts kept, those
 *         pages have left longest go, until 48 are left. */
void pdf_fonts_page(tiku_pdf_t *doc);
/** @brief Font @p dict's text view, read once for the document; NULL
 *         when it is no font. */
pdf_tfont_t *pdf_font_text(tiku_pdf_t *doc, pdf_obj_t *dict);
/** @brief Its drawn view likewise; NULL when it cannot be drawn. */
pdf_gfont_t *pdf_font_drawn(tiku_pdf_t *doc, pdf_obj_t *dict);
/** @brief The fonts kept, given back when the document closes. */
void pdf_fonts_free(tiku_pdf_t *doc);

/* Images: tiku_pdf_image.c                                                  */

/** @brief An image decoded to what drawing needs: colour per sample, an
 *        alpha per sample when it has one, or a stencil alone. */
typedef struct {
    int            w, h;
    unsigned char *rgb;             /**< w*h*3; NULL for a stencil      */
    unsigned char *alpha;           /**< w*h, or NULL when opaque       */
    unsigned interpolate;           /**< requested continuous image sampling */
    int            stencil;         /**< an ImageMask: alpha is the shape */
} pdf_img_t;

/**
 * @brief Decode the image @p dict describes: an XObject stream, or with
 *        @p raw an inline image's bytes.
 *
 * What cannot be decoded comes back as a grey box of the image's size; a
 * JPEG 2000 comes at the size it decodes to, which is smaller when it is
 * too big.  @return 0, or -1 when there is no image to draw.
 */
int pdf_image_decode(tiku_pdf_t *doc, pdf_obj_t *dict,
                     const unsigned char *raw, long rawn, pdf_obj_t *res,
                     pdf_img_t *out);
/** @brief Give a decoded image's samples back. */
void pdf_image_free(pdf_img_t *img);
/**
 * @brief Draw @p img over the unit square that @p ctm places, through
 *        @p clip at @p alpha; a stencil paints @p fill.
 */
void pdf_image_draw(pdf_pix_t *pix, const pdf_img_t *img,
                    const pdf_mat_t *ctm, const pdf_mask_t *clip,
                    uint32_t fill, double alpha);

/* Colour: tiku_pdf_color.c                                                  */

/** @brief A colour space's kind (pdf_cs_t.kind); DeviceN is a CS_SEP. */
enum { CS_GRAY = 1, CS_RGB, CS_CMYK, CS_INDEXED, CS_SEP, CS_LAB, CS_PATTERN };
/** @brief A function's inputs at most, and a colour's numbers. */
#define PDF_FUNC_MAX_IN  8
/** @brief A function's outputs at most. */
#define PDF_FUNC_MAX_OUT 8

typedef struct pdf_func pdf_func_t;
/** @brief A colour space: its kind, how many numbers a colour is, and
 *        for Indexed and Separation what those numbers go through. */
typedef struct pdf_cs {
    int            kind, n;
    struct pdf_cs *base;            /**< Indexed and Separation: the target */
    unsigned char *lookup;          /**< Indexed: hival+1 base colours   */
    int            hival;
    pdf_func_t    *tint;            /**< Separation: tint transform      */
    double         initial[PDF_FUNC_MAX_IN];    /**< set by cs/CS    */
} pdf_cs_t;

/** @brief The colour space @p spec names or defines; @p res answers a
 *        name of the page's own.  NULL for one this cannot use. */
pdf_cs_t *pdf_cs_load(tiku_pdf_t *doc, pdf_obj_t *spec, pdf_obj_t *res);
/** @brief The device space of @p n parts: CMYK for four, RGB for three,
 *         else Gray. */
pdf_cs_t *pdf_cs_device(int n);
/** @brief The space and those it is built on given back; @p cs may be
 *         NULL. */
void pdf_cs_free(pdf_cs_t *cs);
/** @brief The device colour for @p v, @p n numbers in @p cs. */
uint32_t pdf_cs_colour(const pdf_cs_t *cs, const double *v, int n);
/** @brief One sample of @p bpc bits at @p i, mapped to 0..255 per part. */
void pdf_cs_sample(const pdf_cs_t *cs, const unsigned char *s, int bpc,
                   long i, const double *decode, unsigned char *rgb);

/** @brief A PDF function (ISO 32000 7.10), of any of the four types,
 *        or an array of them side by side. */
pdf_func_t *pdf_func_load(tiku_pdf_t *doc, pdf_obj_t *spec);
/** @brief The function and its parts given back; @p f may be NULL. */
void pdf_func_free(pdf_func_t *f);
/** @brief @p f at @p in; @return how many outputs were written. */
int pdf_func_eval(const pdf_func_t *f, const double *in, int nin,
                  double *out);

#endif /* TIKU_PDF_PRIVATE_H_ */
