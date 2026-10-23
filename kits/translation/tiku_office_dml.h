/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_dml.h - DrawingML, as slides and Word's drawings use it:
 * a theme's colours, fonts and formats; colours with their modifiers;
 * fills, lines and their ends; preset and custom geometry as paths; and
 * shapes, pictures and groups drawn through their transforms.  What a
 * shape's text is, and how it is set, is its caller's: a slide's text
 * frame and a Word text box are different things in the same box.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_DML_H_
#define TIKU_OFFICE_DML_H_

#include "tiku_office_private.h"

enum { DML_DK1, DML_LT1, DML_DK2, DML_LT2, DML_ACCENT1, DML_ACCENT2,
       DML_ACCENT3, DML_ACCENT4, DML_ACCENT5, DML_ACCENT6, DML_HLINK,
       DML_FOLHLINK, DML_COLORS };

/** @brief A theme: its colours, fonts and the formats styles refer to. */
typedef struct {
    uint32_t               clr[DML_COLORS];  /* dk1 lt1 dk2 lt2 accent1-6 ... */
    char                   major[64];        /* headings' Latin face     */
    char                   minor[64];        /* body text's              */
    char                   major_ea[64];     /* and East Asian ones      */
    char                   minor_ea[64];
    const tiku_xml_node_t *fills;            /* a:fillStyleLst           */
    const tiku_xml_node_t *lines;            /* a:lnStyleLst             */
    const tiku_xml_node_t *bgfills;          /* a:bgFillStyleLst         */
    tiku_xml_t            *xml;              /* the part they are in     */
    char                  *part;             /* its name                 */
} dml_theme_t;

/** @brief A 2-D transform, PDF's way: x' = a x + c y + e, y' = b x + d y
 *         + f, in points on the page, y down. */
typedef struct {
    double a, b, c, d, e, f;
} dml_mat_t;

enum { DML_NOFILL, DML_SOLID, DML_GRADIENT, DML_PICTURE, DML_PATTERN,
       DML_GROUPFILL };

enum { DML_TONE_NONE, DML_TONE_GREY, DML_TONE_DUO };

/** @brief What a picture's a:blip does to it before it is shown. */
typedef struct {
    int      tone;                  /* DML_TONE_*                        */
    uint32_t ink[2];                /* a duotone's, for black and white  */
    float    bright, contrast;      /* a:lum's, -1 to 1                  */
    int      change;                /* whether a:clrChange swaps one     */
    uint32_t from, to;              /* its colours                       */
    float    alpha;                 /* opacity, 0 to 1                   */
} dml_blip_t;

/** @brief A fill, resolved. */
typedef struct {
    int             kind;           /* DML_*                             */
    uint32_t        color;          /* a solid one's, a pattern's ink    */
    uint32_t        back;           /* a pattern's ground                */
    tiku_pdf_stop_t stop[16];       /* a gradient's                      */
    int             nstop;
    int             radial;         /* a path gradient: out from a point */
    double          angle;          /* a linear one's, degrees clockwise */
    char            picture[256];   /* a picture fill's part             */
    float           crop[4];        /* cut away: left top right bottom   */
    dml_blip_t      fx;             /* and what is done to it            */
} dml_fill_t;

enum { DML_END_NONE, DML_END_TRIANGLE, DML_END_STEALTH, DML_END_DIAMOND,
       DML_END_OVAL, DML_END_ARROW };

/** @brief A line, resolved: its width, ink, dash, ends and joins. */
typedef struct {
    float    width;                 /* points; 0: no line                */
    uint32_t color;                 /* ARGB                              */
    double   dash[8];               /* on, off ... in line widths        */
    int      ndash;
    int      cap, join;             /* PDF's numbers                     */
    int      head, tail;            /* DML_END_* at its start and end    */
    float    head_w, head_l;        /* their size, in line widths        */
    float    tail_w, tail_l;
} dml_line_t;

/** @brief One subpath of a geometry: its points in the shape's own
 *         space, and whether it is filled and stroked. */
typedef struct {
    int    fill, stroke;            /* a:path's own say                  */
    float *pt;                      /* 'M' x y, 'L' x y, 'C' 3 points, 'Z' */
    int    n, cap;                  /* floats used, and room             */
} dml_sub_t;

/** @brief A shape's outline: subpaths, and where its text goes. */
typedef struct {
    dml_sub_t sub[8];               /* a:path elements, in order         */
    int       nsub;
    float     text[4];              /* the text box: left top right bottom */
} dml_geom_t;

/** @brief How text inside a shape is set, the caller's way: in @p box
 *         (points, in the shape's own space, @p m placing it). */
typedef int (*dml_text_fn)(void *ctx, const tiku_xml_node_t *shape,
                           const float *box, const dml_mat_t *m);

/** @brief Drawing a slide or a Word drawing: where from, into what. */
typedef struct {
    of_pkg_t           *pkg;        /* pictures come from here           */
    const char         *part;       /* resolved against this part's rels */
    const dml_theme_t  *theme;
    unsigned char       map[16];    /* bg1, tx1, bg2, tx2, accent1-6,
                                       hlink, folHlink: theme colours    */
    tiku_pdf_writer_t  *w;
    of_faces_t         *fs;
    of_doc_t           *d;          /* looks and memory for text         */
    uint32_t            ph;         /* phClr: a style reference's colour */
    dml_text_fn         text;       /* a shape's text                    */
    void               *text_ctx;
    struct dml_pic     *pics;       /* pictures given to the writer      */
    int                 npics, piccap;
    int                 depth;      /* groups within groups              */
    int               (*frame)(void *ctx, const tiku_xml_node_t *e,
                               const dml_mat_t *m);  /* a graphic frame
                                       the caller draws itself: a table  */
    void               *frame_ctx;
    float               text_pt;    /* a chart's text where it says none */
    float               fit_w, fit_h;  /* what a shape with no place of
                                       its own fills, at the top: Word's */
} dml_ctx_t;

/* Colours and the theme: tiku_office_dml_color.c */

/** @brief The theme part @p part read into @p t, over Office's own theme,
 *         which @p t keeps when there is none.  @return 0, or -1 for none. */
int dml_theme_load(of_pkg_t *pkg, const char *part, dml_theme_t *t);
/** @brief Theme @p base with the schemes of override part @p part (a
 *         chart's a:themeOverride) over it, into @p t, which owns only the
 *         override.  @return 0, or -1 for none. */
int dml_theme_override(of_pkg_t *pkg, const char *part,
                       const dml_theme_t *base, dml_theme_t *t);
/** @brief A theme's part given back. */
void dml_theme_free(dml_theme_t *t);
/** @brief A colour map (p:clrMap, or an override's a:overrideClrMapping)
 *         read into the context; NULL for the usual one. */
void dml_map_read(dml_ctx_t *c, const tiku_xml_node_t *map);
/** @brief The colour an element holds (its a:srgbClr, a:schemeClr ...
 *         child), modifiers applied.  0 when it holds none. */
uint32_t dml_color(const dml_ctx_t *c, const tiku_xml_node_t *parent);
/** @brief The colour element @p e itself resolved; 0 when it is not one. */
uint32_t dml_color_of(const dml_ctx_t *c, const tiku_xml_node_t *e);
/** @brief Colour @p argb with its luminance times @p mod, plus @p off:
 *         a:lumMod and a:lumOff, as a palette darkens and lightens. */
uint32_t dml_lum(uint32_t argb, double mod, double off);
/** @brief A theme font name as a face: "+mj-lt" the headings', "+mn-lt"
 *         the body's, "+mj-ea" and "+mn-ea" their East Asian ones; NULL for
 *         a theme font the theme lacks, @p name itself for any other. */
const char *dml_font(const dml_ctx_t *c, const char *name);

/* Geometry: tiku_office_dml_geom.c */

/** @brief The geometry of @p sppr (its a:prstGeom or a:custGeom) at
 *         @p w by @p h points: a rectangle when it gives none.
 *  @return 0, or -1 out of memory. */
int dml_geometry(const tiku_xml_node_t *sppr, float w, float h,
                 dml_geom_t *g);
/** @brief A geometry's subpaths given back, even one half built. */
void dml_geom_free(dml_geom_t *g);


/* Drawing: tiku_office_dml_draw.c */

/** @brief The fill @p e (spPr, bg, tcPr ...) states, into @p f.
 *  @return 1 when it states one, 0 when it says nothing. */
int dml_fill(const dml_ctx_t *c, const tiku_xml_node_t *e, dml_fill_t *f);
/** @brief The line @p ln (an a:ln) states, over @p l.  @return 1 when
 *         there was one. */
int dml_line(const dml_ctx_t *c, const tiku_xml_node_t *ln, dml_line_t *l);
/** @brief A fill reference (a:fillRef; a:bgRef when @p bg) looked up in
 *         the theme: the fill it names into @p f, its colour as phClr.
 *  @return 1 when it names one. */
int dml_ref_fill(dml_ctx_t *c, const tiku_xml_node_t *ref, int bg,
                 dml_fill_t *f);
/** @brief The same for a line: the one a:lnRef @p ref names, into @p l. */
int dml_ref_line(dml_ctx_t *c, const tiku_xml_node_t *ref, dml_line_t *l);
/** @brief Fill @p f over the path of geometry @p g through @p m, within
 *         the box @p w by @p h it fills. */
void dml_paint_fill(dml_ctx_t *c, const dml_fill_t *f, const dml_geom_t *g,
                    const dml_mat_t *m, float w, float h);
/** @brief Line @p l along geometry @p g through @p m, with its ends. */
void dml_paint_line(dml_ctx_t *c, const dml_line_t *l, const dml_geom_t *g,
                    const dml_mat_t *m);

/* Pictures: tiku_office_dml_pic.c */

/** @brief What a:blip @p blip does to its picture, into @p fx. */
void dml_blip(const dml_ctx_t *c, const tiku_xml_node_t *blip,
              dml_blip_t *fx);
/** @brief A picture part given to the writer, once for each way @p fx
 *         (may be NULL) changes it.  @return its number in the writer;
 *         -1 when it cannot be read, or is a metafile. */
int dml_picture(dml_ctx_t *c, const char *part, const dml_blip_t *fx);
/** @brief Part @p part's bytes when dml_picture() found it a metafile,
 *         their length in @p len; NULL when it was not. */
const unsigned char *dml_metafile(const dml_ctx_t *c, const char *part,
                                  size_t *len);

/* Shapes and frames: tiku_office_dml_draw.c; dml_chart() is in
 * tiku_office_chart.c and dml_done() in tiku_office_dml_pic.c */

/** @brief The transform of an a:xfrm within @p parent: its offset, size,
 *         turn and flips, into @p m and its size into @p w, @p h.  For a
 *         group, @p child gets the transform its children's space takes. */
void dml_xfrm(const tiku_xml_node_t *xfrm, const dml_mat_t *parent,
              dml_mat_t *m, float *w, float *h, dml_mat_t *child);
/** @brief @p m then @p n: the transform that does both. */
dml_mat_t dml_mul(const dml_mat_t *m, const dml_mat_t *n);
/**
 * @brief A shape tree's element drawn: a shape, a picture, a group, a
 *        connector, whatever namespace names it; its fill and line from
 *        its properties, else its style's references, @p inherit giving
 *        a placeholder's properties to fall back on (may be NULL).
 */
int dml_draw(dml_ctx_t *c, const tiku_xml_node_t *e, const dml_mat_t *m,
             const tiku_xml_node_t *inherit);
/**
 * @brief A diagram (SmartArt) in graphic data @p gd drawn through @p m
 *        from the drawing its office suite saved with it: the shapes it
 *        last laid the diagram out as, their text as the context sets it.
 * @return 1 when drawn, 0 when it has no drawing, -1 out of memory.
 */
int dml_diagram(dml_ctx_t *c, const tiku_xml_node_t *gd, const dml_mat_t *m);
/**
 * @brief A chart in graphic data @p gd drawn through @p m in its frame of
 *        @p w by @p h points, from the values its part keeps; its text
 *        the context's text_pt where the part does not say.
 * @return 1 when drawn, 0 for a chart it cannot draw, -1 out of memory.
 */
int dml_chart(dml_ctx_t *c, const tiku_xml_node_t *gd, const dml_mat_t *m,
              float w, float h);
/** @brief A graphic frame @p e drawn through @p m: a chart from its
 *         values, a diagram from its saved drawing, an embedded object
 *         from the picture kept of it, else a grey box of its place.
 *  @return 0, or -1 out of memory. */
int dml_frame(dml_ctx_t *c, const tiku_xml_node_t *e, const dml_mat_t *m);
/** @brief The same, saying what was shown.  @return 1 for the frame's
 *         content, 0 for the grey box or for a frame with no room, -1
 *         out of memory. */
int dml_frame_shown(dml_ctx_t *c, const tiku_xml_node_t *e,
                    const dml_mat_t *m);
/** @brief The context's own memory given back. */
void dml_done(dml_ctx_t *c);

#endif /* TIKU_OFFICE_DML_H_ */
