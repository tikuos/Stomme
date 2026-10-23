/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_math.h - equations as a tree the readers build and the
 * typesetter sets: rows of tokens, fractions, scripts, roots, large
 * operators with their limits, fenced groups, matrices and arrays of
 * equations, accents, bars and braces.  Office Math (Word's, and
 * PowerPoint's) is read into it by tiku_office_omml.c, MathML (as
 * OpenDocument keeps it) by tiku_office_mathml.c; tiku_office_math.c and
 * tiku_office_math_set.c set it as TeX would, with the face and the MATH
 * table of tiku_office_math_font.c, and tiku_office_math.c draws it.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_MATH_H_
#define TIKU_OFFICE_MATH_H_

#include "tiku_office_private.h"

#define OM_DEPTH 48                 /* structures within structures      */

enum { OM_ROW, OM_TOKEN, OM_FRAC, OM_SCRIPTS, OM_ROOT, OM_NARY, OM_FENCE,
       OM_MATRIX, OM_ARRAY, OM_ACCENT, OM_UNDEROVER, OM_BAR, OM_BRACE,
       OM_BOX, OM_SPACE, OM_ALIGN, OM_PHANTOM, OM_STYLE };

/* How a token is spaced from its neighbours: TeX's classes of atom;
 * OC_NONE a space's, which its neighbours look past. */
enum { OC_ORD, OC_OP, OC_BIN, OC_REL, OC_OPEN, OC_CLOSE, OC_PUNCT,
       OC_INNER, OC_NONE };

/* How a token's letters look: OV_AUTO slants single letters, as maths
 * sets variables; OV_TEXT is the document's own face, upright. */
enum { OV_AUTO, OV_NORMAL, OV_ITALIC, OV_BOLD, OV_BOLD_ITALIC, OV_SCRIPT,
       OV_FRAKTUR, OV_DOUBLE, OV_SANS, OV_MONO, OV_TEXT };

/* Flags, by kind. */
#define OMF_NOBAR   0x01            /* a fraction stacked with no rule   */
#define OMF_SKEW    0x02            /* set across a slash, raised        */
#define OMF_LINEAR  0x04            /* written along the line            */
#define OMF_SMALL   0x08            /* in the style of a script          */
#define OMF_LIMITS  0x01            /* a large operator's limits above
                                       and below, not beside             */
#define OMF_BESIDE  0x02            /* ... beside, whatever the style    */
#define OMF_GROW    0x04            /* it grows with what it governs     */
#define OMF_TOP     0x01            /* a bar or brace over, not under    */
#define OMF_BOTTOM  0x02
#define OMF_BORDER  0x01            /* a box drawn round                 */
#define OMF_SHOW    0x01            /* a phantom's contents drawn        */
#define OMF_ZEROW   0x02            /* ... taking no width               */
#define OMF_ZEROH   0x04            /* ... no height or depth            */
#define OMF_DISPLAY 0x01            /* a style: display, not text        */
#define OMF_SCRIPT  0x02            /* ... a script's level              */
#define OMF_INLINE  0x04            /* ... text, not display             */
#define OMF_STRETCH 0x10            /* a fence's or accent's character
                                       stretched to what it covers       */

/** @brief One node of an equation. */
typedef struct of_mnode of_mnode_t;
struct of_mnode {
    unsigned char kind;             /* OM_*                              */
    unsigned char cls;              /* OC_*: a token's                   */
    unsigned char variant;          /* OV_*: a token's                   */
    unsigned char flags;            /* OMF_*, by kind                    */
    const char   *text;             /* a token's characters; a large
                                       operator's, an accent's, a
                                       brace's, UTF-8                    */
    const char   *open, *close, *sep;  /* a fence's characters; "" none  */
    of_mnode_t  **kid;              /* its parts, by kind (see below)    */
    int           nkid;
    int           rows, cols;       /* a matrix's                        */
    const unsigned char *align;     /* its columns': 'l', 'c', 'r'       */
    float         em;               /* a space's width, in ems           */
    float         rel;              /* a token's size over the run's     */
    uint32_t      color;            /* ARGB; 0: the equation's           */
};

/*
 * Parts by kind: OM_ROW and OM_FENCE their items; OM_FRAC numerator,
 * denominator; OM_SCRIPTS base, sub, sup, pre-sub, pre-sup; OM_ROOT
 * radicand, degree; OM_NARY lower, upper, operand; OM_UNDEROVER base,
 * under, over; OM_ACCENT, OM_BAR, OM_BRACE, OM_BOX, OM_PHANTOM and
 * OM_STYLE their base; OM_MATRIX its cells row by row; OM_ARRAY its
 * rows, OM_ALIGN marking where each lines up.  A missing part is NULL.
 */

/* Building: tiku_office_math.c */

/** @brief A node of @p kind with room for @p nkid parts, in @p a.  NULL
 *         out of memory. */
of_mnode_t *om_node(of_arena_t *a, int kind, int nkid);
/** @brief A token of @p text, its class and look, in @p a. */
of_mnode_t *om_token(of_arena_t *a, const char *text, int cls,
                     int variant);
/** @brief The class an operator character @p cp takes standing alone. */
int om_class(uint32_t cp);
/** @brief An equation set apart (@p display) or in a line, @p size points
 *         across its em, in @p color, registered with @p d to be set
 *         before the pages are.  NULL out of memory. */
of_math_t *of_math_new(of_doc_t *d, of_mnode_t *root, int display,
                       float size, uint32_t color);

/* Office Math: tiku_office_omml.c */

/** @brief An Office Math run @p r's own colour, as its format's run
 *         properties give it (w:rPr, a:rPr), with what it needs @p ctx. */
typedef uint32_t (*of_omml_tint_t)(void *ctx, const tiku_xml_node_t *r);

/** @brief Office Math element @p e (m:oMath) read into an equation
 *         registered with @p d: display style when @p display, @p size
 *         points to its em, ink @p color, each run's own by @p tint (NULL:
 *         none).  @return 0 with it in @p out (NULL when it says nothing),
 *         or -1 out of memory. */
int of_omml(of_doc_t *d, const tiku_xml_node_t *e, int display, float size,
            uint32_t color, of_omml_tint_t tint, void *ctx, of_math_t **out);
/** @brief The first math run (m:r) under @p e, whose look its equation
 *         takes; NULL for none. */
const tiku_xml_node_t *of_omml_first(const tiku_xml_node_t *e);

/* MathML: tiku_office_mathml.c */

/** @brief MathML element @p math (a math element, its prefix any) read
 *         into an equation registered with @p d: display style when
 *         @p display, @p size points to its em, ink @p color.  @return 0
 *         with it in @p out (NULL when it says nothing), or -1 out of
 *         memory. */
int of_mathml(of_doc_t *d, const tiku_xml_node_t *math, int display,
              float size, uint32_t color, of_math_t **out);

/* The face: tiku_office_math_font.c */

/* The MATH table's constants, in its order (OpenType 1.9, MathConstants). */
enum { MC_SCRIPT_PCT, MC_SCRIPTSCRIPT_PCT, MC_DELIM_MIN, MC_DISPLAY_OP_MIN,
       MC_LEADING, MC_AXIS, MC_ACCENT_BASE, MC_FLAT_ACCENT_BASE,
       MC_SUB_DOWN, MC_SUB_TOP_MAX, MC_SUB_DROP_MIN, MC_SUP_UP,
       MC_SUP_UP_CRAMPED, MC_SUP_BOTTOM_MIN, MC_SUP_DROP_MAX,
       MC_SUBSUP_GAP_MIN, MC_SUP_BOTTOM_MAX_SUB, MC_SPACE_AFTER_SCRIPT,
       MC_UPPER_GAP_MIN, MC_UPPER_RISE_MIN, MC_LOWER_GAP_MIN,
       MC_LOWER_DROP_MIN, MC_STACK_TOP_UP, MC_STACK_TOP_UP_D,
       MC_STACK_BOTTOM_DOWN, MC_STACK_BOTTOM_DOWN_D, MC_STACK_GAP,
       MC_STACK_GAP_D, MC_STRETCH_TOP_UP, MC_STRETCH_BOTTOM_DOWN,
       MC_STRETCH_GAP_ABOVE, MC_STRETCH_GAP_BELOW, MC_FRAC_NUM_UP,
       MC_FRAC_NUM_UP_D, MC_FRAC_DEN_DOWN, MC_FRAC_DEN_DOWN_D,
       MC_FRAC_NUM_GAP, MC_FRAC_NUM_GAP_D, MC_FRAC_RULE, MC_FRAC_DEN_GAP,
       MC_FRAC_DEN_GAP_D, MC_SKEW_HGAP, MC_SKEW_VGAP, MC_OVERBAR_GAP,
       MC_OVERBAR_RULE, MC_OVERBAR_EXTRA, MC_UNDERBAR_GAP, MC_UNDERBAR_RULE,
       MC_UNDERBAR_EXTRA, MC_RAD_GAP, MC_RAD_GAP_D, MC_RAD_RULE,
       MC_RAD_EXTRA, MC_RAD_KERN_BEFORE, MC_RAD_KERN_AFTER,
       MC_RAD_DEGREE_RAISE, MC_COUNT };

#define MF_PARTS 64                 /* glyphs one stretched shape takes  */

/** @brief A shape stretched along an axis: its glyphs, each from the
 *         start (the bottom, or the left), all in font units. */
typedef struct {
    int      n;
    int      built;                 /* assembled from parts, not a
                                       bigger glyph of its own           */
    unsigned gid[MF_PARTS];
    int      at[MF_PARTS];          /* where each starts                 */
    int      size;                  /* what they reach together          */
} mf_parts_t;

/** @brief The face equations are set in, and what its MATH table says. */
typedef struct {
    of_faces_t    *fs;
    int            face;            /* its index in fs; -1 none at all   */
    int            upem;
    int            has_math;        /* its MATH table was read           */
    int            c[MC_COUNT];     /* the constants, font units         */
    unsigned char *math;            /* the table's bytes                 */
    size_t         nmath;
    struct mf_ink *ink;             /* glyphs' extents, measured once    */
    int            nink, inkcap;
} mf_t;

/** @brief The math face for faces @p fs: @p want first when it has a
 *         MATH table, then the faces known to have one, else Times New
 *         Roman or its stand-in with TeX's lengths.  @return 0, or -1 when
 *         there is no face at all. */
int mf_open(mf_t *m, of_faces_t *fs, const char *want);
/** @brief What mf_open() took, given back. */
void mf_close(mf_t *m);
/** @brief Constant @p which at @p size points: a length, or a per cent. */
float mf_const(const mf_t *m, int which, float size);
/** @brief Glyph @p gid's extent in face @p face, font units, y up:
 *         x0 y0 x1 y1 into @p box; zeros for one with no ink. */
void mf_ink(mf_t *m, int face, unsigned gid, float *box);
/** @brief Glyph @p gid's italic correction, font units; 0 for none. */
int mf_italic(const mf_t *m, unsigned gid);
/** @brief Where an accent over glyph @p gid centres, from its origin, in
 *         font units; -1 when the table does not say. */
int mf_accent_at(const mf_t *m, unsigned gid);
/** @brief Glyph @p gid of the math face grown along an axis (@p vert:
 *         up, else across) to @p size font units: the first variant that
 *         reaches it, else its parts assembled, else its biggest variant.
 *         @return 0 with @p out filled, or -1 when the table has nothing
 *         for it. */
int mf_stretch(mf_t *m, unsigned gid, int vert, int size, mf_parts_t *out);
/** @brief The widest (or tallest) variant of glyph @p gid of the math
 *         face along an axis no bigger than @p size font units: as TeX
 *         takes an accent over a base.  @p gid itself when none is. */
unsigned mf_fit(const mf_t *m, unsigned gid, int vert, int size);

/* The typesetter's own: tiku_office_math.c and tiku_office_math_set.c */

enum { MI_GLYPH, MI_RULE, MI_PATH };
enum { MP_MOVE, MP_LINE, MP_CURVE };

/** @brief What draws a set equation: a glyph, a filled rule, a stroked
 *         path; points from the equation's baseline start, y down. */
struct of_mitem {
    unsigned char kind;             /* MI_*                              */
    unsigned char italic;           /* a glyph's slant, made up          */
    int           face;
    unsigned      gid;
    uint32_t      cp;               /* what the glyph stands for; 0 none */
    uint32_t      color;
    float         x, y;             /* a glyph's origin; a rule's top left */
    float         w, h;             /* a rule's extent; a path's width   */
    float         size;             /* a glyph's em                      */
    int           pt, npt;          /* a path's points in the list       */
};

/** @brief A piece set: its extent about its baseline, its items. */
typedef struct {
    float         w, asc, desc;
    float         ic;               /* its italic correction             */
    int           from, to;         /* its items                         */
    unsigned char lcls, rcls;       /* the classes at its two edges      */
    unsigned char glyph;            /* one glyph alone: scripts sit by it
                                       as by a character                 */
    unsigned char italic;           /* it ends in a slanted letter       */
    unsigned      gid;              /* that glyph                        */
} mb_t;

/** @brief The style a piece is set in: display, text, script or the
 *         script's script (0 to 3), cramped or not, and its em. */
typedef struct {
    int   level;
    int   cramped;
    float size;
} ms_t;

/** @brief An equation being set: its face, and the items and points made
 *         so far. */
typedef struct {
    mf_t            *f;
    struct of_mitem *it;
    int              nit, itcap;
    float           *pt;            /* paths' points: op, x, y each      */
    int              npt, ptcap;
    uint32_t         color;
    float            base;          /* the text style's em               */
    int              depth, bad;
} ml_t;

/** @brief Node @p n set in style @p st into @p out. */
void ml_set(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Nodes @p kid (@p nkid of them) set as a row: a binary
 *         operator with nothing to operate on taken as an ordinary atom,
 *         each spaced from the last as their classes say. */
void ml_row(ml_t *ml, of_mnode_t *const *kid, int nkid, const ms_t *st,
            mb_t *out);
/** @brief Characters @p text in variant @p v and class @p cls. */
void ml_text(ml_t *ml, const char *text, int v, uint32_t color, int cls,
             const ms_t *st, mb_t *out);
/** @brief Character @p cp grown along an axis (@p vert: up) to cover
 *         @p need points; a shape drawn instead when the face cannot. */
void ml_stretch(ml_t *ml, uint32_t cp, int vert, float need,
                const ms_t *st, mb_t *out);
/** @brief Character @p cp's widest variant no wider than @p room, as an
 *         accent sits over a base; its own glyph when the face has none. */
void ml_fit(ml_t *ml, uint32_t cp, float room, const ms_t *st, mb_t *out);
/** @brief A piece with nothing in it, where the items end. */
void ml_empty(ml_t *ml, mb_t *b);
/** @brief Piece @p b's items moved by (@p dx, @p dy). */
void ml_shift(ml_t *ml, mb_t *b, float dx, float dy);
/** @brief A filled rule, top left (@p x, @p y), @p w by @p h. */
void ml_rule(ml_t *ml, float x, float y, float w, float h);
/** @brief Constant @p which of the face at style @p st's size. */
float ml_c(const ml_t *ml, int which, const ms_t *st);
/** @brief The style a script, a fraction's parts, take below @p st;
 *         @p cramp cramps it as subscripts and denominators are. */
ms_t ml_smaller(const ml_t *ml, const ms_t *st, int cramp);
/** @brief Piece @p b, set just after @p into, put right of it with
 *         @p gap between; @p into grows to hold both. */
void ml_append(ml_t *ml, mb_t *into, mb_t *b, float gap);
/** @brief The items set since @p start put in the order of pieces @p seg
 *         (@p nseg of them): the order they are read and copied in. */
void ml_order(ml_t *ml, int start, const mb_t *seg, int nseg);

/* Structures: tiku_office_math_set.c */

/** @brief Fraction @p n set: its numerator over its denominator about the
 *         axis, a rule between unless OMF_NOBAR; along the line when
 *         OMF_LINEAR, across a slash when OMF_SKEW. */
void ml_frac(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Scripts @p n set: its base, sub- and superscripts after it and
 *         pre-scripts before it, clear of each other. */
void ml_scripts(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Root @p n set: its sign grown to the radicand, a rule over the
 *         radicand, the degree before the sign. */
void ml_root(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Large operator @p n set: its limits over and under it in a
 *         display, beside it otherwise, then its operand; grown to the
 *         operand when OMF_GROW. */
void ml_nary(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Fenced group @p n set: its parts between its fences, separators
 *         between the parts, the fences grown to what they hold when
 *         OMF_GROW. */
void ml_fence(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Matrix or array @p n set, centred on the axis: a matrix's cells
 *         as its columns align them, an array's rows cut at their marks
 *         and lined up there. */
void ml_grid(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);
/** @brief Accent, bar or brace @p n set over or under its base, or the
 *         limits and marks of an OM_UNDEROVER put under and over it. */
void ml_over(ml_t *ml, const of_mnode_t *n, const ms_t *st, mb_t *out);

#endif /* TIKU_OFFICE_MATH_H_ */
