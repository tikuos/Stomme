/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_office_chart.h - what the chart drawer's parts share: a chart as
 * its values and looks, whatever file it came from -- groups of series of
 * one kind (bars, lines, areas, pies, points), the axes they are measured
 * against, a title and a legend.  DrawingML's chart parts are read in
 * tiku_office_chart_read.c, OpenDocument's in tiku_office_chart_odf.c;
 * charts are laid out and drawn in tiku_office_chart.c, their axes in
 * tiku_office_chart_axis.c, their series in tiku_office_chart_plot.c, a
 * radar's in tiku_office_chart_radar.c, trendlines in
 * tiku_office_chart_trend.c, error bars in tiku_office_chart_err.c, data
 * tables in tiku_office_chart_table.c, numbers by tiku_office_chart_num.c;
 * all with the pen of tiku_office_chart_ink.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_OFFICE_CHART_H_
#define TIKU_OFFICE_CHART_H_

#include "tiku_office_dml.h"

#define CH_AXES    8                /* axes one chart measures against   */
#define CH_SERIES  64               /* series one chart shows, at most   */
#define CH_POINTS  4096             /* and points one series shows       */
#define CH_FMT     64               /* a number format's bytes, its end  */

enum { CH_BAR, CH_LINE, CH_AREA, CH_PIE, CH_DOUGHNUT, CH_SCATTER, CH_RADAR };
enum { CH_CLUSTERED, CH_STACKED, CH_PERCENT, CH_STANDARD };
enum { CH_CAT_AX, CH_VAL_AX };
enum { CH_MK_NONE, CH_MK_CIRCLE, CH_MK_SQUARE, CH_MK_DIAMOND,
       CH_MK_TRIANGLE, CH_MK_X, CH_MK_PLUS, CH_MK_DASH };
enum { CH_LBL_VAL = 1, CH_LBL_PCT = 2, CH_LBL_CAT = 4, CH_LBL_SER = 8 };
enum { CH_AT_OUT, CH_AT_IN, CH_AT_CENTRE, CH_AT_BASE, CH_AT_ABOVE,
       CH_AT_BELOW, CH_AT_LEFT, CH_AT_RIGHT };
enum { CH_TR_LINEAR, CH_TR_EXP, CH_TR_LOG, CH_TR_POLY, CH_TR_POWER,
       CH_TR_AVERAGE };
enum { CH_EB_BOTH, CH_EB_PLUS, CH_EB_MINUS };
enum { CH_EV_FIXED, CH_EV_PERCENT, CH_EV_STDDEV, CH_EV_STDERR, CH_EV_CUSTOM };

/** @brief How a chart's words look. */
typedef struct {
    char     font[64];              /* a family, or "+mn-lt" and kin     */
    float    size;                  /* points                            */
    uint32_t color;                 /* ARGB                              */
    int      bold, italic;
} ch_font_t;

/** @brief A trendline fitted to a series' points. */
typedef struct {
    int         kind;               /* CH_TR_*                           */
    int         order;              /* a polynomial's, 2 to 6            */
    int         period;             /* a moving average's, in points     */
    double      fwd, back;          /* drawn on past the points, in x    */
    int         has_icpt;           /* it must cross x = 0 ...           */
    double      icpt;               /* ... here                          */
    dml_line_t  line;
    int         has_line;
    const char *name;               /* the legend's words, or NULL       */
    int         eq, r2;             /* its label says its equation, R^2  */
    float       lbl_dx, lbl_dy;     /* the label moved by hand: fractions
                                       of the chart across and down      */
    const char *lbl_fmt;            /* its numbers' format, or NULL      */
    ch_font_t   lbl_font;
} ch_trend_t;

/** @brief A series' error bars along one direction. */
typedef struct {
    int         dir;                /* 'x' or 'y'                        */
    int         type;               /* CH_EB_*                           */
    int         kind;               /* CH_EV_*                           */
    double      val;                /* a fixed amount, a percent, how many
                                       deviations                       */
    double     *plus, *minus;       /* a custom one's amounts, or NULL   */
    int         nplus, nminus;
    int         cap;                /* the ends have caps                */
    dml_line_t  line;
    double      mean, sd;           /* the series' values' mean and
                                       standard deviation                */
    int         count;              /* how many values they are over     */
} ch_err_t;

/** @brief One point's label as the file sets it apart from its series'. */
typedef struct {
    int         idx;                /* the point                         */
    int         labels;             /* CH_LBL_*; 0: taken away           */
    int         at;                 /* CH_AT_*; -1 its series'           */
    const char *text;               /* words of its own, or NULL         */
    const tiku_xml_node_t *rich;    /* and their fields, when they have
                                       some: its value, its category ... */
    const tiku_xml_node_t *sppr;    /* its box's ground and edge, or NULL */
    ch_font_t   font;               /* how its words look                */
    const char *sep;                /* between its parts, or NULL        */
    const char *fmt;                /* its number's format, or NULL      */
    float       dx, dy;             /* moved by hand: fractions of the
                                       chart across and down             */
} ch_label_t;

/** @brief One series: its name, its values, and how it is drawn. */
typedef struct {
    const char  *name;              /* the legend's words for it         */
    int          idx;               /* its turn in the palette           */
    int          n;                 /* its points                        */
    double      *v;                 /* their values; NAN where none      */
    double      *x;                 /* a scatter's x values, or NULL     */
    const char **cat;               /* its categories' names, or NULL    */
    const char **outer[2];          /* and the groups they are in        */
    int          nouter;
    double      *catv;              /* the same as numbers, or NULL      */
    char         cat_fmt[CH_FMT];   /* and their format                  */
    dml_fill_t   fill;              /* its bars', slices', areas' ground */
    dml_line_t   line;              /* its line, or their outline        */
    int          has_fill, has_line;
    int          marker;            /* CH_MK_*; -1 the chart's usual     */
    float        mk_size;           /* points across                     */
    dml_fill_t  *pt_fill;           /* each point's own fill, or NULL    */
    dml_line_t  *pt_line;           /* and outline, with it              */
    unsigned char *pt_has;          /* which they have: 1 fill, 2 line   */
    dml_fill_t   mk_fill;           /* its markers' ground               */
    dml_line_t   mk_line;           /* and their outline                 */
    int          has_mk_fill, has_mk_line;
    int          smooth;            /* its line curved through its points */
    float        explode;           /* its slices pulled out: % of radius */
    float       *pt_explode;        /* each point's own, with pt_fill    */
    double      *size;              /* a bubble's sizes, or NULL         */
    int          labels;            /* CH_LBL_*: what each point says    */
    int          lbl_at;            /* CH_AT_*; -1 where its kind puts it */
    ch_font_t    lbl_font;
    ch_label_t  *own;               /* points' own labels, or NULL       */
    int          nown;
    const tiku_xml_node_t *lbl_sppr;   /* its labels' boxes, or NULL      */
    const char **cells;             /* words each label takes from its
                                       cells ("value from cells"), or NULL */
    int          ncells;
    int          show_cells;        /* every label says them             */
    ch_trend_t  *trend;             /* trendlines fitted to it           */
    int          ntrend;
    ch_err_t     err[2];            /* its error bars, x and y           */
    int          nerr;
    int          invert;            /* a bar below nought filled ...     */
    dml_fill_t   invert_fill;       /* ... with this instead             */
    char         sep[8];            /* between a label's parts           */
    char         fmt[CH_FMT];       /* its values' number format         */
    const char  *lbl_fmt;           /* its labels' own, or NULL          */
} ch_series_t;

/** @brief Series of one kind drawn together: clustered bars, a pie. */
typedef struct {
    int          kind;              /* CH_BAR ...                        */
    int          across;            /* bars lie across, not stand        */
    int          grouping;          /* CH_CLUSTERED ...                  */
    int          vary;              /* each point in a colour of its own */
    float        gap, overlap;      /* bars' gap and overlap, percent    */
    float        hole;              /* a doughnut's hole, percent        */
    float        first;             /* a pie's first slice, degrees      */
    int          ccw;               /* its slices go anticlockwise       */
    int          cat_ax, val_ax;    /* its axes, the chart's; -1 none    */
    int          markers;           /* its series' markers shown         */
    int          lines;             /* a scatter's lines drawn           */
    int          filled;            /* a radar's series filled           */
    int          smooth;            /* and curved                        */
    int          hilo;              /* high-low lines drawn              */
    dml_line_t   hilo_line;
    int          updown;            /* up-down bars drawn                */
    float        updown_gap;        /* their gap, percent                */
    dml_fill_t   up, down;          /* their grounds                     */
    dml_line_t   updown_line;
    float        bubble_scale;      /* bubbles' size, percent            */
    int          of_pie;            /* 'p' pie of pie, 'b' bar of pie    */
    int          split;             /* by 0 position, 1 value, 2 percent,
                                       3 the file's own list             */
    double       split_at;          /* the position, value or percent    */
    unsigned char *second;          /* the file's list: which go there   */
    int          nsecond;
    float        second_size;       /* the second plot's, percent        */
    int          drop;              /* lines dropped from each point     */
    dml_line_t   drop_line;
    int          serlines;          /* stacked bars' tops joined         */
    dml_line_t   ser_line;
    ch_series_t *ser;
    int          nser;
} ch_group_t;

/** @brief An axis: what it measures, where, and how it is drawn. */
typedef struct {
    unsigned long id;               /* the file's c:axId                 */
    int         kind;               /* CH_CAT_AX, CH_VAL_AX              */
    char        pos;                /* 'b', 'l', 't', 'r'                */
    int         deleted;            /* not drawn, its gridlines aside    */
    int         reversed;           /* maximum first                     */
    int         has_min, has_max, has_unit;
    double      min, max, unit;     /* the file's own, where it has them */
    int         grid;               /* major gridlines                   */
    dml_line_t  grid_line;
    int         minor;              /* minor gridlines                   */
    dml_line_t  minor_line;
    double      minor_unit;         /* their step; 0: a fifth of a major */
    int         has_line;
    dml_line_t  line;
    int         tick;               /* 0 none, 1 out, 2 in, 3 across     */
    int         labels;             /* 0 none, 1 beside, 2 low, 3 high   */
    char        fmt[CH_FMT];        /* its numbers' format               */
    int         linked;             /* the data's format instead         */
    double      units;              /* its labels in these; 0: ones      */
    const char *units_name;         /* said beside it, or NULL           */
    int         turn_set;           /* its labels turn as the file says  */
    float       turn_file;          /* by so many degrees                */
    ch_font_t   font;
    const char *title;              /* its own title, or NULL            */
    ch_font_t   title_font;
    int         between;            /* categories fall between ticks     */
    int         crosses;            /* 0 at zero, 1 min, 2 max, 3 at ... */
    double      cross_at;           /* ... this value of the other axis  */
    double      log;                /* its logarithm's base; 0 linear    */
    int         tight;              /* its automatic bounds keep to the
                                       data, never reaching for zero     */
    int         date;               /* a date axis: categories by date   */
    int         flat;               /* multi-level categories' groups not
                                       shown                             */
    int         time_unit;          /* in days 0, months 1, years 2      */
    int         lbl_skip;           /* the file's label spacing; 0 none  */
    int         skip;               /* a category label every so many    */
    int         used;               /* a drawn group measures by it      */
    int         horiz;              /* laid across, not up               */
    int         partner;            /* the axis it crosses; -1 none      */
    double      lo, hi, step;       /* the scale found                   */
    float       from, to;           /* where lo and hi fall, in points   */
    float       at;                 /* where its line is drawn           */
    float       turn;               /* its labels' turn, degrees         */
    float       wrap_w;             /* labels wrapped to this; 0 none    */
    int         lines;              /* the most lines one then takes     */
    float       room;               /* what its labels and title take    */
} ch_axis_t;

/** @brief A chart, read, and where its parts go. */
typedef struct {
    of_arena_t   mem;               /* all it holds, given back at once  */
    dml_ctx_t   *c;                 /* colours, faces, the writer        */
    const dml_mat_t *m;             /* the frame's place on the page     */
    float        w, h;              /* the frame's size                  */
    ch_group_t  *grp;
    int          ngrp;
    int          known;             /* groups of kinds drawn here, a
                                       series or none: an empty chart   */
    ch_axis_t    ax[CH_AXES];
    int          nax;
    const char **cat;               /* the categories' names             */
    int          nnamed;            /* how many of them there are        */
    int          ncat;              /* and how many categories           */
    double      *catv;              /* the same as numbers, or NULL      */
    char         cat_fmt[CH_FMT];   /* and their format                  */
    double      *catpos;            /* each category's place on a date
                                       axis, in its unit; NULL: in turn  */
    const char **outer[2];          /* multi-level categories' groups:
                                       a name where each one starts      */
    int          nouter;
    int          blanks;            /* a missing point: 0 gap, 1 zero,
                                       2 the line spans it               */
    const char  *title;             /* NULL for none                     */
    ch_font_t    title_font;
    int          legend;            /* shown                             */
    char         legend_pos;        /* 'b', 't', 'l', 'r'                */
    ch_font_t    legend_font;
    int          legend_over;       /* it lies over the plot             */
    int         *leg_del;           /* the entries taken out of it       */
    int          nleg_del;
    float        plot_at[4];        /* the file's place for the plot:    */
    int          has_plot_at;       /* x y w h, fractions of the frame   */
    int          plot_inner;        /* and whether it is the axes' box   */
    float        legend_at[4];      /* the same for the legend           */
    int          has_legend_at;
    float        title_at[2];       /* and the title's top left          */
    int          has_title_at;
    const uint32_t *palette;        /* series' colours in turn, or NULL:
                                       the theme's accents               */
    int          npalette;
    dml_fill_t   bg, plot_bg;       /* the chart's and plot's grounds    */
    dml_line_t   border, plot_border;
    int          has_bg, has_plot_bg, has_border, has_plot_border;
    ch_font_t    base;              /* what text is unless it says       */
    int          dtable;            /* a data table under the plot       */
    int          dt_horz, dt_vert;  /* its rules between rows, columns   */
    int          dt_outline, dt_keys;  /* its edge; series' legend keys  */
    dml_line_t   dt_line;           /* its rules                         */
    ch_font_t    dt_font;
} ch_chart_t;

/** @brief A box: left, top, width, height, in the frame's points. */
typedef struct {
    float x, y, w, h;
} ch_box_t;

/* Reading: tiku_office_chart_read.c */

/** @brief DrawingML chart space @p cs read into @p k.  @return how many
 *         groups it has that are drawn here (k->known counts those with
 *         no series too); -1 out of memory. */
int ch_read_dml(ch_chart_t *k, const tiku_xml_node_t *cs);

/* OpenDocument: tiku_office_chart_odf.c */

/** @brief An embedded chart object's content.xml @p doc read into @p k,
 *         with the styles of its styles.xml @p styles (may be NULL).
 *  @return as ch_read_dml(). */
int ch_read_odf(ch_chart_t *k, const tiku_xml_node_t *doc,
                const tiku_xml_node_t *styles);

/* Drawing an OpenDocument chart: tiku_office_chart.c */

/** @brief An embedded chart object's content.xml @p doc, with the styles
 *         of its styles.xml @p styles, read and drawn into the frame @p w
 *         by @p h through @p m, as dml_chart() draws a DrawingML one.
 *  @return 1 drawn, 0 not a chart drawn here, -1 out of memory. */
int ch_draw_odf(dml_ctx_t *c, const tiku_xml_node_t *doc,
                const tiku_xml_node_t *styles, const dml_mat_t *m, float w,
                float h);

/* Numbers: tiku_office_chart_num.c */

/** @brief @p v written as spreadsheet number format @p fmt says
 *         ("General", "0.0%", "#,##0", a currency's, a date's), into
 *         @p out. */
void ch_number(double v, const char *fmt, char *out, size_t max);
/** @brief Serial day @p v as a year, month and day into @p ymd, 1900's
 *         way.  @return 0, or -1 for one no calendar holds. */
int ch_date(double v, int *ymd);

/* Axes: tiku_office_chart_axis.c */

/** @brief Axis @p a's scale for what its groups show over @p len points:
 *         the file's bounds where it gives them, else Excel's way. */
void ch_scale(ch_chart_t *k, ch_axis_t *a, float len);
/** @brief Category @p i's place along the category axis, in its units:
 *         @p i itself, or its date on a date axis (fractions kept, for
 *         the edges between categories). */
double ch_cat(const ch_chart_t *k, double i);
/** @brief Where value @p v falls along axis @p a, in the frame's points. */
float ch_place(const ch_axis_t *a, double v);
/** @brief The value of axis @p a its partner crosses it at. */
double ch_cross(const ch_chart_t *k, const ch_axis_t *a);
/** @brief The room axis @p a's labels and title take beside the plot, the
 *         way its labels turn to fit set as it goes. */
float ch_axis_room(ch_chart_t *k, ch_axis_t *a);
/** @brief Label @p i of axis @p a into @p out: category @p i's name, or
 *         tick @p i's value in the axis' units and format. */
void ch_label(const ch_chart_t *k, const ch_axis_t *a, int i, char *out,
              size_t max);
/** @brief How many intervals axis @p a's ticks part it into. */
int ch_intervals(const ch_axis_t *a);
/** @brief Tick @p i's value along value axis @p a. */
double ch_tick(const ch_axis_t *a, int i);
/** @brief The number format value axis @p a writes its labels in: its
 *         own, or, when it follows the data, "0%" for a group stacked to a
 *         hundred percent, else its first series'. */
const char *ch_value_fmt(const ch_chart_t *k, const ch_axis_t *a);
/** @brief Gridlines across plot box @p p. */
void ch_grid(ch_chart_t *k, const ch_box_t *p);
/** @brief Every axis about plot box @p p: its line, ticks, labels and
 *         title. */
void ch_axes(ch_chart_t *k, const ch_box_t *p);

/* Series: tiku_office_chart_plot.c */

/** @brief Every group's series drawn in plot box @p p, labels last. */
void ch_plot(ch_chart_t *k, const ch_box_t *p);
/** @brief The fill point @p i of series @p s in group @p g takes: its
 *         own, the series', or the palette's (-1: the series' own). */
void ch_point_fill(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
                   int i, dml_fill_t *f);
/** @brief The line series @p s of group @p g draws, or outlines its bars
 *         and slices with: its own, else the usual; width 0 for none. */
void ch_series_line(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
                    dml_line_t *l);
/** @brief Point @p i of series @p s's own label, or NULL. */
const ch_label_t *ch_own_label(const ch_series_t *s, int i);
/** @brief What point @p i of series @p s's label says: CH_LBL_*. */
int ch_labels_of(const ch_series_t *s, int i);
/** @brief Series @p s's marker (CH_MK_*) as its group shows it. */
int ch_marker_of(const ch_group_t *g, const ch_series_t *s);
/** @brief Series @p s's marker look: its ground and outline. */
void ch_marker_look(ch_chart_t *k, const ch_group_t *g, const ch_series_t *s,
                    dml_fill_t *f, dml_line_t *l);
/** @brief A marker @p kind of @p size points centred on @p x, @p y. */
void ch_marker(ch_chart_t *k, int kind, float size, float x, float y,
               const dml_fill_t *f, const dml_line_t *l);

/** @brief Drawing from here held inside plot box @p p until the writer's
 *         state is restored. */
void ch_clip(const ch_chart_t *k, const ch_box_t *p);

/* Trendlines: tiku_office_chart_trend.c */

/** @brief Series @p s's trendlines, fitted to its points and drawn
 *         against axes @p xa (categories counted from one, or x values)
 *         and @p ya; @p across swaps them, as lying bars do. */
void ch_trends(ch_chart_t *k, const ch_series_t *s, const ch_axis_t *xa,
               const ch_axis_t *ya, int across);
/** @brief The line trendline @p t of series @p s is drawn with: its own,
 *         else the series' colour, dotted. */
void ch_trend_line(ch_chart_t *k, const ch_series_t *s, const ch_trend_t *t,
                   dml_line_t *l);

/* Error bars: tiku_office_chart_err.c */

/** @brief The spread a series' values (x values for @p b along x) have,
 *         for error bars @p b of standard deviations and errors. */
void ch_err_stats(const ch_series_t *s, ch_err_t *b);
/** @brief Error bars @p b of point @p i, whose own value is @p v, drawn
 *         about value @p mid along axis @p a, at @p at (points) across
 *         it. */
void ch_err(ch_chart_t *k, const ch_err_t *b, int i, double v, double mid,
            float at, const ch_axis_t *a);

/* Data tables: tiku_office_chart_table.c */

/** @brief The category axis a data table lies under: a used one across,
 *         or NULL when the chart has none, or no table. */
const ch_axis_t *ch_table_axis(const ch_chart_t *k);
/** @brief The room a data table takes: across, for its names, into
 *         @p left; down, for its rows, into @p below. */
void ch_table_room(ch_chart_t *k, float *left, float *below);
/** @brief The data table drawn under plot box @p p: the categories, then
 *         a row for each series with its values. */
void ch_table(ch_chart_t *k, const ch_box_t *p);

/* Radar charts: tiku_office_chart_radar.c */

/** @brief Radar group @p g drawn in plot box @p p: its web of gridlines,
 *         its axes' labels and its series. */
void ch_radar(ch_chart_t *k, const ch_group_t *g, const ch_box_t *p);

/* The pen: tiku_office_chart_ink.c */

/** @brief A chart's path, built in the frame's points. */
typedef struct {
    dml_geom_t g;                   /* one subpath                       */
    float      ox, oy;              /* the box it is measured from       */
    int        bad;                 /* 1: memory ran short building it   */
} ch_path_t;

/** @brief Text @p s in @p f: its width.  */
float ch_width(ch_chart_t *k, const ch_font_t *f, const char *s);
/** @brief The height a line of text in @p f takes. */
float ch_line_h(const ch_font_t *f);
/**
 * @brief Text @p s in @p f drawn with its box's point @p x, @p y where
 *        @p align (0 left, 1 middle, 2 right) and @p valign (0 top,
 *        1 middle, 2 bottom) say; turned @p turn degrees clockwise about
 *        that point.
 */
void ch_text(ch_chart_t *k, const ch_font_t *f, const char *s, float x,
             float y, int align, int valign, float turn);
/** @brief Lines of text @p s (parted at newlines) in @p f: the widest
 *         line's width into @p w, their height into @p h. */
void ch_block_size(ch_chart_t *k, const ch_font_t *f, const char *s,
                   float *w, float *h);
/** @brief Lines of text @p s drawn as one block, each set @p align, the
 *         block's point @p x, @p y where @p align and @p valign say. */
void ch_text_block(ch_chart_t *k, const ch_font_t *f, const char *s, float x,
                   float y, int align, int valign);
/** @brief A path begun, measured from the box whose top left is @p ox,
 *         @p oy, filled and stroked as @p fill and @p stroke say. */
void ch_path(ch_path_t *p, float ox, float oy, int fill, int stroke);
/** @brief The path's pen moved to (@p x, @p y), beginning a piece. */
void ch_move(ch_path_t *p, float x, float y);
/** @brief A line drawn from the pen to (@p x, @p y). */
void ch_line(ch_path_t *p, float x, float y);
/** @brief The path's piece shut. */
void ch_close(ch_path_t *p);
/** @brief A curve drawn from the pen to (@p x3, @p y3) through control
 *         points (@p x1, @p y1) and (@p x2, @p y2). */
void ch_curve(ch_path_t *p, float x1, float y1, float x2, float y2, float x3,
              float y3);
/** @brief An arc about (@p cx, @p cy), radius @p r, from angle @p a0 to
 *         @p a1: radians clockwise from twelve o'clock.
 *  @note The pen is already at the arc's start. */
void ch_arc(ch_path_t *p, float cx, float cy, float r, double a0, double a1);
/** @brief The path filled with @p f (may be NULL), measured over its box
 *         @p w by @p h, then outlined with @p l (may be NULL); given back.
 *         A path memory cut short marks the chart's memory bad. */
void ch_paint(ch_chart_t *k, ch_path_t *p, const dml_fill_t *f,
              const dml_line_t *l, float w, float h);
/** @brief A box filled and outlined. */
void ch_rect(ch_chart_t *k, const ch_box_t *b, const dml_fill_t *f,
             const dml_line_t *l);
/** @brief A straight line from one point to another. */
void ch_segment(ch_chart_t *k, float x0, float y0, float x1, float y1,
                const dml_line_t *l);
/** @brief Series or point @p i's colour from the palette: the file's own
 *         when it gives one, else the theme's accents, darker and lighter
 *         as they come round again. */
uint32_t ch_palette(ch_chart_t *k, int i);

#endif /* TIKU_OFFICE_CHART_H_ */
