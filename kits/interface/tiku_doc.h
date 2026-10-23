/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_doc.h - a documentation page, read and laid out, with no pixels.
 *
 * A help dialect of HTML becomes blocks of inline runs, and a width
 * becomes lines of pieces: the run/line intersections, each one a byte
 * range with a measured x and width.  The piece table is the one thing
 * the painter draws from and the one thing the hit test searches, so a
 * link is where it was drawn.  What a page looks like is not here: the
 * ink vocabulary belongs to the painter, and the words belong to the
 * application's strings.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_DOC_H_
#define TIKU_DOC_H_

#include <stdint.h>
#include <stddef.h>

#include "tiku_geom.h"
#include "tiku_measure.h"

/*
 * The caps, each with its own #ifndef so a command-line override takes
 * the one it names and leaves the rest.  A table that fills sets its
 * TIKU_DOC_FULL_* bit and the read goes on without what did not fit.
 */
#ifndef TIKU_DOC_TEXT_MAX
#define TIKU_DOC_TEXT_MAX    16384  /* the page's words, entities decoded */
#endif
#ifndef TIKU_DOC_BLOCKS_MAX
#define TIKU_DOC_BLOCKS_MAX  256
#endif
#ifndef TIKU_DOC_RUNS_MAX
#define TIKU_DOC_RUNS_MAX    (TIKU_DOC_TEXT_MAX / 16)
#endif
#ifndef TIKU_DOC_LINES_MAX
#define TIKU_DOC_LINES_MAX   1024
#endif
#ifndef TIKU_DOC_PIECES_MAX
#define TIKU_DOC_PIECES_MAX  2048
#endif
#ifndef TIKU_DOC_LINKS_MAX
#define TIKU_DOC_LINKS_MAX   256
#endif
#ifndef TIKU_DOC_ANCHORS_MAX
#define TIKU_DOC_ANCHORS_MAX 64
#endif
#ifndef TIKU_DOC_IMAGES_MAX
#define TIKU_DOC_IMAGES_MAX  32
#endif
#ifndef TIKU_DOC_COLS_MAX
#define TIKU_DOC_COLS_MAX    8
#endif
/* How deep the elements may nest while reading.  Parse-time only. */
#define TIKU_DOC_DEPTH_MAX   16

#ifndef TIKU_DOC_CSS_RULES_MAX
#define TIKU_DOC_CSS_RULES_MAX 64
#endif
#ifndef TIKU_DOC_CSS_BYTES_MAX
#define TIKU_DOC_CSS_BYTES_MAX 32768
#endif

/** @brief Nothing: no link, no line, no block, no piece. */
#define TIKU_DOC_NONE (-1)

/** @brief What a block is, which decides its face, indent and spacing. */
typedef enum {
    TIKU_DOC_BLOCK_P = 0,
    TIKU_DOC_BLOCK_H1,
    TIKU_DOC_BLOCK_H2,
    TIKU_DOC_BLOCK_H3,
    TIKU_DOC_BLOCK_ITEM,        /* a list item; depth and index carried  */
    TIKU_DOC_BLOCK_PRE,
    TIKU_DOC_BLOCK_QUOTE,
    TIKU_DOC_BLOCK_RULE,
    TIKU_DOC_BLOCK_IMAGE,
    /*
     * One cell of a table.  Its row is its index, its column and span
     * are its own; a table is the cells that lie next to each other,
     * since the element that held them is not a block of its own.
     */
    TIKU_DOC_BLOCK_CELL,
    /*
     * What a cap cost, as a block of its own.  A page that was cut gets
     * one at the end, so the cut takes room in the line table and is
     * read back by anything that reads the window's words.
     */
    TIKU_DOC_BLOCK_NOTICE,
    TIKU_DOC_BLOCK_H4,
    TIKU_DOC_BLOCK_H5,
    TIKU_DOC_BLOCK_H6,
    TIKU_DOC_BLOCK_DEFINITION,
    TIKU_DOC_BLOCK_BOX,
    TIKU_DOC_BLOCK_CAPTION,
    TIKU_DOC_BLOCK_KINDS
} tiku_doc_block_kind_t;

/**
 * @brief What a run of words means, which the painter turns into ink.
 *
 * tiku_ink_t is the painter's, in tiku_gfx.h, which a file that compiles
 * for a board may not include.  The painter draws EMPHASIS in an ink of
 * its own: the proportional faces have no italic cut.
 */
typedef enum {
    TIKU_DOC_INK_PLAIN = 0,
    TIKU_DOC_INK_FAINT,         /* the notice, and an image's alt text   */
    TIKU_DOC_INK_CODE,
    TIKU_DOC_INK_EMPHASIS,
    TIKU_DOC_INK_LINK,
    TIKU_DOC_INK_INKS
} tiku_doc_ink_t;

/** @brief A list item's own marker, laid out in the hanging indent. */
#define TIKU_DOC_RUN_MARKER  0x01u
/** @brief This run ends its line, wherever it got to: a <br>. */
#define TIKU_DOC_RUN_BREAK   0x02u
/** @brief Spaces and newlines inside this run are the page's own. */
#define TIKU_DOC_RUN_PRE     0x04u
/** @brief The run has an explicit RGB foreground. */
#define TIKU_DOC_RUN_COLOR   0x08u
/** @brief Underline the run's painted pieces. */
#define TIKU_DOC_RUN_UNDERLINE 0x10u
/** @brief Strike through the run's painted pieces. */
#define TIKU_DOC_RUN_STRIKE  0x20u
/** @brief Suppress the link's default underline; explicit decorations remain. */
#define TIKU_DOC_RUN_LINK_PLAIN 0x40u
/** @brief Paint an RGB background under the run's pieces. */
#define TIKU_DOC_RUN_BACKGROUND 0x80u

/** @brief Style conversion notices; these do not truncate document text. */
enum {
    TIKU_DOC_CSS_UNSUPPORTED = 0x01u,
    TIKU_DOC_CSS_LIMIT = 0x02u,
    TIKU_DOC_CSS_SYNTAX = 0x04u,
    TIKU_DOC_FONT_FALLBACK = 0x08u,
    TIKU_DOC_FLOW_FALLBACK = 0x10u,
    TIKU_DOC_TABLE_FALLBACK = 0x20u
};

/** @brief An ordered list: the item's number is its own. */
#define TIKU_DOC_BLOCK_ORDERED 0x01u
/** @brief A cell of the head of its table: a <th>, drawn bold. */
#define TIKU_DOC_BLOCK_HEADER  0x02u
/** @brief The element's enclosing CSS box supplies its outer spacing. */
#define TIKU_DOC_BLOCK_BOX_CHILD 0x04u
/** @brief A box groups the image and caption content of a figure. */
#define TIKU_DOC_BLOCK_FIGURE 0x08u
/** @brief A box contains one table and its caption. */
#define TIKU_DOC_BLOCK_TABLE 0x10u
/** @brief Maximum row span retained by the bounded table layout. */
#define TIKU_DOC_ROWSPAN_MAX 64

/*
 * Which table filled, as bits; tiku_doc_full_first() names the first as
 * an untranslated noun, with the cap it hit.
 */
#define TIKU_DOC_FULL_TEXT    0x001u
#define TIKU_DOC_FULL_BLOCKS  0x002u
#define TIKU_DOC_FULL_RUNS    0x004u
#define TIKU_DOC_FULL_LINES   0x008u
#define TIKU_DOC_FULL_PIECES  0x010u
#define TIKU_DOC_FULL_LINKS   0x020u
#define TIKU_DOC_FULL_ANCHORS 0x040u
#define TIKU_DOC_FULL_IMAGES  0x080u
#define TIKU_DOC_FULL_DEPTH   0x100u
/**
 * @brief A heading wanted a size the measuring end cannot answer.
 *
 * Desktop headings use the measured font-size ladder. At its largest
 * size, and on boards that provide a single baked metrics table, bold
 * at body size is the fallback.  This bit reports that fallback; it is
 * not a cut and adds no notice.
 */
#define TIKU_DOC_FACE_FLAT    0x200u
/** @brief A word was broken between codepoints to fit a narrow box. */
#define TIKU_DOC_NARROW       0x400u
/**
 * @brief A table had more than TIKU_DOC_COLS_MAX columns, and folded the
 *        rest.
 *
 * Like the two above, this reports how the page was fitted and adds no
 * notice: a cell past the last column is laid in the last one, under
 * what is already there.
 */
#define TIKU_DOC_COLS_FOLDED  0x800u
/** @brief Layout stopped before vertical coordinates exceeded their budget. */
#define TIKU_DOC_FULL_HEIGHT  0x1000u
/** @brief Vertical coordinate budget, including trailing space and notices. */
#define TIKU_DOC_HEIGHT_MAX   (INT32_MAX / 4)

/** @brief Logical font: 16.16 root-size scale plus a 16.16 pixel offset. */
typedef struct {
    int32_t scale, offset;
    uint8_t family, weight, italic, specified;
} tiku_doc_font_t;

/** @brief Left-to-right text alignment within a block's available width. */
enum { TIKU_DOC_ALIGN_LEFT, TIKU_DOC_ALIGN_CENTER, TIKU_DOC_ALIGN_RIGHT };
/** @brief Collapsing and wrapping policy for a run's whitespace. */
enum {
    TIKU_DOC_WHITE_NORMAL, TIKU_DOC_WHITE_PRE_WRAP,
    TIKU_DOC_WHITE_PRE, TIKU_DOC_WHITE_NOWRAP
};
/** @brief Line-height expression: normal, font-size factor or root-size length. */
enum { TIKU_DOC_HEIGHT_NORMAL, TIKU_DOC_HEIGHT_FACTOR, TIKU_DOC_HEIGHT_LENGTH };

/** @brief Inherited flow style with 16.16 line-height scale and pixel offset. */
typedef struct {
    int32_t scale, offset;
    uint8_t height, align, whitespace;
} tiku_doc_flow_t;

/** @brief A bounded CSS length; value uses 16.16 units. */
typedef struct {
    int32_t value;
    uint8_t unit;
} tiku_doc_length_t;
/** @brief CSS box lengths, resolved against the current font and containing box. */
enum { TIKU_DOC_PX, TIKU_DOC_EM, TIKU_DOC_REM, TIKU_DOC_PERCENT, TIKU_DOC_AUTO };
/** @brief Physical CSS sides in clockwise order. */
enum { TIKU_DOC_TOP, TIKU_DOC_RIGHT, TIKU_DOC_BOTTOM, TIKU_DOC_LEFT };

/** @brief Noninherited block box style with a uniform solid border. */
typedef struct {
    tiku_doc_length_t margin[4], padding[4], width, max_width, border, height;
    uint32_t background, border_color;
    uint8_t has_background, border_style, border_declared;
} tiku_doc_box_t;

/**
 * @brief One stretch of one face, one ink and one link.
 *
 * @c off names a NUL-terminated string in the page's own text, so
 * nothing that reads a run can walk past its end.
 */
typedef struct {
    int32_t off;                /* into the page's text                  */
    int32_t len;
    int32_t link;               /* into the link table, or TIKU_DOC_NONE */
    uint16_t face;              /* a TIKU_MEASURE_* number               */
    uint8_t ink;                /* tiku_doc_ink_t                        */
    uint8_t flags;
    uint8_t spare;
    uint32_t color;             /**< RGB foreground when RUN_COLOR is set */
    uint32_t background;
    tiku_doc_font_t font;
    tiku_doc_flow_t flow;
} tiku_doc_run_t;

/** @brief A paragraph, a heading, an item: what the runs hang from. */
typedef struct {
    int32_t y;                  /* set by layout; the space above is in it */
    int32_t h;
    int32_t run0, nrun;
    int32_t line0, nline;       /* set by layout                         */
    int32_t index;              /* an ordered item's number, a cell's row */
    int32_t image;              /* into the image table, or TIKU_DOC_NONE */
    uint8_t kind, depth, flags;
    uint8_t col, span;          /* a cell's column, and how many it takes */
    uint8_t spare;
    tiku_doc_font_t font;
    tiku_doc_flow_t flow;
    tiku_doc_box_t box;
    int32_t end;                /**< first block after a box's descendants */
    int32_t x, w;               /**< laid-out border box */
    int32_t border_px;          /**< resolved uniform border width */
    int32_t parent;             /**< enclosing box, or TIKU_DOC_NONE */
    uint8_t rowspan;
    int32_t content_x, content_w; /**< resolved horizontal content interval */
} tiku_doc_block_t;

/**
 * @brief One run met by one line: the unit drawn, and the unit hit.
 *
 * The face, the ink and the link are read back through @c run, not
 * copied here.  @c w was measured while laying out, in that run's face,
 * so a rectangle needs no second measure.
 */
typedef struct {
    int32_t off;                /* into the page's text: the bytes drawn */
    int32_t len;
    int32_t x;                  /* from the line's own x                 */
    int32_t w;
    int32_t run;
} tiku_doc_piece_t;

/** @brief A line of a block: where it sits and which pieces are on it. */
typedef struct {
    int32_t y;
    int32_t h, ascent;
    int32_t block;
    int32_t piece0, npiece;
    int32_t x, w;               /* the indent, and the advance used      */
} tiku_doc_line_t;

/** @brief A link: its href, its runs and its pieces.  Each range starts at
 *         the link's first run or piece and holds only those that follow
 *         it without a gap. */
typedef struct {
    int32_t href;               /* into the page's text                  */
    int32_t run0, nrun;
    int32_t piece0, npiece;     /* set by layout                         */
} tiku_doc_link_t;

/** @brief A name a link may reach, and the block wearing it. */
typedef struct {
    int32_t id;
    int32_t block, spare;
} tiku_doc_anchor_t;

/** @brief A picture: where it comes from, what it says, how big it is. */
typedef struct {
    int32_t src, alt;
    int32_t w, h;
    int32_t block, spare;
    tiku_doc_length_t width, height, max_width;
} tiku_doc_image_t;

/** @brief What a point landed on. */
typedef enum {
    TIKU_DOC_HIT_NONE = 0,
    TIKU_DOC_HIT_TEXT,
    TIKU_DOC_HIT_LINK,
    TIKU_DOC_HIT_IMAGE
} tiku_doc_hit_kind_t;

/** @brief A point, answered: what is there, and which of it. */
typedef struct {
    int kind;                   /* tiku_doc_hit_kind_t                   */
    int link;                   /* TIKU_DOC_NONE unless kind is LINK     */
    int block;
    int line;
    int piece;
} tiku_doc_hit_t;

/**
 * @brief Where a reader is, in terms a re-layout cannot move.
 *
 * A y holds for one width only; a block and the lines into it hold
 * across a resize and a change of text size.
 */
typedef struct {
    int block;
    int into;                   /* lines into that block                 */
} tiku_doc_place_t;

/**
 * @brief How big a picture is, asked of whoever can decode one.
 *
 * The decoders are the translation kit's, and this kit includes nothing
 * outside itself.  A caller that has them answers here; one that has not
 * returns 0 and the page draws the alt text in a frame.
 * @note The same @p src must get the same size throughout one layout, or
 *       the page drawn and the page hit differ.
 */
typedef int (*tiku_doc_image_fn)(void *ctx, const char *src,
                                 int *w, int *h);

/** @brief Per-document capacities; tiku_doc_init() sets the
 *         TIKU_DOC_*_MAX defaults. */
typedef struct {
    int text, blocks, runs, lines, pieces, links, anchors, images;
} tiku_doc_limits_t;

/**
 * @brief A page: its text and tables, the layout, and their storage.
 *
 * The default tables are inside it, so it is large, and it may point at
 * larger storage of the caller's.
 * @note Hold it through a pointer; copied by value, its table pointers
 *       still point into the original.
 */
typedef struct {
    char *text;
    int  ntext;
    tiku_doc_block_t  *block;
    tiku_doc_run_t    *run;
    tiku_doc_line_t   *line;
    tiku_doc_piece_t  *piece;
    tiku_doc_link_t   *link;
    tiku_doc_anchor_t *anchor;
    tiku_doc_image_t  *image;
    int nblock, nrun, nline, npiece, nlink, nanchor, nimage;
    int title;                  /* into the text, or TIKU_DOC_NONE       */
    unsigned full;              /* which caps filled, and how            */
    unsigned notices;          /**< CSS conversion and resource notices */
    /* What the tables describe: the width they were laid for, and the
     * plain face's height when they were.  A layout asked for again at
     * both is the one already there. */
    int laid_width, laid_height;
    unsigned laid_font_stamp;   /**< font family/source used by layout */
    int height;
    tiku_doc_image_fn image_fn;
    void *image_ctx;
    tiku_doc_limits_t limits;    /* capacities of the bound buffers */
    void *storage;              /* optional caller-owned, aligned memory */
    size_t storage_bytes;
    /* The default tables, inside the page: no heap. */
    char small_text[TIKU_DOC_TEXT_MAX];
    tiku_doc_block_t  small_block[TIKU_DOC_BLOCKS_MAX];
    tiku_doc_run_t    small_run[TIKU_DOC_RUNS_MAX];
    tiku_doc_line_t   small_line[TIKU_DOC_LINES_MAX];
    tiku_doc_piece_t  small_piece[TIKU_DOC_PIECES_MAX];
    tiku_doc_link_t   small_link[TIKU_DOC_LINKS_MAX];
    tiku_doc_anchor_t small_anchor[TIKU_DOC_ANCHORS_MAX];
    tiku_doc_image_t  small_image[TIKU_DOC_IMAGES_MAX];
} tiku_doc_t;

/** @brief An empty page: no blocks, no text, nothing laid out. */
void tiku_doc_init(tiku_doc_t *d);

/** @brief Bytes of caller-owned storage @p limits need, or 0 when a limit
 *         is invalid: fewer than two blocks, any count below 1, or a
 *         size that would overflow. */
size_t tiku_doc_storage_size(const tiku_doc_limits_t *limits);
/**
 * @brief Initialize @p d with tables of @p limits in caller-owned
 *        @p storage; tiku_doc_read() and tiku_doc_layout() keep them.
 * @return 1, or 0 with @p d untouched for invalid limits or storage that
 *         is too small or not 8-byte aligned.
 * @note The storage must outlive @p d's last use, and stays the
 *       caller's: initializing again frees nothing.
 */
int tiku_doc_init_storage(tiku_doc_t *d, const tiku_doc_limits_t *limits,
                         void *storage, size_t bytes);

/** @brief Say who can size a picture.  Both may be NULL. */
void tiku_doc_images(tiku_doc_t *d, tiku_doc_image_fn fn, void *ctx);

/**
 * @brief Read @p html into blocks and runs, and forget what was there.
 *
 * Entities are decoded, runs of blank space in flow content become one
 * space, and the space inside <pre> is the page's own. Element and attribute
 * names ignore ASCII case. Unknown elements retain text; script, style, head
 * and comments omit their content. Embedded stylesheets and inline styles
 * resolve supported text properties; CSS losses set notices. Unterminated
 * quoted tags consume the remainder.
 *
 * @return 1 when the whole page fitted; 0 when a cap filled, which
 *         tiku_doc_full() then names.
 */
int tiku_doc_read(tiku_doc_t *d, const char *html);

/**
 * @brief Lay the page out for @p width.
 *
 * Laying out again at the same width and text size returns at once.
 * Otherwise it costs one pass over the text, measuring each word once in
 * its own face.
 * @return how tall it came to.
 * @note A window resized by a drag lays out when the drag settles.
 */
int tiku_doc_layout(tiku_doc_t *d, int width);

/** @brief How tall the page is, as last laid out. */
int tiku_doc_height(const tiku_doc_t *d);
/** @brief The width the page was last laid out for, or TIKU_DOC_NONE. */
int tiku_doc_width(const tiku_doc_t *d);
/** @brief Whether there is a layout to draw or hit at all. */
int tiku_doc_laid(const tiku_doc_t *d);

/** @brief Which caps filled, as TIKU_DOC_FULL_* bits. */
unsigned tiku_doc_full(const tiku_doc_t *d);
/** @brief CSS conversion and TIKU_DOC_FONT_FALLBACK notices. */
unsigned tiku_doc_notices(const tiku_doc_t *d);

/**
 * @brief The first cap that filled, as a noun and the number it hit.
 *
 * The noun is untranslated: the application writes the sentence.
 *
 * @return 1 when something filled.
 */
int tiku_doc_full_first(const tiku_doc_t *d, const char **what, int *cap);

/** @brief The first heading's words, or NULL. */
const char *tiku_doc_title(const tiku_doc_t *d);

/** @brief How many blocks the page has. */
int tiku_doc_block_count(const tiku_doc_t *d);
/** @brief How many lines the last layout made. */
int tiku_doc_line_count(const tiku_doc_t *d);
/** @brief How many pieces the last layout made. */
int tiku_doc_piece_count(const tiku_doc_t *d);
/** @brief Block @p i, or NULL past the table. */
const tiku_doc_block_t *tiku_doc_block(const tiku_doc_t *d, int i);
/** @brief Heading level 1 through 6, or zero for another block or NULL. */
int tiku_doc_heading_level(const tiku_doc_block_t *block);
/** @brief Run @p i, or NULL past the table. */
const tiku_doc_run_t   *tiku_doc_run(const tiku_doc_t *d, int i);
/** @brief Line @p i, or NULL past the table. */
const tiku_doc_line_t  *tiku_doc_line(const tiku_doc_t *d, int i);
/** @brief Piece @p i, or NULL past the table. */
const tiku_doc_piece_t *tiku_doc_piece(const tiku_doc_t *d, int i);

/** @brief The text at @p off, or "" when there is none there. */
const char *tiku_doc_text(const tiku_doc_t *d, int off);

/** @brief Where a piece sits, in page coordinates. */
tiku_rect_t tiku_doc_piece_rect(const tiku_doc_t *d, int piece);
/** @brief Where a whole block sits: a ground, a rule, a frame. */
tiku_rect_t tiku_doc_block_rect(const tiku_doc_t *d, int block);

/** @brief The line at @p y, or TIKU_DOC_NONE above, below or between. */
int tiku_doc_line_at(const tiku_doc_t *d, int y);

/**
 * @brief What is at (@p x, @p y), in page coordinates.
 *
 * A point below the last line, in the ragged room at the end of a line,
 * or on a page with no layout hits no text or link.
 */
tiku_doc_hit_t tiku_doc_hit(const tiku_doc_t *d, int x, int y);

/** @brief How many links the page has. */
int tiku_doc_link_count(const tiku_doc_t *d);
/** @brief Where link @p link goes, or "" when it names nowhere. */
const char *tiku_doc_link_href(const tiku_doc_t *d, int link);
/** @brief Every piece of a link, which lie together.  @return 1 when drawn. */
int tiku_doc_link_pieces(const tiku_doc_t *d, int link, int *first, int *n);
/**
 * @brief The link after @p from, or before it when @p back.
 *
 * TIKU_DOC_NONE asks for the first, or the last going back; the walk
 * wraps, so a keyboard reaches every link from wherever it starts.
 */
int tiku_doc_link_next(const tiku_doc_t *d, int from, int back);

/** @brief Where a name is, or TIKU_DOC_NONE when the page has none. */
int tiku_doc_anchor_y(const tiku_doc_t *d, const char *id);

/** @brief The reading position at page @p y, in terms a re-layout
 *         keeps. */
tiku_doc_place_t tiku_doc_place(const tiku_doc_t *d, int y);
/** @brief The page y of @p at under the current layout. */
int tiku_doc_place_y(const tiku_doc_t *d, tiku_doc_place_t at);

/**
 * @brief Walk the tables and say whether they agree.  For tests.
 *
 * Every piece inside its run, every line's pieces inside the table,
 * every block's lines inside it, and y never going back but to the top
 * of a table row.
 *
 * @return 1 when they hold.
 */
int tiku_doc_check(const tiku_doc_t *d);

#endif /* TIKU_DOC_H_ */
