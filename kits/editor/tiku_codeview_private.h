/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_codeview_private.h - what the code view's files share: the view
 * itself and the one walk over a line that painting, hit testing and
 * measuring all use.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIKU_CODEVIEW_PRIVATE_H_
#define TIKU_CODEVIEW_PRIVATE_H_

#include "tiku_codeview.h"
#include "tiku_editdoc_private.h"
#include "tiku_text_input.h"

/* Pixels between the bounds' left edge and a line's x 0 when there is no
 * gutter, and between the bounds' top and the first row. */
#define TIKU_CV_PAD_X 5
#define TIKU_CV_PAD_Y 3
/* Pixels added below the face's height to make a row. */
#define TIKU_CV_ROW_GAP 2
/* Pixels each side of a control character's name, inside its cell. */
#define TIKU_CV_NAME_PAD 2
/* The line-number gutter: its pixels besides the digits, its separator
 * included, and the gap from it to a line's x 0. */
#define TIKU_CV_GUTTER_PAD 12
#define TIKU_CV_GUTTER_GAP 4
/* The fold column's width, at the gutter's right edge. */
#define TIKU_CV_FOLD_W 12
/* A scrollbar's thickness. */
#define TIKU_CV_BAR 15
/* Pixels past the widest line the text may scroll, room for the caret. */
#define TIKU_CV_SLACK 12
/* A line of at least TIKU_CV_LONG bytes keeps a checkpoint every
 * TIKU_CV_MARK bytes, so a walk can start near where it is going, and is
 * coloured only as far as it is drawn.  TIKU_CV_LONG_LINES such lines keep
 * checkpoints, the least recently walked given up first. */
#define TIKU_CV_LONG 4096
#define TIKU_CV_MARK 2048
#define TIKU_CV_LONG_LINES 4

/* A place a walk can resume from: a character boundary and its x. */
typedef struct {
    tiku_edit_pos_t pos;            /**< the character boundary         */
    int64_t x;                      /**< the line x at pos              */
} tiku_cv_mark_t;

/* Where the rows of a line that wraps start, kept for the TIKU_CV_WRAPS
 * lines of more than one row asked for last. */
#define TIKU_CV_WRAPS 64
typedef struct {
    int64_t line;                   /**< -1: unused                     */
    uint64_t used;                  /**< when last asked for            */
    tiku_cv_mark_t *rows;           /**< each row's first byte, counted
                                       from the line's start, and its x */
    size_t nrows, cap;              /**< rows kept, and room for them   */
} tiku_cv_wrap_t;

/* The built-in inks, TIKU_INK_PLAIN to TIKU_INK_LINK; the style IDs past
 * them are the application's to define. */
#define TIKU_CV_INKS (TIKU_INK_LINK + 1)

/* Room for the extra keys a list of words is put in by: every printable
 * ASCII character, and a NUL. */
#define TIKU_CV_ACCEPT 96

/* A line's fold state: inside a folded region, so it has no rows, and
 * heading one. */
#define TIKU_CV_HIDDEN 0x1u
#define TIKU_CV_FOLDED 0x2u

/* One row of a line. */
typedef struct {
    int64_t line;                   /**< the line it shows              */
    int64_t sub;                    /**< its place among the line's rows */
    tiku_edit_pos_t start;          /**< its first byte                 */
    tiku_edit_pos_t end;            /**< the next row's first byte, or the
                                       line's end                       */
    int64_t x0;                     /**< line x of its first byte       */
    int last;                       /**< set on the line's last text row */
    int64_t ann;                    /**< -1 a row of text, else the row of
                                       the line's annotations it shows  */
} tiku_cv_row_t;

/* The x a caret keeps on rows up and down, by where the caret is. */
typedef struct {
    tiku_edit_pos_t at;             /**< the caret                      */
    int64_t x;                      /**< the caret's x from its row start */
} tiku_cv_goal_t;

/* The checkpoints of one long line, in order from its start. */
typedef struct {
    int64_t line;                   /**< -1: unused                     */
    tiku_edit_pos_t start;          /**< the line's first byte          */
    uint32_t key;                   /**< the metrics marks were taken in */
    uint64_t used;                  /**< when last walked               */
    tiku_cv_mark_t *marks;          /**< the checkpoints                */
    size_t nmarks, cap;             /**< how many, and room for them    */
} tiku_cv_long_t;

/* A view of a document: its settings, and what it keeps to lay out,
 * colour, paint and edit the text. */
struct tiku_codeview {
    tiku_editdoc_t *doc;            /**< referenced while the view lives */
    tiku_edit_view_t view;          /**< the document's selection for it */
    tiku_edit_listener_t listener;  /**< told of every change           */
    const tiku_font_t *font;        /**< NULL: the monospaced face      */
    int tab_width;                  /**< columns between tab stops      */
    int zoom;                       /**< two-pixel steps of the mono face */
    unsigned ruler;                 /**< the ruler's column, 0 none     */
    int focused;                    /**< draw the caret                 */
    tiku_rect_t bounds;             /**< on the surface                 */
    int64_t top;                    /**< line in the first row          */
    int64_t left;                   /**< pixels scrolled left           */
    int64_t goal;                   /**< x vertical moves keep; -1 none */
    int dragging;                   /**< button 1 is down on the view   */
    int unit;                       /**< a drag selects 1 characters,
                                       2 words, 3 lines                 */
    tiku_edit_range_t pressed;      /**< the word or line first selected */
    int clicks;                     /**< presses counted as one click   */
    int64_t click_at;               /**< the last press's time, in
                                       microseconds                     */
    int click_x, click_y;           /**< where the last press was       */
    unsigned options;               /**< TIKU_CODEVIEW_LINE_NUMBERS ... */
    int held_bar;                   /**< thumb held: 1 vertical, 2 across */
    int grab;                       /**< where in the thumb it was taken */
    int arrow_bar;                  /**< arrow held: 1 vertical, 2 across */
    int arrow_dir;                  /**< -1 back, +1 on                 */
    int arrow_pulses;               /**< ticks since the arrow went down */
    int64_t *widths;                /**< each line's width, -1 unknown  */
    int64_t nwidths, widths_cap;    /**< entries in use and allocated   */
    int64_t widest;                 /**< widest line's width, -1 unknown */
    int widths_ok;                  /**< widths follows the document    */
    uint32_t widths_key;            /**< metrics the widths were taken in */
    tiku_codeview_host_t host;      /**< what the view asks of its host */
    char newline[3];                /**< what Return inserts            */
    tiku_edit_status_t status;      /**< the last event's edit          */
    tiku_text_input_t input;        /**< a text-input commit arriving   */
    tiku_syntax_lang_t lang;        /**< how the text is coloured       */
    tiku_codeview_lexer_t lexer;    /**< the application's lexer; its line
                                       NULL: the language's             */
    tiku_edit_status_t lexer_status; /**< the status of the last line
                                         coloured                     */
    int lexing;                     /**< the lexer is running           */
    tiku_codeview_binding_t *bindings; /**< the host's keys, its own copy */
    size_t nbindings;               /**< how many                       */
    char *zero_pattern;             /**< the query of the empty match Find
                                       selected last, or NULL           */
    unsigned zero_flags;            /**< zero_pattern's query flags     */
    uint64_t zero_generation;       /**< the document's generation when
                                       Find selected the match          */
    uint64_t zero_selection;        /**< the selection's serial when Find
                                       selected the match               */
    tiku_edit_pos_t zero_position;  /**< where the match is             */
    int observed;                   /**< changes were polled for        */
    uint64_t seen_doc;              /**< the generation at the last poll */
    uint64_t seen_selection;        /**< the selection's serial at the
                                       last poll                        */
    int seen_modified;              /**< modified at the last poll      */
    int seen_read_only;             /**< read-only at the last poll     */
    int64_t seen_top, seen_sub;     /**< first row shown at the last poll */
    int64_t seen_left;              /**< scroll across at the last poll */
    uint32_t seen_commands;         /**< the commands enabled at the last
                                       poll, a bit each                 */
    tiku_edit_status_t seen_status; /**< status at the last poll        */
    uint32_t *states;               /**< each line's starting lexer state */
    int64_t nstates, states_cap;    /**< lines followed, and room       */
    int64_t sty_valid;              /**< states up to here are right    */
    int64_t sty_hint;               /**< states from sty_valid to here
                                       are old but may still be right   */
    int64_t sty_dirty;              /**< the last line a change touched */
    char *line_buf;                 /**< a line copied out, and a NUL   */
    size_t line_cap;                /**< room in line_buf               */
    tiku_span_t *runs;              /**< the coloured runs of a line    */
    size_t nruns, runs_cap;         /**< how many, and room for them    */
    int runs_failed;                /**< the runs could not all be kept */
    tiku_cv_long_t longs[TIKU_CV_LONG_LINES]; /**< long lines' checkpoints */
    uint64_t walks;                 /**< walks of long lines so far     */
    tiku_span_t *kept;              /**< the runs of the last long line
                                       coloured, read again unchanged   */
    size_t nkept, kept_cap;         /**< how many, and room for them    */
    int64_t kept_line;              /**< kept's line, -1 none           */
    tiku_edit_pos_t kept_start;     /**< where kept_line started        */
    uint64_t kept_gen;              /**< generation kept was coloured in */
    uint32_t kept_in;               /**< the state kept_line started in */
    tiku_syntax_lang_t kept_lang;   /**< language kept was coloured in  */
    tiku_edit_pos_t kept_cut;       /**< the byte kept reaches to       */
    uint32_t fill_kinds;            /**< marker kinds whose rows are
                                       filled                           */
    tiku_rgb_t fills[TIKU_EDIT_MARKER_KINDS]; /**< each kind's fill colour */
    tiku_rgb_t exec_fill;           /**< the execution line's fill      */
    tiku_rgb_t caret_fill;          /**< the caret line's fill, when set */
    int caret_fill_set;             /**< 0: the theme's note colour     */
    unsigned ink_styles[TIKU_CODEVIEW_STYLE_COUNT]; /**< each style ID's
                                       TIKU_CODEVIEW_BOLD ...           */
    tiku_codeview_style_t style_defs[TIKU_CODEVIEW_STYLE_COUNT]; /**< each
                                       style ID's definition, if any    */
    unsigned char style_defined[TIKU_CODEVIEW_STYLE_COUNT]; /**< each
                                       style ID has a definition        */
    uint32_t *fold_levels;          /**< the parser's levels, a line each;
                                       NULL: the rules fold             */
    size_t fold_level_count;        /**< how many                       */
    uint64_t fold_generation;       /**< generation fold_levels are for */
    int json_folds;                 /**< fold_levels were read from JSON */
    unsigned hot_keys;              /**< the modifiers hotspots need    */
    int hot_seen;                   /**< the pointer has been seen      */
    int hot_x, hot_y;               /**< where the pointer was last seen */
    unsigned hot_mods;              /**< modifiers held at hot_x, hot_y */
    int hot_shown;                  /**< a live hotspot is shown        */
    tiku_edit_range_t hot;          /**< the live hotspot shown         */
    int hot_pressed;                /**< button 1 went down on a hotspot */
    tiku_edit_range_t hot_press;    /**< the hotspot pressed            */
    int list_shown;                 /**< a list of words is shown       */
    int list_inserts;               /**< keys and clicks put a word in  */
    tiku_codeview_completion_item_t *completion_items; /**< a rich list's
                                       items, then their indices and
                                       text, in one allocation; NULL:
                                       a list of words                  */
    size_t *completion_indices;     /**< each item's index among the
                                       host's items                     */
    unsigned completion_rows;       /**< words shown at once, 0: five   */
    char completion_accept[TIKU_CV_ACCEPT]; /**< other keys that put the
                                       chosen word in                   */
    int completion_accepted;        /**< a word was put in, not yet read */
    size_t completion_result;       /**< the index of the word put in
                                       among the host's items           */
    char **list_items;              /**< the list's words, one allocation */
    size_t list_count;              /**< how many                       */
    size_t list_current;            /**< the word chosen                */
    tiku_edit_anchor_t list_start;  /**< where the replaced range starts */
    tiku_edit_anchor_t list_end;    /**< where the replaced range ends  */
    int tip_shown;                  /**< a tip is shown                 */
    char *tip_text;                 /**< what the tip says              */
    tiku_edit_range_t tip_highlight; /**< bytes of tip_text highlighted */
    tiku_edit_range_t *tip_arguments; /**< a signature's arguments in
                                       tip_text, NULL for a plain tip   */
    size_t tip_argument_count;      /**< how many                       */
    size_t tip_argument;            /**< the argument the caret is in   */
    tiku_edit_anchor_t tip_at;      /**< where the tip was shown        */
    tiku_codeview_rules_t rules;    /**< the application's own rules    */
    int own_rules;                  /**< use rules, not the language's  */
    int indent_spaces;              /**< spaces a level; 0 a tab        */
    uint64_t brace_gen;             /**< the generation the bracket by the
                                       caret was found in, 0 none       */
    tiku_edit_pos_t brace_caret;    /**< the caret brace_at was found for */
    const void *brace_rules;        /**< the rules brace_at was found by */
    tiku_edit_pos_t brace_at;       /**< the bracket, -1 none           */
    tiku_edit_pos_t brace_match;    /**< brace_at's match, -1 none, -2
                                       when the search gave up          */
    int64_t top_sub;                /**< rows of the top line above the
                                       first row shown                  */
    tiku_edit_pos_t upstream;       /**< a caret here, at the end of a row,
                                       shows on that row and not at the
                                       next one's start; -1 none        */
    tiku_ed_lines_t row_map;        /**< each line's first row, kept
                                       while rows are kept              */
    unsigned char *row_known;       /**< each line's rows counted       */
    unsigned char *row_fold;        /**< each line's TIKU_CV_HIDDEN and
                                       TIKU_CV_FOLDED                   */
    int64_t folds;                  /**< lines heading a folded region  */
    int64_t row_lines;              /**< lines the map follows, 0: none */
    int64_t row_cap;                /**< room in row_known and row_fold */
    int64_t row_width;              /**< the width the rows break at    */
    uint32_t row_key;               /**< metrics the rows were counted in */
    int64_t unknown_from;           /**< no line before it is uncounted */
    tiku_cv_wrap_t wraps[TIKU_CV_WRAPS]; /**< rows of lines that wrap   */
    uint64_t wrap_uses;             /**< asks of the wraps so far       */
    int ann_shown;                  /**< annotations add rows           */
    uint64_t ann_serial;            /**< the annotations the rows were
                                       counted for                      */
    tiku_codeview_mark_t marks[TIKU_EDIT_INDICATOR_KINDS]; /**< how each
                                       kind of indicator is drawn       */
    tiku_rgb_t mark_colors[TIKU_EDIT_INDICATOR_KINDS]; /**< each kind's
                                       colour                           */
    tiku_edit_selection_t *sels;    /**< the selections read in order   */
    size_t sels_cap;                /**< room in sels                   */
    tiku_edit_selection_t sel_one;  /**< the main selection, painted
                                       alone when sels has no room      */
    int rect;                       /**< the drag makes a rectangle     */
    int64_t rect_x;                 /**< the press's x on its row       */
    int64_t rect_line, rect_sub;    /**< the press's row                */
    tiku_edit_selection_t *held;    /**< the selections a Command press
                                       added one to, and room for it    */
    size_t nheld;                   /**< selections in held, the added
                                       one left out                     */
    tiku_cv_goal_t *goals;          /**< each caret's x on rows up and
                                       down, as the last such move left
                                       them, used while goal is kept    */
    size_t ngoals, goals_cap;       /**< how many, and room             */
};

/* What a character of a line is on the screen. */
typedef enum {
    TIKU_CV_GLYPH = 0,              /**< drawn from the face            */
    TIKU_CV_TAB,                    /**< space to the next stop         */
    TIKU_CV_CONTROL                 /* a control character, by name     */
} tiku_cv_kind_t;

/* One character of a line, as a walk hands it to its visitor. */
typedef struct {
    tiku_edit_pos_t pos;            /**< its first byte                 */
    int len;                        /**< its bytes, 1 to 4              */
    unsigned cp;                    /**< its code point                 */
    tiku_cv_kind_t kind;            /**< what it is on the screen       */
    int64_t x;                      /**< its left edge, from x 0        */
    int adv;                        /**< its width                      */
    char bytes[5];                  /**< its UTF-8 and a NUL            */
} tiku_cv_cell_t;

/* Called for each character in order; nonzero stops the walk. */
typedef int (*tiku_cv_cell_fn)(void *ctx, const tiku_cv_cell_t *cell);

/* The face in force. */
const tiku_font_t *tiku_cv_font(const tiku_codeview_t *v);
/* Pixels between tab stops in the face in force, at least 1. */
int tiku_cv_tab_px(const tiku_codeview_t *v);
/* The name a control character is shown by, in @p name (at least 5
 * bytes); 0 when @p cp is not a control character. */
int tiku_cv_control_name(unsigned cp, char *name);

/* Visit @p line's characters from its start, @p fn may be NULL.  Returns
 * the x where the walk ended: the left edge of the character @p fn
 * stopped at, or the line's width; -1 when there is no such line. */
int64_t tiku_cv_walk(tiku_codeview_t *v, int64_t line, tiku_cv_cell_fn fn,
                     void *ctx);
/* The walk tiku_cv_walk() makes, started at the line's last checkpoint
 * at or before @p pos and at or before line x @p x: every character it
 * skips lies wholly before both.  INT64_MAX leaves either open. */
int64_t tiku_cv_walk_near(tiku_codeview_t *v, int64_t line,
                          tiku_edit_pos_t pos, int64_t x, tiku_cv_cell_fn fn,
                          void *ctx);
/* Drop what a change starting at @p from leaves wrong: the checkpoints
 * past it, and every long line it starts before. */
void tiku_cv_marks_change(tiku_codeview_t *v, tiku_edit_pos_t from);
/* Free every checkpoint. */
void tiku_cv_marks_free(tiku_codeview_t *v);
/* The x of boundary @p pos on @p line, which must hold it. */
int64_t tiku_cv_x_of(tiku_codeview_t *v, int64_t line, tiku_edit_pos_t pos);
/* The caret position on @p line nearest the line's own @p x. */
tiku_edit_pos_t tiku_cv_pos_at(tiku_codeview_t *v, int64_t line, int64_t x);
/* The first byte of the character on @p line under the line's own @p x,
 * or -1 past the line's end. */
tiku_edit_pos_t tiku_cv_char_at(tiku_codeview_t *v, int64_t line, int64_t x);

/* Rows (tiku_codeview_rows.c).  Without the wrap option, annotations or
 * folds each line is one row and nothing is kept; with them a line's
 * text breaks into rows at the text's width, its annotations' rows follow
 * them, a line in a folded region has none, and the rows are counted
 * when they are first needed.  A line not yet counted keeps its last
 * count, or one row.  Positions and x are the line's own; a row's x0 is
 * subtracted where it is drawn. */
/* Whether rows are kept: lines wrap, annotations are shown or lines are
 * folded. */
int tiku_cv_rows_kept(const tiku_codeview_t *v);
/* Whether lines break into rows at the text's width now. */
int tiku_cv_wraps(const tiku_codeview_t *v);
/* The rows of @p line's text, counted now. */
int64_t tiku_cv_text_rows(tiku_codeview_t *v, int64_t line);
/* The first line from @p line on, or with @p back from it back, that is
 * not inside a folded region; -1 when there is none. */
int64_t tiku_cv_shown_line(const tiku_codeview_t *v, int64_t line,
                           int back);
/* Hide lines @p first to @p last, or with @p show show them, setting
 * their rows; lines hidden or shown already are left as they are. */
void tiku_cv_rows_hide(tiku_codeview_t *v, int64_t first, int64_t last,
                       int show);
/* Make sure rows are kept, as folding needs; 0 when there is no room. */
int tiku_cv_rows_keep(tiku_codeview_t *v);
/* Bring the rows in line with the options, width and metrics, keep the
 * first row inside the document, and count the rows of the lines shown. */
void tiku_cv_rows_ready(tiku_codeview_t *v);
/* Rows in all. */
int64_t tiku_cv_rows_total(const tiku_codeview_t *v);
/* The index of @p line's first row. */
int64_t tiku_cv_line_row(const tiku_codeview_t *v, int64_t line);
/* The rows @p line has, counted now. */
int64_t tiku_cv_line_rows(tiku_codeview_t *v, int64_t line);
/* Row @p sub of @p line, which must have it. */
void tiku_cv_row(tiku_codeview_t *v, int64_t line, int64_t sub,
                 tiku_cv_row_t *out);
/* The row holding @p pos; a position where a row breaks is on the next
 * row, or with @p upstream at the end of the row it ends. */
void tiku_cv_row_of(tiku_codeview_t *v, tiku_edit_pos_t pos, int upstream,
                    tiku_cv_row_t *out);
/* Move (@p line, @p sub) @p n rows on, or back when @p n is negative,
 * stopping at the document's ends; @return the rows moved. */
int64_t tiku_cv_rows_step(tiku_codeview_t *v, int64_t *line, int64_t *sub,
                          int64_t n);
/* The index of the first row shown. */
int64_t tiku_cv_top_row(const tiku_codeview_t *v);
/* Make the row at index @p row the first shown. */
void tiku_cv_set_top_row(tiku_codeview_t *v, int64_t row);
/* Rows from the first shown to row @p sub of @p line, negative above it;
 * the lines between are taken as counted. */
int64_t tiku_cv_shown_row(const tiku_codeview_t *v, int64_t line,
                          int64_t sub);
/* The caret position on row @p r nearest the row's own @p x, setting
 * @p upstream when it is the end of a row that is not the line's last. */
tiku_edit_pos_t tiku_cv_row_pos_at(tiku_codeview_t *v,
                                   const tiku_cv_row_t *r, int64_t x,
                                   int *upstream);
/* The row under surface @p y, the document's first or last row past its
 * ends; @return its place among the rows shown. */
int64_t tiku_cv_row_under(tiku_codeview_t *v, int64_t y, tiku_cv_row_t *r);
/* Follow a document change in the rows: lines @p first to @p last_old
 * became @p first to @p last_new, @p lines in all. */
void tiku_cv_rows_change(tiku_codeview_t *v, int64_t first,
                         int64_t last_old, int64_t last_new, int64_t lines);
/* Count lines not yet counted, from the first, until @p budget bytes of
 * them are; whether any were counted. */
int tiku_cv_rows_tick(tiku_codeview_t *v, int64_t budget);
/* Free the rows. */
void tiku_cv_rows_free(tiku_codeview_t *v);
/* A number for everything a line's width depends on. */
uint32_t tiku_cv_metrics_key(const tiku_codeview_t *v);

/* Where line x 0 is on the surface while the text is not scrolled across:
 * past the gutter and its gap, or TIKU_CV_PAD_X inside the bounds with no
 * gutter. */
int tiku_cv_text_x(const tiku_codeview_t *v);
/* Where the first row's top is on the surface, TIKU_CV_PAD_Y below the
 * bounds' top. */
int tiku_cv_text_y(const tiku_codeview_t *v);
/* The gutter's width: the line numbers' and the fold column's, each
 * while its option is on; 0 with both off. */
int tiku_cv_gutter(const tiku_codeview_t *v);
/* The text's ground: the bounds less the gutter and the scrollbars. */
tiku_rect_t tiku_cv_area(const tiku_codeview_t *v);
/* Pixels of a line the text area shows across. */
int64_t tiku_cv_text_w(const tiku_codeview_t *v);
/* A scrollbar's place, empty when the bar is off or the bounds are too
 * small to hold it. */
tiku_rect_t tiku_cv_bar(const tiku_codeview_t *v, int horizontal);
/* Where a bar's thumb is, in thousandths of its travel, and the share of
 * the bar it fills, in thousandths and at least 50. */
void tiku_cv_bar_place(tiku_codeview_t *v, int horizontal, int *pos,
                       int *frac);
/* Bring the first row back inside the document and the horizontal
 * position to 0 or more. */
void tiku_cv_clamp(tiku_codeview_t *v);
/* As tiku_cv_clamp(), and while lines do not wrap stop the text
 * TIKU_CV_SLACK pixels past the end of the widest line, measuring every
 * line not yet measured. */
void tiku_cv_clamp_across(tiku_codeview_t *v);

/* The widest line's width, measuring every line not yet measured; with
 * no room for the table, the widest of the lines shown. */
int64_t tiku_cv_widest(tiku_codeview_t *v);
/* Follow a document change in the width table: lines @p first to
 * @p last_old became @p first to @p last_new, @p lines in all. */
void tiku_cv_widths_change(tiku_codeview_t *v, int64_t first,
                           int64_t last_old, int64_t last_new,
                           int64_t lines);
/* Free the width table. */
void tiku_cv_widths_free(tiku_codeview_t *v);

/* The coloured runs of @p line, @p n of them, or NULL when the line is
 * to be drawn in the document's ink alone.  A long line's runs may end
 * before the line does, but not before @p upto, the last byte that will
 * be drawn, and up to it they colour as the whole line's would.  Valid
 * until the next call. */
const tiku_span_t *tiku_cv_style_line(tiku_codeview_t *v, int64_t line,
                                      tiku_edit_pos_t upto, size_t *n);
/* Follow a document change in the lines' starting states: lines @p first
 * to @p last_old became @p first to @p last_new, @p lines in all. */
void tiku_cv_style_change(tiku_codeview_t *v, int64_t first,
                          int64_t last_old, int64_t last_new,
                          int64_t lines);
/* Forget every line's starting state, the runs kept for a long line and
 * the bracket found, as after a load. */
void tiku_cv_style_reset(tiku_codeview_t *v);
/* Free what colouring holds. */
void tiku_cv_style_free(tiku_codeview_t *v);
/* Colour one line, @p text, from state @p in, the state it leaves going in
 * @p out when that is not NULL (tiku_codeview_lexer.c): by the
 * application's lexer, its runs checked before they reach @p emit, or
 * else by the language.  0 when the application's lexer cannot run now,
 * fails or breaks its rules; lexer_status says why. */
int tiku_cv_lex(tiku_codeview_t *v, const char *text, uint32_t in,
                uint32_t *out, tiku_syntax_run_fn emit, void *ctx);
/* Styles (tiku_codeview_languages.c): the built-in ink style @p id stands
 * for, which tells code from strings and remarks. */
tiku_ink_t tiku_cv_style_role(const tiku_codeview_t *v, unsigned id);
/* The colour style @p id is drawn in: the foreground it defines, else
 * the live theme's shade of the ink it stands for. */
tiku_rgb_t tiku_cv_style_color(const tiku_codeview_t *v, unsigned id);

/* An editing key: 1 when the view took it. */
int tiku_cv_edit_key(tiku_codeview_t *v, const tiku_event_t *e);
/* A key the host bound (tiku_codeview_commands.c): -1 when it is bound to
 * nothing, 0 when it is left to the host, and 1 when its command ran. */
int tiku_cv_binding_event(tiku_codeview_t *v, const tiku_event_t *e);
/* Replace [@p lo, @p hi) with @p length bytes of @p text as one undo
 * step, joining typing when @p typing, the caret after the text (before a
 * CR the text leaves against an LF), and show it. */
tiku_edit_status_t tiku_cv_replace(tiku_codeview_t *v, tiku_edit_pos_t lo,
                                   tiku_edit_pos_t hi, const char *text,
                                   size_t length, int typing);
/* Where a word move left goes from @p pos: back over spaces, then over
 * one run of word characters, of punctuation or of line breaks.  Spaces
 * are blanks and ASCII control characters; word characters are ASCII
 * letters, digits, '_' and bytes past ASCII. */
tiku_edit_pos_t tiku_cv_word_left(const tiku_codeview_t *v,
                                  tiku_edit_pos_t pos);
/* Where a word move right goes from @p pos: on over one run of the kind
 * at @p pos (spaces, word characters, punctuation or line breaks), then
 * over spaces.  Spaces are blanks and ASCII control characters; word
 * characters are ASCII letters, digits, '_' and bytes past ASCII. */
tiku_edit_pos_t tiku_cv_word_right(const tiku_codeview_t *v,
                                   tiku_edit_pos_t pos);

/* Line commands (tiku_codeview_lines.c): the rules in force, the view's
 * own or else its language's. */
const tiku_codeview_rules_t *tiku_cv_rules(const tiku_codeview_t *v);
/* When @p text is one closer typed first on its line, re-indent the line
 * to match the line of the opener it closes and type @p text, as one edit;
 * the edit's status goes in the view's status and 1 is returned.  0, with
 * nothing done, when the selection is not one empty caret, @p text is no
 * such closer, no opener is open before it, or the line has that
 * indentation already. */
int tiku_cv_type_closer(tiku_codeview_t *v, const char *text);

/* A line's colouring read a byte at a time (tiku_codeview_brace.c). */
typedef struct {
    const tiku_span_t *runs;        /**< the line's coloured runs       */
    const tiku_codeview_t *view;    /**< the view, for its styles' roles */
    size_t n;                       /**< how many                       */
    size_t i;                       /**< the run the last offset fell in */
    int64_t start;                  /**< where run i starts             */
} tiku_cv_inks_t;
/* The colouring of @p line, from its first byte through @p upto, valid
 * until a line is coloured again. */
tiku_cv_inks_t tiku_cv_inks(tiku_codeview_t *v, int64_t line,
                            tiku_edit_pos_t upto);
/* Whether the line's byte at offset @p at is code, not in quotes or a
 * remark; past the runs it is. */
int tiku_cv_code_at(tiku_cv_inks_t *k, int64_t at);

/* Brackets (tiku_codeview_brace.c): the bracket by an empty selection's
 * caret, -1 for none, and its match in @p match, -1 for none and -2 when
 * the search gave up; kept until the document, the caret or the rules
 * change. */
tiku_edit_pos_t tiku_cv_brace(tiku_codeview_t *v, tiku_edit_pos_t *match);
/* The opener @p closer pairs with that is still open before @p pos; -1
 * for none and -2 when the search gave up. */
tiku_edit_pos_t tiku_cv_open_before(tiku_codeview_t *v, tiku_edit_pos_t pos,
                                    char closer);
/* Paint the pair, or a bracket with no match in the wrong ink, behind the
 * text of the first @p rows rows shown. */
void tiku_cv_brace_paint(tiku_codeview_t *v, tiku_surface_t *s,
                         int64_t rows);

/* Guides (tiku_codeview_guides.c): the ruler, and with
 * TIKU_CODEVIEW_INDENT_GUIDES the guides in the indentation of row @p w,
 * painted behind its text at surface row @p y. */
void tiku_cv_guides_paint(tiku_codeview_t *v, tiku_surface_t *s,
                          const tiku_cv_row_t *w, int y);

/* Indicators and annotations (tiku_codeview_notes.c): the rows the
 * annotations under @p line take while they are shown. */
int64_t tiku_cv_ann_rows(const tiku_codeview_t *v, int64_t line);
/* The marks of the indicators over row @p w, drawn behind its text at
 * surface row @p y. */
void tiku_cv_marks_paint(tiku_codeview_t *v, tiku_surface_t *s,
                         const tiku_cv_row_t *w, int y);
/* Annotation row @p w drawn at surface row @p y. */
void tiku_cv_annotation_paint(tiku_codeview_t *v, tiku_surface_t *s,
                              const tiku_cv_row_t *w, int y);

/* One selection's change in a change across all of them: [start, end)
 * replaced by @c length bytes of @c text, the selection becoming a caret
 * @c caret bytes into the new text. */
typedef struct {
    tiku_edit_pos_t start;          /**< start of the bytes replaced    */
    tiku_edit_pos_t end;            /**< end of the bytes replaced      */
    const char *text;               /**< what replaces them             */
    size_t length;                  /**< the bytes in text              */
    size_t caret;                   /**< the caret's offset in text     */
} tiku_cv_piece_t;

/* Several selections (tiku_codeview_carets.c).  Whether the view has more
 * than one. */
int tiku_cv_many(const tiku_codeview_t *v);
/* The view's selections in order, read into a buffer the view keeps until
 * the next call: @p n of them, the main one at @p main; NULL, @p n 0,
 * when there is no room for them, and a command then changes nothing and
 * returns TIKU_EDIT_NO_MEMORY. */
const tiku_edit_selection_t *tiku_cv_selections(tiku_codeview_t *v,
                                                size_t *n, size_t *main);
/* Apply @p n pieces, one for each selection in order, as one transaction
 * leaving a caret in each, the one at @p main the main one, and show it. */
tiku_edit_status_t tiku_cv_apply_pieces(tiku_codeview_t *v,
                                        const tiku_cv_piece_t *p, size_t n,
                                        size_t main, int typing);
/* Type @p length bytes of @p text over every selection. */
tiku_edit_status_t tiku_cv_insert_each(tiku_codeview_t *v, const char *text,
                                       size_t length, int typing);
/* Delete every selection, or at each caret the character, or with
 * @p words the word, before it or with @p forward after it.  With two or
 * more carets, at least one always has something to delete. */
tiku_edit_status_t tiku_cv_delete_each(tiku_codeview_t *v, int forward,
                                       int words);
/* Paste @p text over every selection: one line each when it has as many
 * lines as there are selections, else all of it each. */
tiku_edit_status_t tiku_cv_paste_each(tiku_codeview_t *v, const char *text,
                                      size_t length);
/* Offer the selections' text to the clipboard in order, a line break
 * between each two; TIKU_EDIT_NOTHING when none holds any. */
tiku_edit_status_t tiku_cv_copy_each(tiku_codeview_t *v);
/* Select the rectangle from the press's row and x to row @p sub of
 * @p line at row x @p x: on each text row between, from the place nearest
 * the one x to the place nearest the other, the pointer's row main. */
tiku_edit_status_t tiku_cv_rectangle(tiku_codeview_t *v, int64_t line,
                                     int64_t sub, int64_t x);
/* A Command press at @p pos: take away the selection there, when there
 * are several, or else add a caret there as the main one. */
tiku_edit_status_t tiku_cv_add_press(tiku_codeview_t *v, tiku_edit_pos_t pos);
/* The selection a Command press added, dragged to @p anchor, @p caret. */
tiku_edit_status_t tiku_cv_add_drag(tiku_codeview_t *v,
                                    tiku_edit_pos_t anchor,
                                    tiku_edit_pos_t caret);

/* Folds (tiku_codeview_folds.c): whether @p line heads a region, read
 * from its own text or from the parser's levels; while the levels are
 * stale only a line heading a folded region heads one. */
int tiku_cv_fold_head(tiku_codeview_t *v, int64_t line);
/* The last line of the region @p line heads by the parser's levels
 * (tiku_codeview_languages.c), -1 for none or while they are stale. */
int64_t tiku_cv_supplied_fold(const tiku_codeview_t *v, int64_t line);
/* After the parser's levels are set or removed, keep each folded region
 * whose line heads one by the levels or rules now in force folded, over
 * the lines they give it, and unfold the rest. */
void tiku_cv_folds_refit(tiku_codeview_t *v);
/* Unfold the regions @p line is folded in. */
void tiku_cv_fold_reveal(tiku_codeview_t *v, int64_t line);
/* Follow a change to lines @p first to @p last: what it touched, and the
 * folded lines next to it, are shown. */
void tiku_cv_fold_changed(tiku_codeview_t *v, int64_t first, int64_t last);
/* The fold column's box for @p line, at surface row @p y. */
void tiku_cv_folds_paint(tiku_codeview_t *v, tiku_surface_t *s, int64_t line,
                         int y);
/* The mark after a folded line's text, at surface @p x, @p y. */
void tiku_cv_fold_mark(tiku_codeview_t *v, tiku_surface_t *s, int x, int y);
/* A press of @p e in the fold column: 1 when it was there. */
int tiku_cv_folds_press(tiku_codeview_t *v, const tiku_event_t *e);

/* Hotspots (tiku_codeview_hotspots.c): an event they take, 1 when they
 * took it; the pointer's moves and the modifier keys are followed on the
 * way. */
int tiku_cv_hot_event(tiku_codeview_t *v, const tiku_event_t *e);
/* Forget the hotspot shown and any press on it. */
void tiku_cv_hot_forget(tiku_codeview_t *v);
/* Find the hotspot under the pointer again after event @p e: where it was
 * last seen when the wheel moved the text under it, and where a drag let
 * go. */
void tiku_cv_hot_after(tiku_codeview_t *v, const tiku_event_t *e);
/* The live hotspot under the pointer, in @p range when that is not NULL;
 * 0 when none is shown. */
int tiku_cv_hot_shown(const tiku_codeview_t *v, tiku_edit_range_t *range);

/* Lists and tips (tiku_codeview_assist.c): an event a list or tip shown
 * takes, 1 when it took it. */
int tiku_cv_assist_event(tiku_codeview_t *v, const tiku_event_t *e);
/* After any event, keep a tip with the caret: a signature's highlights
 * the argument the caret is in and hides once the caret leaves the call;
 * any other hides once the caret goes before it or off its line. */
void tiku_cv_assist_follow(tiku_codeview_t *v);
/* Paint the list and the tip shown over the text. */
void tiku_cv_assist_paint(tiku_codeview_t *v, tiku_surface_t *s);
/* Hide the list and the tip, freeing what they hold. */
void tiku_cv_assist_free(tiku_codeview_t *v);
/* A signature's tip (tiku_codeview_completion.c) after the caret moved:
 * the argument the caret is in highlighted, or the tip hidden when the
 * caret has left the call. */
void tiku_cv_signature_follow(tiku_codeview_t *v);
/* A text-input event, @p commit the result tiku_text_input_event() gave
 * for it: a complete commit is typed, or, when it is one of the extra
 * keys of a list that inserts, the chosen word is put in. */
int tiku_cv_edit_commit(tiku_codeview_t *v, int commit);

#endif /* TIKU_CODEVIEW_PRIVATE_H_ */
