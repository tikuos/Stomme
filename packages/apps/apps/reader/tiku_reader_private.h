/* tiku_reader_private.h -- the viewer family's own declarations: the
 * window's control ids, its layout, the reading state behind the page, and
 * the calls the family's files make to one another.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TIKU_READER_PRIVATE_H_
#define TIKU_READER_PRIVATE_H_

#include "tiku_reader_window.h"
#include "tiku_reader_document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "tiku_doc_paint.h"
#include "tiku_html_view.h"
#include "tiku_markdown.h"
#include "tiku_font.h"
#include "tiku_strings.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include "tiku_clipboard.h"
#include "tiku_field_event.h"
#include "tiku_list.h"
#include "tiku_image.h"
#include "tiku_iconpaint.h"
#include "tiku_app_product.h"
#include "tiku_dl.h"
#include "tiku_work.h"
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

enum { HELP_BACK = 1, HELP_FORWARD, HELP_HOME, HELP_DONE, HELP_PAGE,
       HELP_FIND, HELP_PREV, HELP_NEXT, HELP_SEARCH, HELP_COPY, HELP_CODE,
       HELP_OUTLINE, HELP_BOOKMARK, HELP_BOOKMARKS, HELP_LINK, HELP_SIDE,
       HELP_LOCATION, HELP_GO, HELP_FIND_CLOSE, HELP_FIND_TOGGLE,
       HELP_CANCEL_SEARCH, HELP_RESULTS, HELP_TEXT_LARGER, HELP_TEXT_SMALLER,
       HELP_TEXT_RESET, HELP_READING_WIDTH };
_Static_assert(HELP_PAGE == TIKU_READER_HELP_CONTROL_PAGE,
               "the page's control id is named in the header");

#define HELP_W 820
#define HELP_H 560
#define HELP_BAR 15                 /* the scrollbar's width */

/** @brief Where the parts of the window go, from one description. */
typedef struct {
    /** the toolbar's four buttons, the title, the paper around the page,
     *  the page, its scrollbar and the status line */
    tiku_rect_t back, forward, home, done, title, paper, page, bar, status;
    /** the Find field and its buttons, and the reading tools' buttons */
    tiku_rect_t find, prev, next, search, copy, code, outline, bookmark;
    /** the Bookmarks and Copy Link buttons, and the side list */
    tiku_rect_t bookmarks, link, side;
    /** a document window's Location field, its label and Go button, and
     *  the Find bar's close button and the Find toggle */
    tiku_rect_t location, location_label, go, find_close, find_toggle;
} help_layout_t;

#define HELP_ROWS 128
#define HELP_REPORTS 8
#define HELP_IMAGE_BYTES (16u * 1024u * 1024u)   /* decoded pixels kept */
#define HELP_IMAGE_METADATA (1u * 1024u * 1024u) /* the image records   */
/** @brief A row of the side list: the page it opens, its title and a
 *         result's matching snippet; the block it names, a heading's level,
 *         and whether headings nest under it. */
typedef struct { char page[TIKU_PATH_MAX], title[128], snippet[160]; int block, level, children; } help_row_t;
/** @brief An image a page names: its file, its size and its pixels. */
typedef struct {
    char *src;                     /**< owned local reference or embedded URI */
    char path[TIKU_PATH_MAX];       /**< the file it resolved to         */
    tiku_image_t image;             /**< decoded pixels; none until drawn */
    int width, height;              /**< from the file's header          */
    int failed;                     /**< refused or undecodable          */
    int embedded;                   /**< a bounded PNG data URI */
    unsigned long used;             /**< image_clock when last drawn     */
} help_image_t;
/** @brief The reading state behind a window's page. */
struct help_reader {
    tiku_html_view_t html;          /**< borrowed model and shared HTML view state */
    char *text;                     /**< the page's text, flattened      */
    /** each run's offset into text; text's length; the selection's two
     *  ends; a selection drag under way; the block last pointed into */
    int *starts, length, a, b, dragging, block;
    size_t text_cap;                /**< bytes text holds                */
    int starts_cap, folded_cap;     /**< entries starts and folded hold  */
    /** the current Find match's offset, -1 for none; the query's length;
     *  the matches; the side list shown: 0 none, 1 outline, 2 search
     *  results, 3 bookmarks; the reports made so far */
    int match, match_len, matches, side, report_id;
    tiku_field_t query;             /**< the Find field                  */
    tiku_field_input_t input;       /**< its scroll and drag             */
    tiku_field_t location;          /**< the Location field              */
    tiku_field_input_t location_input; /**< its scroll and drag          */
    int find_visible, location_dirty; /**< Find bar shown; Location edited */
    /** text size and reading width in pixels; 0 is the desktop's size and
     *  the full width */
    int text_size, reading_width;
    int hover_tool;                 /**< toolbar control under the pointer */
    tiku_rect_t zoom_frame;         /**< the frame the last maximize set */
    tiku_list_t list;               /**< the side list                   */
    help_row_t rows[HELP_ROWS], marks[32]; /**< its rows; the bookmarks  */
    help_row_t results[HELP_ROWS];  /**< the latest folder search's rows */
    int nresults;                   /**< how many of those               */
    char search_query[TIKU_FIELD_MAX]; /**< what they were searched for  */
    struct reader_search *search;   /**< the search running, or NULL     */
    /** rows and bookmarks held; the last search stopped at a limit; the
     *  outline's choice follows the scroll on the next draw */
    int nrows, nmarks, limited, follow_outline;
    int marks_session;              /**< bookmarks unsaved, kept here only */
    size_t search_bytes;            /**< bytes the latest search read    */
    unsigned char *folded;          /**< per block: its heading folded   */
    char hovered[1024];             /**< the link under the pointer      */
    /** generated reports: a name, "@report<n>", and the HTML shown */
    struct { char name[32]; char *html; } reports[HELP_REPORTS];
    help_image_t *images;           /**< the page's images, in order met */
    int nimages, image_cap;         /**< held; room for                  */
    size_t image_bytes;             /**< decoded pixels held, in bytes   */
    size_t image_sources;           /**< owned image reference bytes */
    unsigned long image_clock;      /**< counts drawings, for eviction   */
    tiku_surface_t *surface;        /**< what the page is being drawn on */
};



#pragma GCC visibility push(hidden)


/* Layout. */
help_layout_t tiku_reader_layout(tiku_reader_help_window_t *owner, tiku_rect_t c);
void tiku_reader_settle(tiku_reader_help_window_t *c);
void tiku_reader_refit(tiku_reader_help_window_t *c, help_layout_t l);
void tiku_reader_controls(tiku_reader_help_window_t *c, help_layout_t l);
void tiku_reader_reveal(tiku_reader_help_window_t *c, help_layout_t l);
int tiku_reader_font_begin(tiku_reader_help_window_t *c);
void tiku_reader_font_end(int previous);
int tiku_reader_typography(tiku_reader_help_window_t *c, int command);

/* Navigation. */
int tiku_reader_page_read(tiku_reader_help_window_t *c, const char *name);
void tiku_reader_back_push(tiku_reader_help_window_t *c, const char *page, tiku_reader_help_position_t at);
void tiku_reader_say(tiku_reader_help_window_t *c, const char *what);
int tiku_reader_document_path(const char *name, char *out, size_t cap);
int tiku_reader_realpath(const char *path, char *out, size_t cap);
int tiku_reader_path(tiku_reader_help_window_t *c, const char *name, char *out, size_t cap);

/* Events. */
void tiku_reader_document_menu_pick(tiku_window_t *w, int command, void *ctx);
void tiku_reader_document_menus(tiku_reader_help_window_t *c);
int tiku_reader_window_event(tiku_window_t *window, const tiku_event_t *ev, void *ctx);

/* View. */
void tiku_reader_draw(tiku_window_t *window, tiku_surface_t *s, tiku_rect_t content, void *ctx);
void tiku_reader_side_open(tiku_reader_help_window_t *c);
int tiku_reader_action(tiku_reader_help_window_t *c, int command);
int tiku_reader_tools_event(tiku_reader_help_window_t *c, const tiku_event_t *ev, help_layout_t l);

/* Browser. */
void tiku_reader_document_button(tiku_reader_help_window_t *c, tiku_surface_t *s, tiku_rect_t box, const char *label, int id, unsigned flags);
const char *tiku_reader_document_caption(tiku_reader_help_window_t *c);
void tiku_reader_document_tip(tiku_reader_help_window_t *c, tiku_surface_t *s, tiku_rect_t content);
int tiku_reader_document_hover(tiku_reader_help_window_t *c, const tiku_event_t *ev, help_layout_t l);
void tiku_reader_document_location_sync(tiku_reader_help_window_t *c);
void tiku_reader_document_find(tiku_reader_help_window_t *c, int visible);
int tiku_reader_document_action(tiku_reader_help_window_t *c, int id);
void tiku_reader_document_chrome(tiku_reader_help_window_t *c, tiku_surface_t *s, help_layout_t l);
int tiku_reader_document_event(tiku_reader_help_window_t *c, const tiku_event_t *ev, help_layout_t l);

/* Images. */
int tiku_reader_image_size(void *ctx, const char *src, int *w, int *h);
void tiku_reader_image_draw(void *ctx, const char *src, tiku_rect_t where);

/* Bookmarks. */
void tiku_reader_bookmarks_load(tiku_reader_help_window_t *c);
void tiku_reader_bookmark(tiku_reader_help_window_t *c);

/* Reading. */
int tiku_reader_replace(tiku_reader_help_window_t *c, const char *name, const char *source);
tiku_reader_help_position_t tiku_reader_position(tiku_reader_help_window_t *c);
void tiku_reader_init(tiku_reader_help_window_t *c);
void tiku_reader_images_clear(struct help_reader *r);
int tiku_reader_text(const tiku_doc_t *d, char *out, size_t cap, int *starts);
void tiku_reader_loaded(tiku_reader_help_window_t *c);
int tiku_reader_match(const char *text, const char *query);

/* Search. */
void tiku_reader_search(tiku_reader_help_window_t *c);
void tiku_reader_search_cancel(tiku_reader_help_window_t *c);
void tiku_reader_search_results(tiku_reader_help_window_t *c);
void tiku_reader_outline(tiku_reader_help_window_t *c);
/** @brief Substitute how a search queues work, for a test; NULL restores
 *         tiku_work_post. */
void tiku_reader_test_post(int (*post)(const tiku_work_job_t *job));

#pragma GCC visibility pop

#endif /* TIKU_READER_PRIVATE_H_ */
