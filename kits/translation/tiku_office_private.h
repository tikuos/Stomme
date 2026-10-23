/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_private.h - what the office readers share: a package's
 * parts, the document a reader builds from them (sections of stories of
 * paragraphs and tables), the faces text is set in, a paragraph flowed
 * into lines, and the pages the lines are placed on before they are
 * written.  Lengths are points; colours 0xAARRGGBB, 0 for none.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_PRIVATE_H_
#define TIKU_OFFICE_PRIVATE_H_

#include "tiku_archive.h"
#include "tiku_fontset.h"
#include "tiku_office.h"
#include "tiku_pdf_writer.h"
#include "tiku_ttf.h"
#include "tiku_xml.h"

#include <stdint.h>

#define OF_XML_MAX   (64u << 20)    /* an XML part read, at most        */
#define OF_PART_MAX  (32u << 20)    /* a picture read, at most          */
#define OF_READ_MAX  (512u << 20)   /* every part of one file, together  */
#define OF_PAGES_MAX 5000           /* pages one document may lay out   */
#define OF_NEST_MAX  12             /* tables in cells, boxes in boxes  */

/** @brief One block of an arena's memory (tiku_office_util.c). */
typedef struct of_chunk of_chunk_t;
/** @brief Memory a document is built in, all given back at once. */
typedef struct {
    of_chunk_t *head;               /**< the newest chunk                */
    int         bad;                /**< an allocation failed            */
} of_arena_t;

/** @brief @p n bytes, zeroed.  NULL, and @c bad set, out of memory. */
void *of_alloc(of_arena_t *a, size_t n);
/** @brief @p s copied in; NULL for a NULL @p s or out of memory. */
char *of_strdup(of_arena_t *a, const char *s);
/** @brief The first @p n bytes of @p s copied in, NUL-ended; NULL out of
 *         memory. */
char *of_strndup(of_arena_t *a, const char *s, size_t n);
/** @brief Everything allocated, given back. */
void of_arena_free(of_arena_t *a);

/** @brief An array grown while it is built, then copied into an arena. */
typedef struct {
    void  *p;                       /**< the items                       */
    int    n, cap;                  /**< how many, and room              */
    size_t size;                    /**< one item's bytes                */
} of_vec_t;

/** @brief An empty array of items @p size bytes each. */
void of_vec_init(of_vec_t *v, size_t size);
/** @brief Room for one more, zeroed.  NULL out of memory. */
void *of_vec_push(of_vec_t *v);
/** @brief The items copied into @p a, their count in @p n; the array is
 *         emptied.  NULL, with @p n 0, when there are none or memory is
 *         short. */
void *of_vec_freeze(of_vec_t *v, of_arena_t *a, int *n);
/** @brief The array's memory given back. */
void of_vec_free(of_vec_t *v);

/** @brief One relationship of a part: an id, a type, and its target, a
 *         part's name made whole or an address outside. */
typedef struct {
    char *id, *type, *target;       /**< as read; the target resolved    */
    int   external;                 /**< the target is outside the package */
} of_rel_t;

/** @brief A document's package: its ZIP and the relationships read. */
typedef struct {
    tiku_archive_t *zip;            /**< the file                        */
    struct of_rels *rels;           /**< parts' relationships, each read
                                       when first asked for              */
    int             nrels, relcap;  /**< how many parts' sets, and room  */
    size_t          read;           /**< bytes read from every part, held
                                       under OF_READ_MAX                 */
    int             short_memory;   /**< 1 once a part or relationship was
                                       left unread for want of memory, so
                                       a missing one may be there        */
} of_pkg_t;

/** @brief What of_pkg_open() says when memory ran short. */
#define OF_NO_MEMORY "There is not enough memory to open it."

/** @brief @p path opened as a package.  NULL with why said. */
of_pkg_t *of_pkg_open(const char *path, char *why, size_t max);
/** @brief The package closed and its relationships given back; @p p
 *         may be NULL. */
void of_pkg_close(of_pkg_t *p);
/** @brief Whether the package holds part @p part ("word/document.xml"). */
int of_pkg_has(of_pkg_t *p, const char *part);
/** @brief Part @p part's bytes, at most @p max.  NULL when it is not
 *         there, is too large, or the ceiling is reached.
 *  @note The caller frees the bytes with free(). */
unsigned char *of_pkg_read(of_pkg_t *p, const char *part, size_t max,
                           size_t *len);
/** @brief Part @p part read as XML.  NULL if none.
 *  @note The caller frees the tree with tiku_xml_free(). */
tiku_xml_t *of_pkg_xml(of_pkg_t *p, const char *part);
/** @brief @p part's relationship @p id, or NULL. */
const of_rel_t *of_pkg_rel(of_pkg_t *p, const char *part, const char *id);
/** @brief @p part's first relationship whose type ends "/@p type". */
const of_rel_t *of_pkg_rel_type(of_pkg_t *p, const char *part,
                                const char *type);
/** @brief @p target, relative to part @p base's folder, as a part's
 *         whole name: "media/a.png" from word/document.xml is
 *         word/media/a.png.  Never above the package's root. */
void of_part_join(const char *base, const char *target, char *out,
                  size_t max);

/** @brief How a paragraph's lines, or a table, line up. */
enum { OF_LEFT, OF_CENTER, OF_RIGHT, OF_JUSTIFY };
/** @brief A paragraph's line spacing: a multiple, exactly, or at least. */
enum { OF_LINE_AUTO, OF_LINE_EXACT, OF_LINE_LEAST };
/** @brief The kinds of underline. */
enum { OF_UL_NONE, OF_UL_SINGLE, OF_UL_DOUBLE, OF_UL_THICK, OF_UL_DOTTED,
       OF_UL_DASHED, OF_UL_WAVE };
/** @brief How text lines up at a tab stop. */
enum { OF_TAB_LEFT, OF_TAB_CENTER, OF_TAB_RIGHT, OF_TAB_DECIMAL };
/** @brief How a border or a rule is drawn. */
enum { OF_LS_SINGLE = 1, OF_LS_DOUBLE, OF_LS_DOTTED, OF_LS_DASHED };

/** @brief How characters look, resolved; documents share identical ones
 *         (of_style()), so two runs look alike when their styles are
 *         the same pointer. */
typedef struct {
    const char   *font;             /**< the family, Latin               */
    const char   *font_ea;          /**< for East Asian text; NULL: font */
    float         size;             /**< points                          */
    uint32_t      color;            /**< 0: automatic, black             */
    uint32_t      back;             /**< highlight behind it; 0 none     */
    unsigned char bold, italic;     /**< 1 or 0                          */
    unsigned char caps, smallcaps;  /**< shown in capitals               */
    unsigned char hidden;           /**< not shown at all                */
    unsigned char underline;        /**< OF_UL_*                         */
    unsigned char strike;           /**< 1 single, 2 double              */
    signed char   vert;             /**< 1 superscript, -1 subscript     */
    float         spacing;          /**< added after each character      */
    float         rise;             /**< the baseline raised by          */
    float         scale;            /**< width, 1 as drawn               */
} of_style_t;

/** @brief A tab stop: where, how text lines up at it, what fills to it. */
typedef struct {
    float         pos;              /**< from the paragraph's origin     */
    unsigned char align;            /**< OF_TAB_*                        */
    unsigned char leader;           /**< 0, '.', '-', '_', or 0xb7 (middot) */
} of_tab_t;

/** @brief A border or a rule: none when @c width is 0. */
typedef struct {
    float         width;            /**< points                          */
    float         space;            /**< from what it borders            */
    uint32_t      color;            /**< 0: automatic, black             */
    unsigned char style;            /**< OF_LS_*                         */
} of_border_t;

/** @brief How a paragraph is laid out, resolved. */
typedef struct {
    unsigned char   align;          /**< OF_LEFT ...                     */
    float           left, right;    /**< indents                         */
    float           first;          /**< the first line's more; < 0 hangs */
    float           before, after;  /**< space above and below           */
    unsigned char   line_rule;      /**< OF_LINE_*                       */
    float           line;           /**< a multiple, or points           */
    unsigned char   keep_next;      /**< on the page of the one after    */
    unsigned char   keep_lines;     /**< its lines on one page           */
    unsigned char   break_before;   /**< begins a page                   */
    unsigned char   contextual;     /**< no space between two of its style */
    unsigned char   widows;         /**< no line left alone at a page end */
    signed char     outline;        /**< a heading's level, 0-8; -1 none */
    uint32_t        fill;           /**< shading behind it               */
    of_border_t     border[4];      /**< top, left, bottom, right        */
    const of_tab_t *tab;            /**< its stops, in order             */
    int             ntab;
    const char     *style;          /**< its style's name                */
} of_pstyle_t;

typedef struct of_image of_image_t;
typedef struct of_float of_float_t;
typedef struct of_story of_story_t;
struct of_faces;                    /* the faces text is set in, below   */
struct of_mnode;                    /* an equation's tree: tiku_office_math.h */
struct of_mitem;                    /* what draws one, once it is set    */

/** @brief An equation in the text: what it says, and -- once the faces
 *         are known, by of_math_prepare() -- how it is set. */
typedef struct of_math {
    const struct of_mnode *root;    /**< the tree its reader built       */
    unsigned char display;          /**< set apart, in display style     */
    unsigned char set;              /**< of_math_prepare() has set it    */
    float         size;             /**< its em, points                  */
    uint32_t      color;            /**< ARGB                            */
    float         w, asc, desc;     /**< set: its extent from its baseline */
    struct of_mitem *item;          /**< set: what draws it              */
    int           nitem;
    float        *pt;               /**< its paths' points: op, x, y     */
    int           npt;
    struct of_math *next;           /**< the document's next             */
} of_math_t;

/** @brief What a run is (of_run_t.kind). */
enum { OR_TEXT, OR_TAB, OR_BREAK, OR_PAGE, OR_COLUMN, OR_IMAGE, OR_FLOAT,
       OR_FIELD, OR_NOTE, OR_MARK, OR_MATH };

/* An OR_TAB run's n: 0 goes to the next stop; an absolute tab says where
 * it goes instead -- left, centre or right of the margins, or of the
 * indents -- and with what leader. */
enum { OF_PTAB_LEFT = 1, OF_PTAB_CENTER, OF_PTAB_RIGHT };
#define OF_PTAB(align, indents, leader) \
    ((align) | (indents) << 2 | (leader) << 8)
/** @brief An OR_FIELD run's n: this page's number, or the page count. */
enum { OF_FIELD_PAGE = 1, OF_FIELD_PAGES };

/** @brief A run: text in one style, or one thing in the text's flow. */
typedef struct {
    unsigned char     kind;         /**< OR_*                            */
    const of_style_t *st;           /**< how it looks                    */
    const char       *text;         /**< OR_TEXT's, UTF-8; OR_MARK's name */
    const char       *link;         /**< an address, "#bookmark", or NULL */
    of_image_t       *image;        /**< OR_IMAGE's picture              */
    of_float_t       *flt;          /**< OR_FLOAT's box                  */
    of_math_t        *math;         /**< OR_MATH's equation              */
    int               n;            /**< OR_FIELD: which; OR_NOTE: which;
                                       OR_TAB: 0 or OF_PTAB()            */
} of_run_t;

/** @brief A paragraph. */
typedef struct {
    of_pstyle_t       ps;           /**< its layout                      */
    const of_style_t *mark;       /**< its end's look: an empty one's height */
    of_run_t         *run;          /**< what it says                    */
    int               nrun;
    const char       *label;        /**< a list's number or bullet, or NULL */
    const of_style_t *label_st;     /**< the label's look                */
    unsigned char     label_follow; /**< then: 0 a tab, 1 a space, 2 nothing */
} of_para_t;

/** @brief A table's cell. */
typedef struct {
    of_story_t   *body;             /**< what it holds                   */
    int           span;             /**< grid columns it covers          */
    unsigned char vmerge;         /**< 0 alone, 1 begins, 2 continues a merge */
    unsigned char valign;           /**< 0 top, 1 middle, 2 bottom       */
    uint32_t      fill;             /**< its shading                     */
    of_border_t   border[4];        /**< its own; width < 0: the table's */
    float         margin[4];        /**< top, left, bottom, right; < 0:
                                       the table's                       */
} of_cell_t;

/** @brief A table's row. */
typedef struct {
    of_cell_t    *cell;             /**< its cells, left to right        */
    int           ncell;
    float         height;           /**< at least this, or exactly       */
    unsigned char exact;            /**< the height is exact             */
    unsigned char header;           /**< repeated atop each page         */
} of_row_t;

/** @brief A table. */
typedef struct {
    of_row_t     *row;              /**< its rows, top to bottom         */
    int           nrow;
    float        *grid;             /**< each grid column's width        */
    int           ngrid;
    float         indent;           /**< from the column's left          */
    unsigned char align;            /**< OF_LEFT, OF_CENTER, OF_RIGHT    */
    of_border_t   border[6];        /**< top, left, bottom, right, and the
                                       insides: across, down             */
    float         margin[4];        /**< cells': top, left, bottom, right */
} of_table_t;

/** @brief What a block is (of_block_t.kind). */
enum { OB_PARA, OB_TABLE };

/** @brief A paragraph or a table, as a story holds them. */
typedef struct {
    unsigned char kind;             /**< OB_*                            */
    of_para_t    *para;             /**< OB_PARA's                       */
    of_table_t   *table;            /**< OB_TABLE's                      */
} of_block_t;

/** @brief Blocks in reading order: a body, a header, a cell, a note. */
struct of_story {
    of_block_t *block;              /**< in order                        */
    int         nblock;
};

/** @brief A drawing its reader draws itself, from its own model: Word's
 *         DrawingML shapes, groups and styled pictures. */
typedef struct {
    const void *node;               /**< the reader's: its a:graphicData */
    const char *part;               /**< the part it is in, for pictures */
    of_math_t  *math;               /**< a formula instead, set; drawn
                                       centred in the box                */
} of_art_t;

/** @brief A text box within a drawing: its text, where it is set. */
typedef struct {
    of_story_t   *text;             /**< what it says                    */
    float         x, y, w, h;       /**< from the drawing's top left, its
                                       margins taken off                 */
    unsigned char anchor;           /**< 0 top, 1 middle, 2 bottom       */
} of_textbox_t;

/** @brief A picture, drawn where its run or float puts it. */
struct of_image {
    const char     *part;           /**< its part in the package         */
    float           w, h;           /**< its size drawn                  */
    float           crop[4];        /**< cut away: left, top, right, bottom,
                                       as fractions of the picture       */
    int             pdf;            /**< the writer's; -1 not yet, -2 none */
    const of_art_t *art;            /**< drawn by its reader instead     */
    unsigned char  *meta;           /**< a metafile, kept to draw each time */
    size_t          nmeta;
};

/* What a float's place counts from: the page, the margins' box, the
 * column, the paragraph; the strip before the margin (left, top) or the
 * one after it (right, bottom). */
enum { OF_REL_PAGE, OF_REL_MARGIN, OF_REL_COLUMN, OF_REL_PARA,
       OF_REL_BEFORE, OF_REL_AFTER };
/** @brief How text goes round a float: over or under it, beside its box,
 *         or above and below it only. */
enum { OF_WRAP_NONE, OF_WRAP_SQUARE, OF_WRAP_BLOCK };

/** @brief A box placed on the page, not in the text: a picture or a text
 *         box, with the text flowing around it, or not. */
struct of_float {
    of_image_t   *image;            /**< a picture, or NULL              */
    of_story_t   *text;             /**< a text box's contents, or NULL  */
    const of_art_t *art;            /**< a drawing its reader draws      */
    of_textbox_t *box;              /**< and the text boxes in it        */
    int           nbox;
    uint32_t      fill;             /**< a text box's ground             */
    of_border_t   line;             /**< and its outline                 */
    float         inset[4];         /**< its text's margins              */
    float         w, h;             /**< its size                        */
    unsigned char rel_x, rel_y;     /**< what x and y count from: OF_REL_* */
    unsigned char align_x, align_y; /**< 0 by x, y; 1 start, 2 middle, 3 end */
    float         x, y;             /**< offsets                         */
    unsigned char pct_x, pct_y;     /**< they are fractions of what they
                                       count from, not points            */
    unsigned char wrap;             /**< OF_WRAP_*                       */
    unsigned char behind;           /**< under the text, not over it     */
    float         gap;              /**< kept clear around it            */
};

/** @brief How a page or list number reads: 1, I, i, A or a. */
enum { OF_NUM_DECIMAL, OF_NUM_UROMAN, OF_NUM_LROMAN, OF_NUM_ULETTER,
       OF_NUM_LLETTER };

/** @brief A section: pages of one setup, and what they hold. */
typedef struct {
    of_story_t    body;             /**< its blocks                      */
    float         w, h;             /**< the page                        */
    float         top, bottom;      /**< margins                         */
    float         left, right;
    float         head, foot;       /**< header's and footer's distance  */
    int           cols;             /**< text columns                    */
    float         gap;              /**< between them                    */
    of_story_t   *header[3];        /**< default, first page, even pages */
    of_story_t   *footer[3];
    unsigned char title_page;       /**< its first page has its own      */
    int           start;            /**< numbers start again at; -1 go on */
    unsigned char num;              /**< how page numbers read: OF_NUM_* */
    unsigned char kind;             /**< 0 a new page, 1 continuous      */
} of_section_t;

/** @brief A document, built by a reader. */
typedef struct of_doc {
    of_arena_t        arena;        /**< everything below lives in it    */
    of_pkg_t         *pkg;          /**< where pictures come from        */
    of_section_t     *sect;         /**< in order                        */
    int               nsect;
    of_story_t      **note;         /**< footnotes, as numbered          */
    int               nnote;
    of_story_t      **endnote;      /**< endnotes, as numbered           */
    int               nendnote;
    const char       *title;        /**< the document's own, or NULL     */
    const char       *author;
    float             tab;          /**< default tab stops' interval     */
    unsigned char     even_odd;     /**< even pages' headers are their own */
    unsigned char     tab_indent;   /**< tab stops count from the indent */
    const of_style_t **styles;      /**< each look once: open addressing */
    int               nstyles, stylecap;
    int             (*art_draw)(struct of_doc *d, struct of_faces *fs,
                                const of_art_t *a, const float *box,
                                tiku_pdf_writer_t *w);
                                    /* a drawing drawn in box: x y w h   */
    void            (*art_free)(void *ctx);  /**< what it needs, given back */
    void             *art_ctx;
    of_math_t        *maths;        /**< every equation, to be set       */
} of_doc_t;

/** @brief A new, empty document.  NULL out of memory. */
of_doc_t *of_doc_new(void);
/** @brief The document and everything in it, given back. */
void of_doc_free(of_doc_t *d);
/** @brief @p s as the document keeps it: one copy of each look.
 *  @return it, or NULL out of memory. */
const of_style_t *of_style(of_doc_t *d, const of_style_t *s);
/** @brief The default look: Times New Roman, 12 points, black. */
void of_style_default(of_style_t *s);
/** @brief The default paragraph: left, no indent or space, single
 *         spaced, widow control on. */
void of_pstyle_default(of_pstyle_t *p);
/** @brief @p n as a list or page number reads in format @p fmt, into
 *         @p out. */
void of_number(int n, int fmt, char *out, size_t max);
/** @brief A character of the Symbol or Wingdings fonts as the Unicode
 *         character it looks like: bullets, arrows, Greek. */
uint32_t of_symbol(const char *font, uint32_t code);
/** @brief The branch of mc:AlternateContent @p ac to read: the first
 *         mc:Choice whose Requires names only namespaces read here, else
 *         mc:Fallback; NULL when there is neither. */
const tiku_xml_node_t *of_alternate(const tiku_xml_node_t *ac);

/** @brief A face text is set in: the file found, opened for measuring,
 *         and the writer's font once something is drawn with it. */
typedef struct {
    tiku_fontset_face_t f;          /**< which, and what it lacks        */
    tiku_ttf_t         *ttf;        /**< opened                          */
    int                 pdf;        /**< the writer's; -1 not yet        */
    int                 upem;       /**< units to the em                 */
    int                 ascent;     /**< a line's, font units            */
    int                 height;
    int                 under, under_size;  /**< where its rules go      */
    int                 strike, strike_size;
    char                key[1100];  /**< the family, weight and slant
                                       asked, or a fallback's file       */
} of_face_t;

/** @brief The faces one document uses, and characters sent elsewhere. */
typedef struct of_faces {
    of_face_t *face;                /**< opened as asked                 */
    int        nface, facecap;
    struct of_fall *fall;           /**< characters faces lack, and the
                                       faces that draw them              */
    int        nfall, fallcap;      /**< how many, and room              */
} of_faces_t;

/** @brief The face for @p family at @p bold and @p italic.  @return its
 *         index, or -1 when the machine has no faces at all or the face
 *         cannot be opened. */
int of_face(of_faces_t *fs, const char *family, int bold, int italic);
/** @brief The face drawing @p cp for text in face @p face: it, or one
 *         found for the character.  @return its index, with the glyph in
 *         @p gid (0 when nothing draws it). */
int of_face_for(of_faces_t *fs, int face, uint32_t cp, unsigned *gid);
/** @brief Glyph @p gid's advance in face @p face at @p size points. */
float of_face_advance(const of_faces_t *fs, int face, unsigned gid,
                      float size);
/** @brief Every face's font in the writer forgotten, so that a new
 *         writer is given its own. */
void of_faces_unwritten(of_faces_t *fs);
/** @brief Every face given back. */
void of_faces_free(of_faces_t *fs);

/** @brief What a fragment is (of_frag_t.kind). */
enum { OF_FRAG_GLYPHS, OF_FRAG_IMAGE, OF_FRAG_RECT, OF_FRAG_RULE,
       OF_FRAG_BOX, OF_FRAG_ART, OF_FRAG_MATH };

/** @brief Something to draw, placed: glyphs on a baseline, a picture, a
 *         filled box, a rule, or a float's box. */
typedef struct {
    unsigned char     kind;         /**< OF_FRAG_*                       */
    float             x, y;         /**< glyphs: the baseline's start; else
                                       the top left                      */
    float             w, h;         /**< its extent                      */
    int               face;         /**< glyphs: the face                */
    float             size;         /**< and its size                    */
    uint32_t          color;        /**< glyphs, boxes and rules         */
    unsigned char     bold, italic; /**< glyphs: made up by the writer   */
    unsigned char     dash;         /**< rules: OF_LS_*                  */
    tiku_pdf_glyph_t *g;            /**< glyphs: which, where along      */
    int               ng;
    of_image_t       *image;        /**< OF_FRAG_IMAGE's                 */
    of_float_t       *flt;          /**< OF_FRAG_BOX's and OF_FRAG_ART's */
    const of_math_t  *math;         /**< OF_FRAG_MATH's, its baseline at y */
    const char       *link;         /**< the link over it, or NULL       */
    float             clip_top, clip_bottom; /**< clip limits relative to y */
    unsigned char     clipped;      /**< vertical clip is active         */
} of_frag_t;

/** @brief One character or object of a paragraph, measured. */
typedef struct {
    uint32_t          cp;           /**< the character, or 0 for an object */
    unsigned          gid;          /**< its glyph                       */
    int               face;         /**< in this face                    */
    float             w;            /**< its advance, spacing included   */
    float             size;         /**< the size it is drawn at         */
    float             asc, desc;    /**< above and below the baseline    */
    float             rise;         /**< its baseline raised by          */
    const of_run_t   *run;          /**< where it came from              */
    unsigned char     kind;         /**< OA_*                            */
    unsigned char     brk;          /**< a line may break after it       */
} of_atom_t;

/** @brief What an atom is (of_atom_t.kind). */
enum { OA_GLYPH, OA_SPACE, OA_TAB, OA_BREAK, OA_PAGE, OA_COLUMN, OA_IMAGE,
       OA_FLOAT, OA_NOTE, OA_MARK, OA_LABEL, OA_MATH };

/** @brief A paragraph measured into atoms, ready to be broken into lines. */
typedef struct {
    of_atom_t        *atom;         /**< in order                        */
    int               natom, cap;
    const of_para_t  *para;         /**< what they came from             */
    float             mark_asc;     /**< an empty line's extent          */
    float             mark_desc;
    float            *x;            /**< each atom's place, as last laid */
    unsigned char    *lead;         /**< each tab's leader, as last laid */
} of_flow_t;

/** @brief A line: its extent, and the fragments that draw it. */
typedef struct {
    float h;                        /**< its height                      */
    float base;                     /**< its baseline, down from its top */
    int   frag, nfrag;              /**< its fragments in the list given */
    int   next;                     /**< the atom the next line starts at */
    int   ends;                     /**< 0, or OA_PAGE / OA_COLUMN after it */
} of_line_t;

/** @brief Page numbers text in a header or a body needs. */
typedef struct {
    int page, pages, num;           /**< this page, of how many; the format */
} of_pageno_t;

/** @brief @p p measured into @p out, its fields read with @p no.
 *  @return 0, or -1 out of memory. */
int of_flow_build(of_faces_t *fs, const of_para_t *p, const of_pageno_t *no,
                  of_flow_t *out);
/**
 * @brief The line starting at atom @p from (@p first: the paragraph's
 *        first) in a column @p width wide, kept between @p x0 and @p x1
 *        of it -- all of it but what floats take -- and inside the
 *        paragraph's indents.  Tab stops count from the column's edge,
 *        or from the paragraph's left indent when the document sets
 *        tab_indent (OpenDocument text).
 *        Its fragments are appended to @p frags, their glyphs in @p a, x
 *        from the column's edge, y from the line's top.  @return 0, or -1.
 */
int of_flow_line(of_doc_t *d, of_faces_t *fs, of_flow_t *f, int from,
                 int first, float width, float x0, float x1,
                 of_vec_t *frags, of_arena_t *a, of_line_t *out);
/** @brief The flow's memory given back. */
void of_flow_free(of_flow_t *f);

/** @brief A named place: a bookmark, or a heading for the outline. */
typedef struct {
    const char *name;               /**< a bookmark's; a heading's text  */
    int         level;              /**< a heading's; -1 for a bookmark  */
    int         page;               /**< where it landed                 */
    float       y;
} of_place_t;

/** @brief A page laid out: its size and everything drawn on it. */
typedef struct {
    float     w, h;                 /**< points                          */
    of_frag_t *frag;                /**< in drawing order                */
    int       nfrag;
} of_page_t;

/** @brief A document laid out: its pages, and places for links. */
typedef struct {
    of_arena_t  arena;              /**< glyphs and fragments            */
    of_page_t  *page;               /**< in order                        */
    int         npage;
    of_place_t *place;              /**< bookmarks and headings          */
    int         nplace;
} of_layout_t;

/** @brief @p d laid out into pages with the faces @p fs.
 *  @return 0, or -1 out of memory or past the page ceiling. */
int of_layout(of_doc_t *d, of_faces_t *fs, of_layout_t *out);
/** @brief A layout given back. */
void of_layout_free(of_layout_t *l);
/**
 * @brief Story @p s laid out as one column @p width wide that never ends:
 *        its fragments appended to @p frags, x and y from the column's top
 *        left.  For cells, boxes, headers and notes.
 * @return the height it took, or -1 out of memory.
 */
float of_layout_story(of_doc_t *d, of_faces_t *fs, const of_story_t *s,
                      float width, const of_pageno_t *no, of_vec_t *frags,
                      of_arena_t *a);

/** @brief Layout @p l written with @p w, faces registered as needed.
 *  @return 0, or -1 when the writer ran out of memory. */
int of_draw(of_doc_t *d, of_faces_t *fs, const of_layout_t *l,
            tiku_pdf_writer_t *w);
/** @brief Picture @p im given to the writer, once.  @return its number,
 *         or -1 when it cannot be read or the writer cannot take it; a
 *         metafile is then kept in @p im, to be drawn where it is used. */
int of_draw_image(of_doc_t *d, of_image_t *im, tiku_pdf_writer_t *w);
/** @brief Every equation of @p d not yet set, set with the faces @p fs,
 *         as its reader asked.  @return 0, or -1 out of memory. */
int of_math_prepare(of_doc_t *d, of_faces_t *fs);
/** @brief Equation @p m, set, drawn with its baseline starting at
 *         (@p x, @p y). */
void of_math_draw(of_faces_t *fs, const of_math_t *m, float x, float y,
                  tiku_pdf_writer_t *w);
/** @brief Fragment @p g's glyphs, moved by (@p dx, @p dy), in its face's
 *         font, registered with the writer if need be. */
void of_draw_glyphs(of_faces_t *fs, const of_frag_t *g, float dx, float dy,
                    tiku_pdf_writer_t *w);
/** @brief Whether @p b (@p n bytes) is a Windows metafile, plain or
 *         gzipped: 1 an EMF, 2 a WMF, 0 neither. */
int of_metafile_is(const unsigned char *b, size_t n);
/** @brief Metafile @p b drawn into the box @p x, @p y, @p w by @p h, its
 *         text in faces @p fs.  @return 0, or -1 when it is not one, or
 *         holds nothing this can draw. */
int of_metafile_draw(const unsigned char *b, size_t n, of_faces_t *fs,
                     tiku_pdf_writer_t *wr, float x, float y, float w,
                     float h);
/** @brief One fragment drawn, moved by (@p dx, @p dy): glyphs, a box, a
 *         rule, a picture, a drawing or an equation.  Its link is not
 *         drawn: that is the page's. */
void of_draw_frag(of_doc_t *d, of_faces_t *fs, const of_frag_t *g, float dx,
                  float dy, tiku_pdf_writer_t *w);

/** @brief A .docx read into a document.  NULL with why said. */
of_doc_t *of_docx_read(of_pkg_t *pkg, char *why, size_t max);
/** @brief An .odt read into a document.  NULL with why said. */
of_doc_t *of_odt_read(of_pkg_t *pkg, char *why, size_t max);
/** @brief A .pptx drawn, a slide a page.  @return 0, or -1 with why. */
int of_pptx_draw(of_pkg_t *pkg, of_faces_t *fs, tiku_pdf_writer_t *w,
                 char *why, size_t max);
/** @brief Graphic frame @p gf of slide, layout or master @p part drawn
 *         alone into new writer @p w, unturned, on a page the size it
 *         shows at in a parent scaled @p scale (x, y), in the colours its
 *         master and overrides give: a chart, a diagram's saved drawing or
 *         an object's kept picture.  @return as dml_frame_shown(); no
 *         page is added for a frame with no room. */
int of_pptx_frame(of_pkg_t *pkg, of_faces_t *fs, tiku_pdf_writer_t *w,
                  const char *part, const tiku_xml_node_t *gf,
                  const double scale[2]);

#endif /* TIKU_OFFICE_PRIVATE_H_ */
