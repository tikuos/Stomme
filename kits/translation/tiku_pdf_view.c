/*
 * KintsugiOS v0.01 kits -- the foundation KintsugiDesktop and KintsugiTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@kinstugi-os.org>
 *
 * tiku_pdf_view.c - a PDF shown in any program's window, ready made.
 *
 * Every read of a document runs on a worker, one at a time per document
 * (its jobs share a key); only their done callbacks touch the view, on
 * the program's loop.  The view keeps no window: it is laid out afresh in
 * the rectangle it is drawn in, and takes events in that rectangle.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "tiku_pdf_view.h"

#include "tiku_clipboard.h"
#include "tiku_scroll.h"
#include "tiku_ui.h"
#include "tiku_work.h"

#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VW_MARGIN      12           /* around the pages, in pixels      */
#define VW_GAP         10           /* between two pages                */
#define VW_BAR         15           /* a scrollbar's width              */
#define VW_TILES       48           /* pages kept drawn                 */
#define VW_PIXELS      (24L << 20)  /* the pixels those may hold        */
#define VW_PAGE_PIXELS (16L << 20)  /* the most one page is drawn with  */
#define VW_INFLIGHT    2            /* pages asked for, not yet back    */
#define VW_TEXT_KEEP   64           /* pages whose text is kept         */
#define VW_MATCHES     5000
#define VW_SEARCH_STEP 24           /* pages one search job reads       */
#define VW_HISTORY     32
#define VW_LINE        40           /* an arrow's or a wheel notch's    */
#define VW_PATH        4096
#define VW_QUERY       256          /* characters a search looks for    */
#define VW_GROUND      0xff4b4e52u  /* what the pages lie on            */

/** @brief An open document as its jobs share it. */
typedef struct {
    tiku_pdf_t *pdf;                /* read by one job at a time: the key */
    int refs;                       /* the view and each job; loop only  */
    char key[TIKU_WORK_KEY];
    atomic_int search;              /* the search a job still belongs to */
} vw_doc_t;

/** @brief A page drawn at one scale; pending while a worker draws it. */
typedef struct {
    int page, w, h, pending;
    double scale;                   /* the view's scale it was asked at  */
    uint32_t *px;
    unsigned long used;             /* the frame that last showed it     */
} vw_tile_t;

/** @brief What a page says, once a worker has read it: links, text. */
typedef struct {
    tiku_pdf_link_t *link;
    tiku_pdf_char_t *ch;
    int nlink, text, failed;
    long nch;
    unsigned long used;
} vw_page_t;

/** @brief A search hit: its page and a box a line, four at most. */
typedef struct {
    int page, nr;
    float r[4][4];
} vw_match_t;

/** @brief A place in the text: a page, and a gap between characters. */
typedef struct { int page; long at; } vw_mark_t;

/** @brief Where the view's parts go in the rectangle it is given. */
typedef struct {
    tiku_rect_t canvas, vbar, hbar;
} vw_layout_t;

/** @brief A view: its document, its layout and the pages drawn for it,
 *         and what is selected, found and marked. */
struct tiku_pdf_view {
    tiku_pdf_view_host_t host;
    int refs, generation, loading;
    int gone;                       /* the program let it go            */
    char path[VW_PATH], title[256], status[320], hint[320];
    vw_doc_t *doc;
    int npage, widest;              /* pages, and the widest one shown  */
    float *pw, *ph;                 /* each page as drawn at scale 1    */
    int turn;                       /* the view's turn: 0, 90, 180, 270 */
    long *top, height, width;       /* the pages laid out at the scale  */
    vw_page_t *pages;
    int ntext;                      /* pages holding their text         */
    double scale, laid_scale;
    int fit, laid_w, laid_h;
    int single, single_page;        /* one page alone, and which        */
    tiku_scroll_t vbar, hbar;
    int vdrag, hdrag;
    vw_tile_t tile[VW_TILES];
    int inflight;
    unsigned long frame;
    tiku_pdf_outline_t *outline;
    int noutline, may_copy;
    vw_match_t *match;
    int nmatch, cur, searching, searched, search_gen, want_next;
    char query[VW_QUERY * 4];       /* what the matches are of, UTF-8   */
    unsigned find_flags;            /* and how it was compared          */
    vw_mark_t anchor, focus;        /* a selection runs between the two */
    int selecting, selected, pressing, press_x, press_y;
    int link_page, link_at;         /* the link under the pointer, or -1 */
    int press_page, press_link;     /* the link a press began on        */
    struct { int page; double y; } back[VW_HISTORY];
    int nback;
    int start_page;                 /* asked before the first layout    */
    double start_y;
    uint32_t *view;                 /* the canvas, composed each frame  */
    int view_w, view_h;
};

static void vw_marks(tiku_pdf_view_t *v, int p, int x, int y);
static void vw_open_address(tiku_pdf_view_t *v, const char *uri);
static void vw_zoom_step(tiku_pdf_view_t *v, int dir);

/** @brief Something the view shows has changed: the program is told. */
static void vw_changed(tiku_pdf_view_t *v)
{
    if (!v->gone && v->host.changed) v->host.changed(v->host.ctx);
}

/** @brief Say @p text in the status until something else is said. */
static void vw_say(tiku_pdf_view_t *v, const char *text)
{
    snprintf(v->status, sizeof v->status, "%s", text ? text : "");
    vw_changed(v);
}

/** @brief Whether (@p x, @p y) is in @p r, its right and bottom edges
 *         left out. */
static int vw_in(tiku_rect_t r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

/** @brief Page @p p's width in points as the view shows it: turned. */
static double vw_pw(const tiku_pdf_view_t *v, int p)
{
    return v->turn % 180 ? v->ph[p] : v->pw[p];
}

/** @brief Page @p p's height in points as the view shows it: turned. */
static double vw_ph(const tiku_pdf_view_t *v, int p)
{
    return v->turn % 180 ? v->pw[p] : v->ph[p];
}

/** @brief The widest page as the view shows it, the one Fit Width fits. */
static void vw_find_widest(tiku_pdf_view_t *v)
{
    v->widest = 0;
    for (int p = 1; p < v->npage; p++)
        if (vw_pw(v, p) > vw_pw(v, v->widest)) v->widest = p;
}

/** @brief One job or the view fewer; the last one closes the document. */
static void vw_doc_unref(vw_doc_t *d)
{
    if (d && --d->refs == 0) {
        tiku_pdf_close(d->pdf);
        free(d);
    }
}

/** @brief Everything the shown document brought, given back; the view
 *         is left blank.  Jobs still queued for it are cancelled. */
static void vw_forget(tiku_pdf_view_t *v)
{
    if (v->doc) {
        atomic_store(&v->doc->search, -1);
        (void)tiku_work_cancel(v->doc->key);
        vw_doc_unref(v->doc);
        v->doc = NULL;
    }
    for (int i = 0; i < VW_TILES; i++) {
        free(v->tile[i].px);
        memset(&v->tile[i], 0, sizeof v->tile[i]);
        v->tile[i].page = -1;
    }
    for (int p = 0; v->pages && p < v->npage; p++) {
        tiku_pdf_links_free(v->pages[p].link, v->pages[p].nlink);
        free(v->pages[p].ch);
    }
    free(v->pages); free(v->pw); free(v->ph); free(v->top); free(v->match);
    tiku_pdf_outline_free(v->outline, v->noutline);
    v->pages = NULL; v->pw = v->ph = NULL; v->top = NULL; v->match = NULL;
    v->outline = NULL;
    v->npage = v->noutline = v->nmatch = v->ntext = v->may_copy = 0;
    v->inflight = v->searching = v->searched = v->want_next = 0;
    v->selected = v->selecting = v->pressing = v->nback = 0;
    v->cur = v->link_page = v->link_at = v->press_page = v->press_link = -1;
    v->widest = v->turn = 0;
    v->height = v->width = 0; v->laid_w = v->laid_h = 0;
    v->laid_scale = 0; v->search_gen++; v->query[0] = 0;
    v->path[0] = v->title[0] = v->hint[0] = 0;
    tiku_scroll_init(&v->vbar, VW_LINE);
    tiku_scroll_init(&v->hbar, VW_LINE);
}

/** @brief Let the view go after the last queued job retires. */
static void vw_unref(tiku_pdf_view_t *v)
{
    if (--v->refs) return;
    vw_forget(v);
    free(v->view);
    free(v);
}

#include "tiku_pdf_view_jobs.inl"
#include "tiku_pdf_view_paint.inl"
#include "tiku_pdf_view_text.inl"

/** @brief One step up or down the zoom ladder from the scale shown. */
static void vw_zoom_step(tiku_pdf_view_t *v, int dir)
{
    static const double steps[] = { 0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0,
        1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0 };
    int n = (int)(sizeof steps / sizeof steps[0]), i;
    double to;

    if (dir > 0) {
        for (i = 0; i < n && steps[i] <= v->scale * 1.01; i++) {}
        to = i < n ? steps[i] : steps[n - 1];
    } else {
        for (i = n - 1; i >= 0 && steps[i] >= v->scale * 0.99; i--) {}
        to = i >= 0 ? steps[i] : steps[0];
    }
    vw_set_scale(v, TIKU_PDF_VIEW_FIXED, to);
}

/** @brief Keys with Command: copying, the page's text, going back, the
 *         zoom, the ends of the document. */
static int vw_command_key(tiku_pdf_view_t *v, const tiku_event_t *ev)
{
    unsigned key = ev->key;

    if (key == 'c') (void)tiku_pdf_view_copy(v);
    else if (key == 'a') (void)tiku_pdf_view_select_page(v);
    else if (key == '[') (void)tiku_pdf_view_back(v);
    else if (key == '+' || key == '=') vw_zoom_step(v, 1);
    else if (key == '-') vw_zoom_step(v, -1);
    else if (key == '0') vw_set_scale(v, TIKU_PDF_VIEW_FIXED, 1.0);
    else if (key == TIKU_KEY_UP) vw_go(v, 0, 0, 1);
    else if (key == TIKU_KEY_DOWN) vw_go(v, v->npage - 1, 0, 1);
    else return 0;
    vw_changed(v);
    return 1;
}

/** @brief Keys the pages answer: scrolling, paging, the zoom's steps,
 *         Escape letting the selection go. */
static int vw_key(tiku_pdf_view_t *v, const tiku_event_t *ev)
{
    unsigned key = ev->key;
    int page = v->laid_h - VW_LINE, shift = ev->modifiers & TIKU_MOD_SHIFT;
    int at = tiku_pdf_view_page(v);

    if (!v->doc) return 0;
    if (ev->modifiers & TIKU_MOD_CMD) return vw_command_key(v, ev);
    if (key == TIKU_KEY_UP) vw_scroll(v, 0, -VW_LINE);
    else if (key == TIKU_KEY_DOWN) vw_scroll(v, 0, VW_LINE);
    else if (key == TIKU_KEY_PAGE_UP || (key == ' ' && shift))
        vw_scroll(v, 0, -page);
    else if (key == TIKU_KEY_PAGE_DOWN || key == ' ') vw_scroll(v, 0, page);
    else if (key == TIKU_KEY_HOME) vw_go(v, 0, 0, 0);
    else if (key == TIKU_KEY_END) vw_go(v, v->npage - 1, 0, 0);
    else if (key == TIKU_KEY_LEFT || key == TIKU_KEY_RIGHT) {
        int dir = key == TIKU_KEY_LEFT ? -1 : 1;
        if (tiku_scroll_max(&v->hbar) > 0) vw_scroll(v, dir * VW_LINE, 0);
        else if (at + dir >= 0 && at + dir < v->npage)
            vw_go(v, at + dir, 0, 0);
    } else if (key == '+' || key == '=') vw_zoom_step(v, 1);
    else if (key == '-') vw_zoom_step(v, -1);
    else if (key == TIKU_KEY_ESCAPE && v->selected) v->selected = 0;
    else return 0;
    vw_changed(v);
    return 1;
}

tiku_pdf_view_t *tiku_pdf_view_new(const tiku_pdf_view_host_t *host)
{
    tiku_pdf_view_t *v = calloc(1, sizeof *v);

    if (!v) return NULL;
    if (host) v->host = *host;
    v->refs = 1;
    v->fit = TIKU_PDF_VIEW_FIT_WIDTH;
    v->scale = 1;
    v->start_page = -1;
    vw_forget(v);
    return v;
}

void tiku_pdf_view_free(tiku_pdf_view_t *v)
{
    if (!v) return;
    v->gone = 1;
    v->generation++;
    memset(&v->host, 0, sizeof v->host);
    vw_forget(v);
    vw_unref(v);
}

int tiku_pdf_view_open(tiku_pdf_view_t *v, const char *path,
                       const char *password)
{
    return v && !v->gone && vw_load_as(v, path, "", password ? password : "",
                                       NULL);
}

int tiku_pdf_view_open_key(tiku_pdf_view_t *v, const char *path,
                           const char *keyfile, const char *password)
{
    if (!v || v->gone || !keyfile || !*keyfile) return 0;
    return vw_load_as(v, path, keyfile, password ? password : "", NULL);
}

int tiku_pdf_view_open_as(tiku_pdf_view_t *v, const char *path,
                          tiku_pdf_view_translate_fn translate)
{
    return v && !v->gone && translate && vw_load_as(v, path, "", "",
                                                    translate);
}

void tiku_pdf_view_close(tiku_pdf_view_t *v)
{
    if (!v) return;
    v->generation++;                /* an opening on its way is dropped */
    v->start_page = -1;
    vw_forget(v);
    vw_say(v, "");
}

int tiku_pdf_view_loading(const tiku_pdf_view_t *v)
{
    return v && v->loading;
}

int tiku_pdf_view_event(tiku_pdf_view_t *v, tiku_rect_t r,
                        const tiku_event_t *ev)
{
    vw_layout_t l;

    if (!v || !ev) return 0;
    l = vw_layout(v, r);
    if (ev->type == TIKU_EVENT_KEY_DOWN) return vw_key(v, ev);
    if (ev->type == TIKU_EVENT_DEACTIVATED) {
        v->pressing = v->selecting = v->vdrag = v->hdrag = 0;
        tiku_scroll_release(&v->vbar);
        tiku_scroll_release(&v->hbar);
        return 0;
    }
    if (ev->type != TIKU_EVENT_POINTER_DOWN &&
        ev->type != TIKU_EVENT_POINTER_MOVE &&
        ev->type != TIKU_EVENT_POINTER_UP && ev->type != TIKU_EVENT_WHEEL)
        return 0;
    if (vw_bars_pointer(v, ev, &l)) return 1;
    return vw_canvas_pointer(v, ev, &l);
}

int tiku_pdf_view_pages(const tiku_pdf_view_t *v)
{
    return v ? v->npage : 0;
}

int tiku_pdf_view_page(const tiku_pdf_view_t *v)
{
    return v ? vw_current(v) : 0;
}

void tiku_pdf_view_go(tiku_pdf_view_t *v, int page)
{
    if (v) vw_go(v, page, 0, 1);
}

void tiku_pdf_view_follow(tiku_pdf_view_t *v, const tiku_pdf_dest_t *to)
{
    if (!v || !to) return;
    if (to->page >= 0 && to->page < v->npage)
        vw_go(v, to->page, vw_dest_y(v, to), 1);
    else vw_open_address(v, to->uri);
}

int tiku_pdf_view_back(tiku_pdf_view_t *v)
{
    return v && vw_back(v);
}

void tiku_pdf_view_single(tiku_pdf_view_t *v, int on)
{
    if (!v) return;
    if (on && !v->single) v->single_page = vw_current(v);
    v->single = on != 0;
    vw_set_scale(v, TIKU_PDF_VIEW_FIT_PAGE, v->scale);
}

int
tiku_pdf_view_is_single(const tiku_pdf_view_t *v)
{
    return v && v->single;
}

void tiku_pdf_view_fit(tiku_pdf_view_t *v, tiku_pdf_view_fit_t fit,
                       double scale)
{
    if (!v || (unsigned)fit > (unsigned)TIKU_PDF_VIEW_FIXED) return;
    vw_set_scale(v, fit, fit == TIKU_PDF_VIEW_FIXED ? scale : v->scale);
}

void tiku_pdf_view_zoom(tiku_pdf_view_t *v, int dir)
{
    if (v && dir) vw_zoom_step(v, dir);
}

tiku_pdf_view_fit_t tiku_pdf_view_fitted(const tiku_pdf_view_t *v)
{
    return v ? (tiku_pdf_view_fit_t)v->fit : TIKU_PDF_VIEW_FIT_WIDTH;
}

double tiku_pdf_view_scale(const tiku_pdf_view_t *v)
{
    return v ? v->scale : 1.0;
}

void tiku_pdf_view_turn(tiku_pdf_view_t *v, int by)
{
    by = (by % 360 + 360) % 360;
    if (v && by && by % 90 == 0) vw_turn(v, by);
}

int tiku_pdf_view_turned(const tiku_pdf_view_t *v)
{
    return v ? v->turn : 0;
}

int tiku_pdf_view_find(tiku_pdf_view_t *v, const char *text, int dir,
                       unsigned flags)
{
    return v && text && vw_find(v, text, dir < 0 ? -1 : 1, flags);
}

int tiku_pdf_view_copy(tiku_pdf_view_t *v)
{
    return v && vw_copy(v);
}

int tiku_pdf_view_select_page(tiku_pdf_view_t *v)
{
    return v && v->doc && vw_select_page(v, vw_current(v));
}

int tiku_pdf_view_outline(const tiku_pdf_view_t *v,
                          const tiku_pdf_outline_t **items)
{
    if (items) *items = v ? v->outline : NULL;
    return v ? v->noutline : 0;
}

int tiku_pdf_view_can(const tiku_pdf_view_t *v, tiku_pdf_view_can_t what)
{
    int page;

    if (!v || !v->doc) return 0;
    page = vw_current(v);
    switch (what) {
    case TIKU_PDF_VIEW_CAN_PREV: return page > 0;
    case TIKU_PDF_VIEW_CAN_NEXT: return page + 1 < v->npage;
    case TIKU_PDF_VIEW_CAN_BACK: return v->nback > 0;
    case TIKU_PDF_VIEW_CAN_ZOOM_IN: return v->scale < 3.99;
    case TIKU_PDF_VIEW_CAN_ZOOM_OUT: return v->scale > 0.26;
    case TIKU_PDF_VIEW_CAN_COPY: return v->selected && v->may_copy;
    case TIKU_PDF_VIEW_CAN_FIND: return 1;
    }
    return 0;
}

const char *tiku_pdf_view_path(const tiku_pdf_view_t *v)
{
    return v && v->doc ? v->path : "";
}

const char *tiku_pdf_view_title(const tiku_pdf_view_t *v)
{
    return v && v->doc ? v->title : "";
}

const char *tiku_pdf_view_status(const tiku_pdf_view_t *v)
{
    if (!v) return "";
    return v->hint[0] ? v->hint : v->status;
}
