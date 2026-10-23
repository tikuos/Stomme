/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_office_gdi.h - what the two Windows metafile readers share: the
 * device context their records change and draw through.  An EMF's
 * records (tiku_office_emf.c) and a WMF's (tiku_office_wmf.c) become
 * calls here, which keep GDI's state -- the window and viewport, the
 * world transform, pens, brushes and fonts, the clip, a path being
 * built -- and draw with the PDF writer in the picture's box.  The state
 * and paths are in tiku_office_gdi.c, shapes in tiku_office_gdi_shape.c,
 * text in tiku_office_gdi_text.c, bitmaps in tiku_office_gdi_dib.c.
 * Lengths the callers give are logical units.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_GDI_H_
#define TIKU_OFFICE_GDI_H_

#include "tiku_office_private.h"

#define GDI_OBJECTS 65536           /* a metafile's objects, at most     */
#define GDI_DEPTH   64              /* saved states kept                 */
#define GDI_STOCK   0x80000000u     /* an EMF's stock objects            */

enum { GDI_NONE, GDI_PEN, GDI_BRUSH, GDI_FONT, GDI_PALETTE, GDI_REGION,
       GDI_OTHER };
enum { GDI_MM_TEXT = 1, GDI_MM_LOMETRIC, GDI_MM_HIMETRIC,
       GDI_MM_LOENGLISH, GDI_MM_HIENGLISH, GDI_MM_TWIPS,
       GDI_MM_ISOTROPIC, GDI_MM_ANISOTROPIC };
enum { GDI_FILL = 1, GDI_STROKE = 2 };
enum { GDI_SHUT = 1, GDI_MORE = 2 };
enum { GDI_RGN_AND = 1, GDI_RGN_OR, GDI_RGN_XOR, GDI_RGN_DIFF,
       GDI_RGN_COPY };

/* Text options (ETO_*) and alignment (TA_*) as GDI numbers them. */
#define GDI_ETO_OPAQUE  0x0002
#define GDI_ETO_CLIPPED 0x0004
#define GDI_ETO_GLYPHS  0x0010
#define GDI_ETO_PDY     0x2000
#define GDI_TA_UPDATECP 0x0001
#define GDI_TA_RIGHT    0x0002
#define GDI_TA_CENTER   0x0006
#define GDI_TA_BOTTOM   0x0008
#define GDI_TA_BASELINE 0x0018

/** @brief An affine map, x' = a x + c y + e, y' = b x + d y + f. */
typedef struct {
    double a, b, c, d, e, f;
} gdi_mat_t;

/** @brief A pen: how lines are drawn. */
typedef struct {
    int      style;                 /* PS_*: kind, ends and joins        */
    double   width;                 /* logical units; 0 one pixel        */
    uint32_t color;                 /* ARGB                              */
    int      cosmetic;              /* one pixel, whatever the width     */
    double   dash[8];               /* a user style's, logical units     */
    int      ndash;
} gdi_pen_t;

/** @brief A brush: how shapes are filled. */
typedef struct {
    int      style;                 /* BS_*                              */
    int      hatch;                 /* HS_*                              */
    uint32_t color;                 /* ARGB; a pattern's average         */
} gdi_brush_t;

/** @brief A font as a record asks for it. */
typedef struct {
    double height;                  /* < 0 the em, > 0 the cell; logical */
    double width;                   /* characters' average; 0 natural    */
    int    escape;                  /* tenths of a degree, anticlockwise */
    int    weight;                  /* 400 regular, 700 bold             */
    int    italic, underline, strike;
    int    charset;                 /* 2: a symbol font's own codes      */
    char   face[96];                /* UTF-8                             */
} gdi_font_t;

/** @brief One slot of the object table. */
typedef struct {
    int kind;                       /* GDI_*                             */
    union {
        gdi_pen_t   pen;
        gdi_brush_t brush;
        gdi_font_t  font;
        struct {
            uint32_t *c;            /* a palette's colours, ARGB         */
            int       n;
        } pal;
        struct {
            double *r;              /* a region's boxes: l t r b ...     */
            int     n;
        } rgn;
    } u;
} gdi_obj_t;

/** @brief The state SaveDC keeps and RestoreDC gives back. */
typedef struct {
    int         map, gmode;         /* the mapping mode; GM_*            */
    double      wox, woy, wex, wey; /* the window: origin, extent        */
    double      vox, voy, vex, vey; /* the viewport                      */
    gdi_mat_t   world;              /* world to page                     */
    double      x, y;               /* the current position, logical     */
    gdi_pen_t   pen;
    gdi_brush_t brush;
    gdi_font_t  font;
    uint32_t    text, bk;           /* the text's colour, the ground's   */
    int         bkmode, align, fill, rop2, arcdir, pal;
    double      extra;              /* space after each character        */
    int         clip, meta;         /* the clip's newest step; the meta
                                       region's, where resets stop       */
} gdi_dc_t;

/** @brief A point of a path, in the box's points: move, line, one of a
 *         curve's three, or a close. */
typedef struct {
    float x, y;
    int   op;
} gdi_pt_t;

enum { GDI_MOVE, GDI_LINE, GDI_CURVE, GDI_CLOSE };

/** @brief A metafile being drawn. */
typedef struct {
    tiku_pdf_writer_t *w;
    of_faces_t        *fs;
    gdi_dc_t           dc;
    gdi_dc_t          *stack;       /* SaveDC's states                   */
    int                nstack, over; /* kept; saved past the room        */
    gdi_obj_t         *obj;         /* the object table                  */
    int                nobj;
    int                wmf;         /* the window spans the box          */
    double             box[4];      /* x y w h, points                   */
    gdi_mat_t          out;         /* an EMF's device pixels to the box */
    double             ppx, ppy;    /* its device's pixels per mm        */
    gdi_mat_t          m;           /* logical to the box, made          */
    int                made;        /* m is up to date                   */
    gdi_pt_t          *pt;          /* the path being built              */
    int                npt, ptcap;
    int                inpath;      /* inside BeginPath and EndPath      */
    int                fig;         /* a figure is open in the path      */
    int                wide;        /* WidenPath: fill it as a stroke    */
    struct gdi_clip   *clips;       /* the clip's steps, every one made  */
    int                nclip, clipcap;
    gdi_pt_t          *cpt;         /* their paths                       */
    int                ncpt, cptcap;
    int                applied;     /* the step the writer clips to      */
    int                level;       /* the writer holds a save for it    */
    long               budget;      /* points left to draw               */
    long               pixels;      /* and bitmaps' pixels to decode     */
    int                drawn;       /* something was drawn               */
} gdi_t;

/* The player: tiku_office_gdi.c */

/** @brief @p g made ready to draw into the box @p x, @p y, @p w, @p h
 *         with @p wr and the faces @p fs: GDI's first state, its window
 *         the box's when @p wmf, no objects. */
void gdi_begin(gdi_t *g, tiku_pdf_writer_t *wr, of_faces_t *fs, int wmf,
               double x, double y, double w, double h);
/** @brief The writer given back its state; everything freed. */
void gdi_end(gdi_t *g);
/** @brief Little-endian unsigned 16 bits of @p b (@p n bytes) at @p off;
 *         0 past its end. */
unsigned gdi_u16(const unsigned char *b, size_t n, size_t off);
/** @brief Little-endian signed 16 bits of @p b (@p n bytes) at @p off; 0
 *         past its end. */
int gdi_i16(const unsigned char *b, size_t n, size_t off);
/** @brief Little-endian unsigned 32 bits of @p b (@p n bytes) at @p off;
 *         0 past its end. */
uint32_t gdi_u32(const unsigned char *b, size_t n, size_t off);
/** @brief Little-endian signed 32 bits of @p b (@p n bytes) at @p off; 0
 *         past its end. */
int32_t gdi_i32(const unsigned char *b, size_t n, size_t off);
/** @brief A little-endian 32-bit float of @p b (@p n bytes) at @p off; 0
 *         past its end, or for one not finite or past 1e30. */
double gdi_f32(const unsigned char *b, size_t n, size_t off);
/** @brief Character @p cp written as UTF-8 into @p out (four bytes of
 *         room).  @return how many it took. */
size_t gdi_utf8(uint32_t cp, char *out);
/** @brief A COLORREF as ARGB: a palette's entry when it names one. */
uint32_t gdi_color(const gdi_t *g, uint32_t ref);
/** @brief Logical (@p x, @p y) in the box's points. */
void gdi_map(gdi_t *g, double x, double y, double *ox, double *oy);
/** @brief A point of the box back in logical units; left as it is when
 *         the mapping has no inverse. */
void gdi_unmap(gdi_t *g, double x, double y, double *lx, double *ly);
/** @brief The mapping changed: made again when next used. */
void gdi_moved(gdi_t *g);
/** @brief Slot @p i of the object table, grown to hold it; NULL past
 *         GDI_OBJECTS.  Whatever it held is given back first. */
gdi_obj_t *gdi_slot(gdi_t *g, int i);
/** @brief Object @p i selected: a stock one's when @p i has GDI_STOCK. */
void gdi_select(gdi_t *g, uint32_t i);
/** @brief Object @p i deleted. */
void gdi_delete(gdi_t *g, int i);
/** @brief SaveDC: the state kept; past GDI_DEPTH, only counted. */
void gdi_save(gdi_t *g);
/** @brief RestoreDC: the DC set back to the state saved @p n-th (from 1),
 *         or -@p n saves back when @p n is negative, and that save and the
 *         ones after it dropped.  A state saved past GDI_DEPTH was never
 *         kept: restoring it drops the count and leaves the DC as it is. */
void gdi_restore(gdi_t *g, int n);
/** @brief World transform @p x set, or multiplied in (@p how: 1 reset,
 *         2 first, 3 after, 4 set). */
void gdi_world(gdi_t *g, const gdi_mat_t *x, int how);

/* Paths and painting */

/** @brief A figure begun in the path at logical (@p x, @p y). */
void gdi_move(gdi_t *g, double x, double y);
/** @brief A line in the path to logical (@p x, @p y). */
void gdi_line(gdi_t *g, double x, double y);
/** @brief A Bézier in the path through @p p's three logical points. */
void gdi_curve(gdi_t *g, const double *p);
/** @brief The path's open figure shut. */
void gdi_close(gdi_t *g);
/** @brief The path built painted as @p what (GDI_FILL, GDI_STROKE) says,
 *         unless a path bracket is keeping it. */
void gdi_paint(gdi_t *g, int what);
/** @brief The path built filled with gradient @p gr, unless a path
 *         bracket is keeping it. */
void gdi_paint_gradient(gdi_t *g, const tiku_pdf_gradient_t *gr);
/** @brief Text's axes at @p angle radians anticlockwise, as seen, and
 *         upright whichever way the mapping turns y: a device unit along
 *         its baseline (@p a) and down its glyphs (@p b) in the box's
 *         points, and device units a logical one is along each (@p k).
 *         Glyphs keep their shape in the device, as GDI draws them, and
 *         stretch with the picture; a WMF's device is its logical space. */
void gdi_axes(gdi_t *g, double angle, double *a, double *b, double *k);
/** @brief @p open 1: BeginPath; 0: EndPath; 2: AbortPath. */
void gdi_path(gdi_t *g, int open);
/** @brief The bracket's path filled, stroked or both, then forgotten. */
void gdi_path_paint(gdi_t *g, int what);
/** @brief The bracket's path made the clip, combined as @p mode. */
void gdi_path_clip(gdi_t *g, int mode);
/** @brief Boxes @p r (l t r b ..., @p n of them) combined into the clip
 *         as @p mode; @p device: in device pixels, not logical units. */
void gdi_clip_boxes(gdi_t *g, const double *r, int n, int mode,
                    int device);
/** @brief The meta region set: resets stop at the clip as it is. */
void gdi_clip_meta(gdi_t *g);
/** @brief Boxes @p r (@p n of them; @p device: in device pixels) filled
 *         with @p b (NULL: the brush selected), or, when @p lw > 0, a frame
 *         @p lw wide drawn round their bounds in its colour. */
void gdi_boxes(gdi_t *g, const double *r, int n, const gdi_brush_t *b,
               int device, double lw);
/** @brief The writer's clip made the DC's, before drawing. */
void gdi_clip_apply(gdi_t *g);

/* Shapes: tiku_office_gdi_shape.c */

/** @brief Rectangle @p l, @p t, @p r, @p b as a figure, filled and
 *         outlined. */
void gdi_rect(gdi_t *g, double l, double t, double r, double b);
/** @brief The ellipse in box @p l, @p t, @p r, @p b as a figure, filled
 *         and outlined. */
void gdi_ellipse(gdi_t *g, double l, double t, double r, double b);
/** @brief Rectangle @p box (l t r b) with corners @p w by @p h round, as a
 *         figure, filled and outlined. */
void gdi_round(gdi_t *g, const double *box, double w, double h);
/** @brief An arc of the ellipse in @p box from the ray through @p p to
 *         the ray through @p q; @p kind 0 an arc, 1 ArcTo, 2 a chord, 3 a
 *         pie. */
void gdi_arc(gdi_t *g, const double *box, const double *p, const double *q,
             int kind);
/** @brief AngleArc: a line from the current position, then @p sweep
 *         degrees of the circle round (@p x, @p y) from @p from. */
void gdi_angle_arc(gdi_t *g, double x, double y, double r, double from,
                   double sweep);
/** @brief Points @p p (@p n pairs) as a polyline, a polygon (GDI_SHUT),
 *         from the current position (@p to); painted unless GDI_MORE. */
void gdi_poly(gdi_t *g, const double *p, int n, int flags, int to);
/** @brief Béziers: @p p's first point then threes; @p to: from the
 *         current position, all threes. */
void gdi_beziers(gdi_t *g, const double *p, int n, int to);
/** @brief PolyDraw: points @p p with GDI's PT_* @p types. */
void gdi_polydraw(gdi_t *g, const double *p, const unsigned char *types,
                  int n);
/** @brief A linear gradient over the box @p r, from @p c0 to @p c1,
 *         across (@p vertical 0) or down. */
void gdi_gradient(gdi_t *g, const double *r, uint32_t c0, uint32_t c1,
                  int vertical);

/* Text: tiku_office_gdi_text.c */

/** @brief One string of text as ExtTextOut draws it. */
typedef struct {
    double          x, y;           /* the reference point, logical      */
    const uint32_t *ch;             /* its characters, or glyphs         */
    int             n;
    const double   *dx;             /* each one's advance, or NULL       */
    const double   *dy;             /* ETO_PDY: each one's rise          */
    int             opts;           /* ETO_*                             */
    double          rect[4];        /* the opaque or clip box, l t r b   */
} gdi_text_t;

/** @brief Text @p t drawn in the font, colour and ground selected. */
void gdi_text(gdi_t *g, const gdi_text_t *t);
/** @brief Byte @p c of the font's character set as Unicode. */
uint32_t gdi_char(const gdi_font_t *f, unsigned c);

/* Bitmaps: tiku_office_gdi_dib.c */

/** @brief A bitmap to draw and where from: its header and bits. */
typedef struct {
    const unsigned char *bmi, *bits;
    size_t               nbmi, nbits;
    int                  usage;     /* 1: its table is palette indices   */
    int                  mono;      /* a WMF's device bitmap, 1 bit      */
} gdi_dib_t;

/** @brief How one is drawn: the box, the part of the bitmap, the raster
 *         operation and what AlphaBlend and TransparentBlt add. */
typedef struct {
    double   p[6];                  /* the box's corners: top left, top
                                       right, bottom left; logical       */
    double   src[4];                /* x y w h in pixels; w 0: all of it */
    int      from_bottom;           /* src's y counts from the bottom    */
    uint32_t rop;                   /* SRCCOPY ...                       */
    int      alpha;                 /* 0-255 over it all; -1 none        */
    int      premul;                /* its pixels carry alpha, multiplied */
    int      keyed;                 /* key is see-through                */
    uint32_t key;                   /* a COLORREF                        */
} gdi_blit_t;

/** @brief Bitmap @p d drawn as @p b says; with @p mask (may be NULL) as
 *         MaskBlt's.  No bitmap: the brush, black or white fills it. */
void gdi_blit(gdi_t *g, const gdi_dib_t *d, const gdi_blit_t *b,
              const gdi_dib_t *mask);
/** @brief The bytes of a DIB's header and colours at @p bmi, where its
 *         bits start; 0 for one this cannot read. */
size_t gdi_dib_size(const unsigned char *bmi, size_t n, int usage);
/** @brief A pattern brush's colour: its bitmap's average. */
uint32_t gdi_dib_average(gdi_t *g, const gdi_dib_t *d);

/* The readers: tiku_office_emf.c, tiku_office_wmf.c */

/** @brief An EMF drawn into @p box (x y w h).  @return 0, or -1 when it
 *         cannot be read, or holds EMF+ alone and draws nothing. */
int of_emf_draw(const unsigned char *b, size_t n, of_faces_t *fs,
                tiku_pdf_writer_t *w, const double *box);
/** @brief A WMF, placeable or not, drawn into @p box (x y w h): the EMF
 *         it carries when it carries one.  @return 0, or -1 when it
 *         cannot be read or has no extent. */
int of_wmf_draw(const unsigned char *b, size_t n, of_faces_t *fs,
                tiku_pdf_writer_t *w, const double *box);

#endif /* TIKU_OFFICE_GDI_H_ */
