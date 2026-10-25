/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_view.h - a PDF viewer for any program's window, built on
 * tiku_pdf.h.
 *
 * Pages are drawn on workers and kept, shown one under another, scrolled,
 * zoomed and turned; links are followed; text is found, selected and
 * copied.  The program gives the view a rectangle to draw in and the
 * events that land there.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_PDF_VIEW_H_
#define TIKU_PDF_VIEW_H_

#include "tiku_event.h"
#include "tiku_gfx.h"
#include "tiku_pdf.h"

typedef struct tiku_pdf_view tiku_pdf_view_t;

/** @brief How the pages are sized to the view. */
typedef enum {
    TIKU_PDF_VIEW_FIT_WIDTH = 0,    /* the widest page across the view   */
    TIKU_PDF_VIEW_FIT_PAGE,         /* the page in view, the whole of it */
    TIKU_PDF_VIEW_FIXED             /* a scale of the program's choosing */
} tiku_pdf_view_fit_t;

/** @brief What tiku_pdf_view_can() is asked about: whether the command
 *         of the same name would do anything now. */
typedef enum {
    TIKU_PDF_VIEW_CAN_PREV = 0, TIKU_PDF_VIEW_CAN_NEXT,
    TIKU_PDF_VIEW_CAN_BACK, TIKU_PDF_VIEW_CAN_ZOOM_IN,
    TIKU_PDF_VIEW_CAN_ZOOM_OUT, TIKU_PDF_VIEW_CAN_COPY,
    TIKU_PDF_VIEW_CAN_FIND
} tiku_pdf_view_can_t;

/* How tiku_pdf_view_find() compares. */
#define TIKU_PDF_FIND_CASE  0x1u    /* capitals matter                   */
#define TIKU_PDF_FIND_WORDS 0x2u    /* no letter or digit at either end  */

/**
 * @brief What the view tells the program showing it; any may be NULL.
 *        Each is called on the program's loop: from tiku_work_collect(),
 *        where the view's jobs land, or from a call into the view.
 */
typedef struct {
    void (*changed)(void *ctx);                     /* draw it again     */
    void (*opened)(void *ctx, tiku_pdf_err_t err);  /* an open has ended */
    void (*address)(void *ctx, const char *uri);    /* a link out of it  */
    void *ctx;
} tiku_pdf_view_host_t;

/**
 * @brief An empty view, drawn blank; NULL out of memory.  With no address
 *        callback, a link out of the document is put on the clipboard and
 *        said.
 * @note Its reading and drawing are jobs on the tiku_work pool: the
 *       program's loop calls tiku_work_collect() for them to land.
 */
tiku_pdf_view_t *tiku_pdf_view_new(const tiku_pdf_view_host_t *host);

/** @brief The view let go.  Jobs already running finish on their own and
 *         tell no one; the document goes with the last of them. */
void tiku_pdf_view_free(tiku_pdf_view_t *v);

/**
 * @brief Open @p path on a worker, with @p password when it is locked
 *        (NULL or "" for none).  The document shown stays until the new
 *        one is open, and stays if it cannot be; host->opened says how it
 *        went.  One opening at a time.  @return 0 when none was begun.
 */
int tiku_pdf_view_open(tiku_pdf_view_t *v, const char *path,
                       const char *password);

/** @brief The same for a document sealed to certificates: the reader's
 *         key file, PKCS#12 or PEM, and the password that unlocks it. */
int tiku_pdf_view_open_key(tiku_pdf_view_t *v, const char *path,
                           const char *keyfile, const char *password);

/** @brief A file made a PDF, on a worker: its bytes, their count in
 *         @p len; NULL with why said in @p why, in a person's words.
 *  @note The view frees the bytes with free(). */
typedef unsigned char *(*tiku_pdf_view_translate_fn)(const char *path,
                                                      size_t *len, char *why,
                                                      size_t max);

/**
 * @brief Open @p path as @p translate makes it a PDF -- a Word document,
 *        slides -- on a worker, as tiku_pdf_view_open() opens a PDF.  Its
 *        path is the file's own; a failure is said in the translator's
 *        words.  @return 0 when none was begun.
 */
int tiku_pdf_view_open_as(tiku_pdf_view_t *v, const char *path,
                          tiku_pdf_view_translate_fn translate);

/** @brief The document let go; the view is blank again. */
void tiku_pdf_view_close(tiku_pdf_view_t *v);

/** @brief Whether an opening is on its way. */
int tiku_pdf_view_loading(const tiku_pdf_view_t *v);

/**
 * @brief The view drawn into @p r of @p s: the pages over a dark ground,
 *        a scrollbar down the right and, when the pages are wider than
 *        @p r, one along the bottom.  Pages not drawn yet show white while
 *        workers draw them, asked for here.  Blank with nothing open.
 */
void tiku_pdf_view_draw(tiku_pdf_view_t *v, tiku_surface_t *s,
                        tiku_rect_t r);

/**
 * @brief An event for the view drawn at @p r.  The wheel scrolls
 *        (sideways with Shift, zooms with Command), a click follows a
 *        link, a drag selects text, the bars scroll; arrows, Page Up and
 *        Down, Space, Home, End, + and - move and zoom, Escape lets the
 *        selection go; Command with C, A, [, +, - and 0 copies, selects
 *        the page, goes back and zooms, and with Up or Down goes to the
 *        first or last page.
 * @return 1 when the view took it.
 */
int tiku_pdf_view_event(tiku_pdf_view_t *v, tiku_rect_t r,
                        const tiku_event_t *ev);

/** @brief How many pages the document has; 0 with none open. */
int tiku_pdf_view_pages(const tiku_pdf_view_t *v);

/** @brief The page in view, from 0: the one across its middle. */
int tiku_pdf_view_page(const tiku_pdf_view_t *v);

/** @brief Bring page @p page (from 0) to the top of the view; where the
 *         view was is kept for tiku_pdf_view_back().  Asked before the
 *         view is first drawn, it is where the view starts. */
void tiku_pdf_view_go(tiku_pdf_view_t *v, int page);

/** @brief Go where @p to leads -- an outline entry's or a link's place, or
 *         an address, which goes to host->address or, without one, onto
 *         the clipboard. */
void tiku_pdf_view_follow(tiku_pdf_view_t *v, const tiku_pdf_dest_t *to);

/** @brief Back to where the view was before its last jump.
 *  @return 0 when there is nowhere to go back to. */
int tiku_pdf_view_back(tiku_pdf_view_t *v);

/** @brief Size the pages by @p fit; @p scale is the pixels to a point
 *         for TIKU_PDF_VIEW_FIXED (1 is 72 dpi), from 0.25 to 4. */
void tiku_pdf_view_fit(tiku_pdf_view_t *v, tiku_pdf_view_fit_t fit,
                       double scale);

/** @brief The page in view shown alone, fitted whole and centred on
 *         black, with no bars: a slide presented (@p on), or the pages
 *         one under another again.  tiku_pdf_view_go() steps it. */
void tiku_pdf_view_single(tiku_pdf_view_t *v, int on);
/** @brief Whether the view shows one page alone; zero for a NULL view. */
int tiku_pdf_view_is_single(const tiku_pdf_view_t *v);

/** @brief One step in (@p dir 1) or out (-1) of the zoom ladder, 25% to
 *         400%; the fit becomes fixed. */
void tiku_pdf_view_zoom(tiku_pdf_view_t *v, int dir);

/** @brief How the pages are sized now. */
tiku_pdf_view_fit_t tiku_pdf_view_fitted(const tiku_pdf_view_t *v);

/** @brief The scale shown: pixels to a point. */
double tiku_pdf_view_scale(const tiku_pdf_view_t *v);

/** @brief Turn the view @p by degrees clockwise more, a multiple of 90;
 *         the page in view stays in view. */
void tiku_pdf_view_turn(tiku_pdf_view_t *v, int by);

/** @brief How far the view is turned: 0, 90, 180 or 270. */
int tiku_pdf_view_turned(const tiku_pdf_view_t *v);

/**
 * @brief Find @p text in @p dir (1 on, -1 back) with @p flags
 *        (TIKU_PDF_FIND_*).  New text starts a search of the whole
 *        document on workers, every match marked as it is found; the same
 *        text and flags step to the next match.
 * @return 0 with nothing open, an empty @p text, or no worker free for
 *         the search.
 */
int tiku_pdf_view_find(tiku_pdf_view_t *v, const char *text, int dir,
                       unsigned flags);

/** @brief Native PDF character-gap selection metadata, excluding selected text. */
typedef struct {
    int anchor_page,focus_page;
    int64_t anchor,focus;
    int selected,copy_allowed;
} tiku_pdf_view_selection_t;
/** @brief Copy native selection endpoints and author copy permission; zero without a document. */
int tiku_pdf_view_selection(const tiku_pdf_view_t *,
                            tiku_pdf_view_selection_t *);

/** @brief The selected text onto the clipboard, as far as the clipboard
 *         and the document's author allow; the status says how it went.
 *  @return 1 when something was copied. */
int tiku_pdf_view_copy(tiku_pdf_view_t *v);

/** @brief The text of the page in view selected, all of it.
 *  @return 0 when the page's text has not been read yet. */
int tiku_pdf_view_select_page(tiku_pdf_view_t *v);

/** @brief The document's outline (bookmarks) as read when it opened,
 *         depth first: follow an entry with tiku_pdf_view_follow().
 *  @return how many, into @p items.
 *  @note The entries are the view's, kept until its document closes. */
int tiku_pdf_view_outline(const tiku_pdf_view_t *v,
                          const tiku_pdf_outline_t **items);

/** @brief Whether command @p what would do anything now; 0 with nothing
 *         open. */
int tiku_pdf_view_can(const tiku_pdf_view_t *v, tiku_pdf_view_can_t what);

/** @brief The shown document's path; "" with none. */
const char *tiku_pdf_view_path(const tiku_pdf_view_t *v);

/** @brief Its title as it gives it, else its file's name; "" with none. */
const char *tiku_pdf_view_title(const tiku_pdf_view_t *v);

/** @brief The text for a status line: where the link under the pointer
 *         leads, else what happened last -- "Opening...", "12 pages.",
 *         "Match 2 of 5.", or why something did not work. */
const char *tiku_pdf_view_status(const tiku_pdf_view_t *v);

#endif /* TIKU_PDF_VIEW_H_ */
