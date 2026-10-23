/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_docx.h - what the .docx reader's parts share: properties as
 * WordprocessingML states them, each only where set, so that defaults,
 * table styles, paragraph and character styles and direct formatting can
 * be laid over each other in Word's order; the styles and lists read
 * from their parts; and the reader's state while it walks the body.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_DOCX_H_
#define TIKU_OFFICE_DOCX_H_

#include "tiku_office_dml.h"

enum {                              /* which run properties are set      */
    RP_FONT = 1u << 0, RP_FONT_EA = 1u << 1, RP_SIZE = 1u << 2,
    RP_COLOR = 1u << 3, RP_BACK = 1u << 4, RP_BOLD = 1u << 5,
    RP_ITALIC = 1u << 6, RP_CAPS = 1u << 7, RP_SMALLCAPS = 1u << 8,
    RP_HIDDEN = 1u << 9, RP_UNDERLINE = 1u << 10, RP_STRIKE = 1u << 11,
    RP_VERT = 1u << 12, RP_SPACING = 1u << 13, RP_RISE = 1u << 14,
    RP_SCALE = 1u << 15
};

/** @brief Run properties, each only where @c set says so. */
typedef struct {
    unsigned      set;              /* RP_*                              */
    char          font[64];         /* a family, or "+minor", "+major"   */
    char          font_ea[64];      /* the same for East Asian text      */
    float         size;             /* points                            */
    uint32_t      color, back;      /* ARGB                              */
    unsigned char bold, italic, caps, smallcaps, hidden;
    unsigned char underline, strike;  /* OF_UL_*; 1 or 2               */
    signed char   vert;             /* 1 super, -1 sub, 0 baseline       */
    float         spacing, rise;    /* points                            */
    float         scale;            /* 1 as drawn                        */
} dx_rpr_t;

enum {                              /* which paragraph properties are set */
    PP_ALIGN = 1u << 0, PP_LEFT = 1u << 1, PP_RIGHT = 1u << 2,
    PP_FIRST = 1u << 3, PP_BEFORE = 1u << 4, PP_AFTER = 1u << 5,
    PP_LINE = 1u << 6, PP_KEEP_NEXT = 1u << 7, PP_KEEP_LINES = 1u << 8,
    PP_BREAK = 1u << 9, PP_CONTEXT = 1u << 10, PP_WIDOWS = 1u << 11,
    PP_OUTLINE = 1u << 12, PP_FILL = 1u << 13, PP_BORDER = 1u << 14,
    PP_NUM = 1u << 15, PP_TABS = 1u << 16
};

#define DX_TABS 32                  /* tab stops one paragraph keeps     */
#define DX_FIELDS 8                 /* fields nested in fields           */

/** @brief Paragraph properties, each only where @c set says so. */
typedef struct {
    unsigned      set;              /* PP_*                              */
    unsigned char align;            /* OF_*                              */
    float         left, right, first;
    float         before, after;
    unsigned char line_rule;        /* OF_LINE_*                         */
    float         line;
    unsigned char keep_next, keep_lines, brk, contextual, widows;
    signed char   outline;          /* 0-8; 9 none                       */
    uint32_t      fill;             /* ARGB                              */
    of_border_t   border[4];        /* top, left, bottom, right          */
    int           num_id, num_lvl;  /* its list, and level               */
    of_tab_t      tab[DX_TABS];     /* stops set; align 0xff clears one  */
    int           ntab;
} dx_ppr_t;

/** @brief A table's properties, as its style or itself states them. */
typedef struct {
    int         has_border[6];      /* which of @c border are set        */
    of_border_t border[6];          /* top left bottom right, inside h, v */
    int         has_margin[4];
    float       margin[4];          /* cells': top left bottom right     */
    uint32_t    fill;               /* cells' shading                    */
} dx_tpr_t;

enum { TS_WHOLE, TS_FIRST_ROW, TS_LAST_ROW, TS_FIRST_COL, TS_LAST_COL,
       TS_BAND1_H, TS_BAND2_H, TS_BAND1_V, TS_BAND2_V, TS_KINDS };

/** @brief One style of styles.xml, its chain resolved when first used. */
typedef struct {
    char     id[64];                /* w:styleId                         */
    char     based[64];             /* the style it is based on          */
    char     name[64];              /* as Word names it: "heading 1"     */
    char     type;                  /* 'p', 'c', 't' or 'n'              */
    int      dflt;                  /* the type's default                */
    dx_ppr_t ppr;                   /* its own                           */
    dx_rpr_t rpr;
    dx_tpr_t tpr[TS_KINDS];         /* a table style's, by the part of   */
    dx_rpr_t trpr[TS_KINDS];        /* the table it formats              */
    int      has[TS_KINDS];         /* which parts it formats            */
    int      done;                  /* resolved: 0 no, 1 busy, 2 yes     */
    dx_ppr_t all_ppr;               /* with what it is based on          */
    dx_rpr_t all_rpr;
} dx_style_t;

/** @brief One level of a list: how its label is written and indented. */
typedef struct {
    int      start;                 /* the first number                  */
    int      fmt;                   /* OF_NUM_*, or -1 bullet, -2 none   */
    char     text[48];              /* "%1.%2." or a bullet              */
    int      follow;                /* 0 tab, 1 space, 2 nothing         */
    int      legal;                 /* numbers from upper levels decimal */
    int      restart;               /* restarts after this level; -1 any */
    dx_ppr_t ppr;                   /* its indent and stops              */
    dx_rpr_t rpr;                   /* the label's look                  */
} dx_level_t;

/** @brief A list: an abstract numbering, as a w:num names it. */
typedef struct {
    int        id;                  /* w:numId                           */
    int        abstract;            /* w:abstractNumId                   */
    dx_level_t lvl[9];              /* the abstract's levels, overridden */
    int        count[9];            /* where each level's count is       */
    int        own;                 /* it restarts: counts of its own    */
} dx_list_t;

/** @brief The reader, as it walks a part. */
typedef struct {
    of_doc_t     *d;                /* the document built                */
    of_pkg_t     *pkg;              /* its package                       */
    const char   *part;             /* the part being read, for links    */
    dx_style_t   *style;            /* styles.xml's                      */
    int           nstyle;
    dx_rpr_t      def_rpr;          /* the document's defaults           */
    dx_ppr_t      def_ppr;
    dx_list_t    *list;             /* numbering.xml's, by w:num         */
    int           nlist;
    char          minor[64];        /* the theme's body font             */
    char          major[64];        /* and its headings' font            */
    char          minor_ea[64];
    char          major_ea[64];
    tiku_xml_t   *notes;            /* footnotes.xml, to take notes from */
    tiku_xml_t   *endnotes;         /* endnotes.xml                      */
    int           nnotes;           /* notes numbered so far             */
    of_vec_t      endnote;          /* of_story_t *: in reference order  */
    const dx_style_t *table;        /* the table style text is in        */
    int           table_part;       /* the part of it: TS_*              */
    int           nfield;           /* fields open, the outermost first  */
    unsigned char fstate[DX_FIELDS];/* each: 1 its code, 2 its result,
                                       3 a result not shown              */
    const char   *flink[DX_FIELDS]; /* each HYPERLINK field's target     */
    char          code[256];        /* the innermost field's code        */
    const char   *mark_text;        /* what a note's own mark reads      */
    const char   *note_part;        /* the notes' part, for their links  */
    const char   *endnote_part;
    int           depth;            /* tables in cells, boxes in boxes   */
    of_vec_t      marks;            /* bookmarks between paragraphs      */
    of_vec_t      notes_v;          /* of_story_t *: footnotes in order  */
    of_story_t   *header[3];        /* the section's, carried on         */
    of_story_t   *footer[3];
    int           arts;             /* drawings made, as DrawingML       */
} dx_t;

/* Properties: tiku_office_docx_style.c */

/** @brief @p e (a w:rPr) read into @p r over what it holds. */
void dx_rpr_read(dx_t *x, const tiku_xml_node_t *e, dx_rpr_t *r);
/** @brief @p e (a w:pPr) read into @p p over what it holds. */
void dx_ppr_read(dx_t *x, const tiku_xml_node_t *e, dx_ppr_t *p);
/** @brief @p e (a w:tblPr or w:tcPr's borders) read into @p t. */
void dx_tpr_read(const tiku_xml_node_t *e, dx_tpr_t *t);
/** @brief What is set in run properties @p src laid over @p dst. */
void dx_rpr_over(dx_rpr_t *dst, const dx_rpr_t *src);
/** @brief What is set in paragraph properties @p src laid over @p dst;
 *         its tab stops merged into @p dst's, a clear taking one away. */
void dx_ppr_over(dx_ppr_t *dst, const dx_ppr_t *src);
/** @brief The style @p id of type @p type ('p', 'c', 't'), or its type's
 *         default for NULL or an id not found; resolved with what it is
 *         based on. */
const dx_style_t *dx_style(dx_t *x, const char *id, char type);
/** @brief styles.xml, the theme's fonts and numbering.xml read. */
void dx_styles_load(dx_t *x, const char *main);
/** @brief A run's look: the defaults, the table's, paragraph style
 *         @p pstyle's, character style @p rstyle's and its own @p own,
 *         in that order.  NULL out of memory. */
const of_style_t *dx_look(dx_t *x, const char *pstyle, const char *rstyle,
                          const dx_rpr_t *own);
/** @brief Paragraph @p p's layout from the defaults, the table's, style
 *         @p pstyle's, its list level's and its own @p own; its list's
 *         label made, in its mark's look @p mark. */
void dx_layout(dx_t *x, const char *pstyle, const dx_ppr_t *own,
               const dx_rpr_t *mark, of_para_t *p);
/** @brief A border element (w:top ...) read: width 0 for none. */
void dx_border(const tiku_xml_node_t *e, of_border_t *b);
/** @brief A measure as points: twentieths of a point, or a number with
 *         pt, in, cm, mm, pc or pi after it; @p dflt when absent. */
float dx_twips(const char *v, float dflt);
/** @brief Whether an on/off property is on: present, and not "0",
 *         "false", "off" or "none". */
int dx_on(const tiku_xml_node_t *e);
/** @brief "RRGGBB" as ARGB; 0 for "auto" or nonsense. */
uint32_t dx_color(const char *v);

/* Drawings and tables: tiku_office_docx_draw.c */

/** @brief A w:drawing, w:pict or w:object read as a run: a picture in the
 *         text, or a float.  @return 0 when it was one; -1 for nothing to
 *         draw. */
int dx_drawing(dx_t *x, const tiku_xml_node_t *e, of_run_t *run);
/** @brief A w:tbl read into @p t.  @return 0, or -1 out of memory. */
int dx_table(dx_t *x, const tiku_xml_node_t *e, of_table_t *t);
/** @brief Property @p key of a VML or CSS style, into @p out ("" when it
 *         is not there). */
void dx_css(const char *style, const char *key, char *out, size_t max);
/** @brief A VML colour -- a name, or "#rrggbb" with Word's own note after
 *         it -- as ARGB; @p dflt when it is neither. */
uint32_t dx_vml_color(const char *v, uint32_t dflt);

/* Drawings drawn as DrawingML: tiku_office_docx_art.c */

/** @brief Whether a:graphicData @p gd is drawn as DrawingML: shapes,
 *         groups, a canvas, a diagram, a chart, or a picture cut to a
 *         shape, turned, mirrored, outlined or recoloured. */
int dx_art_wants(const tiku_xml_node_t *gd);
/** @brief @p gd, @p w by @p h points, made a drawing in @p f: what is
 *         drawn and the text boxes in it.  @return 0, or -1 out of memory. */
int dx_art(dx_t *x, const tiku_xml_node_t *gd, float w, float h,
           of_float_t *f);
/** @brief Part @p xml, read: kept with the document when drawings were
 *         made from it (more than @p since of them), else freed. */
void dx_art_keep(dx_t *x, tiku_xml_t *xml, int since);

/* The body: tiku_office_docx.c */

/** @brief The blocks of @p e (a body, a cell, a header, a text box).
 *  @return the story, or NULL out of memory. */
of_story_t *dx_story(dx_t *x, const tiku_xml_node_t *e);
/** @brief The same for text that stands apart from where it is named --
 *         a note, a header, a text box: read in part @p part (NULL: the
 *         same part), outside any table or field around it. */
of_story_t *dx_story_apart(dx_t *x, const tiku_xml_node_t *e,
                           const char *part);

#endif /* TIKU_OFFICE_DOCX_H_ */
